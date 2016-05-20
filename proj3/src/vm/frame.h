#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

#include <debug.h>
#include <hash.h>
#include <stdint.h>

struct hash frame_table;		/* Frame Table implementation */
struct list frame_list;			/* Frame List implementation */
struct lock table_lock;			/* Lock to ensure atomic access to table */
struct lock removal_lock;		/* Lock used when evicting frmo table */

struct frame_entry {
	uint32_t *addr;				/* Frame returned from palloc_get_page*/
	struct page_entry *page;	/* Pointer to supplemental page */
	struct thread *owner;		/* Owner */
	uint64_t access_time;

	int32_t swap_index;

	struct hash_elem hash_entry;
	struct list_elem elem;
};

void initialize_frame_table (void);										/* Initialize table. */
void* create_frame (enum palloc_flags flags, struct page_entry *page);	/* Create new frame. */
void evict_frame_from_table (void);										/* Evict frame fromt table. */
bool remove_frame (struct frame_entry *frame);							/* Remove frame. */
struct frame_entry* fetch_frame (void *addr);							/* Fetches frame at address */

// Helper functions for hash table
unsigned frame_hash (const struct hash_elem *elem, void *aux UNUSED);
bool frame_less (const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED);

#endif /* vm/frame.h */
