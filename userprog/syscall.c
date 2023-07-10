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
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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
	
	lock_init(&open_lock);
}

void
address_valid(void *addr)
{
	struct thread *current=thread_current();
	//printf("%d\n", is_user_vaddr(addr));
	//printf("%x\n", pml4_get_page(current->pml4, addr));
	if(is_user_vaddr(addr) && addr!=NULL && pml4_get_page(current->pml4, addr)!=NULL)
	{
		return;
	}
	else
	{
		//printf("asdf");
		exit(-1);
	}
}
/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	//printf ("system call!\n");
	//struct thread *current=thread_current();
	//printf("system call!\n");
	//printf("%d\n", f->R.rax);
	switch(f->R.rax)
	{
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			
			//memcpy(&thread_current()->tf, f, sizeof(struct intr_frame));
			f->R.rax=fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax=wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax=create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax=remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax=open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax=filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax=read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax=write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax=tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;

	}
}

void halt()
{
	power_off();
}

void exit(int status)
{
	struct thread *current=thread_current();
	current->exit_status=status;
	printf("%s: exit(%d)\n", current->name, status);
	thread_exit();
}

int fork(const char *thread_name, struct intr_frame *f)
{
	address_valid(thread_name);
	return process_fork(thread_name, f);
}

int exec(const char *cmd_line)
{
	address_valid(cmd_line);
	char *executable=palloc_get_page(PAL_ZERO);
	//char *executable=(char *)malloc(sizeof(cmd_line));
	if(executable==NULL)
	{
		exit(-1);
	}
	//memcpy(executable, cmd_line, strlen(cmd_line)+1);
	strlcpy(executable, cmd_line, strlen(cmd_line)+1);
	if(process_exec(executable)==-1)
	{
		exit(-1);
		//return -1;
	}
}

int wait(tid_t pid)
{
	return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
	address_valid(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	address_valid(file);
	return filesys_remove(file);
}

int open(const char *file)
{
	address_valid(file);
	struct file *file_opened=filesys_open(file);
	struct thread *current=thread_current();
	struct file **fdt=current->descriptor_table;
	if(file_opened==NULL)
	{
		return -1;
	}
	int i=2;
	while(i<PGSIZE/sizeof(fdt[2]))
	{
		/*
		if(fdt[i]==file_opened)
		{
			//fdt[i]=fdt[i]+1;
			current->open_index=i;
		}
		*/
		if(fdt[i]==NULL)
		{
			fdt[i]=file_opened;
			current->open_index=i;
			return i;
		}
	i++;
	}
	file_close(file_opened);
	return -1;
}

int filesize(int fd)
{
	struct thread *current=thread_current();
	struct file *file_fd=current->descriptor_table[fd];
	if(file_fd==NULL)
	{
		return -1;
	}
	return file_length(file_fd);
}

int read(int fd, void *buffer, unsigned size)
{
	address_valid(buffer);
	struct thread *current=thread_current();
	if(fd==0)
	{
		lock_acquire(&open_lock);
		int read_bytes=input_getc();
		lock_release(&open_lock);
		return read_bytes;
	}
	else if(fd>=2 && fd<PGSIZE/sizeof(current->descriptor_table[2]))
	{
		struct file *file_fd=current->descriptor_table[fd];
		lock_acquire(&open_lock);
		if(file_fd==NULL)
		{
			lock_release(&open_lock);
			return -1;
		}
		int read_bytes=file_read(file_fd, buffer, size);
		lock_release(&open_lock);
		return read_bytes;
	}
	else
	{
		return -1;
		
	}
}

int write(int fd, void *buffer, unsigned size)
{
	address_valid(buffer);
	//printf("write : %d", fd);
	struct thread *current=thread_current();
	if(fd==1)
	{
		//lock_acquire(&open_lock);
		putbuf(buffer, size);
		//lock_release(&open_lock);
		return size;
	}
	else if(fd>=2 && fd<PGSIZE/sizeof(current->descriptor_table[2]))	
	{
		lock_acquire(&open_lock);
		struct file *file_fd=current->descriptor_table[fd];

		if(file_fd==NULL)
		{
			lock_release(&open_lock);
			return -1;
		}
		else
		{
			int write_bytes=file_write(file_fd, buffer, size);
			lock_release(&open_lock);
			return write_bytes;
		}
	}
	else
	{
		return -1;
	}
}

void seek(int fd, unsigned position)
{
	struct thread *current=thread_current();
	struct file *file=current->descriptor_table[fd];
	if(file!=NULL)
	{
		file_seek(file, position);
	}
}

unsigned tell(int fd)
{
	struct thread *current=thread_current();
	if(fd<2)
	{
		return;
	}
	struct file *file=current->descriptor_table[fd];
	if(file!=NULL)
	{
		file_tell(file);
	}
}

void close(int fd)
{
	struct thread *current=thread_current();
	if(fd<2 || fd>=PGSIZE/sizeof(current->descriptor_table[2]))
	{
		return;
	}
	struct file *file=current->descriptor_table[fd];
	if(file==NULL)
	{
		return;
	}
	else
	{
		lock_acquire(&open_lock);
		file_close(file);
		current->descriptor_table[fd]=NULL;
		lock_release(&open_lock);
	}
}
