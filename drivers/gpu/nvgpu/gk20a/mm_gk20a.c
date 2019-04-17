/*
 * Copyright (c) 2011-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <trace/events/gk20a.h>

#include <nvgpu/mm.h>
#include <nvgpu/vm.h>
#include <nvgpu/vm_area.h>
#include <nvgpu/dma.h>
#include <nvgpu/kmem.h>
#include <nvgpu/timers.h>
#include <nvgpu/pramin.h>
#include <nvgpu/list.h>
#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/allocator.h>
#include <nvgpu/semaphore.h>
#include <nvgpu/page_allocator.h>
#include <nvgpu/log.h>
#include <nvgpu/bug.h>
#include <nvgpu/log2.h>
#include <nvgpu/enabled.h>
#include <nvgpu/vidmem.h>
#include <nvgpu/sizes.h>
#include <nvgpu/io.h>
#include <nvgpu/utils.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/pd_cache.h>
#include <nvgpu/fence.h>

#include "mm_gk20a.h"

#include <nvgpu/hw/gk20a/hw_gmmu_gk20a.h>
#include <nvgpu/hw/gk20a/hw_pram_gk20a.h>

/*
 * GPU mapping life cycle
 * ======================
 *
 * Kernel mappings
 * ---------------
 *
 * Kernel mappings are created through vm.map(..., false):
 *
 *  - Mappings to the same allocations are reused and refcounted.
 *  - This path does not support deferred unmapping (i.e. kernel must wait for
 *    all hw operations on the buffer to complete before unmapping).
 *  - References to dmabuf are owned and managed by the (kernel) clients of
 *    the gk20a_vm layer.
 *
 *
 * User space mappings
 * -------------------
 *
 * User space mappings are created through as.map_buffer -> vm.map(..., true):
 *
 *  - Mappings to the same allocations are reused and refcounted.
 *  - This path supports deferred unmapping (i.e. we delay the actual unmapping
 *    until all hw operations have completed).
 *  - References to dmabuf are owned and managed by the vm_gk20a
 *    layer itself. vm.map acquires these refs, and sets
 *    mapped_buffer->own_mem_ref to record that we must release the refs when we
 *    actually unmap.
 *
 */

/* make sure gk20a_init_mm_support is called before */
int gk20a_init_mm_setup_hw(struct gk20a *g)
{
	struct mm_gk20a *mm = &g->mm;
	int err;

	nvgpu_log_fn(g, " ");

	if (g->ops.fb.set_mmu_page_size != NULL) {
		g->ops.fb.set_mmu_page_size(g);
	}

	if (g->ops.fb.set_use_full_comp_tag_line != NULL) {
		mm->use_full_comp_tag_line =
			g->ops.fb.set_use_full_comp_tag_line(g);
	}

	g->ops.fb.init_hw(g);

	if (g->ops.bus.bar1_bind != NULL) {
		g->ops.bus.bar1_bind(g, &mm->bar1.inst_block);
	}

	if (g->ops.bus.bar2_bind != NULL) {
		err = g->ops.bus.bar2_bind(g, &mm->bar2.inst_block);
		if (err != 0) {
			return err;
		}
	}

	if (g->ops.mm.cache.fb_flush(g) != 0 ||
	    g->ops.mm.cache.fb_flush(g) != 0) {
		return -EBUSY;
	}

	nvgpu_log_fn(g, "done");
	return 0;
}

/* for gk20a the "video memory" apertures here are misnomers. */
static inline u32 big_valid_pde0_bits(struct gk20a *g,
				      struct nvgpu_gmmu_pd *pd, u64 addr)
{
	u32 pde0_bits =
		nvgpu_aperture_mask(g, pd->mem,
				    gmmu_pde_aperture_big_sys_mem_ncoh_f(),
				    gmmu_pde_aperture_big_sys_mem_coh_f(),
				    gmmu_pde_aperture_big_video_memory_f()) |
		gmmu_pde_address_big_sys_f(
			   (u32)(addr >> gmmu_pde_address_shift_v()));

	return pde0_bits;
}

static inline u32 small_valid_pde1_bits(struct gk20a *g,
					struct nvgpu_gmmu_pd *pd, u64 addr)
{
	u32 pde1_bits =
		nvgpu_aperture_mask(g, pd->mem,
				    gmmu_pde_aperture_small_sys_mem_ncoh_f(),
				    gmmu_pde_aperture_small_sys_mem_coh_f(),
				    gmmu_pde_aperture_small_video_memory_f()) |
		gmmu_pde_vol_small_true_f() | /* tbd: why? */
		gmmu_pde_address_small_sys_f(
			   (u32)(addr >> gmmu_pde_address_shift_v()));

	return pde1_bits;
}

static void update_gmmu_pde_locked(struct vm_gk20a *vm,
				   const struct gk20a_mmu_level *l,
				   struct nvgpu_gmmu_pd *pd,
				   u32 pd_idx,
				   u64 virt_addr,
				   u64 phys_addr,
				   struct nvgpu_gmmu_attrs *attrs)
{
	struct gk20a *g = gk20a_from_vm(vm);
	bool small_valid, big_valid;
	u32 pd_offset = nvgpu_pd_offset_from_index(l, pd_idx);
	u32 pde_v[2] = {0, 0};

	small_valid = attrs->pgsz == GMMU_PAGE_SIZE_SMALL;
	big_valid   = attrs->pgsz == GMMU_PAGE_SIZE_BIG;

	pde_v[0] = gmmu_pde_size_full_f();
	pde_v[0] |= big_valid ?
		big_valid_pde0_bits(g, pd, phys_addr) :
		gmmu_pde_aperture_big_invalid_f();

	pde_v[1] |= (small_valid ? small_valid_pde1_bits(g, pd, phys_addr) :
		     (gmmu_pde_aperture_small_invalid_f() |
		      gmmu_pde_vol_small_false_f()))
		|
		(big_valid ? (gmmu_pde_vol_big_true_f()) :
		 gmmu_pde_vol_big_false_f());

	pte_dbg(g, attrs,
		"PDE: i=%-4u size=%-2u offs=%-4u pgsz: %c%c | "
		"GPU %#-12llx  phys %#-12llx "
		"[0x%08x, 0x%08x]",
		pd_idx, l->entry_size, pd_offset,
		small_valid ? 'S' : '-',
		big_valid ?   'B' : '-',
		virt_addr, phys_addr,
		pde_v[1], pde_v[0]);

	nvgpu_pd_write(g, &vm->pdb, (size_t)pd_offset + (size_t)0, pde_v[0]);
	nvgpu_pd_write(g, &vm->pdb, (size_t)pd_offset + (size_t)1, pde_v[1]);
}

static void __update_pte_sparse(u32 *pte_w)
{
	pte_w[0]  = gmmu_pte_valid_false_f();
	pte_w[1] |= gmmu_pte_vol_true_f();
}

static void __update_pte(struct vm_gk20a *vm,
			 u32 *pte_w,
			 u64 phys_addr,
			 struct nvgpu_gmmu_attrs *attrs)
{
	struct gk20a *g = gk20a_from_vm(vm);
	u32 page_size = vm->gmmu_page_sizes[attrs->pgsz];
	u32 pte_valid = attrs->valid ?
		gmmu_pte_valid_true_f() :
		gmmu_pte_valid_false_f();
	u32 phys_shifted = phys_addr >> gmmu_pte_address_shift_v();
	u32 addr = attrs->aperture == APERTURE_SYSMEM ?
		gmmu_pte_address_sys_f(phys_shifted) :
		gmmu_pte_address_vid_f(phys_shifted);
	int ctag_shift = 0;
	int shamt = ilog2(g->ops.fb.compression_page_size(g));
	if (shamt < 0) {
		nvgpu_err(g, "shift amount 'shamt' is negative");
	} else {
		ctag_shift = shamt;
	}

	pte_w[0] = pte_valid | addr;

	if (attrs->priv) {
		pte_w[0] |= gmmu_pte_privilege_true_f();
	}

	pte_w[1] = nvgpu_aperture_mask_raw(g, attrs->aperture,
					 gmmu_pte_aperture_sys_mem_ncoh_f(),
					 gmmu_pte_aperture_sys_mem_coh_f(),
					 gmmu_pte_aperture_video_memory_f()) |
		gmmu_pte_kind_f(attrs->kind_v) |
		gmmu_pte_comptagline_f((U32(attrs->ctag) >> U32(ctag_shift)));

	if ((attrs->ctag != 0ULL) &&
	     vm->mm->use_full_comp_tag_line &&
	    ((phys_addr & 0x10000ULL) != 0ULL)) {
		pte_w[1] |= gmmu_pte_comptagline_f(
			BIT32(gmmu_pte_comptagline_s() - 1U));
	}

	if (attrs->rw_flag == gk20a_mem_flag_read_only) {
		pte_w[0] |= gmmu_pte_read_only_true_f();
		pte_w[1] |= gmmu_pte_write_disable_true_f();
	} else if (attrs->rw_flag == gk20a_mem_flag_write_only) {
		pte_w[1] |= gmmu_pte_read_disable_true_f();
	}

	if (!attrs->cacheable) {
		pte_w[1] |= gmmu_pte_vol_true_f();
	}

	if (attrs->ctag != 0ULL) {
		attrs->ctag += page_size;
	}
}

static void update_gmmu_pte_locked(struct vm_gk20a *vm,
				   const struct gk20a_mmu_level *l,
				   struct nvgpu_gmmu_pd *pd,
				   u32 pd_idx,
				   u64 virt_addr,
				   u64 phys_addr,
				   struct nvgpu_gmmu_attrs *attrs)
{
	struct gk20a *g = gk20a_from_vm(vm);
	u32 page_size  = vm->gmmu_page_sizes[attrs->pgsz];
	u32 pd_offset = nvgpu_pd_offset_from_index(l, pd_idx);
	u32 pte_w[2] = {0, 0};
	int ctag_shift = 0;
	int shamt = ilog2(g->ops.fb.compression_page_size(g));
	if (shamt < 0) {
		nvgpu_err(g, "shift amount 'shamt' is negative");
	} else {
		ctag_shift = shamt;
	}

	if (phys_addr != 0ULL) {
		__update_pte(vm, pte_w, phys_addr, attrs);
	} else if (attrs->sparse) {
		__update_pte_sparse(pte_w);
	}

	pte_dbg(g, attrs,
		"PTE: i=%-4u size=%-2u offs=%-4u | "
		"GPU %#-12llx  phys %#-12llx "
		"pgsz: %3dkb perm=%-2s kind=%#02x APT=%-6s %c%c%c%c "
		"ctag=0x%08x "
		"[0x%08x, 0x%08x]",
		pd_idx, l->entry_size, pd_offset,
		virt_addr, phys_addr,
		page_size >> 10,
		nvgpu_gmmu_perm_str(attrs->rw_flag),
		attrs->kind_v,
		nvgpu_aperture_str(g, attrs->aperture),
		attrs->cacheable ? 'C' : '-',
		attrs->sparse    ? 'S' : '-',
		attrs->priv      ? 'P' : '-',
		attrs->valid     ? 'V' : '-',
		U32(attrs->ctag) >> U32(ctag_shift),
		pte_w[1], pte_w[0]);

	nvgpu_pd_write(g, pd, (size_t)pd_offset + (size_t)0, pte_w[0]);
	nvgpu_pd_write(g, pd, (size_t)pd_offset + (size_t)1, pte_w[1]);
}

u32 gk20a_get_pde_pgsz(struct gk20a *g, const struct gk20a_mmu_level *l,
				struct nvgpu_gmmu_pd *pd, u32 pd_idx)
{
	/*
	 * big and small page sizes are the same
	 */
	return GMMU_PAGE_SIZE_SMALL;
}

u32 gk20a_get_pte_pgsz(struct gk20a *g, const struct gk20a_mmu_level *l,
				struct nvgpu_gmmu_pd *pd, u32 pd_idx)
{
	/*
	 * return invalid
	 */
	return GMMU_NR_PAGE_SIZES;
}

const struct gk20a_mmu_level gk20a_mm_levels_64k[] = {
	{.hi_bit = {NV_GMMU_VA_RANGE-1, NV_GMMU_VA_RANGE-1},
	 .lo_bit = {26, 26},
	 .update_entry = update_gmmu_pde_locked,
	 .entry_size = 8,
	 .get_pgsz = gk20a_get_pde_pgsz},
	{.hi_bit = {25, 25},
	 .lo_bit = {12, 16},
	 .update_entry = update_gmmu_pte_locked,
	 .entry_size = 8,
	 .get_pgsz = gk20a_get_pte_pgsz},
	{.update_entry = NULL}
};

const struct gk20a_mmu_level gk20a_mm_levels_128k[] = {
	{.hi_bit = {NV_GMMU_VA_RANGE-1, NV_GMMU_VA_RANGE-1},
	 .lo_bit = {27, 27},
	 .update_entry = update_gmmu_pde_locked,
	 .entry_size = 8,
	 .get_pgsz = gk20a_get_pde_pgsz},
	{.hi_bit = {26, 26},
	 .lo_bit = {12, 17},
	 .update_entry = update_gmmu_pte_locked,
	 .entry_size = 8,
	 .get_pgsz = gk20a_get_pte_pgsz},
	{.update_entry = NULL}
};

int gk20a_vm_bind_channel(struct vm_gk20a *vm, struct channel_gk20a *ch)
{
	int err = 0;

	nvgpu_log_fn(ch->g, " ");

	nvgpu_vm_get(vm);
	ch->vm = vm;
	err = channel_gk20a_commit_va(ch);
	if (err != 0) {
		ch->vm = NULL;
	}

	nvgpu_log(gk20a_from_vm(vm), gpu_dbg_map, "Binding ch=%d -> VM:%s",
		  ch->chid, vm->name);

	return err;
}

void gk20a_init_inst_block(struct nvgpu_mem *inst_block, struct vm_gk20a *vm,
		u32 big_page_size)
{
	struct gk20a *g = gk20a_from_vm(vm);
	u64 pdb_addr = nvgpu_pd_gpu_addr(g, &vm->pdb);

	nvgpu_log_info(g, "inst block phys = 0x%llx, kv = 0x%p",
		nvgpu_inst_block_addr(g, inst_block), inst_block->cpu_va);

	g->ops.ramin.init_pdb(g, inst_block, pdb_addr, vm->pdb.mem);

	g->ops.ramin.set_adr_limit(g, inst_block, vm->va_limit - 1U);

	if ((big_page_size != 0U) && (g->ops.ramin.set_big_page_size != NULL)) {
		g->ops.ramin.set_big_page_size(g, inst_block, big_page_size);
	}
}

int gk20a_alloc_inst_block(struct gk20a *g, struct nvgpu_mem *inst_block)
{
	int err;

	nvgpu_log_fn(g, " ");

	err = nvgpu_dma_alloc(g, g->ops.ramin.alloc_size(), inst_block);
	if (err != 0) {
		nvgpu_err(g, "%s: memory allocation failed", __func__);
		return err;
	}

	nvgpu_log_fn(g, "done");
	return 0;
}

u32 gk20a_mm_get_iommu_bit(struct gk20a *g)
{
	return 34;
}

const struct gk20a_mmu_level *gk20a_mm_get_mmu_levels(struct gk20a *g,
						      u32 big_page_size)
{
	return (big_page_size == SZ_64K) ?
		 gk20a_mm_levels_64k : gk20a_mm_levels_128k;
}

u64 gk20a_mm_bar1_map_userd(struct gk20a *g, struct nvgpu_mem *mem, u32 offset)
{
	struct fifo_gk20a *f = &g->fifo;
	u64 gpu_va = f->userd_gpu_va + offset;

	return nvgpu_gmmu_map_fixed(g->mm.bar1.vm, mem, gpu_va,
				    PAGE_SIZE, 0,
				    gk20a_mem_flag_none, false,
				    mem->aperture);
}
