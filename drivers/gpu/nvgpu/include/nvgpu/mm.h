/*
 * Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef NVGPU_MM_H
#define NVGPU_MM_H

#include <nvgpu/vm.h>
#include <nvgpu/types.h>
#include <nvgpu/cond.h>
#include <nvgpu/thread.h>
#include <nvgpu/lock.h>
#include <nvgpu/atomic.h>
#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/allocator.h>
#include <nvgpu/list.h>
#include <nvgpu/sizes.h>
#include <nvgpu/mmu_fault.h>

struct gk20a;
struct vm_gk20a;
struct nvgpu_mem;
struct nvgpu_pd_cache;

enum nvgpu_flush_op {
	NVGPU_FLUSH_DEFAULT,
	NVGPU_FLUSH_FB,
	NVGPU_FLUSH_L2_INV,
	NVGPU_FLUSH_L2_FLUSH,
	NVGPU_FLUSH_CBC_CLEAN,
};

struct mm_gk20a {
	struct gk20a *g;

	/* GPU VA default sizes address spaces for channels */
	struct {
		u64 user_size;   /* userspace-visible GPU VA region */
		u64 kernel_size; /* kernel-only GPU VA region */
	} channel;

	struct {
		u32 aperture_size;
		struct vm_gk20a *vm;
		struct nvgpu_mem inst_block;
	} bar1;

	struct {
		u32 aperture_size;
		struct vm_gk20a *vm;
		struct nvgpu_mem inst_block;
	} bar2;

	struct engine_ucode {
		u32 aperture_size;
		struct vm_gk20a *vm;
		struct nvgpu_mem inst_block;
	} pmu, sec2, gsp;

	struct {
		/* using pmu vm currently */
		struct nvgpu_mem inst_block;
	} hwpm;

	struct {
		struct vm_gk20a *vm;
		struct nvgpu_mem inst_block;
	} perfbuf;

	struct {
		struct vm_gk20a *vm;
	} cde;

	struct {
		struct vm_gk20a *vm;
	} ce;

	struct nvgpu_pd_cache *pd_cache;

	struct nvgpu_mutex l2_op_lock;
	struct nvgpu_mutex tlb_lock;
	struct nvgpu_mutex priv_lock;

	struct nvgpu_mem bar2_desc;

	struct nvgpu_mem hw_fault_buf[NVGPU_MMU_FAULT_TYPE_NUM];
	struct mmu_fault_info fault_info[NVGPU_MMU_FAULT_TYPE_NUM];
	struct nvgpu_mutex hub_isr_mutex;
#ifdef CONFIG_NVGPU_CE
	/*
	 * Separate function to cleanup the CE since it requires a channel to
	 * be closed which must happen before fifo cleanup.
	 */
	void (*remove_ce_support)(struct mm_gk20a *mm);
#endif
	void (*remove_support)(struct mm_gk20a *mm);
	bool sw_ready;
	int physical_bits;
	bool use_full_comp_tag_line;
	bool ltc_enabled_current;
	bool ltc_enabled_target;
	bool disable_bigpage;

	struct nvgpu_mem sysmem_flush;

	u32 pramin_window;
	struct nvgpu_spinlock pramin_window_lock;

	struct {
		size_t size;
		u64 base;
		size_t bootstrap_size;
		u64 bootstrap_base;

		struct nvgpu_allocator allocator;
		struct nvgpu_allocator bootstrap_allocator;

		u32 ce_ctx_id;
		volatile bool cleared;
		struct nvgpu_mutex first_clear_mutex;

		struct nvgpu_list_node clear_list_head;
		struct nvgpu_mutex clear_list_mutex;

		struct nvgpu_cond clearing_thread_cond;
		struct nvgpu_thread clearing_thread;
		struct nvgpu_mutex clearing_thread_lock;
		nvgpu_atomic_t pause_count;

		nvgpu_atomic64_t bytes_pending;
	} vidmem;

	struct nvgpu_mem mmu_wr_mem;
	struct nvgpu_mem mmu_rd_mem;
};

#define gk20a_from_mm(mm) ((mm)->g)
#define gk20a_from_vm(vm) ((vm)->mm->g)

static inline u32 bar1_aperture_size_mb_gk20a(void)
{
	return 16U; /* 16MB is more than enough atm. */
}

/* The maximum GPU VA range supported */
#define NV_GMMU_VA_RANGE          38

/* The default userspace-visible GPU VA size */
#define NV_MM_DEFAULT_USER_SIZE   (1ULL << 37)

/* The default kernel-reserved GPU VA size */
#define NV_MM_DEFAULT_KERNEL_SIZE (1ULL << 32)

/*
 * When not using unified address spaces, the bottom 56GB of the space are used
 * for small pages, and the remaining high memory is used for large pages.
 */
static inline u64 nvgpu_gmmu_va_small_page_limit(void)
{
	return ((u64)SZ_1G * 56U);
}

#ifdef CONFIG_NVGPU_CE
void nvgpu_init_mm_ce_context(struct gk20a *g);
#endif
int nvgpu_init_mm_support(struct gk20a *g);

int nvgpu_alloc_inst_block(struct gk20a *g, struct nvgpu_mem *inst_block);
u64 nvgpu_inst_block_addr(struct gk20a *g, struct nvgpu_mem *inst_block);
u32 nvgpu_inst_block_ptr(struct gk20a *g, struct nvgpu_mem *inst_block);
void nvgpu_free_inst_block(struct gk20a *g, struct nvgpu_mem *inst_block);

int nvgpu_mm_suspend(struct gk20a *g);
u32 nvgpu_mm_get_default_big_page_size(struct gk20a *g);
u32 nvgpu_mm_get_available_big_page_sizes(struct gk20a *g);

int nvgpu_mm_setup_hw(struct gk20a *g);

#endif /* NVGPU_MM_H */
