#include "vm/memory_map.h"

#include "threads/malloc.h"
#include "threads/thread.h"

void
initialize_memory_table (void)
{
  hash_init (&mapped_table, memory_map_hash, memory_map_less, 0);
  lock_init (&mapped_table_lock);
}

struct mapped_entry*
create_mapped_entry (int map, int fd, void* addr, size_t size)
{
 if (map < 0 || fd < 2 || addr == NULL || size == 0)
    return NULL;

  struct mapped_entry *entry = malloc (sizeof (struct mapped_entry));

  entry->mid = map;
  entry->fd = fd;
  entry->addr = addr;
  entry->size = size;

  lock_acquire (&mapped_table_lock);
  hash_insert (&mapped_table, &entry->hash_elem);
  thread_current()->mapped_item = entry;
  lock_release (&mapped_table_lock);

  return entry;
}

bool
remove_mapped_entry (int map)
{
  lock_acquire (&mapped_table_lock);
  struct mapped_entry *e = find_mapped_entry (map);
  if (e == NULL)
  {
    lock_release (&mapped_table_lock);
    return false;
  }
  struct mapped_entry *entry = hash_entry (hash_delete (&mapped_table, &e->hash_elem), struct mapped_entry, hash_elem);

  struct page_entry *page = fetch_page (entry->addr);
  if (page != NULL)
    write_out_file (page);

  free (entry);
  lock_release (&mapped_table_lock);
  return true;
}

struct mapped_entry*
find_mapped_entry (int map)
{
  struct mapped_entry temp;
  temp.mid = map;
  struct hash_elem *e = hash_find (&mapped_table, &temp.hash_elem);

  return e != NULL ? hash_entry (e, struct mapped_entry, hash_elem) : NULL;
}


//---Hash table helper functions---//
// Source: http://web.stanford.edu/class/cs140/projects/pintos/pintos_6.html#SEC123
unsigned
memory_map_hash (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct mapped_entry *e = hash_entry (elem, struct mapped_entry, hash_elem);
  return hash_bytes (&e->mid, sizeof e->mid);
}

/* Returns true if page a precedes page b. */
bool
memory_map_less (const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED)
{
  const struct mapped_entry *a = hash_entry (elem_a, struct mapped_entry, hash_elem);
  const struct mapped_entry *b = hash_entry (elem_b, struct mapped_entry, hash_elem);

  return a->mid < b->mid;
}
