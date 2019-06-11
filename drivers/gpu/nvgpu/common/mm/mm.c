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

#include <nvgpu/mm.h>
#include <nvgpu/vm.h>
#include <nvgpu/dma.h>
#include <nvgpu/vm_area.h>
#include <nvgpu/acr.h>
#include <nvgpu/gmmu.h>
#include <nvgpu/vidmem.h>
#include <nvgpu/semaphore.h>
#include <nvgpu/pramin.h>
#include <nvgpu/enabled.h>
#include <nvgpu/ce.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/engines.h>
#include <nvgpu/power_features/cg.h>

int nvgpu_mm_suspend(struct gk20a *g)
{
	int err;

	nvgpu_log_info(g, "MM suspend running...");

	nvgpu_vidmem_thread_pause_sync(&g->mm);

	g->ops.mm.cache.cbc_clean(g);
	err = g->ops.mm.cache.l2_flush(g, false);
	if (err != 0) {
		nvgpu_err(g, "l2_flush failed");
		return err;
	}

	if (g->ops.fb.intr.disable != NULL) {
		g->ops.fb.intr.disable(g);
	}

	if (g->ops.mm.mmu_fault.disable_hw != NULL) {
		g->ops.mm.mmu_fault.disable_hw(g);
	}

	nvgpu_log_info(g, "MM suspend done!");

	return err;
}

u64 nvgpu_inst_block_addr(struct gk20a *g, struct nvgpu_mem *inst_block)
{
	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_NVLINK)) {
		return nvgpu_mem_get_phys_addr(g, inst_block);
	} else {
		return nvgpu_mem_get_addr(g, inst_block);
	}
}

u32 nvgpu_inst_block_ptr(struct gk20a *g, struct nvgpu_mem *inst_block)
{
	u64 addr = nvgpu_inst_block_addr(g, inst_block) >>
			g->ops.ramin.base_shift();

	nvgpu_assert(u64_hi32(addr) == 0U);
	return u64_lo32(addr);
}

void nvgpu_free_inst_block(struct gk20a *g, struct nvgpu_mem *inst_block)
{
	if (nvgpu_mem_is_valid(inst_block)) {
		nvgpu_dma_free(g, inst_block);
	}
}

int nvgpu_alloc_inst_block(struct gk20a *g, struct nvgpu_mem *inst_block)
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

static int nvgpu_alloc_sysmem_flush(struct gk20a *g)
{
	return nvgpu_dma_alloc_sys(g, SZ_4K, &g->mm.sysmem_flush);
}

#ifdef CONFIG_NVGPU_CE
static void nvgpu_remove_mm_ce_support(struct mm_gk20a *mm)
{
	struct gk20a *g = gk20a_from_mm(mm);

	if (mm->vidmem.ce_ctx_id != NVGPU_CE_INVAL_CTX_ID) {
		nvgpu_ce_delete_context(g, mm->vidmem.ce_ctx_id);
	}
	mm->vidmem.ce_ctx_id = NVGPU_CE_INVAL_CTX_ID;

	nvgpu_vm_put(mm->ce.vm);
}
#endif

static void nvgpu_remove_mm_support(struct mm_gk20a *mm)
{
	struct gk20a *g = gk20a_from_mm(mm);

	nvgpu_dma_free(g, &mm->mmu_wr_mem);
	nvgpu_dma_free(g, &mm->mmu_rd_mem);

	if (g->ops.mm.mmu_fault.info_mem_destroy != NULL) {
		g->ops.mm.mmu_fault.info_mem_destroy(g);
	}

	if (g->ops.mm.remove_bar2_vm != NULL) {
		g->ops.mm.remove_bar2_vm(g);
	}

	nvgpu_free_inst_block(g, &mm->bar1.inst_block);
	nvgpu_vm_put(mm->bar1.vm);

	nvgpu_free_inst_block(g, &mm->pmu.inst_block);
	nvgpu_free_inst_block(g, &mm->hwpm.inst_block);
	nvgpu_vm_put(mm->pmu.vm);

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_SEC2_VM)) {
		nvgpu_free_inst_block(g, &mm->sec2.inst_block);
		nvgpu_vm_put(mm->sec2.vm);
	}

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_GSP_VM)) {
		nvgpu_free_inst_block(g, &mm->gsp.inst_block);
		nvgpu_vm_put(mm->gsp.vm);
	}

	if (g->has_cde) {
		nvgpu_vm_put(mm->cde.vm);
	}

	nvgpu_semaphore_sea_destroy(g);
	nvgpu_vidmem_destroy(g);
	nvgpu_pd_cache_fini(g);

	if (g->ops.ramin.deinit_pdb_cache_war != NULL) {
		g->ops.ramin.deinit_pdb_cache_war(g);
	}
}

/* pmu vm, share channel_vm interfaces */
static int nvgpu_init_system_vm(struct mm_gk20a *mm)
{
	int err;
	struct gk20a *g = gk20a_from_mm(mm);
	struct nvgpu_mem *inst_block = &mm->pmu.inst_block;
	u32 big_page_size = g->ops.mm.gmmu.get_default_big_page_size();
	u64 low_hole, aperture_size;

	/*
	 * For some reason the maxwell PMU code is dependent on the large page
	 * size. No reason AFAICT for this. Probably a bug somewhere.
	 */
	if (nvgpu_is_enabled(g, NVGPU_MM_FORCE_128K_PMU_VM)) {
		big_page_size = U32(SZ_128K);
	}

	/*
	 * No user region - so we will pass that as zero sized.
	 */
	low_hole = SZ_4K * 16UL;
	aperture_size = GK20A_PMU_VA_SIZE;

	mm->pmu.aperture_size = GK20A_PMU_VA_SIZE;
	nvgpu_log_info(g, "pmu vm size = 0x%x", mm->pmu.aperture_size);

	mm->pmu.vm = nvgpu_vm_init(g, big_page_size,
				   low_hole,
				   aperture_size - low_hole,
				   aperture_size,
				   true,
				   false,
				   false,
				   "system");
	if (mm->pmu.vm == NULL) {
		return -ENOMEM;
	}

	err = nvgpu_alloc_inst_block(g, inst_block);
	if (err != 0) {
		goto clean_up_vm;
	}
	g->ops.mm.init_inst_block(inst_block, mm->pmu.vm, big_page_size);

	return 0;

clean_up_vm:
	nvgpu_vm_put(mm->pmu.vm);
	return err;
}

static int nvgpu_init_hwpm(struct mm_gk20a *mm)
{
	int err;
	struct gk20a *g = gk20a_from_mm(mm);
	struct nvgpu_mem *inst_block = &mm->hwpm.inst_block;

	err = nvgpu_alloc_inst_block(g, inst_block);
	if (err != 0) {
		return err;
	}
	g->ops.mm.init_inst_block(inst_block, mm->pmu.vm, 0);

	return 0;
}

static int nvgpu_init_cde_vm(struct mm_gk20a *mm)
{
	struct gk20a *g = gk20a_from_mm(mm);
	u32 big_page_size = g->ops.mm.gmmu.get_default_big_page_size();

	mm->cde.vm = nvgpu_vm_init(g, big_page_size,
				   U64(big_page_size) << U64(10),
				   NV_MM_DEFAULT_KERNEL_SIZE,
				   NV_MM_DEFAULT_KERNEL_SIZE + NV_MM_DEFAULT_USER_SIZE,
				   false, false, false, "cde");
	if (mm->cde.vm == NULL) {
		return -ENOMEM;
	}
	return 0;
}

static int nvgpu_init_ce_vm(struct mm_gk20a *mm)
{
	struct gk20a *g = gk20a_from_mm(mm);
	u32 big_page_size = g->ops.mm.gmmu.get_default_big_page_size();

	mm->ce.vm = nvgpu_vm_init(g, big_page_size,
				  U64(big_page_size) << U64(10),
				  NV_MM_DEFAULT_KERNEL_SIZE,
				  NV_MM_DEFAULT_KERNEL_SIZE + NV_MM_DEFAULT_USER_SIZE,
				  false, false, false, "ce");
	if (mm->ce.vm == NULL) {
		return -ENOMEM;
	}
	return 0;
}

static int nvgpu_init_mmu_debug(struct mm_gk20a *mm)
{
	struct gk20a *g = gk20a_from_mm(mm);
	int err;

	if (!nvgpu_mem_is_valid(&mm->mmu_wr_mem)) {
		err = nvgpu_dma_alloc_sys(g, SZ_4K, &mm->mmu_wr_mem);
		if (err != 0) {
			goto err;
		}
	}

	if (!nvgpu_mem_is_valid(&mm->mmu_rd_mem)) {
		err = nvgpu_dma_alloc_sys(g, SZ_4K, &mm->mmu_rd_mem);
		if (err != 0) {
			goto err_free_wr_mem;
		}
	}
	return 0;

 err_free_wr_mem:
	nvgpu_dma_free(g, &mm->mmu_wr_mem);
 err:
	return -ENOMEM;
}

#ifdef CONFIG_NVGPU_CE
void nvgpu_init_mm_ce_context(struct gk20a *g)
{
#if defined(CONFIG_GK20A_VIDMEM)
	if (g->mm.vidmem.size > 0U &&
	   (g->mm.vidmem.ce_ctx_id == NVGPU_CE_INVAL_CTX_ID)) {
		g->mm.vidmem.ce_ctx_id =
			nvgpu_ce_create_context(g,
				nvgpu_engine_get_fast_ce_runlist_id(g),
				-1,
				-1);

		if (g->mm.vidmem.ce_ctx_id == NVGPU_CE_INVAL_CTX_ID) {
			nvgpu_err(g,
				"Failed to allocate CE context for vidmem page clearing support");
		}
	}
#endif
}
#endif /* NVGPU_FENCE_CE */

static int nvgpu_init_mm_reset_enable_hw(struct gk20a *g)
{
	if (g->ops.mc.fb_reset != NULL) {
		g->ops.mc.fb_reset(g);
	}

	nvgpu_cg_slcg_fb_ltc_load_enable(g);

	nvgpu_cg_blcg_fb_ltc_load_enable(g);

	if (g->ops.fb.init_fs_state != NULL) {
		g->ops.fb.init_fs_state(g);
	}

	return 0;
}

static int nvgpu_init_bar1_vm(struct mm_gk20a *mm)
{
	int err;
	struct gk20a *g = gk20a_from_mm(mm);
	struct nvgpu_mem *inst_block = &mm->bar1.inst_block;
	u32 big_page_size = g->ops.mm.gmmu.get_default_big_page_size();

	mm->bar1.aperture_size = bar1_aperture_size_mb_gk20a() << 20;
	nvgpu_log_info(g, "bar1 vm size = 0x%x", mm->bar1.aperture_size);
	mm->bar1.vm = nvgpu_vm_init(g,
				    big_page_size,
				    SZ_64K,
				    mm->bar1.aperture_size - SZ_64K,
				    mm->bar1.aperture_size,
				    true, false, false,
				    "bar1");
	if (mm->bar1.vm == NULL) {
		return -ENOMEM;
	}

	err = nvgpu_alloc_inst_block(g, inst_block);
	if (err != 0) {
		goto clean_up_vm;
	}
	g->ops.mm.init_inst_block(inst_block, mm->bar1.vm, big_page_size);

	return 0;

clean_up_vm:
	nvgpu_vm_put(mm->bar1.vm);
	return err;
}

static int nvgpu_init_engine_ucode_vm(struct gk20a *g,
	struct engine_ucode *ucode, const char *address_space_name)
{
	int err;
	struct nvgpu_mem *inst_block = &ucode->inst_block;
	u32 big_page_size = g->ops.mm.gmmu.get_default_big_page_size();

	/* ucode aperture size is 32MB */
	ucode->aperture_size = U32(32) << 20U;
	nvgpu_log_info(g, "%s vm size = 0x%x", address_space_name,
		ucode->aperture_size);

	ucode->vm = nvgpu_vm_init(g, big_page_size, SZ_4K,
		ucode->aperture_size - SZ_4K,
		ucode->aperture_size, false, false, false, address_space_name);
	if (ucode->vm == NULL) {
		return -ENOMEM;
	}

	/* allocate instance mem for engine ucode */
	err = nvgpu_alloc_inst_block(g, inst_block);
	if (err != 0) {
		goto clean_up_va;
	}

	g->ops.mm.init_inst_block(inst_block, ucode->vm, big_page_size);

	return 0;

clean_up_va:
	nvgpu_vm_put(ucode->vm);
	return err;
}

static int nvgpu_init_mm_setup_sw(struct gk20a *g)
{
	struct mm_gk20a *mm = &g->mm;
	int err = 0;

	if (mm->sw_ready) {
		nvgpu_log_info(g, "skip init");
		return 0;
	}

	mm->g = g;
	nvgpu_mutex_init(&mm->l2_op_lock);

	/*TBD: make channel vm size configurable */
	mm->channel.user_size = NV_MM_DEFAULT_USER_SIZE -
		NV_MM_DEFAULT_KERNEL_SIZE;
	mm->channel.kernel_size = NV_MM_DEFAULT_KERNEL_SIZE;

	nvgpu_log_info(g, "channel vm size: user %uMB  kernel %uMB",
		   U32(mm->channel.user_size >> U64(20)),
		   U32(mm->channel.kernel_size >> U64(20)));

	nvgpu_init_pramin(mm);

	mm->vidmem.ce_ctx_id = NVGPU_CE_INVAL_CTX_ID;

	err = nvgpu_vidmem_init(mm);
	if (err != 0) {
		return err;
	}

	/*
	 * this requires fixed allocations in vidmem which must be
	 * allocated before all other buffers
	 */

	if (!nvgpu_is_enabled(g, NVGPU_MM_UNIFIED_MEMORY)) {
		err = nvgpu_acr_alloc_blob_prerequisite(g, g->acr, 0);
		if (err != 0) {
			return err;
		}
	}

	err = nvgpu_alloc_sysmem_flush(g);
	if (err != 0) {
		return err;
	}

	err = nvgpu_init_bar1_vm(mm);
	if (err != 0) {
		return err;
	}

	if (g->ops.mm.init_bar2_vm != NULL) {
		err = g->ops.mm.init_bar2_vm(g);
		if (err != 0) {
			return err;
		}
	}
	err = nvgpu_init_system_vm(mm);
	if (err != 0) {
		return err;
	}

	err = nvgpu_init_hwpm(mm);
	if (err != 0) {
		return err;
	}

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_SEC2_VM)) {
		err = nvgpu_init_engine_ucode_vm(g, &mm->sec2, "sec2");
		if (err != 0) {
			return err;
		}
	}

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_GSP_VM)) {
		err = nvgpu_init_engine_ucode_vm(g, &mm->gsp, "gsp");
		if (err != 0) {
			return err;
		}
	}

	if (g->has_cde) {
		err = nvgpu_init_cde_vm(mm);
			if (err != 0) {
				return err;
			}
	}

	err = nvgpu_init_ce_vm(mm);
	if (err != 0) {
		return err;
	}

	err = nvgpu_init_mmu_debug(mm);
	if (err != 0) {
		return err;
	}

	/*
	 * Some chips support replayable MMU faults. For such chips make sure
	 * SW is initialized.
	 */
	if (g->ops.mm.mmu_fault.setup_sw != NULL) {
		err = g->ops.mm.mmu_fault.setup_sw(g);
		if (err != 0) {
			return err;
		}
	}

	mm->remove_support = nvgpu_remove_mm_support;
#ifdef CONFIG_NVGPU_CE
	mm->remove_ce_support = nvgpu_remove_mm_ce_support;
#endif

	mm->sw_ready = true;

	return 0;
}

static int nvgpu_init_mm_pdb_cache_war(struct gk20a *g)
{
	int err;

	if (g->ops.ramin.init_pdb_cache_war != NULL) {
		err = g->ops.ramin.init_pdb_cache_war(g);
		if (err != 0) {
			return err;
		}
	}

	if (g->ops.fb.apply_pdb_cache_war != NULL) {
		err = g->ops.fb.apply_pdb_cache_war(g);
		if (err != 0) {
			return err;
		}
	}

	return 0;
}

/*
 * Called through the HAL to handle vGPU: the vGPU doesn't have HW to initialize
 * here.
 */
int nvgpu_mm_setup_hw(struct gk20a *g)
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
		err = g->ops.bus.bar1_bind(g, &mm->bar1.inst_block);
		if (err != 0) {
			return err;
		}
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

	if (g->ops.mm.mmu_fault.setup_hw != NULL) {
		g->ops.mm.mmu_fault.setup_hw(g);
	}

	nvgpu_log_fn(g, "done");
	return 0;
}

int nvgpu_init_mm_support(struct gk20a *g)
{
	int err;

	err = nvgpu_init_mm_reset_enable_hw(g);
	if (err != 0) {
		return err;
	}

	err = nvgpu_init_mm_pdb_cache_war(g);
	if (err != 0) {
		return err;
	}

	err = nvgpu_init_mm_setup_sw(g);
	if (err != 0) {
		return err;
	}

	if (g->ops.mm.setup_hw != NULL) {
		err = g->ops.mm.setup_hw(g);
	}

	return err;
}

u32 nvgpu_mm_get_default_big_page_size(struct gk20a *g)
{
	u32 big_page_size;

	big_page_size = g->ops.mm.gmmu.get_default_big_page_size();

	if (g->mm.disable_bigpage) {
		big_page_size = 0;
	}

	return big_page_size;
}

u32 nvgpu_mm_get_available_big_page_sizes(struct gk20a *g)
{
	u32 available_big_page_sizes = 0;

	if (g->mm.disable_bigpage) {
		return available_big_page_sizes;
	}

	available_big_page_sizes = g->ops.mm.gmmu.get_default_big_page_size();
	if (g->ops.mm.gmmu.get_big_page_sizes != NULL) {
		available_big_page_sizes |= g->ops.mm.gmmu.get_big_page_sizes();
	}

	return available_big_page_sizes;
}
