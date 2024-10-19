/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include <bitmap.h>
#include "threads/mmu.h"
#include "threads/malloc.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

struct swap_table{
	struct bitmap *swap_used_map;
};

static struct swap_table swap_table;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	disk_sector_t size = disk_size(swap_disk);
	swap_table.swap_used_map = bitmap_create(size / 8);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	struct frame *frame = &page->frame;

	int idx = frame->bit_idx;
	if(idx < 0) {
		return false;
	}
	
	for (int i = 0; i < 8; i++) {
		disk_sector_t sec_no = (disk_sector_t) idx*8 + i;
		disk_read(swap_disk, sec_no, kva + SECTOR_SIZE * i);
	}

	frame->bit_idx = -1;
	frame->kva = kva;
	bitmap_flip(swap_table.swap_used_map, idx);

	struct list_elem *e;
	for (e = list_begin (&frame->conn_page_list); e != list_end (&frame->conn_page_list); e = list_next (e)) {
		struct page *now_page = list_entry(e, struct page, conn_elem);
		pml4_set_page(now_page->page_thread->pml4, now_page->va, frame->kva, now_page->is_writable);
	}
	
	list_remove(&frame->evict_elem);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct frame *victim_frame = page->frame;
	int idx = bitmap_scan(swap_table.swap_used_map, 0, 1, false);
	if (idx == BITMAP_ERROR) {
		return false;
	}

	for (int i = 0; i < 8; i++) {
		disk_sector_t sec_no = (disk_sector_t) idx*8 + i;
		disk_write(swap_disk, sec_no, (victim_frame->kva) + SECTOR_SIZE * i);
	}

	victim_frame->bit_idx = idx;

	struct list_elem *e;
	for (e = list_begin (&victim_frame->conn_page_list); e != list_end (&victim_frame->conn_page_list); e = list_next (e)) {
		struct page *now_page = list_entry(e, struct page, conn_elem);
		pml4_clear_page(now_page->page_thread->pml4, now_page->va);
	}

	bitmap_flip(swap_table.swap_used_map, idx);

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// TODOTODO : pml4 에서 삭제, spt 에서 삭제, frame 삭제 해야하지 않을까?
	struct thread *cur = thread_current();
	hash_delete(&cur->spt.pages, &page->page_elem);
	if (page->frame){
		pml4_clear_page(cur->pml4, page->va);
		palloc_free_page(page->frame->kva); 
		list_remove(&page->frame->frame_elem);
		free(page->frame);
		page->frame = NULL;
	}
	
}
