/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "lib/string.h"

/* Project 2 */
uint64_t hash_func(const struct hash_elem *e, void *aux) {
	const struct page *p = hash_entry(e, struct page, elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
	const struct page *pa = hash_entry(a, struct page, elem);
	const struct page *pb = hash_entry(b, struct page, elem);
	return pa->va < pb->va;
}

void hash_destructor(struct hash_elem *e, void *aux) {
	const struct page *p = hash_entry(e, struct page, elem);
	destroy(p);
	free(p);
}

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
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

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
		struct page *page = (struct page*)malloc(sizeof(struct page));
		if(page == NULL)
			goto err;

		typedef bool (*initializerFunc)(struct page*, enum vm_type, void *);
		initializerFunc initializer = NULL;

		switch(VM_TYPE(type)) {
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
		}

		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;
		page->owner = thread_current();
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
// find va in spt, return va page
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {

	/* TODO: Fill this function. */
	struct page page;
	page.va = pg_round_down(va);
	struct hash_elem *e = hash_find(&spt->spt_hash, &page.elem);

	if(e != NULL)
		return hash_entry(e, struct page, elem);
	else
		return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {

	/* TODO: Fill this function. */
	if(!hash_insert(&spt->spt_hash, &page->elem))
		return true;
	
	return false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&thread_current()->spt.spt_hash, &page->elem);
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	
	/* TODO: The policy for eviction is up to you. */
	struct frame *victim = NULL;
	struct thread *curr = thread_current();
	
	// second-chance algorithm
	struct list_elem *e;
	for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
		victim = list_entry(e, struct frame, elem);
		struct page *page = victim->page;
		if (page == NULL)
			continue;
		
		struct thread *owner = page->owner;
		if (owner == NULL || owner->pml4 == NULL)
			continue;

		if(pml4_is_accessed(curr->pml4, victim->page->va))  // check frame is accessed recently
			pml4_set_accessed(curr->pml4, victim->page->va, false);  // if accessed, init access bit (false)
		else  // else return victim frame
			return victim;
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if(victim->page != NULL)
		swap_out(victim->page);

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	
	/* TODO: Fill this function. */
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	frame->kva = palloc_get_page(PAL_USER);  // allocate virtual memory

	if(frame->kva == NULL)
		frame = vm_evict_frame();  // swap out
	
	else
		// if allocate success, add frame into frame table
		list_push_back(&frame_table, &frame->elem);

	frame->page = NULL;
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	void *page_addr = pg_round_down(addr);
	if(vm_alloc_page(VM_ANON | VM_MARKER_0, addr, true)) {
		if (vm_claim_page(page_addr)) {
            struct thread *curr = thread_current();
            if (page_addr < curr->stack_bottom)
                curr->stack_bottom = page_addr;
        }
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
	void *old_kva = page->frame->kva;

	void *new_kva = palloc_get_page(PAL_USER);	// 새 프레임 할당하고

	if (new_kva == NULL)						// 할당 실패하면 false 처리
		return false;

	memcpy(new_kva, old_kva, PGSIZE);			// 기존 프레임 데이터 복사
	page->frame->kva = new_kva;					// 페이지 프레임 업데이트

	if(!pml4_set_page(thread_current()->pml4, page->va, page->frame->kva, page->accessible))
		return false;

	// 기존 프레임 해제
    palloc_free_page(old_kva);
	
	return true;
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {

	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	struct thread *curr = thread_current();

	struct supplemental_page_table *spt = &curr->spt;
	struct page *page = spt_find_page(&curr->spt, addr);

	if(addr == NULL || is_kernel_vaddr(addr))
		return false;

	if(!not_present && write) {
        if (page == NULL || !page->writable)	// 페이지가 NULL이거나 쓰기 가능하지 않으면 처리 불가
            return false;
        return vm_handle_wp(page);
    }
    
	if(page == NULL) {
		void *stack_pointer = user ? f->rsp : curr->stack_pointer;
		if (addr >= stack_pointer - 8 && addr >= STACK_LIMIT && addr < USER_STACK) {
			vm_stack_growth(curr->stack_bottom - PGSIZE);
			return true;
		}
		return false;
	}

	return vm_do_claim_page(page);
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
	
	/* TODO: Fill this function */
	struct page *page = spt_find_page(&thread_current()->spt, va);

	if(page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {

	struct frame *frame = vm_get_frame ();  // allocate frame

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// mapping virtual address - physical address and set writable
	if(!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
		return false;

	return swap_in (page, frame->kva);  // restore data in swap area
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

/* Copy supplemental page table from src to dst */
// for fork system call
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	
	//struct hash_iterator *iter;
	struct hash_iterator iter;
	struct page *src_page, *dst_page;
	enum vm_type type;
	void *upage;
	bool writable;

	hash_first(&iter, &src->spt_hash);

	while (hash_next(&iter)) {
		src_page = hash_entry(hash_cur(&iter), struct page, elem);
		type = src_page->operations->type;
		upage = src_page->va;
		writable = src_page->writable;

		if(type == VM_UNINIT) {  // if page is not yet init
			if(!vm_alloc_page_with_initializer(page_get_type(src_page),
					src_page->va, src_page->writable, src_page->uninit.init, src_page->uninit.aux))
				return false;
		} else {
            /* 이미 초기화된 페이지 처리 */
            if (!vm_alloc_page(type, upage, writable))
                return false;
            if (!vm_claim_page(upage))
                return false;

            dst_page = spt_find_page(dst, upage);
            if (dst_page == NULL)
                return false;

            /* 부모의 프레임에서 자식의 프레임으로 데이터 복사 */
            memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
		}
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	hash_clear(&spt->spt_hash, hash_destructor);
}

static bool vm_copy_claim_page(struct supplemental_page_table *dst, void *va, void *kva, bool writable) {
	struct page *page = spt_find_page(dst, va);
    if (page == NULL)
        return false;

    // struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	struct frame *frame = vm_get_frame();
	if (frame == NULL)
		return false;
    /* Set links */
    //page->accessible = writable;
    frame->page = page;
    page->frame = frame;
    //frame->kva = kva;
	// 부모의 데이터를 자식의 kva로 복사
    memcpy(frame->kva, kva, PGSIZE);

    // list_push_back(&frame_table, &frame->elem);
    if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, writable))
        return false;
    // return swap_in(page, frame->kva);
	return true;
}