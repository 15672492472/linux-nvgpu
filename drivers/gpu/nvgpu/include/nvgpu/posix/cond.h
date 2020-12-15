/*
 * Copyright (c) 2018-2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef NVGPU_POSIX_COND_H
#define NVGPU_POSIX_COND_H

#include <nvgpu/static_analysis.h>
#include <nvgpu/bug.h>
#include <nvgpu/lock.h>

/**
 * Define value used to indicate a wait without timeout.
 */
#define NVGPU_COND_WAIT_TIMEOUT_MAX_MS	~0U

struct nvgpu_cond {
	/**
	 * Indicates the initialization status of the condition variable.
	 */
	bool initialized;
	/**
	 * Mutex associated with the condition variable.
	 */
	struct nvgpu_mutex mutex;
	/**
	 * Underlying pthread condition variable.
	 */
	pthread_cond_t cond;
	/**
	 * Attributes associated with the condition variable.
	 */
	pthread_condattr_t attr;
};

#ifdef NVGPU_UNITTEST_FAULT_INJECTION_ENABLEMENT
struct nvgpu_posix_fault_inj *nvgpu_cond_get_fault_injection(void);
#endif

/**
 * @brief Timed wait for a condition variable.
 *
 * @param cond [in]	Condition variable to wait.
 * @param ms [in]	Timeout to wait.
 *
 * Waits for a condition variable for the time duration passed as param \a ms.
 *
 * @return If successful, this function returns 0. Otherwise, an error number
 * is returned to indicate the error.
 */
int nvgpu_cond_timedwait(struct nvgpu_cond *c, unsigned int *ms);

/**
 * @brief Signal a condition variable.
 *
 * @param cond [in]	Condition variable to signal.
 *
 * Wakes up a waiter for a condition variable to check if its condition has
 * been satisfied. This API has to be used after explicitly locking the mutex
 * associated with the condition variable.
 */
void nvgpu_cond_signal_locked(struct nvgpu_cond *cond);

/**
 * @brief Signal all waiters of a condition variable.
 *
 * @param cond [in]	Condition variable to broadcast.
 *
 * Wake up all waiters for a condition variable to check if their conditions
 * have been satisfied. This API has to be used after explicitly locking the
 * mutex associated with the condition variable.
 *
 * @return If successful a value of 0 shall be returned; otherwise, an error
 * number to indicate the error is returned.
 */
int nvgpu_cond_broadcast_locked(struct nvgpu_cond *cond);

/**
 * @brief Acquire the mutex associated with condition variable.
 *
 * @param cond [in]	Condition variable for which the mutex has to be
 *			acquired.
 *
 * Acquires the mutex associated with the condition variable referenced
 * by the param \a cond.
 */
void nvgpu_cond_lock(struct nvgpu_cond *cond);

/**
 * @brief Release the mutex associated with condition variable.
 *
 * @param cond [in]	Condition variable for which the mutex has to be
 *			released.
 *
 * Releases the mutex associated with the condition variable referenced
 * by the param \a cond.
 */
void nvgpu_cond_unlock(struct nvgpu_cond *cond);

/**
 * @brief Wait for a condition to be true.
 *
 * @param cond [in]		The condition variable to sleep on.
 * @param condition [in]	The condition that needs to be checked.
 * @param timeout_ms [in]	Timeout in milliseconds or 0 for infinite wait.
 *
 * Wait for a condition to become true. Differentiates between timed wait
 * and infinite wait from the parameter \a timeout_ms. Returns -ETIMEOUT if
 * the wait timed out with condition false.
 */
#define NVGPU_COND_WAIT_LOCKED(cond, condition, timeout_ms)	\
({								\
	int ret = 0;						\
	u32 cond_timeout_ms = (timeout_ms);			\
	NVGPU_COND_WAIT_TIMEOUT_LOCKED((cond), (condition),	\
		(ret),						\
		((cond_timeout_ms) != 0U) ? (cond_timeout_ms) :	\
		NVGPU_COND_WAIT_TIMEOUT_MAX_MS);		\
	ret;							\
})

/**
 * @brief Initiate a wait for a condition variable.
 *
 * @param cond [in]		The condition variable to sleep on.
 * @param condition [in]	The condition that needs to be true.
 * @param timeout_ms [in]	Timeout in milliseconds or 0 for infinite wait.
 *
 * Wait for a condition to become true. Returns -ETIMEOUT if the wait timed out
 * with condition false. Acquires the mutex associated with the condition
 * variable before attempting to wait.
 */
#define NVGPU_COND_WAIT(cond, condition, timeout_ms)			\
({									\
	int cond_wait_ret = 0;						\
	u32 cond_wait_timeout = (timeout_ms);				\
	nvgpu_mutex_acquire(&(cond)->mutex);				\
	NVGPU_COND_WAIT_TIMEOUT_LOCKED((cond), (condition),		\
		(cond_wait_ret),					\
		(cond_wait_timeout != 0U) ?				\
			(cond_wait_timeout) :				\
			NVGPU_COND_WAIT_TIMEOUT_MAX_MS);		\
	nvgpu_mutex_release(&(cond)->mutex);				\
	cond_wait_ret;							\
})

/**
 * @brief Interruptible wait for a condition to be true.
 *
 * @param cond [in]		The condition variable to sleep on.
 * @param condition [in]	The condition that needs to be true.
 * @param timeout_ms [in]	Timeout in milliseconds or 0 for infinite wait.
 *
 * In posix implementation the functionality of interruptible wait is same as
 * uninterruptible wait. Macro is defined to be congruent with implementations
 * which has interruptible and uninterruptible waits.
 */
#define NVGPU_COND_WAIT_INTERRUPTIBLE(cond, condition, timeout_ms) \
			NVGPU_COND_WAIT((cond), (condition), (timeout_ms))

/**
 * @brief Wait for a condition to be true.
 *
 * @param cond [in]		The condition variable to sleep on.
 * @param condition [in]	The condition that needs to be true.
 * @param ret [out]		Return value.
 * @param timeout_ms [in]	Timeout in milliseconds or 0 for infinite wait.
 *
 * Wait for a condition to become true. Returns -ETIMEOUT if the wait timed out
 * with condition false.
 */
#define NVGPU_COND_WAIT_TIMEOUT_LOCKED(cond, condition, ret, timeout_ms)\
do {									\
	unsigned int cond_wait_timeout_timeout = (timeout_ms);		\
	ret = 0;							\
	while (!(condition) && ((ret) == 0)) {				\
		ret = nvgpu_cond_timedwait(cond,			\
				&cond_wait_timeout_timeout);		\
	}								\
NVGPU_COV_WHITELIST(false_positive, NVGPU_MISRA(Rule, 14_4), "Bug 2623654") \
} while (false)

#endif /* NVGPU_POSIX_COND_H */
