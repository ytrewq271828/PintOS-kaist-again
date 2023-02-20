/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}


/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// In the semaphore there is a list of waiting jobs, called waiters
		// Insert the jobs so that the waiters list can be sorted by priority
		//list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, thread_priority_compare, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	/* Flag to check whether to yield - if we unblocked the thread and priority conditions are met then yield */
	int unblocked=0;
	if (!list_empty (&sema->waiters)){
		/* Sort the jobs in list waiters by priority */
		list_sort(&sema->waiters,thread_priority_compare, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
		unblocked=1;
	}
	sema->value++;
	if(whether_to_yield() && unblocked==1) // If priority of current thread is larger than that of ready_list
	{
		thread_yield();	
	}
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	//lock->holdee = NULL;
	sema_init (&lock->semaphore, 1);
	//lock.elem=NULL;
}

/*
void
lock_acquire_recursive(struct lock *lock, struct thread *lock_holdee_thread)
{
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	
	//list_insert_ordered(&lock->semaphore.waiters, &lock_holdee_thread, thread_priority_compare, NULL);
	
	//lock->holder = thread_current ();
	//thread_current()->lock=lock;		//thread_current() is not always holding the lock - we need to specify the lock's holder explicitly
	struct thread *lock_holder_thread=lock->holder;
	struct list_elem *e;
	struct list_elem *f;
	struct thread *lock_holdee_thread_search;
	//struct thread *lock_holdee_thread=thread_current;
	struct lock *lockk=lock_holdee_thread->lock_waiting; // Same as lock in first recursive iteration
	
	if(lock_holdee_thread->lock_waiting!=NULL && lock_holder_thread!=NULL)
	{
		sema_down(&lock->semaphore);
		printf("Loop just started\n");
		ASSERT(list_size(&lock->semaphore.waiters)!=0);
		printf("ASSERTED\n");
		for(e=list_begin(&lock->semaphore.waiters);e!=list_end(&lock->semaphore.waiters);e=list_next(e))
		{
			
			printf("First Loop\n");
			if(donation_required(lock, e)==true) 
			{
				printf("Donation Setting");
				list_sort(&lock->semaphore.waiters, thread_priority_compare, NULL);
				lock_holdee_thread_search=list_entry(e, struct thread, elem);
				lock_holdee_thread_search->lock_waiting=lock;
				//lock->holder->priority=lock_holdee_thread->priority;
				lock_holder_thread->donate_flag=1;
				lock_holdee_thread_search->donated_flag=1;
				list_insert_ordered(&lock_holder_thread->donate_list, &lock_holdee_thread_search->elem, thread_priority_compare, NULL);
				//list_insert_ordered(&thread_current()->donee, lock_holdee_thread->elem, thread_priority_compare, NULL);
			}
			printf("ggg");
		}
		ASSERT(list_size(&lock_holder_thread->donate_list)!=0);
		printf("Half Loop\n");
		for(f=list_begin(&lock_holder_thread->donate_list);f!=list_end(&lock_holder_thread->donate_list);f=list_next(f))
		{
			ASSERT(list_size(&lock_holder_thread->donate_list)==0);
			printf("Second Loop\n");
			struct thread *donor_thread=list_entry(f, struct thread, elem);
			lock_holder_thread->priority=donor_thread->priority;
			if(lock_holder_thread->lock_waiting!=NULL)
			{
				lock_holder_thread=lock_holder_thread->lock_waiting->holder;
				lock_holdee_thread_search=lock_holder_thread;
				//lock=lock_holdee_thread_search->lock_waiting;
				if(lock_holdee_thread_search->lock_waiting!=NULL)
				{
					lock_acquire_recursive(lock_holdee_thread_search->lock_waiting, lock_holdee_thread_search);
				}
			}
			
			
			else
			{
				lock=lock_holdee_thread_search->lock_waiting;
				lock_holder_thread=NULL;
			}
			
			
		}
	}
	return;
}
*/
/*
	Current thread is acquiring the lock - there's nothing in lock->semaphore.waiters now
	The element is added to waiters list when we down the semaphore
	So we need to down the semaphore in the last stage of acquiring

	I first thought that single thread can donate its priority to multiple threads at the same time
	So I created donate_list in the struct thread and searched through donate_list when donating
	But a thread can only donate the priority to single thread - so we don't need to search through donate_list

	Therefore my initial implementation searches through watiers and donate_list
	But these are not required - so the implementation became way more simple

*/
bool
thread_donate_priority_compare(const struct list_elem *temp1, const struct list_elem *temp2, void *aux) {
	int priority1=list_entry(temp1, struct thread, donate_elem)->priority;
	int priority2=list_entry(temp2, struct thread, donate_elem)->priority;
	//Below return gives me the reverse order, so I changed the direction of inequality.
	//return priority1<=priority2;
	return priority1>priority2;
}
/* 
Priority donation while acquiring the lock
struct thread should receive the lock holder's priority
*/
void
lock_acquire_nonrecursive(struct lock *lock, struct thread *thread)
{
	ASSERT (!lock==NULL);
	//ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));
	
	/* Priority donor thread */
	struct thread* donor_thread=NULL;
	/* Thread checking whether to donate - lock_holder_thread work as priority donee */
	//struct thread* lock_holder_thread=thread->lock_waiting->holder;
	//ASSERT(!lock_holder_thread==NULL)
	//struct list_elem* e;
	//struct list_elem* f;
	int nest=0;
	struct thread *thread_pointer=thread;
	list_insert_ordered(&lock->holder->donate_list, &thread->donate_elem, thread_donate_priority_compare, NULL);
	
	/* While the donee thread is not null*/
	while(thread_pointer->lock_waiting!=NULL)
	{
		if(nest>7)
		{
			break;
		}
		//sema_down(&lock->semaphore);
		//printf("Loop just started\n");
		//ASSERT(list_size(&lock->semaphore.waiters)!=0);
		//printf("ASSERTED\n");
		/*
		if(list_size(&lock->semaphore.waiters)==0)
		{
			break;
		}
		*/
		//for(e=list_begin(&lock->semaphore.waiters);e!=list_end(&lock->semaphore.waiters);e=list_next(e))
		//{
		//thread=thread_current();
			//printf("First Loop\n");
			
		if(donation_required(thread_pointer)) 
		{
			//printf("Half Loop\n");
			//printf("Donation Setting");
			//if(list_empty(&lock->semaphore.waiters))
			//{
				//thread->lock_waiting=lock;
				//thread->donate_flag=1;
				//lock->holder->donated_flag=1;
				//list_insert_ordered(&lock->holder->donate_list, &thread->elem, thread_priority_compare, NULL);
			//}
			/*
			else
			{
				list_sort(&lock->semaphore.waiters, thread_priority_compare, NULL);
				//lock_holdee_thread_search=list_entry(list_begin(&lock->semaphore.waiters), struct thread, elem);
				//lock_holdee_thread_search->lock_waiting=lock;
				//lock_holdee_thread_search->donated_flag=1;
				thread_temp=list_entry(list_begin(&lock->semaphore.waiters), struct thread, elem);
				thread_temp->lock_waiting=lock;
				thread_temp->donate_flag=1;
				lock_holder_thread->donated_flag=1;
				list_insert_ordered(&lock_holder_thread->donate_list, &thread_temp->elem, thread_priority_compare, NULL);
			}
			//list_insert_ordered(&lock_holder_thread->donate_list, &lock_holdee_thread_search->elem, thread_priority_compare, NULL);
			*/
			//ASSERT(!list_empty(&thread->lock_waiting->holder->donate_list));
			//printf("%d\n", (int)list_size(&lock_holder_thread->donate_list));
			//list_push_front(&lock_holder_thread->lock_list, &lock->elem);
			//list_insert_ordered(&thread_current()->donee, lock_holdee_thread->elem, thread_priority_compare, NULL);
			//printf("Second Loop\n");
			
			
			//printf("%d\n", donor_thread->priority);
			
			//printf("Nested\n");
			thread_pointer->lock_waiting->holder->priority=thread_pointer->priority;
			thread_pointer=thread_pointer->lock_waiting->holder;
			nest++;
			//lock=lock_holder_thread->lock_waiting;
		
			
			/*  Nested donation check
			if donor thread have lock waiting for other thread, nested donation is required
			Then we need to set lock_holder_thread to donee thread (the "thread" pointer)
			and lock_holder_thread's donor thread to donor thread (the "lock_holder_thread" pointer)
			And just follow the loop again - loop works as donation process 
		*/
		
			
		}
		/*  If donation is not required, we just need to down the semaphore and set the owner of the lock to current thread
			So break the loop and just follow the code in lock_acquire() */
		else
		{
			break;
		}
		
		/*
		else
		{
			//lock=lock_holder_thread->lock_waiting;
			printf("Gonna Break\n");
			break;
		}
		*/
		
	//}
	//ASSERT(list_size(&lock_holder_thread->donate_list)!=0);
	
	//for(f=list_begin(&lock_holder_thread->donate_list);f!=list_end(&lock_holder_thread->donate_list);f=list_next(f))
	//{
		//ASSERT(list_size(&lock_holder_thread->donate_list)==0);
		
		//struct thread *donor_thread=list_entry(f, struct thread, elem);
		//list_sort(&lock_holder_thread->donate_list, thread_priority_compare, NULL);
		
		
		
	//}
	}
	
}	

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	/*  lock_holdee_thread pointer is not necessary - because the lock holdee will be thread_current()
		But the lock holder is present always - just  refer to it by lock->holder */
	//struct thread *lock_holdee_thread;
	//struct lock *current_lock;
	struct list_elem *e;
	struct thread *current=thread_current();
	//memset(current_lock, 0, sizeof(struct lock));
	//memcpy(current_lock, lock, sizeof(struct lock));	//deep copy of lock -> Lock can be changed during recursive steps
	//memset(lock_holdee_thread, 0, sizeof(struct thread));
	//memcpy(lock_holdee_thread, thread_current(), sizeof(struct thread));
	//sema_down (&lock->semaphore);
	//lock->holder = thread_current ();
	//thread_current()->lock=lock;		
	//sema_down (&lock->semaphore);
	//ASSERT(lock->holder!=NULL);
	if(!thread_mlfqs && lock->holder!=NULL)
	{
		//lock_acquire_recursive(lock, lock_holdee_thread);
		//printf("Holder not null\n");
		//printf("%d\n", current->priority);
		
		/*
		thread_current() should wait for the lock holder thread to release the lock
		So I created lock_waiting argument in the struct thread
		and thread_current()->lock_waiting should be the argument lock temporarily
		Then lock_acquire_nonrecursive deals with priority donation
		*/
		current->lock_waiting=lock;
		lock_acquire_nonrecursive(lock, current);
		
		//thread_current()->priority=list_entry(list_begin(&thread_current()->donate_list), struct thread, elem)->priority;
	}
	/*
	sema_down(&lock->semaphore);
	thread_current()->lock_waiting=NULL;
	list_push_back(&thread_current()->lock_list, &current_lock->elem);
	lock->holder=thread_current();
	*/
	//printf("Sema will go down\n")

	/*  
	After donation - down the semaphore to stop current thread's job
	The role of function lock_acquire is to make the current thread to lock's holder thread
	Now thread_current() becomes lock's holder, not the holdee
	lock_list is the list of thread's locks holding, so we need to append the lock to the lock_list of current thread
	*/
    //list_sort(&current->donate_list, thread_donate_priority_compare, NULL);
	sema_down(&lock->semaphore);
	current->lock_waiting=NULL;
	lock->holder=current;
	if(!thread_mlfqs)
	{
		list_push_front(&current->lock_list, &lock->elem);
	}
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	/* thread_current()==lock->holder */
	ASSERT (lock_held_by_current_thread (lock)); 
	struct thread *current=thread_current();
	struct list_elem *e;  /* For iterating through lock_list of thread_current() */
	struct list_elem *f;  /* For iterating through waiters list of each lock */
	struct list_elem *g;
	struct list *waiters; /* Pointer to list waiters of each semaphore */
	
	//thread_current()->priority=thread_current()->priority_original;
	lock->holder = NULL;
	if(!thread_mlfqs)
	{
		for(g=list_begin(&current->donate_list);g!=list_end(&current->donate_list);g=list_next(g))
		{
			struct thread *lockThread=list_entry(g, struct thread, donate_elem);
			struct lock *lock_each=lockThread->lock_waiting;
			if(lock_each==lock)
			{
				//printf("lol\n");
				list_remove(&lockThread->donate_elem);
				//printf("done\n");
				
			}
		}
		//printf("lollol\n");
		list_remove(&lock->elem);
		if(!list_empty(&thread_current()->lock_list))
		{
			/* max_priority : the variable used for checking the maximum priority in each iteration */
			int max_priority=thread_current()->priority_original;
			/* 
			thread_current() can have multiple locks - these locks are in the lock_list list
			Therefore we need to iterate through thread_current() and check every lock's semaphore
			And check the highest priority value among the jobs in the semaphore's waiters list
			*/
			for(e=list_begin(&thread_current()->lock_list);e!=list_end(&thread_current()->lock_list);e=list_next(e))
			{
				struct lock *e_lock=list_entry(e, struct lock, elem);
				waiters=&e_lock->semaphore.waiters;
				if(!list_empty(waiters))
				{
					//for(f=list_begin(&waiters);f!=list_end(&waiters);f=list_next(f))
					//{
						list_sort(waiters, thread_priority_compare, NULL);
						/* lock_holdee_thread : The highest priority thread in the waiters list*/
						struct thread *lock_holdee_thread=list_entry(list_begin(waiters), struct thread, elem);
						if(lock_holdee_thread->priority>max_priority)
						{
							max_priority=lock_holdee_thread->priority;
						}
					//}
				}
			}
	
			/* Resetting the priority - with the highest priority job among jobs in the locks that thread_current() holds  */
			thread_current()->priority=max_priority;
		
			//if(max_priority>current_priority)
			//{
			//	thread_current()->priority=max_priority;
			//}
		
		}
		/*If current thread does not have locks - just original priority becomes the priority*/
		else
		{
			thread_current()->priority=thread_current()->priority_original;
		}

		struct thread *curr=thread_current();
		curr->priority=curr->priority_original;
		if(!list_empty(&curr->donate_list))
		{
			int priority_max=list_entry(list_begin(&curr->donate_list), struct thread, donate_elem)->priority;
			if(priority_max>curr->priority)
			{
				curr->priority=priority_max;
			}
		}
		
	}
	/* Releasing the lock - holder becomes NULL and up the semaphore */
	
	sema_up (&lock->semaphore);
	
	/*
	if(list_empty(&lock->semaphore.waiters))
	{
		thread_current()->lock=NULL;
		thread_current()->donated_flag=0;
	}
	*/
	
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}
/*  If the lock holder's priority is lower than current thread's priority
	Then donation is required 
	I tried to check the donation condition using the lock but of no result
	The donor is current thread - so we only need the current thread to check the donation condition */
bool
donation_required(struct thread *thread) {
	
	//ASSERT (thread->lock_waiting!=NULL);
	if (thread->lock_waiting==NULL)
	{
		return false;
	}
	if (thread->lock_waiting->holder==NULL)
	{
		return false;
	}
	int holder_priority=thread->lock_waiting->holder->priority;
	int holdee_priority=thread->priority;
	//int holdee_priority=list_entry(element, struct thread, elem)->priority;
	//int holdee_priority=list_entry(list_begin(&lock->semaphore.waiters), struct thread, elem)->priority;
	//if(list_empty(&lock->semaphore.waiters))
	//{
	
	//}
	/*
	else
	{
		holdee_priority=list_entry(list_begin(&lock->semaphore.waiters), struct thread, elem)->priority;
	}
	*/
	return holder_priority<holdee_priority;
}
/*
void
lock_donate(const struct lock *lock) {
	ASSERT (lock!=NULL);
	if(donation_required(lock))
	{
		//lock->holder->priority=list_entry(lock->semaphore.waiters, struct thread, lock)->priority;
	}
}
*/
/*
void
lock_regain(const struct lock *lock) {
	ASSERT (lock!=NULL);
	thread_current()->priority=lock->holder->priority;
}
*/

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/*
void
semaphore_compare(struct list_elem *temp1, struct list_elem *temp2, void *aux)
{
	struct semaphore_elem *sema_elem1=list_entry(temp1, struct semaphore_elem, elem);
	struct semaphore_elem *sema_elem2=list_entry(temp2, struct semaphore_elem, elem);
}
*/
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	//list_push_back (&cond->waiters, &waiter.elem);
	/* Setting the semaphore's priority to the thread_current()'s priority - to deal with semaphore priority problems */
	waiter.semaphore.priority=thread_current()->priority;
	list_insert_ordered(&cond->waiters, &waiter.elem, semaphore_waiter_compare, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
	{
		/* Sort and up the semaphore */
		list_sort(&cond->waiters, semaphore_waiter_compare, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters)){
		//list_sort(&cond->waiters, semaphore_waiter_compare, NULL);
		cond_signal (cond, lock);
	}
}

/*
Helper function to compare the semaphore's priority
I added the list_elem *elem to the semaphore_elem - to put the semaphore_elems in the list
We can sort semaphore_elems in the list using this helper function
*/
bool
semaphore_waiter_compare(struct list_elem *elem1, struct list_elem *elem2)
{
	struct semaphore sema1=list_entry(elem1, struct semaphore_elem, elem)->semaphore;
	struct semaphore sema2=list_entry(elem2, struct semaphore_elem, elem)->semaphore;
	//list_sort(&sema1.waiters, thread_priority_compare, NULL);
	//list_sort(&sema2.waiters, thread_priority_compare, NULL);
	//struct thread *thread1=list_begin(&sema1.waiters);
	//struct thread *thread2=list_begin(&sema2.waiters);
	return sema1.priority>sema2.priority;
}

