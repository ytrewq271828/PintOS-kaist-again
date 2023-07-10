#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H
typedef int FPReal;

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	int priority_original; 				/* Original priority - to be accessed in lock_release */
	int64_t suspend_ticks; 				/* Ticks to be suspended for. */
	//struct semaphore sema;
	//struct lock *lock; 					/* Lock of current thread - NULL if not blocked */
	struct lock *lock_waiting; 			/* The lock that the current thread is waiting for */
	//int donate_flag;					/* 1 if the thread donated the priority to other thread - otherwise 0 */
	//int donated_flag;					/* 1 if the thread is donated the priority by other thread - otherwise 0 */
	struct list donate_list;			/* List of priority donors of the thread (Multiple donation is possible) */
	
	struct list lock_list; 				/* List of locks the thread holds */
	//struct list donee;				/* List of priority donees of the thread */
	//struct thread *lock_holder;		/* The lock holder of current blocked thread */
	int nice_value;
	FPReal recent_cpu;
	/* Projecdt 2 : USERPROG project */
	int exit_status; 					/* Userprog project : used in exit() system call */
	//struct intr_frame parent_intr_frame; /* Interrupt frame of parent process - to be copied to child process in process_fork() */
	//struct thread* parent;
	struct list child_list;				/* List of child processes - fork() can be done multiple times*/
	struct semaphore fork_sema;			/* Make the parent wait until child process finishes fork() */
	struct semaphore wait_sema; 		/* Make the parent thread wait for child processes and retrieve the exit status */
	struct semaphore exit_sema;			/* Make the parent thread wait until child processes exits */
	//struct lock open_lock;				/* Lock to implement synchronization of file open/close */
	struct file **descriptor_table;		/* File descriptor table - exist in each processes*/
	struct file *running_executable;    /* The name of executable currently running in the process */
	int open_index;						/* The index of opened file in file descriptor table (FDT)*/
	//int fdt_limit;						/* Limit of FDT length - set to PGSIZE/file descriptor pointer size == PGSIZE/8 */
	struct intr_frame parent_if;		/* Interrupt frame of parent thread*/

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	struct list_elem donate_elem; 		/* Element to find the thread in donate_list*/
	struct list_elem elem2; 			/* List element for every_list */
	/* Project 2 : USERPROG project */
	struct list_elem child_elem; 		/* List element for child_list */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void ready_list_iterate(void);
void thread_yield (void);

void thread_suspend (int64_t);
bool thread_tick_compare(const struct list_elem *temp1, const struct list_elem *temp2, void *aux);
void thread_unsuspend (int64_t);

bool thread_priority_compare(const struct list_elem *temp1, const struct list_elem *temp2, void *aux);
void thread_donate(struct thread *thread);
int thread_get_priority (void);
void thread_set_priority (int);
bool whether_to_yield(void);
void mlfqs_set_priority(struct thread *thread);
int thread_get_nice (void);
void thread_set_nice (int);
FPReal thread_get_recent_cpu (void);
FPReal thread_get_load_avg (void);
void calculate_load_avg (void);
void thread_calculate_recent_cpu(struct thread *thread);
void increment_recent_cpu(struct thread *thread);
void every_calculate_mlfqs_priority(void);
void every_calculate_recent_cpu(void);
void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */

