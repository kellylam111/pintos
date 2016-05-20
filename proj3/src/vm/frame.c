#include "vm/frame.h"

#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "vm/page.h"
#include "vm/swap.h"

bool access_time_compare (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

void
initialize_frame_table ()
{
  hash_init (&frame_table, frame_hash, frame_less, 0);
  list_init (&frame_list);
  lock_init (&table_lock);
  lock_init (&removal_lock);
}

void*
create_frame (enum palloc_flags flags, struct page_entry *page)
{
  if (flags == PAL_ASSERT)
    return NULL;
  
  void *f = palloc_get_page(flags);

  while (f == NULL)
  {
    evict_frame_from_table ();
    f = palloc_get_page(flags);
  }

  struct frame_entry *entry = malloc (sizeof (struct frame_entry));
  entry->addr = f;
  entry->page = page;
  entry->owner = thread_current ();
  entry->access_time = timer_ticks();

  lock_acquire (&table_lock);
  hash_insert (&frame_table, &entry->hash_entry);
  list_insert_ordered (&frame_list, &entry->elem, &access_time_compare, 0);
  lock_release (&table_lock);
  return f;
}

void
evict_frame_from_table ()
{
  if (list_empty (&frame_list))
    return;

  lock_acquire (&removal_lock);
  
  struct list_elem *e = list_begin (&frame_list);

  while (e != NULL && e != list_end (&frame_list))
  {
    struct frame_entry *current = list_entry (e, struct frame_entry, elem);
    struct page_entry *page = current->page;

    // Not pinned, going to evict
    if (!page->pinned_page)
    {
      unload_page (page);
      list_remove (e);
      remove_frame (current);
      break;
    } 

    e = list_next (e);
  }

  lock_release (&removal_lock);
}

bool
remove_frame (struct frame_entry *frame)
{
  if (frame == NULL)
    return false;

  lock_acquire (&table_lock);
  struct hash_elem *e = hash_delete (&frame_table, &frame->hash_entry);

  if (e == NULL)
  {
    lock_release (&table_lock);
    return false;
  }

  palloc_free_page (frame->addr);
  free (frame);
  lock_release (&table_lock);

  return true;
}

struct frame_entry*
fetch_frame (void *addr)
{
  struct frame_entry temp;
  temp.addr = pg_round_down (addr);
  struct hash_elem *frame = hash_find(&frame_table, &temp.hash_entry);

  if (frame == NULL)
    return NULL;
  return hash_entry (frame, struct frame_entry, hash_entry);
}

//---Hash table helper functions---//
// Source: http://web.stanford.edu/class/cs140/projects/pintos/pintos_6.html#SEC123
unsigned
frame_hash (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct frame_entry *e = hash_entry (elem, struct frame_entry, hash_entry);
  return hash_bytes (&e->addr, sizeof e->addr);
}

/* Returns true if page a precedes page b. */
bool
frame_less (const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED)
{
  const struct frame_entry *a = hash_entry (elem_a, struct frame_entry, hash_entry);
  const struct frame_entry *b = hash_entry (elem_b, struct frame_entry, hash_entry);

  return a->addr < b->addr;
}

/* Compares the wakeup time between two threads. Earlier wakeup time comes first. */
bool
access_time_compare (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  int64_t a_time = list_entry (a, struct frame_entry, elem)->access_time;
  int64_t b_time = list_entry (b, struct frame_entry, elem)->access_time;

  ASSERT(a_time != -1 && b_time != -1); //Sanity check

  return a_time < b_time;
}