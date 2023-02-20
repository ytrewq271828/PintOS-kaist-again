#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore {
	unsigned value;             /* Current value. */
	struct list waiters;        /* List of waiting threads. */
	int priority; 				/* Used in condvar */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	//struct thread *holdee;    /* Thread restricted by lock -> Inside the semaphore.waiters*/
	struct semaphore semaphore; /* Binary semaphore controlling access. */
	struct list_elem elem;		/* Element required since lock should be stored in list */
};

void lock_init (struct lock *);
void lock_acquire_nonrecursive(struct lock *lock, struct thread *thread);
bool thread_donate_priority_compare(const struct list_elem *temp1, const struct list_elem *temp2, void *aux);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);
bool donation_required(struct thread *thread);
//void lock_donate(const struct lock *lock);
//void lock_regain(const struct lock *lock);
/* Condition variable. */

struct condition {
	struct list waiters;        /* List of waiting threads. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);
bool semaphore_waiter_compare(struct list_elem *elem1, struct list_elem *elem2);
/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */

