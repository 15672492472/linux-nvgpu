/*
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __NVGPU_ATOMIC_H__
#define __NVGPU_ATOMIC_H__

#include <nvgpu/linux/atomic.h>

#define NVGPU_ATOMIC_INIT(i)	__nvgpu_atomic_init(i)
#define NVGPU_ATOMIC64_INIT(i)	__nvgpu_atomic64_init(i)

static inline void nvgpu_atomic_set(nvgpu_atomic_t *v, int i)
{
	__nvgpu_atomic_set(v, i);
}
static inline int nvgpu_atomic_read(nvgpu_atomic_t *v)
{
	return __nvgpu_atomic_read(v);
}
static inline void nvgpu_atomic_inc(nvgpu_atomic_t *v)
{
	__nvgpu_atomic_inc(v);
}
static inline int nvgpu_atomic_inc_return(nvgpu_atomic_t *v)
{
	return __nvgpu_atomic_inc_return(v);
}
static inline void nvgpu_atomic_dec(nvgpu_atomic_t *v)
{
	 __nvgpu_atomic_dec(v);
}
static inline int nvgpu_atomic_dec_return(nvgpu_atomic_t *v)
{
	return __nvgpu_atomic_dec_return(v);
}
static inline int nvgpu_atomic_cmpxchg(nvgpu_atomic_t *v, int old, int new)
{
	return __nvgpu_atomic_cmpxchg(v, old, new);
}
static inline int nvgpu_atomic_xchg(nvgpu_atomic_t *v, int new)
{
	return __nvgpu_atomic_xchg(v, new);
}
static inline bool nvgpu_atomic_inc_and_test(nvgpu_atomic_t *v)
{
	return __nvgpu_atomic_inc_and_test(v);
}
static inline bool nvgpu_atomic_dec_and_test(nvgpu_atomic_t *v)
{
	return __nvgpu_atomic_dec_and_test(v);
}
static inline bool nvgpu_atomic_sub_and_test(int i, nvgpu_atomic_t *v)
{
	return __nvgpu_atomic_sub_and_test(i, v);
}
static inline int nvgpu_atomic_add_return(int i, nvgpu_atomic_t *v)
{
	return __nvgpu_atomic_add_return(i, v);
}
static inline int nvgpu_atomic_add_unless(nvgpu_atomic_t *v, int a, int u)
{
	return __nvgpu_atomic_add_unless(v, a, u);
}
static inline void nvgpu_atomic64_set(nvgpu_atomic64_t *v, long i)
{
	return  __nvgpu_atomic64_set(v, i);
}
static inline long nvgpu_atomic64_read(nvgpu_atomic64_t *v)
{
	return  __nvgpu_atomic64_read(v);
}
static inline void nvgpu_atomic64_add(long x, nvgpu_atomic64_t *v)
{
	__nvgpu_atomic64_add(x, v);
}
static inline void nvgpu_atomic64_inc(nvgpu_atomic64_t *v)
{
	__nvgpu_atomic64_inc(v);
}
static inline long nvgpu_atomic64_inc_return(nvgpu_atomic64_t *v)
{
	return __nvgpu_atomic64_inc_return(v);
}
static inline void nvgpu_atomic64_dec(nvgpu_atomic64_t *v)
{
	__nvgpu_atomic64_dec(v);
}
static inline void nvgpu_atomic64_dec_return(nvgpu_atomic64_t *v)
{
	__nvgpu_atomic64_dec_return(v);
}
static inline long nvgpu_atomic64_cmpxchg(nvgpu_atomic64_t *v, long old,
					long new)
{
	return __nvgpu_atomic64_cmpxchg(v, old, new);
}
static inline void nvgpu_atomic64_sub(long x, nvgpu_atomic64_t *v)
{
	__nvgpu_atomic64_sub(x, v);
}
static inline long nvgpu_atomic64_sub_return(long x, nvgpu_atomic64_t *v)
{
	return __nvgpu_atomic64_sub_return(x, v);
}

#endif /* __NVGPU_ATOMIC_H__ */
