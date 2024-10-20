/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/mmu.h"
#include "userprog/syscall.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
static bool lazy_load_file(struct page *page, void *aux);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	pml4_set_page(page->page_thread->pml4, page->va, page->frame->kva, page->is_writable);
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page = &page->file;
	struct file *file = file_page->file;
	file_seek(file, file_page->ofs);
	file_read_at(file, kva, file_page->page_read_bytes, file_page->ofs);

	// struct list_elem *e;
	struct frame *frame = page->frame;
	// for (e = list_begin (&frame->conn_page_list); e != list_end (&frame->conn_page_list); e = list_next (e)) {
	// 	struct page *page = list_entry(e, struct page, conn_elem);
	// 	if(!pml4_set_page(page->page_thread->pml4, page->va, frame->kva, false))
	// 		return false;
	// }

	list_remove(&frame->evict_elem);
	return true;
}

static bool check_dirty(struct frame *frame){
	struct list_elem *e;
	for (e = list_begin (&frame->conn_page_list); e != list_end (&frame->conn_page_list); e = list_next (e)) {
		struct page *page = list_entry(e, struct page, conn_elem);
		if(pml4_is_dirty(page->page_thread->pml4, page->va))
			return true;
	}
	return false;
}

static void set_dirty(struct frame *frame, bool dirty){
	struct list_elem *e;
	for (e = list_begin (&frame->conn_page_list); e != list_end (&frame->conn_page_list); e = list_next (e)) {
		struct page *page = list_entry(e, struct page, conn_elem);
		pml4_set_dirty(page->page_thread->pml4, page->va, dirty);
	}
}

static void for_pml4_clear_page(struct frame *frame){
	struct list_elem *e;
	for (e = list_begin (&frame->conn_page_list); e != list_end (&frame->conn_page_list); e = list_next (e)) {
		struct page *page = list_entry(e, struct page, conn_elem);
		pml4_clear_page(page->page_thread->pml4, page->va);
	}
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	uint64_t *pml4 = thread_current()->pml4;
	struct file_page *file_page = &page->file;

	if(check_dirty(page->frame)){
		struct file *file = file_page->file;
		file_seek(file, file_page->ofs);
		file_write_at(file, page->frame->kva, file_page->page_read_bytes, file_page->ofs);
		set_dirty(page->frame, false);
	}

	for_pml4_clear_page(page->frame);

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	uint64_t *pml4 = thread_current()->pml4;
	struct supplemental_page_table *spt = &thread_current()->spt;

	hash_delete(&spt->pages, &page->page_elem);

	if(page->frame != NULL){
		// 페이지가 수정되었는지 확인
		if(check_dirty(page->frame)){// 페이지가 수정되었으면 디스크에 있는 파일에 반영
			file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->ofs);
			set_dirty(page->frame, false);
		}

		pml4_clear_page(pml4, page->va);

		list_remove(&page->conn_elem);
		if(list_empty(&page->frame->conn_page_list)){
			// if(page->frame->kva)
			// 	palloc_free_page(page->frame->kva);
			free(page->frame);
			list_remove(&page->frame->frame_elem);
		}
		page->frame = NULL;
	}
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	void *result = addr;
	size_t file_start_page = addr;
	size_t file_end_page = addr + length;
	off_t file_size = file_length(file);
	length = file_size > length ? length : file_size;

	while (length > 0){
		size_t read_bytes = length > PGSIZE ? PGSIZE : length;
		size_t zero_bytes = PGSIZE - read_bytes;

		struct load_info *aux = malloc(sizeof(struct load_info));
		aux->file = file_reopen(file);
		aux->ofs = offset;
		aux->writable = writable;
		aux->page_read_bytes = read_bytes;
		aux->page_zero_bytes = zero_bytes;
		aux->file_start_page = file_start_page;
		aux->file_end_page = file_end_page;

		if(!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_file, aux))
			return NULL;
		
		offset += read_bytes;
		length -= read_bytes;
		addr += read_bytes;
		// TODO : 중간에 실패하면 기존에 만들었던 aux를 free해야되지 않을까?
	}
	return result;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page = spt_find_page(spt, addr);
	if (page == NULL)
		return;
	struct file_page file_page = page->file;
	if(file_page.file_start_page != addr)
		return;

	for(void *va = addr; va < file_page.file_end_page; va += PGSIZE){
		struct page *p = spt_find_page(spt, va);
		if(p == NULL)
			return;

		spt_remove_page(spt, p);
	}
}

static bool lazy_load_file(struct page *page, void *aux){
	struct load_info *info = (struct load_info *)aux;
	uint8_t *kpage = page->frame->kva;

	file_seek(info->file, info->ofs);
	int read_bytes = file_read(info->file, kpage, info->page_read_bytes);
	if (read_bytes != (int)info->page_read_bytes){
		palloc_free_page(kpage);
		free(aux);
		return false;
	}
	memset(kpage + info->page_read_bytes, 0, info->page_zero_bytes);

	page->file.file = info->file;
	page->file.file_start_page = info->file_start_page;
	page->file.file_end_page = info->file_end_page;
	page->file.page_read_bytes = info->page_read_bytes;
	page->file.page_zero_bytes = info->page_zero_bytes;
	page->file.ofs = info->ofs;

	free(aux);

	return true;
}