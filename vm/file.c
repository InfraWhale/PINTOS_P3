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
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	uint64_t *pml4 = thread_current()->pml4;
	struct supplemental_page_table *spt = &thread_current()->spt;


	if(page->frame != NULL){
		// 페이지가 수정되었는지 확인
		if(pml4_is_dirty(pml4, page->va))// 페이지가 수정되었으면 디스크에 있는 파일에 반영
			file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->ofs);
		
		pml4_clear_page(pml4, page->va);
		hash_delete(&spt->pages, &page->page_elem);

		// TODOTODO : 프레임 테이블에서 프레임 제거해야될거 같음
		free(page->frame);
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

	lock_acquire(&filesys_lock);
	file_seek(info->file, info->ofs);
	int read_bytes = file_read(info->file, kpage, info->page_read_bytes);
	if (read_bytes != (int)info->page_read_bytes){
		palloc_free_page(kpage);
		free(aux);
		lock_release(&filesys_lock);
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
	lock_release(&filesys_lock);

	return true;
}