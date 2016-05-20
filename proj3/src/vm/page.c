#include "vm/page.h"

#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/swap.h"

void
initialize_page_table (struct hash *table)
{
  hash_init (table, page_hash, page_less, 0);
  lock_init (&read_lock);
  lock_init (&write_lock);
}

struct page_entry*
create_sup_file_page (void* upage, bool writable, struct file *file,
                  off_t ofs, uint32_t read_bytes, uint32_t zero_bytes)
{
  struct page_entry *entry = malloc (sizeof (struct page_entry));
  if (entry == NULL)
    return NULL;

  struct thread *t = thread_current ();

  entry->type = FILE;
  entry->addr = upage;
  entry->kpage = NULL;
  entry->pagedir = t->pagedir;
  entry->pinned_page = false;
  entry->writable = writable;
  entry->loaded = false;
  entry->file = file;
  entry->ofs = ofs;
  entry->read_bytes = read_bytes;
  entry->zero_bytes = zero_bytes;

  hash_insert (&t->sup_page_table, &entry->hash_entry);
  return entry;
}

struct page_entry*
create_sup_mmap_page (void* upage, bool writable, struct file *file,
                  off_t ofs, uint32_t read_bytes, uint32_t zero_bytes)
{
  struct page_entry *entry = create_sup_file_page (upage, writable, file, ofs, read_bytes, zero_bytes);
  entry->type = MMAP;

  return entry;
}

bool
grow_stack (void* addr)
{
  struct page_entry *entry = malloc (sizeof (struct page_entry));

  if (entry == NULL)
    return false;

  struct thread *t = thread_current ();

  entry->type = ZERO;
  entry->addr = pg_round_down (addr);
  entry->kpage = NULL;
  entry->pagedir = t->pagedir;
  entry->writable = true;
  entry->loaded = false;

  hash_insert (&t->sup_page_table, &entry->hash_entry);
  
  bool success = load_page (entry);
  entry->pinned_page = true;
  return success;
}

struct page_entry*
fetch_page (void* user_vaddr)
{
  struct page_entry temp;
  temp.addr = pg_round_down (user_vaddr);
  struct hash_elem *page = hash_find(&thread_current()->sup_page_table, &temp.hash_entry);

  if (page == NULL)
    return NULL;
  return hash_entry (page, struct page_entry, hash_entry);
}

bool
load_page (struct page_entry *page)
{
  if (page->loaded)
    return false;
  page->pinned_page = true;

  switch (page->type)
  {
    case FILE:
    case MMAP:
    {
      page->kpage = create_frame (PAL_USER, page);
      if (page->kpage == NULL)
        return false;
      
      /* Load this page. */
      if (page->read_bytes > 0)
      {
        if (file_read_at (page->file, page->kpage, page->read_bytes, page->ofs) != (int) page->read_bytes)
          return !remove_frame (page->kpage);
        memset (page->kpage + page->read_bytes, 0, page->zero_bytes);
      }

      /* Add the page to the process's address space. */
      if (pagedir_get_page (page->pagedir, page->addr) != NULL
        || !pagedir_set_page (page->pagedir, page->addr, page->kpage, page->writable))
          return !remove_frame (page->kpage);
      break;
    }

    case ZERO:
    {
      page->kpage = create_frame ((PAL_USER | PAL_ZERO), page);
      if (page->kpage == NULL)
        return !remove_frame (page->kpage);

      memset (page->kpage, 0, PGSIZE);
      if (pagedir_get_page (page->pagedir, page->addr) != NULL
        || !pagedir_set_page (page->pagedir, page->addr, page->kpage, page->writable))
          return !remove_frame (page->kpage);
      break;
    }

    case SWAP:
    {
      page->kpage = create_frame (PAL_USER, page);
      if (page->kpage == NULL)
        return false;

      if (pagedir_get_page (page->pagedir, page->addr) != NULL
        || !pagedir_set_page (page->pagedir, page->addr, page->kpage, page->writable))
          return !remove_frame (page->kpage);

      fetch_from_swap (((struct frame_entry *) page->kpage)->swap_index, page->addr);
      break;
    }

    default:
      return false;
  }

  page->loaded = true;
  page->pinned_page = false;
  pagedir_set_dirty (page->pagedir, page->addr, false);
  pagedir_set_accessed (page->pagedir, page->addr, true);

  return true;
}

bool
unload_page (struct page_entry *page)
{
  if (pagedir_is_accessed(page->pagedir, page->addr))
    pagedir_set_accessed(page->pagedir, page->addr, false);

  // Need to flush changes
  if (pagedir_is_dirty(page->pagedir, page->addr))
  {
    if (page->type == MMAP)
      write_out_file (page);
    else
      ((struct frame_entry *) page->kpage)->swap_index = write_to_swap (page->kpage);
  }

  pagedir_clear_page (page->pagedir, page->addr);
  page->kpage = NULL;
  page->loaded = false;
  page->pinned_page = false;
  return true;
}

bool
remove_page (struct page_entry *page)
{
  struct thread *t = thread_current ();

  lock_acquire (&read_lock);
  struct hash_elem *e = hash_delete (&t->sup_page_table, &page->hash_entry);

  if (e == NULL)
  {
    lock_release (&read_lock);
    return false;
  }
  
  unload_page (page);
  file_close (page->file);
  free (page);
  lock_release (&read_lock);

  return true;
}

void
write_out_file (struct page_entry *page)
{
  if (!pagedir_is_dirty (page->pagedir, page->addr))
    return;

  page->pinned_page = true;
  lock_acquire (&write_lock);
  file_write_at (page->file, page->kpage, page->read_bytes, page->ofs);
  lock_release (&write_lock);
  page->pinned_page = false;
}

bool
in_stack_range (const void *esp, void *addr)
{
  return addr >= (esp - 32);
}

//---Hash table helper functions---//
// Obtained from: http://web.stanford.edu/class/cs140/projects/pintos/pintos_6.html#SEC123

unsigned
page_hash (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct page_entry *e = hash_entry (elem, struct page_entry, hash_entry);
  return hash_bytes (&e->addr, sizeof e->addr);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED)
{
  const struct page_entry *a = hash_entry (elem_a, struct page_entry, hash_entry);
  const struct page_entry *b = hash_entry (elem_b, struct page_entry, hash_entry);

  return a->addr < b->addr;
}
