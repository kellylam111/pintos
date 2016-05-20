#include "vm/swap.h"

#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define SWAP_PIECE (PGSIZE / BLOCK_SECTOR_SIZE)

void
initialize_swap_table ()
{
  lock_init (&table_lock);
  swap_block = block_get_role (BLOCK_SWAP);
  ASSERT (swap_block != NULL)
  bitmap = bitmap_create (block_size (swap_block) / SWAP_PIECE);
  ASSERT (bitmap != NULL)
  bitmap_set_all(bitmap, 0);
}

/* Fetch from swap table */
void
fetch_from_swap (size_t swap, void *address)
{
  lock_acquire (&table_lock);

  for (size_t i = 0; i < SWAP_PIECE; i++)
    block_read(swap_block, SWAP_PIECE * swap + i, (uint8_t *) address + i * BLOCK_SECTOR_SIZE);
  
  bitmap_flip(bitmap, swap);
  lock_release (&table_lock);
}

/* Write page to swap table */
size_t
write_to_swap (void *address)
{
  lock_acquire (&table_lock);

  size_t val = bitmap_scan_and_flip (bitmap, 0, 1, 0);
  ASSERT (val != BITMAP_ERROR); // Check if full

  for (size_t i = 0; i < SWAP_PIECE; i++)
    block_write (swap_block, SWAP_PIECE * val + i, (uint8_t *) address + (i * BLOCK_SECTOR_SIZE));

  lock_release (&table_lock);
  return val;
}
