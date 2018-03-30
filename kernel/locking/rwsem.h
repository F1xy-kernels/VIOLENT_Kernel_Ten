/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __INTERNAL_RWSEM_H
#define __INTERNAL_RWSEM_H
#include <linux/rwsem.h>

extern void __down_read(struct rw_semaphore *sem);
extern void __up_read(struct rw_semaphore *sem);

#ifdef CONFIG_DEBUG_RWSEMS
# define DEBUG_RWSEMS_WARN_ON(c)	DEBUG_LOCKS_WARN_ON(c)
#else
# define DEBUG_RWSEMS_WARN_ON(c)
#endif

#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
/*
 * All writes to owner are protected by WRITE_ONCE() to make sure that
 * store tearing can't happen as optimistic spinners may read and use
 * the owner value concurrently without lock. Read from owner, however,
 * may not need READ_ONCE() as long as the pointer value is only used
 * for comparison and isn't being dereferenced.
 */
static inline void rwsem_set_owner(struct rw_semaphore *sem)
{
	WRITE_ONCE(sem->owner, current);
}

static inline void rwsem_clear_owner(struct rw_semaphore *sem)
{
	WRITE_ONCE(sem->owner, NULL);
}

static inline void rwsem_set_reader_owned(struct rw_semaphore *sem)
{
	/*
	 * We check the owner value first to make sure that we will only
	 * do a write to the rwsem cacheline when it is really necessary
	 * to minimize cacheline contention.
	 */
	if (READ_ONCE(sem->owner) != RWSEM_READER_OWNED)
		WRITE_ONCE(sem->owner, RWSEM_READER_OWNED);
}

/*
 * Return true if the a rwsem waiter can spin on the rwsem's owner
 * and steal the lock, i.e. the lock is not anonymously owned.
 * N.B. !owner is considered spinnable.
 */
static inline bool is_rwsem_owner_spinnable(struct task_struct *owner)
{
	return !((unsigned long)owner & RWSEM_ANONYMOUSLY_OWNED);
}

/*
 * Return true if rwsem is owned by an anonymous writer or readers.
 */
static inline bool rwsem_has_anonymous_owner(struct task_struct *owner)
{
	return (unsigned long)owner & RWSEM_ANONYMOUSLY_OWNED;
}
#else
static inline void rwsem_set_owner(struct rw_semaphore *sem)
{
}

static inline void rwsem_clear_owner(struct rw_semaphore *sem)
{
}

static inline void rwsem_set_reader_owned(struct rw_semaphore *sem)
{
}
#endif

#ifdef CONFIG_RWSEM_PRIO_AWARE

#define RWSEM_MAX_PREEMPT_ALLOWED 3000

/*
 * Return true if current waiter is added in the front of the rwsem wait list.
 */
static inline bool rwsem_list_add_per_prio(struct rwsem_waiter *waiter_in,
				    struct rw_semaphore *sem)
{
	struct list_head *pos;
	struct list_head *head;
	struct rwsem_waiter *waiter = NULL;

	pos = head = &sem->wait_list;
	/*
	 * Rules for task prio aware rwsem wait list queueing:
	 * 1:	Only try to preempt waiters with which task priority
	 *	which is higher than DEFAULT_PRIO.
	 * 2:	To avoid starvation, add count to record
	 *	how many high priority waiters preempt to queue in wait
	 *	list.
	 *	If preempt count is exceed RWSEM_MAX_PREEMPT_ALLOWED,
	 *	use simple fifo until wait list is empty.
	 */
	if (list_empty(head)) {
		list_add_tail(&waiter_in->list, head);
		sem->m_count = 0;
		return true;
	}

	if (waiter_in->task->prio < DEFAULT_PRIO
		&& sem->m_count < RWSEM_MAX_PREEMPT_ALLOWED) {

		list_for_each(pos, head) {
			waiter = list_entry(pos, struct rwsem_waiter, list);
			if (waiter->task->prio > waiter_in->task->prio) {
				list_add(&waiter_in->list, pos->prev);
				sem->m_count++;
				return &waiter_in->list == head->next;
			}
		}
	}

	list_add_tail(&waiter_in->list, head);

	return false;
}
#else
static inline bool rwsem_list_add_per_prio(struct rwsem_waiter *waiter_in,
				    struct rw_semaphore *sem)
{
	list_add_tail(&waiter_in->list, &sem->wait_list);
	return false;
}
#endif
