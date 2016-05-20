#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

#include <debug.h>
#include <hash.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct lock read_lock;
struct lock write_lock;

enum page_type {
	ZERO,
	FILE,
	SWAP,
	MMAP
};

struct page_entry {
	enum page_type type;						/* Page type */
	void* addr;											/* Address of page in user memory */
	void* kpage;										/* Address of frame in kernel */
	uint32_t* pagedir;							/* Address of allcated page */
	bool pinned_page;								/* If page should be prevented from being evicted */
	bool writable;									/* If writable */
	bool loaded;										/* If currently in a frame. */

	struct file *file;							/* Pointer to file contained in page */
	int32_t ofs;										/* File offset */
	uint32_t read_bytes;						/* How much to read */
	uint32_t zero_bytes;

	struct hash_elem hash_entry;		/* Hash element*/
};

void initialize_page_table (struct hash *table);
struct page_entry* create_sup_file_page (void* upage, bool writable, struct file *file,
									off_t ofs, uint32_t read_bytes, uint32_t zero_bytes);	/* Create & add a new page to table. */
struct page_entry* create_sup_mmap_page (void* upage, bool writable, struct file *file,
                  					off_t ofs, uint32_t read_bytes, uint32_t zero_bytes);
bool grow_stack (void* upage);																/* Create & add a new zero page to table. */
struct page_entry* fetch_page (void* user_vaddr);
bool load_page (struct page_entry *page);
bool unload_page (struct page_entry *page);
bool remove_page (struct page_entry *page);
void write_out_file (struct page_entry *page);
bool in_stack_range (const void *esp, void *addr);

// Helper functions for hash table
unsigned page_hash (const struct hash_elem *elem, void *aux UNUSED);
bool page_less (const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux UNUSED);

#endif /* vm/frame.h */
