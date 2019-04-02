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

#include <trace/events/gk20a.h>

#include <nvgpu/log.h>
#include <nvgpu/types.h>
#include <nvgpu/timers.h>
#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/io.h>
#include <nvgpu/utils.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/bug.h>

#include "hal/fb/fb_gv11b.h"
#include "hal/fb/fb_gv100.h"
#include "hal/mc/mc_tu104.h"

#include "tu104/func_tu104.h"

#include "fb_tu104.h"

#include "nvgpu/hw/tu104/hw_fb_tu104.h"
#include "nvgpu/hw/tu104/hw_func_tu104.h"

void tu104_fb_enable_hub_intr(struct gk20a *g)
{
	u32 info_fault = nvgpu_readl(g, fb_mmu_int_vector_info_fault_r());
	u32 nonreplay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_NONREPLAY_REG_INDEX));
	u32 replay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_REPLAY_REG_INDEX));
	u32 ecc_error = nvgpu_readl(g, fb_mmu_int_vector_ecc_error_r());

	intr_tu104_vector_en_set(g,
		fb_mmu_int_vector_info_fault_vector_v(info_fault));
	intr_tu104_vector_en_set(g,
		fb_mmu_int_vector_fault_notify_v(nonreplay_fault));
	intr_tu104_vector_en_set(g,
		fb_mmu_int_vector_fault_error_v(nonreplay_fault));
	intr_tu104_vector_en_set(g,
		fb_mmu_int_vector_fault_notify_v(replay_fault));
	intr_tu104_vector_en_set(g,
		fb_mmu_int_vector_fault_error_v(replay_fault));
	intr_tu104_vector_en_set(g,
		fb_mmu_int_vector_ecc_error_vector_v(ecc_error));
}

void tu104_fb_disable_hub_intr(struct gk20a *g)
{
	u32 info_fault = nvgpu_readl(g, fb_mmu_int_vector_info_fault_r());
	u32 nonreplay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_NONREPLAY_REG_INDEX));
	u32 replay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_REPLAY_REG_INDEX));
	u32 ecc_error = nvgpu_readl(g, fb_mmu_int_vector_ecc_error_r());

	intr_tu104_vector_en_clear(g,
		fb_mmu_int_vector_info_fault_vector_v(info_fault));
	intr_tu104_vector_en_clear(g,
		fb_mmu_int_vector_fault_notify_v(nonreplay_fault));
	intr_tu104_vector_en_clear(g,
		fb_mmu_int_vector_fault_error_v(nonreplay_fault));
	intr_tu104_vector_en_clear(g,
		fb_mmu_int_vector_fault_notify_v(replay_fault));
	intr_tu104_vector_en_clear(g,
		fb_mmu_int_vector_fault_error_v(replay_fault));
	intr_tu104_vector_en_clear(g,
		fb_mmu_int_vector_ecc_error_vector_v(ecc_error));
}

bool tu104_fb_mmu_fault_pending(struct gk20a *g)
{
	u32 info_fault = nvgpu_readl(g, fb_mmu_int_vector_info_fault_r());
	u32 nonreplay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_NONREPLAY_REG_INDEX));
	u32 replay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_REPLAY_REG_INDEX));
	u32 ecc_error = nvgpu_readl(g, fb_mmu_int_vector_ecc_error_r());

	if (intr_tu104_vector_intr_pending(g,
		fb_mmu_int_vector_fault_notify_v(replay_fault)) ||
	    intr_tu104_vector_intr_pending(g,
		fb_mmu_int_vector_fault_error_v(replay_fault)) ||
	    intr_tu104_vector_intr_pending(g,
		fb_mmu_int_vector_fault_notify_v(nonreplay_fault)) ||
	    intr_tu104_vector_intr_pending(g,
		fb_mmu_int_vector_fault_error_v(nonreplay_fault)) ||
	    intr_tu104_vector_intr_pending(g,
		fb_mmu_int_vector_info_fault_vector_v(info_fault)) ||
	    intr_tu104_vector_intr_pending(g,
		fb_mmu_int_vector_ecc_error_vector_v(ecc_error))) {
		return true;
	}

	return false;
}

static void tu104_fb_handle_mmu_fault(struct gk20a *g)
{
	u32 info_fault = nvgpu_readl(g, fb_mmu_int_vector_info_fault_r());
	u32 nonreplay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_NONREPLAY_REG_INDEX));
	u32 replay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_REPLAY_REG_INDEX));
	u32 fault_status = g->ops.fb.read_mmu_fault_status(g);

	nvgpu_log(g, gpu_dbg_intr, "mmu_fault_status = 0x%08x", fault_status);

	if (intr_tu104_vector_intr_pending(g,
			fb_mmu_int_vector_info_fault_vector_v(info_fault))) {
		intr_tu104_intr_clear_leaf_vector(g,
			fb_mmu_int_vector_info_fault_vector_v(info_fault));

		gv11b_fb_handle_dropped_mmu_fault(g, fault_status);
		gv11b_fb_handle_other_fault_notify(g, fault_status);
	}

	if (gv11b_fb_is_fault_buf_enabled(g,
			NVGPU_FB_MMU_FAULT_NONREPLAY_REG_INDEX)) {
		if (intr_tu104_vector_intr_pending(g,
				fb_mmu_int_vector_fault_notify_v(nonreplay_fault))) {
			intr_tu104_intr_clear_leaf_vector(g,
				fb_mmu_int_vector_fault_notify_v(nonreplay_fault));

			gv11b_fb_handle_mmu_nonreplay_replay_fault(g,
					fault_status,
					NVGPU_FB_MMU_FAULT_NONREPLAY_REG_INDEX);

			/*
			 * When all the faults are processed,
			 * GET and PUT will have same value and mmu fault status
			 * bit will be reset by HW
			 */
		}

		if (intr_tu104_vector_intr_pending(g,
				fb_mmu_int_vector_fault_error_v(nonreplay_fault))) {
			intr_tu104_intr_clear_leaf_vector(g,
				fb_mmu_int_vector_fault_error_v(nonreplay_fault));

			gv11b_fb_handle_nonreplay_fault_overflow(g,
				 fault_status);
		}
	}

	if (gv11b_fb_is_fault_buf_enabled(g,
			NVGPU_FB_MMU_FAULT_REPLAY_REG_INDEX)) {
		if (intr_tu104_vector_intr_pending(g,
				fb_mmu_int_vector_fault_notify_v(replay_fault))) {
			intr_tu104_intr_clear_leaf_vector(g,
				fb_mmu_int_vector_fault_notify_v(replay_fault));

			gv11b_fb_handle_mmu_nonreplay_replay_fault(g,
					fault_status,
					NVGPU_FB_MMU_FAULT_REPLAY_REG_INDEX);
		}

		if (intr_tu104_vector_intr_pending(g,
				fb_mmu_int_vector_fault_error_v(replay_fault))) {
			intr_tu104_intr_clear_leaf_vector(g,
				fb_mmu_int_vector_fault_error_v(replay_fault));

			gv11b_fb_handle_replay_fault_overflow(g,
				 fault_status);
		}
	}

	nvgpu_log(g, gpu_dbg_intr, "clear mmu fault status");
	g->ops.fb.write_mmu_fault_status(g,
				fb_mmu_fault_status_valid_clear_f());
}

void tu104_fb_hub_isr(struct gk20a *g)
{
	u32 info_fault = nvgpu_readl(g, fb_mmu_int_vector_info_fault_r());
	u32 nonreplay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_NONREPLAY_REG_INDEX));
	u32 replay_fault = nvgpu_readl(g,
		fb_mmu_int_vector_fault_r(NVGPU_FB_MMU_FAULT_REPLAY_REG_INDEX));
	u32 ecc_error = nvgpu_readl(g, fb_mmu_int_vector_ecc_error_r());
	u32 status;

	nvgpu_mutex_acquire(&g->mm.hub_isr_mutex);

	if (intr_tu104_vector_intr_pending(g,
			fb_mmu_int_vector_ecc_error_vector_v(ecc_error))) {
		nvgpu_info(g, "ecc uncorrected error notify");

		intr_tu104_intr_clear_leaf_vector(g,
			fb_mmu_int_vector_ecc_error_vector_v(ecc_error));

		status = nvgpu_readl(g, fb_mmu_l2tlb_ecc_status_r());
		if (status != 0U) {
			gv11b_handle_l2tlb_ecc_isr(g, status);
		}

		status = nvgpu_readl(g, fb_mmu_hubtlb_ecc_status_r());
		if (status != 0U) {
			gv11b_handle_hubtlb_ecc_isr(g, status);
		}

		status = nvgpu_readl(g, fb_mmu_fillunit_ecc_status_r());
		if (status != 0U) {
			gv11b_handle_fillunit_ecc_isr(g, status);
		}
	}

	if (intr_tu104_vector_intr_pending(g,
			fb_mmu_int_vector_fault_notify_v(replay_fault)) ||
	    intr_tu104_vector_intr_pending(g,
			fb_mmu_int_vector_fault_error_v(replay_fault)) ||
	    intr_tu104_vector_intr_pending(g,
			fb_mmu_int_vector_fault_notify_v(nonreplay_fault)) ||
	    intr_tu104_vector_intr_pending(g,
			fb_mmu_int_vector_fault_error_v(nonreplay_fault)) ||
	    intr_tu104_vector_intr_pending(g,
			fb_mmu_int_vector_info_fault_vector_v(info_fault))) {
		nvgpu_log(g, gpu_dbg_intr, "MMU Fault");
		tu104_fb_handle_mmu_fault(g);
	}

	nvgpu_mutex_release(&g->mm.hub_isr_mutex);
}

void fb_tu104_write_mmu_fault_buffer_lo_hi(struct gk20a *g, u32 index,
	u32 addr_lo, u32 addr_hi)
{
	nvgpu_func_writel(g,
		func_priv_mmu_fault_buffer_lo_r(index), addr_lo);
	nvgpu_func_writel(g,
		func_priv_mmu_fault_buffer_hi_r(index), addr_hi);
}

u32 fb_tu104_read_mmu_fault_buffer_get(struct gk20a *g, u32 index)
{
	return nvgpu_func_readl(g,
		func_priv_mmu_fault_buffer_get_r(index));
}

void fb_tu104_write_mmu_fault_buffer_get(struct gk20a *g, u32 index,
	u32 reg_val)
{
	nvgpu_func_writel(g,
		func_priv_mmu_fault_buffer_get_r(index),
			reg_val);
}

u32 fb_tu104_read_mmu_fault_buffer_put(struct gk20a *g, u32 index)
{
	return nvgpu_func_readl(g,
		func_priv_mmu_fault_buffer_put_r(index));
}

u32 fb_tu104_read_mmu_fault_buffer_size(struct gk20a *g, u32 index)
{
	return nvgpu_func_readl(g,
		func_priv_mmu_fault_buffer_size_r(index));
}

void fb_tu104_write_mmu_fault_buffer_size(struct gk20a *g, u32 index,
	u32 reg_val)
{
	nvgpu_func_writel(g,
		func_priv_mmu_fault_buffer_size_r(index),
			reg_val);
}

void fb_tu104_read_mmu_fault_addr_lo_hi(struct gk20a *g,
	u32 *addr_lo, u32 *addr_hi)
{
	*addr_lo = nvgpu_func_readl(g,
			func_priv_mmu_fault_addr_lo_r());
	*addr_hi = nvgpu_func_readl(g,
			func_priv_mmu_fault_addr_hi_r());
}

void fb_tu104_read_mmu_fault_inst_lo_hi(struct gk20a *g,
	u32 *inst_lo, u32 *inst_hi)
{
	*inst_lo = nvgpu_func_readl(g,
			func_priv_mmu_fault_inst_lo_r());
	*inst_hi = nvgpu_func_readl(g,
			func_priv_mmu_fault_inst_hi_r());
}

u32 fb_tu104_read_mmu_fault_info(struct gk20a *g)
{
	return nvgpu_func_readl(g,
			func_priv_mmu_fault_info_r());
}

u32 fb_tu104_read_mmu_fault_status(struct gk20a *g)
{
	return nvgpu_func_readl(g,
			func_priv_mmu_fault_status_r());
}

void fb_tu104_write_mmu_fault_status(struct gk20a *g, u32 reg_val)
{
	nvgpu_func_writel(g, func_priv_mmu_fault_status_r(),
			   reg_val);
}

int fb_tu104_tlb_invalidate(struct gk20a *g, struct nvgpu_mem *pdb)
{
	struct nvgpu_timeout timeout;
	u32 addr_lo;
	u32 data;
	int err = 0;

	nvgpu_log_fn(g, " ");

	/*
	 * pagetables are considered sw states which are preserved after
	 * prepare_poweroff. When gk20a deinit releases those pagetables,
	 * common code in vm unmap path calls tlb invalidate that touches
	 * hw. Use the power_on flag to skip tlb invalidation when gpu
	 * power is turned off
	 */
	if (!g->power_on) {
		return 0;
	}

	addr_lo = u64_lo32(nvgpu_mem_get_addr(g, pdb) >> 12);

	err = nvgpu_timeout_init(g, &timeout, 1000, NVGPU_TIMER_RETRY_TIMER);
	if (err != 0) {
		return err;
	}

	nvgpu_mutex_acquire(&g->mm.tlb_lock);

	trace_gk20a_mm_tlb_invalidate(g->name);

	nvgpu_func_writel(g, func_priv_mmu_invalidate_pdb_r(),
		fb_mmu_invalidate_pdb_addr_f(addr_lo) |
		nvgpu_aperture_mask(g, pdb,
				fb_mmu_invalidate_pdb_aperture_sys_mem_f(),
				fb_mmu_invalidate_pdb_aperture_sys_mem_f(),
				fb_mmu_invalidate_pdb_aperture_vid_mem_f()));

	nvgpu_func_writel(g, func_priv_mmu_invalidate_r(),
		fb_mmu_invalidate_all_va_true_f() |
		fb_mmu_invalidate_trigger_true_f());

	do {
		data = nvgpu_func_readl(g,
				func_priv_mmu_invalidate_r());
		if (fb_mmu_invalidate_trigger_v(data) !=
				fb_mmu_invalidate_trigger_true_v()) {
			break;
		}
		nvgpu_udelay(2);
	} while (nvgpu_timeout_expired_msg(&timeout,
					 "wait mmu invalidate") == 0);

	trace_gk20a_mm_tlb_invalidate_done(g->name);

	nvgpu_mutex_release(&g->mm.tlb_lock);
	return err;
}

int fb_tu104_mmu_invalidate_replay(struct gk20a *g,
			 u32 invalidate_replay_val)
{
	int err = -ETIMEDOUT;
	u32 reg_val;
	struct nvgpu_timeout timeout;

	nvgpu_log_fn(g, " ");

	/* retry 200 times */
	err = nvgpu_timeout_init(g, &timeout, 200, NVGPU_TIMER_RETRY_TIMER);
	if (err != 0) {
		return err;
	}

	nvgpu_mutex_acquire(&g->mm.tlb_lock);

	reg_val = nvgpu_func_readl(g, func_priv_mmu_invalidate_r());

	reg_val |= fb_mmu_invalidate_all_va_true_f() |
		fb_mmu_invalidate_all_pdb_true_f() |
		invalidate_replay_val |
		fb_mmu_invalidate_trigger_true_f();

	nvgpu_func_writel(g, func_priv_mmu_invalidate_r(), reg_val);

	do {
		reg_val = nvgpu_func_readl(g,
				func_priv_mmu_invalidate_r());
		if (fb_mmu_invalidate_trigger_v(reg_val) !=
				fb_mmu_invalidate_trigger_true_v()) {
			err = 0;
			break;
		}
		nvgpu_udelay(5);
	} while (nvgpu_timeout_expired_msg(&timeout,
			    "invalidate replay failed on 0x%llx") == 0);
	if (err != 0) {
		nvgpu_err(g, "invalidate replay timedout");
	}

	nvgpu_mutex_release(&g->mm.tlb_lock);
	return err;
}

void tu104_fb_cbc_configure(struct gk20a *g, struct nvgpu_cbc *cbc)
{
	u64 base_divisor;
	u64 compbit_store_base;
	u64 compbit_store_pa;
	u64 cbc_start_addr, cbc_end_addr;
	u64 cbc_top;
	u64 cbc_top_size;
	u32 cbc_max;

	compbit_store_pa = nvgpu_mem_get_addr(g, &cbc->compbit_store.mem);
	base_divisor = g->ops.cbc.get_base_divisor(g);
	compbit_store_base = DIV_ROUND_UP(compbit_store_pa, base_divisor);

	cbc_start_addr = (u64)nvgpu_ltc_get_ltc_count(g) *
			(compbit_store_base <<
			 fb_mmu_cbc_base_address_alignment_shift_v());
	cbc_end_addr = cbc_start_addr + cbc->compbit_backing_size;

	cbc_top = (cbc_end_addr / nvgpu_ltc_get_ltc_count(g)) >>
		  fb_mmu_cbc_base_address_alignment_shift_v();
	cbc_top_size = u64_lo32(cbc_top) - compbit_store_base;

	nvgpu_assert(cbc_top_size < U64(U32_MAX));
	nvgpu_writel(g, fb_mmu_cbc_top_r(),
			fb_mmu_cbc_top_size_f(U32(cbc_top_size)));

	cbc_max = nvgpu_readl(g, fb_mmu_cbc_max_r());
	cbc_max = set_field(cbc_max,
		  fb_mmu_cbc_max_comptagline_m(),
		  fb_mmu_cbc_max_comptagline_f(cbc->max_comptag_lines));
	nvgpu_writel(g, fb_mmu_cbc_max_r(), cbc_max);

	nvgpu_assert(compbit_store_base < U64(U32_MAX));
	nvgpu_writel(g, fb_mmu_cbc_base_r(),
		fb_mmu_cbc_base_address_f(U32(compbit_store_base)));

	nvgpu_log(g, gpu_dbg_info | gpu_dbg_map_v | gpu_dbg_pte,
		"compbit base.pa: 0x%x,%08x cbc_base:0x%llx\n",
		(u32)(compbit_store_pa >> 32),
		(u32)(compbit_store_pa & 0xffffffffU),
		compbit_store_base);

	cbc->compbit_store.base_hw = compbit_store_base;

}

static int tu104_fb_wait_mmu_bind(struct gk20a *g)
{
	struct nvgpu_timeout timeout;
	u32 val;
	int err;

	err = nvgpu_timeout_init(g, &timeout, 1000, NVGPU_TIMER_RETRY_TIMER);
	if (err != 0) {
		return err;
	}

	do {
		val = nvgpu_readl(g, fb_mmu_bind_r());
		if ((val & fb_mmu_bind_trigger_true_f()) !=
			   fb_mmu_bind_trigger_true_f()) {
			return 0;
		}
		nvgpu_udelay(2);
	} while (nvgpu_timeout_expired_msg(&timeout, "mmu bind timedout") == 0);

	return -ETIMEDOUT;
}

int tu104_fb_apply_pdb_cache_war(struct gk20a *g)
{
	u64 inst_blk_base_addr;
	u32 inst_blk_addr;
	u32 i;
	int err;

	if (!nvgpu_mem_is_valid(&g->pdb_cache_war_mem)) {
		return -EINVAL;
	}

	inst_blk_base_addr = nvgpu_mem_get_addr(g, &g->pdb_cache_war_mem);

	/* Bind 256 instance blocks to unused engine ID 0x0 */
	for (i = 0U; i < 256U; i++) {
		inst_blk_addr = u64_lo32((inst_blk_base_addr +
						(U64(i) * U64(PAGE_SIZE)))
				>> fb_mmu_bind_imb_addr_alignment_v());

		nvgpu_writel(g, fb_mmu_bind_imb_r(),
			fb_mmu_bind_imb_addr_f(inst_blk_addr) |
			nvgpu_aperture_mask(g, &g->pdb_cache_war_mem,
				fb_mmu_bind_imb_aperture_sys_mem_nc_f(),
				fb_mmu_bind_imb_aperture_sys_mem_c_f(),
				fb_mmu_bind_imb_aperture_vid_mem_f()));

		nvgpu_writel(g, fb_mmu_bind_r(),
			fb_mmu_bind_engine_id_f(0x0U) |
			fb_mmu_bind_trigger_true_f());

		err = tu104_fb_wait_mmu_bind(g);
		if (err != 0) {
			return err;
		}
	}

	/* first unbind */
	nvgpu_writel(g, fb_mmu_bind_imb_r(),
		fb_mmu_bind_imb_aperture_f(0x1U) |
		fb_mmu_bind_imb_addr_f(0x0U));

	nvgpu_writel(g, fb_mmu_bind_r(),
		fb_mmu_bind_engine_id_f(0x0U) |
		fb_mmu_bind_trigger_true_f());

	err = tu104_fb_wait_mmu_bind(g);
	if (err != 0) {
		return err;
	}

	/* second unbind */
	nvgpu_writel(g, fb_mmu_bind_r(),
		fb_mmu_bind_engine_id_f(0x0U) |
		fb_mmu_bind_trigger_true_f());

	err = tu104_fb_wait_mmu_bind(g);
	if (err != 0) {
		return err;
	}

	/* Bind 257th (last) instance block that reserves PDB cache entry 255 */
	inst_blk_addr = u64_lo32((inst_blk_base_addr + (256ULL * U64(PAGE_SIZE)))
			>> U64(fb_mmu_bind_imb_addr_alignment_v()));

	nvgpu_writel(g, fb_mmu_bind_imb_r(),
		fb_mmu_bind_imb_addr_f(inst_blk_addr) |
		nvgpu_aperture_mask(g, &g->pdb_cache_war_mem,
			fb_mmu_bind_imb_aperture_sys_mem_nc_f(),
			fb_mmu_bind_imb_aperture_sys_mem_c_f(),
			fb_mmu_bind_imb_aperture_vid_mem_f()));

	nvgpu_writel(g, fb_mmu_bind_r(),
		fb_mmu_bind_engine_id_f(0x0U) |
		fb_mmu_bind_trigger_true_f());

	err = tu104_fb_wait_mmu_bind(g);
	if (err != 0) {
		return err;
	}

	return 0;
}

size_t tu104_fb_get_vidmem_size(struct gk20a *g)
{
	u32 range = gk20a_readl(g, fb_mmu_local_memory_range_r());
	u32 mag = fb_mmu_local_memory_range_lower_mag_v(range);
	u32 scale = fb_mmu_local_memory_range_lower_scale_v(range);
	u32 ecc = fb_mmu_local_memory_range_ecc_mode_v(range);
	size_t bytes = ((size_t)mag << scale) * SZ_1M;

	if (nvgpu_is_enabled(g, NVGPU_IS_FMODEL) && (bytes == 0)) {
		/* 192 MB */
		bytes = 192*1024*1024;
	}

	if (ecc != 0U) {
		bytes = bytes / 16U * 15U;
	}

	return bytes;
}


int tu104_fb_enable_nvlink(struct gk20a *g)
{
	int ret = 0;
	u32 data;

	nvgpu_log(g, gpu_dbg_nvlink|gpu_dbg_info, "enabling nvlink");

	ret = gv100_fb_enable_nvlink(g);
	if (ret != 0) {
		return ret;
	}

	/* NV_PFB_PRI_MMU_CTRL_ATOMIC_CAPABILITY_SYS_NCOH_MODE to L2 */
	data = nvgpu_readl(g, fb_mmu_ctrl_r());
	data = set_field(data, fb_mmu_ctrl_atomic_capability_sys_ncoh_mode_m(),
		fb_mmu_ctrl_atomic_capability_sys_ncoh_mode_l2_f());
	nvgpu_writel(g, fb_mmu_ctrl_r(), data);

	/* NV_PFB_FBHUB_NUM_ACTIVE_LTCS_HUB_SYS_NCOH_ATOMIC_MODE to USE_READ */
	data = nvgpu_readl(g, fb_fbhub_num_active_ltcs_r());
	data = set_field(data,
		fb_fbhub_num_active_ltcs_hub_sys_ncoh_atomic_mode_m(),
		fb_fbhub_num_active_ltcs_hub_sys_ncoh_atomic_mode_use_read_f());
	nvgpu_writel(g, fb_fbhub_num_active_ltcs_r(), data);

	return ret;
}
