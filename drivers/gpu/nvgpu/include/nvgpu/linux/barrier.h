/*
 * Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __NVGPU_BARRIER_LINUX_H__
#define __NVGPU_BARRIER_LINUX_H__

#include <asm/barrier.h>

#define nvgpu_mb_impl()	mb()
#define nvgpu_rmb_impl()	rmb()
#define nvgpu_wmb_impl()	wmb()

#define nvgpu_smp_mb_impl()	smp_mb()
#define nvgpu_smp_rmb_impl()	smp_rmb()
#define nvgpu_smp_wmb_impl()	smp_wmb()

#define NV_ACCESS(x)	ACCESS_ONCE(x)

#define nvgpu_speculation_barrier_impl() speculation_barrier()

#endif /* __NVGPU_BARRIER_LINUX_H__ */
