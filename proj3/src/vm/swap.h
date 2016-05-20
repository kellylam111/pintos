#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#include <bitmap.h>

struct lock table_lock;
struct block *swap_block;
struct bitmap *bitmap;

void initialize_swap_table (void);
void fetch_from_swap (size_t swap, void *address);
size_t write_to_swap (void *address);

#endif /* vm/swap.h */
