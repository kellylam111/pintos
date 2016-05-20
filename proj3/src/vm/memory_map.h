#ifndef VM_MEMORY_MAP_H
#define VM_MEMORY_MAP_H

#include "threads/synch.h"

#include <debug.h>
#include <hash.h>
#include <list.h>
#include <stdio.h>
#include <stdlib.h>


struct hash mapped_table;				/* Memory Map Table implementation */
struct lock mapped_table_lock;			/* Lock to ensure single access to table */

struct mapped_entry {
	int mid;					/* ID of mapped element */
	int fd;						/* Process file descriptor for file */
	void *addr;					/* Pointer to start */
	size_t size;				/* Size of entry*/

	struct hash_elem hash_elem;
};

void initialize_memory_table (void);										/* Initialize table. */
struct mapped_entry* create_mapped_entry (int map, int fd, void* addr, size_t size);		/* Create new mapped entry. */
bool remove_mapped_entry (int map);											/* Create new mapped entry. */
struct mapped_entry* find_mapped_entry (int map);							/* Find mapped entry. */

// Helper functions for hash table
unsigned memory_map_hash (const struct hash_elem *elem, void *aux UNUSED);
bool memory_map_less (const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED);

#endif /* vm/memory_map.h */
