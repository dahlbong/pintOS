#define USERPROG

#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/palloc.h"
#include "threads/init.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void addr_validation(const char addr);

void halt(void);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int exec(const char *addr);
int fork(const char *thread_name, struct intr_frame *_if);
int wait(int pid);
int open(const char *file);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

void addr_validation(const char addr) {
	struct thread *cur = thread_current ();
    if (addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(cur->pml4, addr) == NULL) 
        exit(-1);		
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	switch (f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
		
	printf ("system call!\n");
	thread_exit ();
	}
}




int exec(const char *addr) {
	addr_validation(addr);

	char *addr_copy;
	addr_copy = palloc_get_page(0);
	if (addr_copy == NULL)
		exit(-1);
	strlcpy(addr_copy, addr, PGSIZE);

	if (process_exec(addr) == -1)
		exit(-1);
}

void halt(void) {
	power_off();
}

void exit(int status) {
	struct thread *cur = thread_current();
	cur->exit_status = status;

	printf("%s: exit(%d)\n", thread_name(), cur->exit_status);
	thread_exit();
}

bool create(const char *file, unsigned initial_size) {
	addr_validation(file);
	return filesys_create(file, initial_size);		// 파일 이름과 파일 사이즈를 인자 값으로 받아 파일을 생성하는 함수
}

bool remove(const char *file) {
	addr_validation(file);
	return filesys_remove(file);					// 파일 제거 성공 시 true, 실패 시 false
}

int fork(const char *thread_name, struct intr_frame *_if) {
	return process_fork(thread_name, _if);
}

int wait(int pid) {
	return process_wait(pid);
}

int open(const char *file) {
	addr_validation(file);
	struct file *file_open = filesys_open(file);
	if (file_open == NULL)
		return -1;
	int fd = process_add_file(file_open);
	if (fd == -1) 
		file_close(file_open);
	return fd;
}