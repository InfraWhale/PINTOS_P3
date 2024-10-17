/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include <string.h>

static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED);
unsigned
frame_hash (const struct hash_elem *p_, void *aux UNUSED);

bool
frame_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED);

static struct page *page_lookup (struct supplemental_page_table *spt, const void *address);

struct hash frame_table;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
	hash_init(&frame_table, frame_hash, frame_less, NULL);
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	struct page* page = spt_find_page (spt, upage);

	/* Check wheter the upage is already occupied or not. */
	if (page == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		page = malloc(sizeof(struct page));
		if (page == NULL)
			goto err;
		bool (*initializer)(struct page *, enum vm_type, void *);
		switch (VM_TYPE(type)){
		case VM_ANON:
			initializer = anon_initializer;
			break;
		case VM_FILE:
			initializer = file_backed_initializer;
			break;
		default:
			goto err;
		}

		uninit_new(page, upage, init, type, aux, initializer);
		page->is_writable = writable;
		
		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	return page_lookup(spt, va);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	struct hash_elem * result = hash_insert(&spt->pages, &page->page_elem);
	if(result == NULL)
		succ = true;
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	ASSERT (frame != NULL);
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER);
	frame->page = NULL;
	if(frame->kva == NULL)
		PANIC ("todo");
	
	ASSERT (frame->page == NULL);
	
	hash_insert(&frame_table, &frame->frame_elem);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
    if(vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), 1))
		thread_current()->stack_bottom -= PGSIZE;
	
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	/* TODO: Validate the fault */
	if(addr == NULL || is_kernel_vaddr(addr)){
		return false;
	}
	
	if (!not_present){
		return false;
	}

	void *rsp = f->rsp; // user access인 경우 rsp는 유저 stack을 가리킨다.
    if (!user) // kernel access인 경우 thread에서 rsp를 가져와야 한다.
		rsp = thread_current()->rsp;

	// 스택 확장으로 처리할 수 있는 폴트인 경우, vm_stack_growth를 호출한다.
    if (rsp-8 <= addr  && USER_STACK - 0x100000 <= addr && addr <= USER_STACK)
		vm_stack_growth(pg_round_down(addr));
	
	struct page *page = spt_find_page(spt, addr);
	if (page == NULL){
		return false;
	}
	if (write && !page->is_writable){
		return false;
	}
	
	/* TODO: Your code goes here */
	bool success = vm_do_claim_page (page);
	return success;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct thread *cur = thread_current();
	struct page *page = spt_find_page(&cur->spt, va);
	/* TODO: Fill this function */
	if (page == NULL){
		return false;
	}
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */

	struct thread *cur = thread_current();
	pml4_set_page(cur->pml4, page->va, frame->kva, page->is_writable);
	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init (&spt->pages, page_hash, page_less, NULL);
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, page_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, page_elem);
  const struct page *b = hash_entry (b_, struct page, page_elem);

  return a->va < b->va;
}

unsigned
frame_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct frame *p = hash_entry (p_, struct frame, frame_elem);
  return hash_bytes (&p->kva, sizeof p->kva);
}

bool
frame_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct frame *a = hash_entry (a_, struct frame, frame_elem);
  const struct frame *b = hash_entry (b_, struct frame, frame_elem);

  return a->kva < b->kva;
}

/* Returns the page containing the given virtual address, or a null pointer if no such page exists. */
struct page *
page_lookup (struct supplemental_page_table *spt, const void *va) {
	struct page p;
	struct hash_elem *e;
	
	p.va = pg_round_down(va);
	e = hash_find (&spt->pages, &p.page_elem);
	return e != NULL ? hash_entry (e, struct page, page_elem) : NULL;
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	struct hash_iterator i;
	struct hash_elem *elem;
	hash_first(&i, &src->pages);
	while ((elem = hash_next(&i))){
		struct page *p = hash_entry(elem, struct page, page_elem);
		enum vm_type type = page_get_type(p);

		if (VM_TYPE(p->operations->type) == VM_UNINIT){
			//if(!vm_alloc_page_with_initializer(VM_ANON, p->va, p->is_writable, p->uninit.init, p->uninit.aux)) // 왜 ANON?
			struct load_info *copy_aux = (struct load_info *)malloc(sizeof(struct load_info));
			memcpy(copy_aux, p->uninit.aux, sizeof(struct load_info));
			if(!vm_alloc_page_with_initializer(type, p->va, p->is_writable, p->uninit.init, copy_aux))
				return false;
		}else{
			if(vm_alloc_page(type, p->va, p->is_writable)
			&& vm_claim_page(p->va)){
				struct page* copy = spt_find_page(dst, p->va);
				memcpy(copy->frame->kva, p->frame->kva, PGSIZE);
				copy->frame->page = copy;
			}else
				return false;
		}
		
	}
	return true;
	
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	* TODO: writeback all the modified contents to the storage. */
	// hash_destroy(&spt->pages, page_dealloc);
	hash_clear(&spt->pages, page_dealloc);
}

void page_dealloc(struct hash_elem *e, void *aux UNUSED) {
	struct page *target = hash_entry (e, struct page, page_elem);
	destroy(target);
    free(target);
}