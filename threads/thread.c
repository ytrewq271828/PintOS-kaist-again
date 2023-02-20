#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
/* List of threads in suspended state. Implemented for 1-1 : Alarm Clock*/
static struct list suspend_list;
/* List of all threads */
static struct list every_thread_list;

static struct semaphore semaT;
/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
//typedef int FPReal;

extern int64_t tick_to_awake;
static FPReal load_avg; 			/* Ready list load average value - used in 1.3 */

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);
//bool thread_tick_compare(const struct list_elem *temp1, const struct list_elem *temp2, void *aux);


/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the global thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&suspend_list);
	list_init (&every_thread_list);
	list_init (&destruction_req);
	//load_avg=0;

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	list_push_back(&every_thread_list, &(initial_thread->elem2));
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
	//initial_thread->donate_flag=0;
	//initial_thread->donated_flag=0;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);
	//load_avg=0;
	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
	{
		//for(;;);
		return TID_ERROR;
	}
	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	struct thread *current=thread_current();
	/* Append to the child list of current thread*/
	list_push_back(&current->child_list, &t->child_elem);
	//t->parent=thread_current();

	/* File descriptor table initialization*/
	/* 0 for stdout / 1 for stdin / 2+ for other opened files */
	/* Set the length of table to 3 - default value 2 + 1*/
	t->descriptor_table=palloc_get_page(PAL_ZERO);
	if(t->descriptor_table==NULL)
	{
		//for(;;);
		return TID_ERROR;
	}
	
	t->open_index=2; /* Opened file index starts from 2 */
	t->descriptor_table[0]=0;
	t->descriptor_table[1]=1;
	//t->fdt_limit=128;


	//list_init(&t->donate_list);
	//list_init(&t->lock_list);
	thread_unblock (t);

	/* Add to run queue. */
	if(whether_to_yield()) // If priority of current thread is larger than that of ready_list
	{
		thread_yield();	
	}
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	//FIFO implementation : list_push_back
	//But we need to check the priority of threads via "priority" element of threads
	
	//list_push_back (&ready_list, &t->elem); // Original
	list_insert_ordered(&ready_list, &t->elem, thread_priority_compare, NULL);

	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	list_remove(&thread_current()->elem2);
	intr_disable ();
	
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */

void
ready_list_iterate(void)
{
	struct list_elem *elem;
	int i=0;
	//printf("Length of ready_list : %d\n", list_size(&ready_list));
	for(elem=list_begin(&ready_list);elem!=list_end(&ready_list);elem=list_next(elem))
	{
		struct thread *ready=list_entry(elem, struct thread, elem);
		//printf("Element thread #%d : tid = %d , exit_status = %d\n", i, ready->tid, ready->exit_status);
		i=i+1;
	}
}
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered (&ready_list, &curr->elem, thread_priority_compare, NULL);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}
void
thread_suspend(int64_t suspendTicks) {
	enum intr_level old_level; 
	struct thread *currThread=thread_current(); // Current thread
	
	
	old_level=intr_disable(); // Temporarily disable the interrupt. return value : INTR_ON
	//ASSERT (!intr_context()); // Check if the CPU is ready to receive interrupts
	if (currThread!=idle_thread) // Unless otherwise idle
	{
		//sema_init(&semaT, 0);
		//printf("Going to Suspend\n");
		
		//currThread=thread_current();
		currThread->suspend_ticks=suspendTicks; // Set the suspend_ticks parameter value of current thread
		if(suspendTicks<tick_to_awake)
		{
			tick_to_awake=suspendTicks;
		}
		list_push_back (&suspend_list, &currThread->elem); // Put the current job in the suspended state list
		//list_insert_ordered (&suspend_list,&currThread->elem, thread_tick_compare, NULL);
		//printf("Pushed to suspend_list\n");
		//sema_down(&semaT);
		//thread_block(); // Block the thread until thread_unblock() is called. Will not be run.
		
		// Sorting : just to call list_remove only once inside the while block
		// I initially called list_next but it gives me odd behaviors consistently
		// So I sorted the suspend_list to use break statement
		list_sort(&suspend_list, thread_tick_compare, NULL);
	}
	do_schedule(THREAD_BLOCKED);
	intr_set_level (old_level); // Enables CPU of interrupt again
	
	
}
// Helper function to sort the suspend_list at the below function
bool
thread_tick_compare(const struct list_elem *temp1, const struct list_elem *temp2, void *aux) {
	int64_t tick1=list_entry(temp1, struct thread, elem)->suspend_ticks;
	int64_t tick2=list_entry(temp2, struct thread, elem)->suspend_ticks;
	return tick1<tick2;
}
void
thread_unsuspend(int64_t unsuspendTicks) {
	// Jobs of each thread are in the suspend_list
	//list_sort(&suspend_list, thread_tick_compare, NULL);
	struct list_elem *element=list_begin(&suspend_list); // To iterate through the suspend_list
	//struct list_elem *tail_sl=list_tail(&suspend_list);
	//struct list_elem *currThread=list_begin(&suspend_list);
	//struct list_elem *element;
	int64_t new_tick_to_awake=INT64_MAX;
		

	while(element!=list_end(&suspend_list))// End condition of iteration
	//while(currThread!=list_end(&suspend_list))
	{
		//element=list_pop_front(&suspend_list);
		struct thread *currThread=list_entry(element, struct thread, elem); // Get the elem of entry struct element
		//struct thread *currJob=element;
		//printf("Unsuspend loop on\n");
		ASSERT(currThread!=NULL);
		if(unsuspendTicks<currThread->suspend_ticks){  // if the parameter unsuspendTick is smaller than the 
													   // suspend_ticks value of the thread of entry element
			//element=list_remove(element);
			//currThread=list_next(currThread);
			//printf("Give me next one\n");
			//continue;
			//printf("Gonna break\n");
			//list_push_back(&suspend_list, element);
			element=list_next(element);
			//continue;
			//list_sort(&suspend_list, thread_tick_compare, NULL);
			//break;
			if(currThread->suspend_ticks<new_tick_to_awake)
			{
				new_tick_to_awake=currThread->suspend_ticks;
			}
		}
		else
		{
			element=list_remove(&currThread->elem);
			thread_unblock(currThread);
			
		}
		//barrier();
		//element=list_remove(element); // Loop condition	
		//if(list_empty(&suspend_list)){
		//	break;
		//}
		//struct thread *currThread=list_entry(element, struct thread, elem);
		
		
		//printf("Unblocked the thread\n");
		//currThread=list_remove(currThread);
	
	}
	tick_to_awake=new_tick_to_awake;
	//list_sort(&suspend_list, thread_tick_compare, NULL);
}
bool
thread_priority_compare(const struct list_elem *temp1, const struct list_elem *temp2, void *aux) {
	int priority1=list_entry(temp1, struct thread, elem)->priority;
	int priority2=list_entry(temp2, struct thread, elem)->priority;
	//Below return gives me the reverse order, so I changed the direction of inequality.
	//return priority1<=priority2;
	return priority1>priority2;
}
void
thread_donate(struct thread *thread)
{
	
	for(int i=0;i<8;i++)
	{
		int priority=thread->priority;
		if(thread->lock_waiting==NULL)
		{
			break;
		}
		struct thread *lock_holder=thread->lock_waiting->holder;
		lock_holder->priority=priority;
		thread=lock_holder;
	}
}
/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	
	if(!thread_mlfqs)
	{
		struct thread *curr=thread_current();
		curr->priority_original=new_priority;
		curr->priority=curr->priority_original;
		if(!list_empty(&curr->donate_list))
		{
			int priority_max=list_entry(list_begin(&curr->donate_list), struct thread, donate_elem)->priority;	
			if(curr->priority<priority_max)
			{
				curr->priority=priority_max;
			}
		}
		/*
		thread_current()->priority_original=new_priority;
		
		if(thread_current()->priority<new_priority || thread_current()->priority_original==thread_current()->priority)
		{
			thread_current()->priority = new_priority;
		}
		*/
		//Sorting not required in this function : already sorted by priority in thread_unblock and thread_yield
	
		
	
		if(whether_to_yield())
		{
			thread_yield();
		}
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	
	return thread_current()->priority;
}
// Returns whether the current thread's priority is lower than that of the first element of ready_list. 
bool
whether_to_yield(void) {
	if(list_empty(&ready_list))
	{
		return false;
	}
	int currPriority=thread_current()->priority;
	int readyPriority=list_entry(list_front(&ready_list), struct thread, elem)->priority;
	return (currPriority<readyPriority && !intr_context()); //intr_context() : Are we dealing with external interrupt?
}
void
mlfqs_set_priority(struct thread *thread)
{
	int f = 1<<14;
	int priority;
	if(thread!=idle_thread)
	{
		FPReal recent_cpu_temp=(thread->recent_cpu)/4;
		if(recent_cpu_temp>=0)
		{
			priority=(PRI_MAX-(recent_cpu_temp+f/2)/f-(thread->nice_value*2));
		}
		else
		{
			priority=(PRI_MAX-(recent_cpu_temp-f/2)/f-(thread->nice_value*2));
		}
		//int priority_temp=(PRI_MAX-(thread->recent_cpu)/4-(thread->nice_value*2)*f);
		if (priority>PRI_MAX)
		{
			priority=PRI_MAX;
		}
		if (priority<PRI_MIN)
		{
			priority=PRI_MIN;
		}
		thread->priority=priority;

	}
}
/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level=intr_disable();
	if(thread_current()!=idle_thread)
	{
		int f=1<<14;
		thread_current()->nice_value=nice;
		mlfqs_set_priority(thread_current());
		if(whether_to_yield())
		{
			thread_yield();
		}
	}
	intr_set_level(old_level);
	
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level=intr_disable();
	int nice_value=thread_current()->nice_value;
	intr_set_level(old_level);
	return nice_value;
}
/* 
Fixed point real arithmetic is required
p.q fixed point format : 1 sign bit + p bits before the point + q fractional bits
14.17 fixed point format is applied
*/
/* Returns 100 times the system load average. */
FPReal
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level=intr_disable();
	int f = 1<<14;
	FPReal load_avg_return=((load_avg*100)+f/2)/f;
	intr_set_level(old_level);
	return load_avg_return;	
}

void
calculate_load_avg (void)
{
	int ready_threads=list_size(&ready_list);
	if(thread_current()!=idle_thread)
	{
		ready_threads=ready_threads+1;
	}
	//printf("%d\n", ready_threads);

	int f = 1 << 14;
	//printf("--------------");
	FPReal coef=(59*f)/60;
	load_avg=(int64_t)coef*load_avg/f+(coef/59)*ready_threads;
	//printf("%d\n", load_avg);
	//return load_avg
}
/* Returns 100 times the current thread's recent_cpu value. */
FPReal
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level=intr_disable();
	FPReal recent_cpu_temp=thread_current()->recent_cpu*100;
	int f = 1 << 14;
	FPReal recent_cpu_return;
	if(recent_cpu_temp>=0)
	{
		//return (recent_cpu_temp+f/2)/f;
		recent_cpu_return=(recent_cpu_temp+f/2)/f;
	}
	else
	{
		recent_cpu_return=(recent_cpu_temp-f/2)/f;
	}
	intr_set_level(old_level);
	return recent_cpu_return;
	
}
void
thread_calculate_recent_cpu(struct thread *thread)
{
	int f = 1 << 14;
	if(thread!=idle_thread)
	{
	FPReal coef=((int64_t)(load_avg*2)*f/(2*load_avg+1*f));
	thread->recent_cpu=((int64_t)coef)*thread->recent_cpu/f+thread->nice_value*f;
	}
}
void
increment_recent_cpu(struct thread *thread)
{
	int f=1<<14;
	if(thread!=idle_thread)
	{
		thread->recent_cpu=thread->recent_cpu+1*f;
	}
}

void
every_calculate_mlfqs_priority()
{
	struct list_elem *element;
	if(list_empty(&every_thread_list))
	{
		return;
	}
	for(element=list_begin(&every_thread_list);element!=list_end(&every_thread_list);element=list_next(element))
	{
		struct thread *thread=list_entry(element, struct thread, elem2);
		mlfqs_set_priority(thread);
	}
	//list_sort(&ready_list, thread_priority_compare, NULL);

}

void
every_calculate_recent_cpu()
{
	struct list_elem *element;
	if(list_empty(&every_thread_list))
	{
		return;
	}
	for(element=list_begin(&every_thread_list);element!=list_end(&every_thread_list);element=list_next(element))
	{
		struct thread *thread=list_entry(element, struct thread, elem2);
		thread_calculate_recent_cpu(thread);
	}

}


/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	
	
	list_init(&t->lock_list);
	list_init(&t->donate_list);
	t->lock_waiting=NULL;
	t->priority_original=priority;
	//t->donate_flag=0;
	//t->donated_flag=0;
	t->nice_value=0;
	t->recent_cpu=0;
	

	/* Project 2 : USERPROG */
	t->exit_status=0;
	list_init(&t->child_list);
	sema_init(&t->fork_sema, 0);
	sema_init(&t->wait_sema, 0);
	sema_init(&t->exit_sema, 0);
	//lock_init(&t->open_lock);
	t->open_index=2;

	t->running_executable=NULL;
	//enum intr_level old_level;
	//old_level=intr_disable();
	if(t!=idle_thread)
	{
		list_push_back(&every_thread_list, &t->elem2);
	}
	//intr_set_level(old_level);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static 
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
