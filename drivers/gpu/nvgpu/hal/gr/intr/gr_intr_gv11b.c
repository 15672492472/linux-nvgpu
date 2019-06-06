/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/gk20a.h>
#include <nvgpu/io.h>
#include <nvgpu/class.h>
#include <nvgpu/safe_ops.h>
#include <nvgpu/nvgpu_err.h>

#include <nvgpu/gr/config.h>
#include <nvgpu/gr/gr.h>
#include <nvgpu/gr/gr_intr.h>
#include <nvgpu/gr/gr_falcon.h>

#include "gr_intr_gp10b.h"
#include "gr_intr_gv11b.h"

#include <nvgpu/hw/gv11b/hw_gr_gv11b.h>

static void gv11b_gr_intr_handle_fecs_ecc_error(struct gk20a *g)
{
	struct nvgpu_fecs_ecc_status fecs_ecc_status;

	(void) memset(&fecs_ecc_status, 0, sizeof(fecs_ecc_status));

	g->ops.gr.falcon.handle_fecs_ecc_error(g, &fecs_ecc_status);

	g->ecc.gr.fecs_ecc_corrected_err_count[0].counter +=
				fecs_ecc_status.corrected_delta;
	g->ecc.gr.fecs_ecc_uncorrected_err_count[0].counter +=
				fecs_ecc_status.uncorrected_delta;

	if (fecs_ecc_status.imem_corrected_err) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_FECS, 0,
			GPU_FECS_FALCON_IMEM_ECC_CORRECTED,
			fecs_ecc_status.ecc_addr,
			g->ecc.gr.fecs_ecc_corrected_err_count[0].counter);
		nvgpu_log(g, gpu_dbg_intr, "imem ecc error corrected");
	}
	if (fecs_ecc_status.imem_uncorrected_err) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_FECS, 0,
			GPU_FECS_FALCON_IMEM_ECC_UNCORRECTED,
			fecs_ecc_status.ecc_addr,
			g->ecc.gr.fecs_ecc_uncorrected_err_count[0].counter);
		nvgpu_log(g, gpu_dbg_intr, "imem ecc error uncorrected");
	}
	if (fecs_ecc_status.dmem_corrected_err) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_FECS, 0,
			GPU_FECS_FALCON_DMEM_ECC_CORRECTED,
			fecs_ecc_status.ecc_addr,
			g->ecc.gr.fecs_ecc_corrected_err_count[0].counter);
		nvgpu_log(g, gpu_dbg_intr, "dmem ecc error corrected");
	}
	if (fecs_ecc_status.dmem_uncorrected_err) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_FECS, 0,
			GPU_FECS_FALCON_DMEM_ECC_UNCORRECTED,
			fecs_ecc_status.ecc_addr,
			g->ecc.gr.fecs_ecc_uncorrected_err_count[0].counter);
		nvgpu_log(g, gpu_dbg_intr,
					"dmem ecc error uncorrected");
	}

	nvgpu_log(g, gpu_dbg_intr,
		"ecc error count corrected: %d, uncorrected %d",
		g->ecc.gr.fecs_ecc_corrected_err_count[0].counter,
		g->ecc.gr.fecs_ecc_uncorrected_err_count[0].counter);
}

int gv11b_gr_intr_handle_fecs_error(struct gk20a *g,
				struct nvgpu_channel *ch_ptr,
				struct nvgpu_gr_isr_data *isr_data)
{
	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr, " ");

	/* Handle ECC errors */
	gv11b_gr_intr_handle_fecs_ecc_error(g);

	return gp10b_gr_intr_handle_fecs_error(g, ch_ptr, isr_data);
}

void gv11b_gr_intr_set_tex_in_dbg(struct gk20a *g, u32 data)
{
	u32 val;
	u32 flag;

	nvgpu_log_fn(g, " ");

	val = nvgpu_readl(g, gr_gpcs_tpcs_tex_in_dbg_r());
	flag = (data & NVC397_SET_TEX_IN_DBG_TSL1_RVCH_INVALIDATE) != 0U
		? 1U : 0U;
	val = set_field(val, gr_gpcs_tpcs_tex_in_dbg_tsl1_rvch_invalidate_m(),
			gr_gpcs_tpcs_tex_in_dbg_tsl1_rvch_invalidate_f(flag));
	nvgpu_writel(g, gr_gpcs_tpcs_tex_in_dbg_r(), val);

	val = nvgpu_readl(g, gr_gpcs_tpcs_sm_l1tag_ctrl_r());
	flag = (data &
		NVC397_SET_TEX_IN_DBG_SM_L1TAG_CTRL_CACHE_SURFACE_LD) != 0U
		? 1U : 0U;
	val = set_field(val, gr_gpcs_tpcs_sm_l1tag_ctrl_cache_surface_ld_m(),
			gr_gpcs_tpcs_sm_l1tag_ctrl_cache_surface_ld_f(flag));
	flag = (data &
		NVC397_SET_TEX_IN_DBG_SM_L1TAG_CTRL_CACHE_SURFACE_ST) != 0U
		? 1U : 0U;

	val = set_field(val, gr_gpcs_tpcs_sm_l1tag_ctrl_cache_surface_st_m(),
			gr_gpcs_tpcs_sm_l1tag_ctrl_cache_surface_st_f(flag));
	nvgpu_writel(g, gr_gpcs_tpcs_sm_l1tag_ctrl_r(), val);
}

void gv11b_gr_intr_set_skedcheck(struct gk20a *g, u32 data)
{
	u32 reg_val;

	reg_val = nvgpu_readl(g, gr_sked_hww_esr_en_r());

	if ((data & NVC397_SET_SKEDCHECK_18_MASK) ==
			NVC397_SET_SKEDCHECK_18_DISABLE) {
		reg_val = set_field(reg_val,
		 gr_sked_hww_esr_en_skedcheck18_l1_config_too_small_m(),
		 gr_sked_hww_esr_en_skedcheck18_l1_config_too_small_disabled_f()
		 );
	} else {
		if ((data & NVC397_SET_SKEDCHECK_18_MASK) ==
				NVC397_SET_SKEDCHECK_18_ENABLE) {
			reg_val = set_field(reg_val,
			 gr_sked_hww_esr_en_skedcheck18_l1_config_too_small_m(),
			 gr_sked_hww_esr_en_skedcheck18_l1_config_too_small_enabled_f()
			 );
		}
	}
	nvgpu_log_info(g, "sked_hww_esr_en = 0x%x", reg_val);
	nvgpu_writel(g, gr_sked_hww_esr_en_r(), reg_val);

}

void gv11b_gr_intr_set_shader_cut_collector(struct gk20a *g, u32 data)
{
	u32 val;

	nvgpu_log_fn(g, "gr_gv11b_set_shader_cut_collector");

	val = nvgpu_readl(g, gr_gpcs_tpcs_sm_l1tag_ctrl_r());
	if ((data & NVC397_SET_SHADER_CUT_COLLECTOR_STATE_ENABLE) != 0U) {
		val = set_field(val,
		 gr_gpcs_tpcs_sm_l1tag_ctrl_always_cut_collector_m(),
		 gr_gpcs_tpcs_sm_l1tag_ctrl_always_cut_collector_enable_f());
	} else {
		val = set_field(val,
		 gr_gpcs_tpcs_sm_l1tag_ctrl_always_cut_collector_m(),
		 gr_gpcs_tpcs_sm_l1tag_ctrl_always_cut_collector_disable_f());
	}
	nvgpu_writel(g, gr_gpcs_tpcs_sm_l1tag_ctrl_r(), val);
}

int gv11b_gr_intr_handle_sw_method(struct gk20a *g, u32 addr,
				     u32 class_num, u32 offset, u32 data)
{
	int ret = 0;

	nvgpu_log_fn(g, " ");

	if (class_num == VOLTA_COMPUTE_A) {
		switch (offset << 2) {
		case NVC0C0_SET_SHADER_EXCEPTIONS:
			g->ops.gr.intr.set_shader_exceptions(g, data);
			break;
		case NVC3C0_SET_SKEDCHECK:
			gv11b_gr_intr_set_skedcheck(g, data);
			break;
		case NVC3C0_SET_SHADER_CUT_COLLECTOR:
			gv11b_gr_intr_set_shader_cut_collector(g, data);
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

	if (ret != 0) {
		goto fail;
	}

#if defined(NVGPU_DEBUGGER) && defined(NVGPU_GRAPHICS)
	if (class_num == VOLTA_A) {
		switch (offset << 2) {
		case NVC397_SET_SHADER_EXCEPTIONS:
			g->ops.gr.intr.set_shader_exceptions(g, data);
			break;
		case NVC397_SET_CIRCULAR_BUFFER_SIZE:
			g->ops.gr.set_circular_buffer_size(g, data);
			break;
		case NVC397_SET_ALPHA_CIRCULAR_BUFFER_SIZE:
			g->ops.gr.set_alpha_circular_buffer_size(g, data);
			break;
		case NVC397_SET_GO_IDLE_TIMEOUT:
			gp10b_gr_intr_set_go_idle_timeout(g, data);
			break;
		case NVC097_SET_COALESCE_BUFFER_SIZE:
			gp10b_gr_intr_set_coalesce_buffer_size(g, data);
			break;
		case NVC397_SET_TEX_IN_DBG:
			gv11b_gr_intr_set_tex_in_dbg(g, data);
			break;
		case NVC397_SET_SKEDCHECK:
			gv11b_gr_intr_set_skedcheck(g, data);
			break;
		case NVC397_SET_BES_CROP_DEBUG3:
			g->ops.gr.set_bes_crop_debug3(g, data);
			break;
		case NVC397_SET_BES_CROP_DEBUG4:
			g->ops.gr.set_bes_crop_debug4(g, data);
			break;
		case NVC397_SET_SHADER_CUT_COLLECTOR:
			gv11b_gr_intr_set_shader_cut_collector(g, data);
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}
#endif

fail:
	return ret;
}

void gv11b_gr_intr_set_shader_exceptions(struct gk20a *g, u32 data)
{
	nvgpu_log_fn(g, " ");

	if (data == NVA297_SET_SHADER_EXCEPTIONS_ENABLE_FALSE) {
		nvgpu_writel(g, gr_gpcs_tpcs_sms_hww_warp_esr_report_mask_r(),
				 0);
		nvgpu_writel(g, gr_gpcs_tpcs_sms_hww_global_esr_report_mask_r(),
				 0);
	} else {
		g->ops.gr.intr.set_hww_esr_report_mask(g);
	}
}

void gv11b_gr_intr_handle_gcc_exception(struct gk20a *g, u32 gpc,
				u32 tpc, u32 gpc_exception,
				u32 *corrected_err, u32 *uncorrected_err)
{
	u32 offset = nvgpu_gr_gpc_offset(g, gpc);
	u32 gcc_l15_ecc_status, gcc_l15_ecc_corrected_err_status = 0;
	u32 gcc_l15_ecc_uncorrected_err_status = 0;
	u32 gcc_l15_corrected_err_count_delta = 0;
	u32 gcc_l15_uncorrected_err_count_delta = 0;
	bool is_gcc_l15_ecc_corrected_total_err_overflow = false;
	bool is_gcc_l15_ecc_uncorrected_total_err_overflow = false;

	if (gr_gpc0_gpccs_gpc_exception_gcc_v(gpc_exception) == 0U) {
		return;
	}

	/* Check for gcc l15 ECC errors. */
	gcc_l15_ecc_status = nvgpu_readl(g,
		nvgpu_safe_add_u32(
			gr_pri_gpc0_gcc_l15_ecc_status_r(), offset));
	gcc_l15_ecc_corrected_err_status = gcc_l15_ecc_status &
		(gr_pri_gpc0_gcc_l15_ecc_status_corrected_err_bank0_m() |
		 gr_pri_gpc0_gcc_l15_ecc_status_corrected_err_bank1_m());
	gcc_l15_ecc_uncorrected_err_status = gcc_l15_ecc_status &
		(gr_pri_gpc0_gcc_l15_ecc_status_uncorrected_err_bank0_m() |
		 gr_pri_gpc0_gcc_l15_ecc_status_uncorrected_err_bank1_m());

	if ((gcc_l15_ecc_corrected_err_status == 0U) &&
	    (gcc_l15_ecc_uncorrected_err_status == 0U)) {
		return;
	}

	gcc_l15_corrected_err_count_delta =
		gr_pri_gpc0_gcc_l15_ecc_corrected_err_count_total_v(
		 nvgpu_readl(g,
			     gr_pri_gpc0_gcc_l15_ecc_corrected_err_count_r() +
			     offset));
	gcc_l15_uncorrected_err_count_delta =
		gr_pri_gpc0_gcc_l15_ecc_uncorrected_err_count_total_v(
		 nvgpu_readl(g,
			gr_pri_gpc0_gcc_l15_ecc_uncorrected_err_count_r() +
			offset));
	is_gcc_l15_ecc_corrected_total_err_overflow =
	 gr_pri_gpc0_gcc_l15_ecc_status_corrected_err_total_counter_overflow_v(
						gcc_l15_ecc_status) != 0U;
	is_gcc_l15_ecc_uncorrected_total_err_overflow =
	 gr_pri_gpc0_gcc_l15_ecc_status_uncorrected_err_total_counter_overflow_v(
						gcc_l15_ecc_status) != 0U;

	if ((gcc_l15_corrected_err_count_delta > 0U) ||
	    is_gcc_l15_ecc_corrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"corrected error (SBE) detected in GCC L1.5!"
			"err_mask [%08x] is_overf [%d]",
			gcc_l15_ecc_corrected_err_status,
			is_gcc_l15_ecc_corrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_gcc_l15_ecc_corrected_total_err_overflow) {
			gcc_l15_corrected_err_count_delta +=
			 BIT32(
			  gr_pri_gpc0_gcc_l15_ecc_corrected_err_count_total_s()
			 );
		}
		*corrected_err += gcc_l15_corrected_err_count_delta;
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_GCC, gpc,
				GPU_GCC_L15_ECC_CORRECTED,
				0, *corrected_err);
		nvgpu_writel(g,
		 gr_pri_gpc0_gcc_l15_ecc_corrected_err_count_r() + offset, 0);
	}
	if ((gcc_l15_uncorrected_err_count_delta > 0U) ||
	    is_gcc_l15_ecc_uncorrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Uncorrected error (DBE) detected in GCC L1.5!"
			"err_mask [%08x] is_overf [%d]",
			gcc_l15_ecc_uncorrected_err_status,
			is_gcc_l15_ecc_uncorrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_gcc_l15_ecc_uncorrected_total_err_overflow) {
			gcc_l15_uncorrected_err_count_delta +=
			BIT32(
			gr_pri_gpc0_gcc_l15_ecc_uncorrected_err_count_total_s()
			);
		}
		*uncorrected_err += gcc_l15_uncorrected_err_count_delta;
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_GCC, gpc,
				GPU_GCC_L15_ECC_UNCORRECTED,
				0, *uncorrected_err);
		nvgpu_writel(g,
		 gr_pri_gpc0_gcc_l15_ecc_uncorrected_err_count_r() + offset,
		 0);
	}

	nvgpu_writel(g, gr_pri_gpc0_gcc_l15_ecc_status_r() + offset,
			gr_pri_gpc0_gcc_l15_ecc_status_reset_task_f());
}

void gv11b_gr_intr_handle_gpc_gpcmmu_exception(struct gk20a *g, u32 gpc,
		u32 gpc_exception, u32 *corrected_err, u32 *uncorrected_err)
{
	u32 offset = nvgpu_gr_gpc_offset(g, gpc);
	u32 ecc_status, ecc_addr, corrected_cnt, uncorrected_cnt;
	u32 corrected_delta, uncorrected_delta;
	u32 corrected_overflow, uncorrected_overflow;
	u32 hww_esr;

	if ((gpc_exception & gr_gpc0_gpccs_gpc_exception_gpcmmu_m()) == 0U) {
		return;
	}

	hww_esr = nvgpu_readl(g,
			nvgpu_safe_add_u32(gr_gpc0_mmu_gpcmmu_global_esr_r(),
						offset));

	if ((hww_esr & (gr_gpc0_mmu_gpcmmu_global_esr_ecc_corrected_m() |
		gr_gpc0_mmu_gpcmmu_global_esr_ecc_uncorrected_m())) == 0U) {
		return;
	}

	ecc_status = nvgpu_readl(g,
		gr_gpc0_mmu_l1tlb_ecc_status_r() + offset);
	ecc_addr = nvgpu_readl(g,
		gr_gpc0_mmu_l1tlb_ecc_address_r() + offset);
	corrected_cnt = nvgpu_readl(g,
		gr_gpc0_mmu_l1tlb_ecc_corrected_err_count_r() + offset);
	uncorrected_cnt = nvgpu_readl(g,
		gr_gpc0_mmu_l1tlb_ecc_uncorrected_err_count_r() + offset);

	corrected_delta = gr_gpc0_mmu_l1tlb_ecc_corrected_err_count_total_v(
							corrected_cnt);
	uncorrected_delta =
		gr_gpc0_mmu_l1tlb_ecc_uncorrected_err_count_total_v(
							uncorrected_cnt);
	corrected_overflow = ecc_status &
	 gr_gpc0_mmu_l1tlb_ecc_status_corrected_err_total_counter_overflow_m();

	uncorrected_overflow = ecc_status &
	 gr_gpc0_mmu_l1tlb_ecc_status_uncorrected_err_total_counter_overflow_m();

	/* clear the interrupt */
	if ((corrected_delta > 0U) || (corrected_overflow != 0U)) {
		nvgpu_writel(g,
			gr_gpc0_mmu_l1tlb_ecc_corrected_err_count_r() +
			offset, 0);
	}
	if ((uncorrected_delta > 0U) || (uncorrected_overflow != 0U)) {
		nvgpu_writel(g,
			gr_gpc0_mmu_l1tlb_ecc_uncorrected_err_count_r() +
			offset, 0);
	}

	nvgpu_writel(g, gr_gpc0_mmu_l1tlb_ecc_status_r() + offset,
				gr_gpc0_mmu_l1tlb_ecc_status_reset_task_f());

	/* Handle overflow */
	if (corrected_overflow != 0U) {
		corrected_delta +=
		   BIT32(gr_gpc0_mmu_l1tlb_ecc_corrected_err_count_total_s());
	}
	if (uncorrected_overflow != 0U) {
		uncorrected_delta +=
		  BIT32(gr_gpc0_mmu_l1tlb_ecc_uncorrected_err_count_total_s());
	}

	*corrected_err += corrected_delta;
	*uncorrected_err += uncorrected_delta;

	nvgpu_log(g, gpu_dbg_intr,
		"mmu l1tlb gpc:%d ecc interrupt intr: 0x%x", gpc, hww_esr);

	if ((ecc_status &
	     gr_gpc0_mmu_l1tlb_ecc_status_corrected_err_l1tlb_sa_data_m()) !=
									0U) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_MMU, gpc,
				GPU_MMU_L1TLB_SA_DATA_ECC_CORRECTED,
				0, (u32)*corrected_err);
		nvgpu_log(g, gpu_dbg_intr, "corrected ecc sa data error");
	}
	if ((ecc_status &
	     gr_gpc0_mmu_l1tlb_ecc_status_uncorrected_err_l1tlb_sa_data_m()) !=
									 0U) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_MMU, gpc,
				GPU_MMU_L1TLB_SA_DATA_ECC_UNCORRECTED,
				0, (u32)*uncorrected_err);
		nvgpu_log(g, gpu_dbg_intr, "uncorrected ecc sa data error");
	}
	if ((ecc_status &
	     gr_gpc0_mmu_l1tlb_ecc_status_corrected_err_l1tlb_fa_data_m()) !=
									0U) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_MMU, gpc,
				GPU_MMU_L1TLB_FA_DATA_ECC_CORRECTED,
				0, (u32)*corrected_err);
		nvgpu_log(g, gpu_dbg_intr, "corrected ecc fa data error");
	}
	if ((ecc_status &
	     gr_gpc0_mmu_l1tlb_ecc_status_uncorrected_err_l1tlb_fa_data_m()) !=
									0U) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_MMU, gpc,
				GPU_MMU_L1TLB_FA_DATA_ECC_UNCORRECTED,
				0, (u32)*uncorrected_err);
		nvgpu_log(g, gpu_dbg_intr, "uncorrected ecc fa data error");
	}
	if ((corrected_overflow != 0U) || (uncorrected_overflow != 0U)) {
		nvgpu_info(g, "mmu l1tlb ecc counter overflow!");
	}

	nvgpu_log(g, gpu_dbg_intr,
		"ecc error address: 0x%x", ecc_addr);
	nvgpu_log(g, gpu_dbg_intr,
		"ecc error count corrected: %d, uncorrected %d",
		(u32)*corrected_err, (u32)*uncorrected_err);
}

void gv11b_gr_intr_handle_gpc_gpccs_exception(struct gk20a *g, u32 gpc,
		u32 gpc_exception, u32 *corrected_err, u32 *uncorrected_err)
{
	u32 offset = nvgpu_gr_gpc_offset(g, gpc);
	u32 ecc_status, ecc_addr, corrected_cnt, uncorrected_cnt;
	u32 corrected_delta, uncorrected_delta;
	u32 corrected_overflow, uncorrected_overflow;
	u32 hww_esr;

	if ((gpc_exception & gr_gpc0_gpccs_gpc_exception_gpccs_m()) == 0U) {
		return;
	}

	hww_esr = nvgpu_readl(g,
			nvgpu_safe_add_u32(gr_gpc0_gpccs_hww_esr_r(),
						offset));

	if ((hww_esr & (gr_gpc0_gpccs_hww_esr_ecc_uncorrected_m() |
			gr_gpc0_gpccs_hww_esr_ecc_corrected_m())) == 0U) {
		return;
	}

	ecc_status = nvgpu_readl(g,
		gr_gpc0_gpccs_falcon_ecc_status_r() + offset);
	ecc_addr = nvgpu_readl(g,
		gr_gpc0_gpccs_falcon_ecc_address_r() + offset);
	corrected_cnt = nvgpu_readl(g,
		gr_gpc0_gpccs_falcon_ecc_corrected_err_count_r() + offset);
	uncorrected_cnt = nvgpu_readl(g,
		gr_gpc0_gpccs_falcon_ecc_uncorrected_err_count_r() + offset);

	corrected_delta =
		gr_gpc0_gpccs_falcon_ecc_corrected_err_count_total_v(
							corrected_cnt);
	uncorrected_delta =
		gr_gpc0_gpccs_falcon_ecc_uncorrected_err_count_total_v(
							uncorrected_cnt);
	corrected_overflow = ecc_status &
	 gr_gpc0_gpccs_falcon_ecc_status_corrected_err_total_counter_overflow_m();

	uncorrected_overflow = ecc_status &
	 gr_gpc0_gpccs_falcon_ecc_status_uncorrected_err_total_counter_overflow_m();


	/* clear the interrupt */
	if ((corrected_delta > 0U) || (corrected_overflow != 0U)) {
		nvgpu_writel(g,
			gr_gpc0_gpccs_falcon_ecc_corrected_err_count_r() +
			offset, 0);
	}
	if ((uncorrected_delta > 0U) || (uncorrected_overflow != 0U)) {
		nvgpu_writel(g,
			gr_gpc0_gpccs_falcon_ecc_uncorrected_err_count_r() +
			offset, 0);
	}

	nvgpu_writel(g, gr_gpc0_gpccs_falcon_ecc_status_r() + offset,
			gr_gpc0_gpccs_falcon_ecc_status_reset_task_f());

	*corrected_err += corrected_delta;
	*corrected_err += uncorrected_delta;

	nvgpu_log(g, gpu_dbg_intr,
			"gppcs gpc:%d ecc interrupt intr: 0x%x", gpc, hww_esr);

	if ((ecc_status &
	     gr_gpc0_gpccs_falcon_ecc_status_corrected_err_imem_m()) != 0U) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_GPCCS,
				gpc, GPU_GPCCS_FALCON_IMEM_ECC_CORRECTED,
				ecc_addr, (u32)*corrected_err);
		nvgpu_log(g, gpu_dbg_intr, "imem ecc error corrected");
	}
	if ((ecc_status &
	     gr_gpc0_gpccs_falcon_ecc_status_uncorrected_err_imem_m()) != 0U) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_GPCCS,
				gpc, GPU_GPCCS_FALCON_IMEM_ECC_UNCORRECTED,
				ecc_addr, (u32)*uncorrected_err);
		nvgpu_log(g, gpu_dbg_intr, "imem ecc error uncorrected");
	}
	if ((ecc_status &
	     gr_gpc0_gpccs_falcon_ecc_status_corrected_err_dmem_m()) != 0U) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_GPCCS,
				gpc, GPU_GPCCS_FALCON_DMEM_ECC_CORRECTED,
				ecc_addr, (u32)*corrected_err);
		nvgpu_log(g, gpu_dbg_intr, "dmem ecc error corrected");
	}
	if ((ecc_status &
	     gr_gpc0_gpccs_falcon_ecc_status_uncorrected_err_dmem_m()) != 0U) {
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_GPCCS,
				gpc, GPU_GPCCS_FALCON_DMEM_ECC_UNCORRECTED,
				ecc_addr, (u32)*uncorrected_err);
		nvgpu_log(g, gpu_dbg_intr, "dmem ecc error uncorrected");
	}
	if ((corrected_overflow != 0U) || (uncorrected_overflow != 0U)) {
		nvgpu_info(g, "gpccs ecc counter overflow!");
	}

	nvgpu_log(g, gpu_dbg_intr,
		"ecc error row address: 0x%x",
		gr_gpc0_gpccs_falcon_ecc_address_row_address_v(ecc_addr));

	nvgpu_log(g, gpu_dbg_intr,
			"ecc error count corrected: %d, uncorrected %d",
			(u32)*corrected_err, (u32)*uncorrected_err);
}

void gv11b_gr_intr_handle_tpc_mpc_exception(struct gk20a *g, u32 gpc, u32 tpc)
{
	u32 esr;
	u32 offset = nvgpu_safe_add_u32(nvgpu_gr_gpc_offset(g, gpc),
					nvgpu_gr_tpc_offset(g, tpc));

	esr = nvgpu_readl(g,
			nvgpu_safe_add_u32(gr_gpc0_tpc0_mpc_hww_esr_r(),
						offset));
	nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg, "mpc hww esr 0x%08x", esr);

	nvgpu_gr_intr_report_exception(g, ((gpc << 8U) | tpc),
			GPU_PGRAPH_MPC_EXCEPTION,
			esr);

	esr = nvgpu_readl(g,
			nvgpu_safe_add_u32(gr_gpc0_tpc0_mpc_hww_esr_info_r(),
						offset));
	nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			"mpc hww esr info: veid 0x%08x",
			gr_gpc0_tpc0_mpc_hww_esr_info_veid_v(esr));

	nvgpu_writel(g,
		     nvgpu_safe_add_u32(gr_gpc0_tpc0_mpc_hww_esr_r(),
						offset),
		     gr_gpc0_tpc0_mpc_hww_esr_reset_trigger_f());
}

void gv11b_gr_intr_enable_hww_exceptions(struct gk20a *g)
{
	/* enable exceptions */

	nvgpu_writel(g, gr_fe_hww_esr_r(),
		     gr_fe_hww_esr_en_enable_f() |
		     gr_fe_hww_esr_reset_active_f());
	nvgpu_writel(g, gr_memfmt_hww_esr_r(),
		     gr_memfmt_hww_esr_en_enable_f() |
		     gr_memfmt_hww_esr_reset_active_f());
	nvgpu_writel(g, gr_pd_hww_esr_r(),
		     gr_pd_hww_esr_en_enable_f() |
		     gr_pd_hww_esr_reset_active_f());
	nvgpu_writel(g, gr_scc_hww_esr_r(),
		     gr_scc_hww_esr_en_enable_f() |
		     gr_scc_hww_esr_reset_active_f());
	nvgpu_writel(g, gr_ds_hww_esr_r(),
		     gr_ds_hww_esr_en_enabled_f() |
		     gr_ds_hww_esr_reset_task_f());
	nvgpu_writel(g, gr_ssync_hww_esr_r(),
		     gr_ssync_hww_esr_en_enable_f() |
		     gr_ssync_hww_esr_reset_active_f());
	nvgpu_writel(g, gr_mme_hww_esr_r(),
		     gr_mme_hww_esr_en_enable_f() |
		     gr_mme_hww_esr_reset_active_f());

	/* For now leave POR values */
	nvgpu_log(g, gpu_dbg_info, "gr_sked_hww_esr_en_r 0x%08x",
			nvgpu_readl(g, gr_sked_hww_esr_en_r()));
}

void gv11b_gr_intr_enable_exceptions(struct gk20a *g,
				     struct nvgpu_gr_config *gr_config,
				     bool enable)
{
	u32 reg_val = 0U;

	if (!enable) {
		nvgpu_writel(g, gr_exception_en_r(), reg_val);
		nvgpu_writel(g, gr_exception1_en_r(), reg_val);
		nvgpu_writel(g, gr_exception2_en_r(), reg_val);
		return;
	}

	/*
	 * clear exceptions :
	 * other than SM : hww_esr are reset in *enable_hww_excetpions*
	 * SM            : cleared in *set_hww_esr_report_mask*
	 */

	/* enable exceptions */
	nvgpu_writel(g, gr_exception2_en_r(), 0x0U); /* BE not enabled */

	reg_val = (u32)BIT32(nvgpu_gr_config_get_gpc_count(gr_config));
	nvgpu_writel(g, gr_exception1_en_r(),
				nvgpu_safe_sub_u32(reg_val, 1U));

	reg_val = gr_exception_en_fe_enabled_f() |
			gr_exception_en_memfmt_enabled_f() |
			gr_exception_en_pd_enabled_f() |
			gr_exception_en_scc_enabled_f() |
			gr_exception_en_ds_enabled_f() |
			gr_exception_en_ssync_enabled_f() |
			gr_exception_en_mme_enabled_f() |
			gr_exception_en_sked_enabled_f() |
			gr_exception_en_gpc_enabled_f();

	nvgpu_log(g, gpu_dbg_info, "gr_exception_en 0x%08x", reg_val);

	nvgpu_writel(g, gr_exception_en_r(), reg_val);
}

void gv11b_gr_intr_enable_gpc_exceptions(struct gk20a *g,
					 struct nvgpu_gr_config *gr_config)
{
	u32 tpc_mask, tpc_mask_calc;

	nvgpu_writel(g, gr_gpcs_tpcs_tpccs_tpc_exception_en_r(),
			gr_gpcs_tpcs_tpccs_tpc_exception_en_sm_enabled_f() |
			gr_gpcs_tpcs_tpccs_tpc_exception_en_mpc_enabled_f());

	tpc_mask_calc = (u32)BIT32(
			 nvgpu_gr_config_get_max_tpc_per_gpc_count(gr_config));
	tpc_mask =
		gr_gpcs_gpccs_gpc_exception_en_tpc_f(
			nvgpu_safe_sub_u32(tpc_mask_calc, 1U));

	nvgpu_writel(g, gr_gpcs_gpccs_gpc_exception_en_r(),
		(tpc_mask | gr_gpcs_gpccs_gpc_exception_en_gcc_f(1U) |
			    gr_gpcs_gpccs_gpc_exception_en_gpccs_f(1U) |
			    gr_gpcs_gpccs_gpc_exception_en_gpcmmu_f(1U)));
}

void gv11b_gr_intr_set_hww_esr_report_mask(struct gk20a *g)
{

	/* clear hww */
	nvgpu_writel(g, gr_gpcs_tpcs_sms_hww_global_esr_r(), 0xffffffffU);
	nvgpu_writel(g, gr_gpcs_tpcs_sms_hww_global_esr_r(), 0xffffffffU);

	/* setup sm warp esr report masks */
	nvgpu_writel(g, gr_gpcs_tpcs_sms_hww_warp_esr_report_mask_r(),
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_stack_error_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_api_stack_error_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_pc_wrap_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_misaligned_pc_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_pc_overflow_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_misaligned_reg_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_illegal_instr_encoding_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_illegal_instr_param_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_oor_reg_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_oor_addr_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_misaligned_addr_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_invalid_addr_space_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_invalid_const_addr_ldc_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_stack_overflow_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_mmu_fault_report_f() |
		gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_mmu_nack_report_f());

	/* setup sm global esr report mask. vat_alarm_report is not enabled */
	nvgpu_writel(g, gr_gpcs_tpcs_sms_hww_global_esr_report_mask_r(),
		gr_gpc0_tpc0_sm0_hww_global_esr_report_mask_multiple_warp_errors_report_f());
}

static void gv11b_gr_intr_handle_l1_tag_exception(struct gk20a *g, u32 gpc, u32 tpc,
			bool *post_event, struct nvgpu_channel *fault_ch,
			u32 *hww_global_esr)
{
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 tpc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_TPC_IN_GPC_STRIDE);
	u32 offset;
	u32 l1_tag_ecc_status, l1_tag_ecc_corrected_err_status = 0;
	u32 l1_tag_ecc_uncorrected_err_status = 0;
	u32 l1_tag_corrected_err_count_delta = 0;
	u32 l1_tag_uncorrected_err_count_delta = 0;
	bool is_l1_tag_ecc_corrected_total_err_overflow = false;
	bool is_l1_tag_ecc_uncorrected_total_err_overflow = false;

	offset = nvgpu_safe_add_u32(
			nvgpu_safe_mult_u32(gpc_stride, gpc),
			nvgpu_safe_mult_u32(tpc_in_gpc_stride, tpc));

	/* Check for L1 tag ECC errors. */
	l1_tag_ecc_status = nvgpu_readl(g, nvgpu_safe_add_u32(
		gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_r(), offset));
	l1_tag_ecc_corrected_err_status = l1_tag_ecc_status &
		(gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_corrected_err_el1_0_m() |
		 gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_corrected_err_el1_1_m() |
		 gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_corrected_err_pixrpf_m() |
		 gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_corrected_err_miss_fifo_m());
	l1_tag_ecc_uncorrected_err_status = l1_tag_ecc_status &
		(gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_uncorrected_err_el1_0_m() |
		 gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_uncorrected_err_el1_1_m() |
		 gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_uncorrected_err_pixrpf_m() |
		 gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_uncorrected_err_miss_fifo_m());

	if ((l1_tag_ecc_corrected_err_status == 0U) && (l1_tag_ecc_uncorrected_err_status == 0U)) {
		return;
	}

	l1_tag_corrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_l1_tag_ecc_corrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_l1_tag_ecc_corrected_err_count_r(),
				offset)));
	l1_tag_uncorrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_l1_tag_ecc_uncorrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_l1_tag_ecc_uncorrected_err_count_r(),
				offset)));
	is_l1_tag_ecc_corrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_corrected_err_total_counter_overflow_v(l1_tag_ecc_status) != 0U;
	is_l1_tag_ecc_uncorrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_uncorrected_err_total_counter_overflow_v(l1_tag_ecc_status) != 0U;

	if ((l1_tag_corrected_err_count_delta > 0U) || is_l1_tag_ecc_corrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"corrected error (SBE) detected in SM L1 tag! err_mask [%08x] is_overf [%d]",
			l1_tag_ecc_corrected_err_status, is_l1_tag_ecc_corrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_l1_tag_ecc_corrected_total_err_overflow) {
			l1_tag_corrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_l1_tag_ecc_corrected_err_count_total_s());
		}
		g->ecc.gr.sm_l1_tag_ecc_corrected_err_count[gpc][tpc].counter +=
							l1_tag_corrected_err_count_delta;
		if ((l1_tag_ecc_status &
			(gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_corrected_err_el1_0_m() |
			 gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_corrected_err_el1_1_m())) != 0U) {
				(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_L1_TAG_ECC_CORRECTED, 0,
					g->ecc.gr.sm_l1_tag_ecc_corrected_err_count[gpc][tpc].counter);
		}
		if ((l1_tag_ecc_status &
			gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_corrected_err_miss_fifo_m()) != 0U) {
				(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_L1_TAG_MISS_FIFO_ECC_CORRECTED, 0,
					g->ecc.gr.sm_l1_tag_ecc_corrected_err_count[gpc][tpc].counter);
		}
		if ((l1_tag_ecc_status &
			gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_corrected_err_pixrpf_m()) != 0U) {
				(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_L1_TAG_S2R_PIXPRF_ECC_CORRECTED, 0,
					g->ecc.gr.sm_l1_tag_ecc_corrected_err_count[gpc][tpc].counter);
		}
		nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_pri_gpc0_tpc0_sm_l1_tag_ecc_corrected_err_count_r(), offset),
			0);
	}
	if ((l1_tag_uncorrected_err_count_delta > 0U) || is_l1_tag_ecc_uncorrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Uncorrected error (DBE) detected in SM L1 tag! err_mask [%08x] is_overf [%d]",
			l1_tag_ecc_uncorrected_err_status, is_l1_tag_ecc_uncorrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_l1_tag_ecc_uncorrected_total_err_overflow) {
			l1_tag_uncorrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_l1_tag_ecc_uncorrected_err_count_total_s());
		}
		g->ecc.gr.sm_l1_tag_ecc_uncorrected_err_count[gpc][tpc].counter +=
							l1_tag_uncorrected_err_count_delta;
		if ((l1_tag_ecc_status &
			(gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_uncorrected_err_el1_0_m() |
			 gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_uncorrected_err_el1_1_m())) != 0U) {
				(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_L1_TAG_ECC_UNCORRECTED, 0,
					g->ecc.gr.sm_l1_tag_ecc_uncorrected_err_count[gpc][tpc].counter);
		}
		if ((l1_tag_ecc_status &
			gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_uncorrected_err_miss_fifo_m()) != 0U) {
				(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_L1_TAG_MISS_FIFO_ECC_UNCORRECTED, 0,
					g->ecc.gr.sm_l1_tag_ecc_uncorrected_err_count[gpc][tpc].counter);
		}
		if ((l1_tag_ecc_status &
			gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_uncorrected_err_pixrpf_m()) != 0U) {
				(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_L1_TAG_S2R_PIXPRF_ECC_UNCORRECTED, 0,
					g->ecc.gr.sm_l1_tag_ecc_uncorrected_err_count[gpc][tpc].counter);
		}
		nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_pri_gpc0_tpc0_sm_l1_tag_ecc_uncorrected_err_count_r(), offset),
			0);
	}

	nvgpu_writel(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_r(), offset),
			gr_pri_gpc0_tpc0_sm_l1_tag_ecc_status_reset_task_f());
}

static void gv11b_gr_intr_handle_lrf_exception(struct gk20a *g, u32 gpc, u32 tpc,
			bool *post_event, struct nvgpu_channel *fault_ch,
			u32 *hww_global_esr)
{
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 tpc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_TPC_IN_GPC_STRIDE);
	u32 offset;
	u32 lrf_ecc_status, lrf_ecc_corrected_err_status = 0;
	u32 lrf_ecc_uncorrected_err_status = 0;
	u32 lrf_corrected_err_count_delta = 0;
	u32 lrf_uncorrected_err_count_delta = 0;
	bool is_lrf_ecc_corrected_total_err_overflow = false;
	bool is_lrf_ecc_uncorrected_total_err_overflow = false;

	offset = nvgpu_safe_add_u32(
			nvgpu_safe_mult_u32(gpc_stride, gpc),
			nvgpu_safe_mult_u32(tpc_in_gpc_stride, tpc));

	/* Check for LRF ECC errors. */
	lrf_ecc_status = nvgpu_readl(g,
		nvgpu_safe_add_u32(gr_pri_gpc0_tpc0_sm_lrf_ecc_status_r(),
				   offset));
	lrf_ecc_corrected_err_status = lrf_ecc_status &
		(gr_pri_gpc0_tpc0_sm_lrf_ecc_status_corrected_err_qrfdp0_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_corrected_err_qrfdp1_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_corrected_err_qrfdp2_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_corrected_err_qrfdp3_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_corrected_err_qrfdp4_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_corrected_err_qrfdp5_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_corrected_err_qrfdp6_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_corrected_err_qrfdp7_m());
	lrf_ecc_uncorrected_err_status = lrf_ecc_status &
		(gr_pri_gpc0_tpc0_sm_lrf_ecc_status_uncorrected_err_qrfdp0_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_uncorrected_err_qrfdp1_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_uncorrected_err_qrfdp2_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_uncorrected_err_qrfdp3_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_uncorrected_err_qrfdp4_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_uncorrected_err_qrfdp5_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_uncorrected_err_qrfdp6_m() |
		 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_uncorrected_err_qrfdp7_m());

	if ((lrf_ecc_corrected_err_status == 0U) && (lrf_ecc_uncorrected_err_status == 0U)) {
		return;
	}

	lrf_corrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_lrf_ecc_corrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_lrf_ecc_corrected_err_count_r(),
				offset)));
	lrf_uncorrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_lrf_ecc_uncorrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_lrf_ecc_uncorrected_err_count_r(),
				offset)));
	is_lrf_ecc_corrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_lrf_ecc_status_corrected_err_total_counter_overflow_v(lrf_ecc_status) != 0U;
	is_lrf_ecc_uncorrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_lrf_ecc_status_uncorrected_err_total_counter_overflow_v(lrf_ecc_status) != 0U;

	if ((lrf_corrected_err_count_delta > 0U) || is_lrf_ecc_corrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"corrected error (SBE) detected in SM LRF! err_mask [%08x] is_overf [%d]",
			lrf_ecc_corrected_err_status, is_lrf_ecc_corrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_lrf_ecc_corrected_total_err_overflow) {
			lrf_corrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_lrf_ecc_corrected_err_count_total_s());
		}
		g->ecc.gr.sm_lrf_ecc_single_err_count[gpc][tpc].counter +=
							lrf_corrected_err_count_delta;
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
				(gpc << 8) | tpc,
				GPU_SM_LRF_ECC_CORRECTED, 0,
				g->ecc.gr.sm_lrf_ecc_single_err_count[gpc][tpc].counter);
		nvgpu_writel(g,
			gr_pri_gpc0_tpc0_sm_lrf_ecc_corrected_err_count_r() + offset,
			0);
	}
	if ((lrf_uncorrected_err_count_delta > 0U) || is_lrf_ecc_uncorrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Uncorrected error (DBE) detected in SM LRF! err_mask [%08x] is_overf [%d]",
			lrf_ecc_uncorrected_err_status, is_lrf_ecc_uncorrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_lrf_ecc_uncorrected_total_err_overflow) {
			lrf_uncorrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_lrf_ecc_uncorrected_err_count_total_s());
		}
		g->ecc.gr.sm_lrf_ecc_double_err_count[gpc][tpc].counter +=
							lrf_uncorrected_err_count_delta;
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
				(gpc << 8) | tpc,
				GPU_SM_LRF_ECC_UNCORRECTED, 0,
				g->ecc.gr.sm_lrf_ecc_double_err_count[gpc][tpc].counter);
		nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_pri_gpc0_tpc0_sm_lrf_ecc_uncorrected_err_count_r(), offset),
			0);
	}

	nvgpu_writel(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_lrf_ecc_status_r(), offset),
			gr_pri_gpc0_tpc0_sm_lrf_ecc_status_reset_task_f());
}

static void gv11b_gr_intr_handle_cbu_exception(struct gk20a *g, u32 gpc, u32 tpc,
			bool *post_event, struct nvgpu_channel *fault_ch,
			u32 *hww_global_esr)
{
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 tpc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_TPC_IN_GPC_STRIDE);
	u32 offset;
	u32 cbu_ecc_status, cbu_ecc_corrected_err_status = 0;
	u32 cbu_ecc_uncorrected_err_status = 0;
	u32 cbu_corrected_err_count_delta = 0;
	u32 cbu_uncorrected_err_count_delta = 0;
	bool is_cbu_ecc_corrected_total_err_overflow = false;
	bool is_cbu_ecc_uncorrected_total_err_overflow = false;

	offset = nvgpu_safe_add_u32(
			nvgpu_safe_mult_u32(gpc_stride, gpc),
			nvgpu_safe_mult_u32(tpc_in_gpc_stride, tpc));

	/* Check for CBU ECC errors. */
	cbu_ecc_status = nvgpu_readl(g, nvgpu_safe_add_u32(
		gr_pri_gpc0_tpc0_sm_cbu_ecc_status_r(), offset));
	cbu_ecc_corrected_err_status = cbu_ecc_status &
		(gr_pri_gpc0_tpc0_sm_cbu_ecc_status_corrected_err_warp_sm0_m() |
		 gr_pri_gpc0_tpc0_sm_cbu_ecc_status_corrected_err_warp_sm1_m() |
		 gr_pri_gpc0_tpc0_sm_cbu_ecc_status_corrected_err_barrier_sm0_m() |
		 gr_pri_gpc0_tpc0_sm_cbu_ecc_status_corrected_err_barrier_sm1_m());
	cbu_ecc_uncorrected_err_status = cbu_ecc_status &
		(gr_pri_gpc0_tpc0_sm_cbu_ecc_status_uncorrected_err_warp_sm0_m() |
		 gr_pri_gpc0_tpc0_sm_cbu_ecc_status_uncorrected_err_warp_sm1_m() |
		 gr_pri_gpc0_tpc0_sm_cbu_ecc_status_uncorrected_err_barrier_sm0_m() |
		 gr_pri_gpc0_tpc0_sm_cbu_ecc_status_uncorrected_err_barrier_sm1_m());

	if ((cbu_ecc_corrected_err_status == 0U) && (cbu_ecc_uncorrected_err_status == 0U)) {
		return;
	}

	cbu_corrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_cbu_ecc_corrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_cbu_ecc_corrected_err_count_r(),
				offset)));
	cbu_uncorrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_cbu_ecc_uncorrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_cbu_ecc_uncorrected_err_count_r(),
				offset)));
	is_cbu_ecc_corrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_cbu_ecc_status_corrected_err_total_counter_overflow_v(cbu_ecc_status) != 0U;
	is_cbu_ecc_uncorrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_cbu_ecc_status_uncorrected_err_total_counter_overflow_v(cbu_ecc_status) != 0U;

	if ((cbu_corrected_err_count_delta > 0U) || is_cbu_ecc_corrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"corrected error (SBE) detected in SM CBU! err_mask [%08x] is_overf [%d]",
			cbu_ecc_corrected_err_status, is_cbu_ecc_corrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_cbu_ecc_corrected_total_err_overflow) {
			cbu_corrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_cbu_ecc_corrected_err_count_total_s());
		}
		g->ecc.gr.sm_cbu_ecc_corrected_err_count[gpc][tpc].counter +=
							cbu_corrected_err_count_delta;
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
				(gpc << 8) | tpc,
				GPU_SM_CBU_ECC_CORRECTED,
				0, g->ecc.gr.sm_cbu_ecc_corrected_err_count[gpc][tpc].counter);
		nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_pri_gpc0_tpc0_sm_cbu_ecc_corrected_err_count_r(), offset),
			0);
	}
	if ((cbu_uncorrected_err_count_delta > 0U) || is_cbu_ecc_uncorrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Uncorrected error (DBE) detected in SM CBU! err_mask [%08x] is_overf [%d]",
			cbu_ecc_uncorrected_err_status, is_cbu_ecc_uncorrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_cbu_ecc_uncorrected_total_err_overflow) {
			cbu_uncorrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_cbu_ecc_uncorrected_err_count_total_s());
		}
		g->ecc.gr.sm_cbu_ecc_uncorrected_err_count[gpc][tpc].counter +=
							cbu_uncorrected_err_count_delta;
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
				(gpc << 8) | tpc,
				GPU_SM_CBU_ECC_UNCORRECTED,
				0, g->ecc.gr.sm_cbu_ecc_uncorrected_err_count[gpc][tpc].counter);
		nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_pri_gpc0_tpc0_sm_cbu_ecc_uncorrected_err_count_r(), offset),
			0);
	}

	nvgpu_writel(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_cbu_ecc_status_r(), offset),
			gr_pri_gpc0_tpc0_sm_cbu_ecc_status_reset_task_f());
}

static void gv11b_gr_intr_handle_l1_data_exception(struct gk20a *g, u32 gpc, u32 tpc,
			bool *post_event, struct nvgpu_channel *fault_ch,
			u32 *hww_global_esr)
{
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 tpc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_TPC_IN_GPC_STRIDE);
	u32 offset;
	u32 l1_data_ecc_status, l1_data_ecc_corrected_err_status = 0;
	u32 l1_data_ecc_uncorrected_err_status = 0;
	u32 l1_data_corrected_err_count_delta = 0;
	u32 l1_data_uncorrected_err_count_delta = 0;
	bool is_l1_data_ecc_corrected_total_err_overflow = false;
	bool is_l1_data_ecc_uncorrected_total_err_overflow = false;

	offset = nvgpu_safe_add_u32(
			nvgpu_safe_mult_u32(gpc_stride, gpc),
			nvgpu_safe_mult_u32(tpc_in_gpc_stride, tpc));

	/* Check for L1 data ECC errors. */
	l1_data_ecc_status = nvgpu_readl(g, nvgpu_safe_add_u32(
		gr_pri_gpc0_tpc0_sm_l1_data_ecc_status_r(), offset));
	l1_data_ecc_corrected_err_status = l1_data_ecc_status &
		(gr_pri_gpc0_tpc0_sm_l1_data_ecc_status_corrected_err_el1_0_m() |
		 gr_pri_gpc0_tpc0_sm_l1_data_ecc_status_corrected_err_el1_1_m());
	l1_data_ecc_uncorrected_err_status = l1_data_ecc_status &
		(gr_pri_gpc0_tpc0_sm_l1_data_ecc_status_uncorrected_err_el1_0_m() |
		 gr_pri_gpc0_tpc0_sm_l1_data_ecc_status_uncorrected_err_el1_1_m());

	if ((l1_data_ecc_corrected_err_status == 0U) && (l1_data_ecc_uncorrected_err_status == 0U)) {
		return;
	}

	l1_data_corrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_l1_data_ecc_corrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_l1_data_ecc_corrected_err_count_r(),
				offset)));
	l1_data_uncorrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_l1_data_ecc_uncorrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_l1_data_ecc_uncorrected_err_count_r(),
				offset)));
	is_l1_data_ecc_corrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_l1_data_ecc_status_corrected_err_total_counter_overflow_v(l1_data_ecc_status) != 0U;
	is_l1_data_ecc_uncorrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_l1_data_ecc_status_uncorrected_err_total_counter_overflow_v(l1_data_ecc_status) != 0U;

	if ((l1_data_corrected_err_count_delta > 0U) || is_l1_data_ecc_corrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"corrected error (SBE) detected in SM L1 data! err_mask [%08x] is_overf [%d]",
			l1_data_ecc_corrected_err_status, is_l1_data_ecc_corrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_l1_data_ecc_corrected_total_err_overflow) {
			l1_data_corrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_l1_data_ecc_corrected_err_count_total_s());
		}
		g->ecc.gr.sm_l1_data_ecc_corrected_err_count[gpc][tpc].counter +=
							l1_data_corrected_err_count_delta;
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
				(gpc << 8) | tpc,
				GPU_SM_L1_DATA_ECC_CORRECTED,
				0, g->ecc.gr.sm_l1_data_ecc_corrected_err_count[gpc][tpc].counter);
		nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_pri_gpc0_tpc0_sm_l1_data_ecc_corrected_err_count_r(), offset),
			0);
	}
	if ((l1_data_uncorrected_err_count_delta > 0U) || is_l1_data_ecc_uncorrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Uncorrected error (DBE) detected in SM L1 data! err_mask [%08x] is_overf [%d]",
			l1_data_ecc_uncorrected_err_status, is_l1_data_ecc_uncorrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_l1_data_ecc_uncorrected_total_err_overflow) {
			l1_data_uncorrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_l1_data_ecc_uncorrected_err_count_total_s());
		}
		g->ecc.gr.sm_l1_data_ecc_uncorrected_err_count[gpc][tpc].counter +=
							l1_data_uncorrected_err_count_delta;
		(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
				(gpc << 8) | tpc,
				GPU_SM_L1_DATA_ECC_UNCORRECTED,
				0, g->ecc.gr.sm_l1_data_ecc_uncorrected_err_count[gpc][tpc].counter);
		nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_pri_gpc0_tpc0_sm_l1_data_ecc_uncorrected_err_count_r(), offset),
			0);
	}
	nvgpu_writel(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_l1_data_ecc_status_r(), offset),
			gr_pri_gpc0_tpc0_sm_l1_data_ecc_status_reset_task_f());
}

static void gv11b_gr_intr_handle_icache_exception(struct gk20a *g, u32 gpc, u32 tpc,
			bool *post_event, struct nvgpu_channel *fault_ch,
			u32 *hww_global_esr)
{
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 tpc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_TPC_IN_GPC_STRIDE);
	u32 offset;
	u32 icache_ecc_status, icache_ecc_corrected_err_status = 0;
	u32 icache_ecc_uncorrected_err_status = 0;
	u32 icache_corrected_err_count_delta = 0;
	u32 icache_uncorrected_err_count_delta = 0;
	bool is_icache_ecc_corrected_total_err_overflow = false;
	bool is_icache_ecc_uncorrected_total_err_overflow = false;

	offset = nvgpu_safe_add_u32(
			nvgpu_safe_mult_u32(gpc_stride, gpc),
			nvgpu_safe_mult_u32(tpc_in_gpc_stride, tpc));

	/* Check for L0 && L1 icache ECC errors. */
	icache_ecc_status = nvgpu_readl(g, nvgpu_safe_add_u32(
		gr_pri_gpc0_tpc0_sm_icache_ecc_status_r(), offset));
	icache_ecc_corrected_err_status = icache_ecc_status &
		(gr_pri_gpc0_tpc0_sm_icache_ecc_status_corrected_err_l0_data_m() |
		 gr_pri_gpc0_tpc0_sm_icache_ecc_status_corrected_err_l0_predecode_m() |
		 gr_pri_gpc0_tpc0_sm_icache_ecc_status_corrected_err_l1_data_m() |
		 gr_pri_gpc0_tpc0_sm_icache_ecc_status_corrected_err_l1_predecode_m());
	icache_ecc_uncorrected_err_status = icache_ecc_status &
		(gr_pri_gpc0_tpc0_sm_icache_ecc_status_uncorrected_err_l0_data_m() |
		 gr_pri_gpc0_tpc0_sm_icache_ecc_status_uncorrected_err_l0_predecode_m() |
		 gr_pri_gpc0_tpc0_sm_icache_ecc_status_uncorrected_err_l1_data_m() |
		 gr_pri_gpc0_tpc0_sm_icache_ecc_status_uncorrected_err_l1_predecode_m());

	if ((icache_ecc_corrected_err_status == 0U) && (icache_ecc_uncorrected_err_status == 0U)) {
		return;
	}

	icache_corrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_icache_ecc_corrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_icache_ecc_corrected_err_count_r(),
				offset)));
	icache_uncorrected_err_count_delta =
		gr_pri_gpc0_tpc0_sm_icache_ecc_uncorrected_err_count_total_v(
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_icache_ecc_uncorrected_err_count_r(),
				offset)));
	is_icache_ecc_corrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_icache_ecc_status_corrected_err_total_counter_overflow_v(icache_ecc_status) != 0U;
	is_icache_ecc_uncorrected_total_err_overflow =
		gr_pri_gpc0_tpc0_sm_icache_ecc_status_uncorrected_err_total_counter_overflow_v(icache_ecc_status) != 0U;

	if ((icache_corrected_err_count_delta > 0U) || is_icache_ecc_corrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"corrected error (SBE) detected in SM L0 && L1 icache! err_mask [%08x] is_overf [%d]",
			icache_ecc_corrected_err_status, is_icache_ecc_corrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_icache_ecc_corrected_total_err_overflow) {
			icache_corrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_icache_ecc_corrected_err_count_total_s());
		}
		g->ecc.gr.sm_icache_ecc_corrected_err_count[gpc][tpc].counter +=
							icache_corrected_err_count_delta;
		nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_pri_gpc0_tpc0_sm_icache_ecc_corrected_err_count_r(), offset),
			0);
		if ((icache_ecc_status &
			   gr_pri_gpc0_tpc0_sm_icache_ecc_status_corrected_err_l0_data_m()) != 0U) {
			(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_ICACHE_L0_DATA_ECC_CORRECTED,
					0, g->ecc.gr.sm_icache_ecc_corrected_err_count[gpc][tpc].counter);
		}
		if ((icache_ecc_status &
		      gr_pri_gpc0_tpc0_sm_icache_ecc_status_corrected_err_l0_predecode_m()) != 0U) {
			(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_ICACHE_L0_PREDECODE_ECC_CORRECTED,
					0, g->ecc.gr.sm_icache_ecc_corrected_err_count[gpc][tpc].counter);
		}
		if ((icache_ecc_status  &
			   gr_pri_gpc0_tpc0_sm_icache_ecc_status_corrected_err_l1_data_m()) != 0U) {
			(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_ICACHE_L1_DATA_ECC_CORRECTED,
					0, g->ecc.gr.sm_icache_ecc_corrected_err_count[gpc][tpc].counter);
		}
		if ((icache_ecc_status &
		      gr_pri_gpc0_tpc0_sm_icache_ecc_status_corrected_err_l1_predecode_m()) != 0U) {
			(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_ICACHE_L1_PREDECODE_ECC_CORRECTED,
					0, g->ecc.gr.sm_icache_ecc_corrected_err_count[gpc][tpc].counter);
		}
	}
	if ((icache_uncorrected_err_count_delta > 0U) || is_icache_ecc_uncorrected_total_err_overflow) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Uncorrected error (DBE) detected in SM L0 && L1 icache! err_mask [%08x] is_overf [%d]",
			icache_ecc_uncorrected_err_status, is_icache_ecc_uncorrected_total_err_overflow);

		/* HW uses 16-bits counter */
		if (is_icache_ecc_uncorrected_total_err_overflow) {
			icache_uncorrected_err_count_delta +=
				BIT32(gr_pri_gpc0_tpc0_sm_icache_ecc_uncorrected_err_count_total_s());
		}
		g->ecc.gr.sm_icache_ecc_uncorrected_err_count[gpc][tpc].counter +=
							icache_uncorrected_err_count_delta;
		nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_pri_gpc0_tpc0_sm_icache_ecc_uncorrected_err_count_r(), offset),
			0);
		if ((icache_ecc_status &
			  gr_pri_gpc0_tpc0_sm_icache_ecc_status_uncorrected_err_l0_data_m()) != 0U) {
			(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_ICACHE_L0_DATA_ECC_UNCORRECTED,
					0, g->ecc.gr.sm_icache_ecc_uncorrected_err_count[gpc][tpc].counter);
		}
		if ((icache_ecc_status &
		     gr_pri_gpc0_tpc0_sm_icache_ecc_status_uncorrected_err_l0_predecode_m()) != 0U) {
			(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_ICACHE_L0_PREDECODE_ECC_UNCORRECTED,
					0, g->ecc.gr.sm_icache_ecc_uncorrected_err_count[gpc][tpc].counter);
		}
		if ((icache_ecc_status  &
			  gr_pri_gpc0_tpc0_sm_icache_ecc_status_uncorrected_err_l1_data_m()) != 0U) {
			(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_ICACHE_L1_DATA_ECC_UNCORRECTED,
					0, g->ecc.gr.sm_icache_ecc_uncorrected_err_count[gpc][tpc].counter);
		}
		if ((icache_ecc_status &
		     gr_pri_gpc0_tpc0_sm_icache_ecc_status_uncorrected_err_l1_predecode_m()) != 0U) {
			(void) nvgpu_report_ecc_err(g, NVGPU_ERR_MODULE_SM,
					(gpc << 8) | tpc,
					GPU_SM_ICACHE_L1_PREDECODE_ECC_UNCORRECTED,
					0, g->ecc.gr.sm_icache_ecc_uncorrected_err_count[gpc][tpc].counter);
		}
	}

	nvgpu_writel(g, nvgpu_safe_add_u32(
				gr_pri_gpc0_tpc0_sm_icache_ecc_status_r(), offset),
			gr_pri_gpc0_tpc0_sm_icache_ecc_status_reset_task_f());
}

void gv11b_gr_intr_handle_tpc_sm_ecc_exception(struct gk20a *g,
		u32 gpc, u32 tpc,
		bool *post_event, struct nvgpu_channel *fault_ch,
		u32 *hww_global_esr)
{
	/* Check for L1 tag ECC errors. */
	gv11b_gr_intr_handle_l1_tag_exception(g, gpc, tpc, post_event, fault_ch, hww_global_esr);

	/* Check for LRF ECC errors. */
	gv11b_gr_intr_handle_lrf_exception(g, gpc, tpc, post_event, fault_ch, hww_global_esr);

	/* Check for CBU ECC errors. */
	gv11b_gr_intr_handle_cbu_exception(g, gpc, tpc, post_event, fault_ch, hww_global_esr);

	/* Check for L1 data ECC errors. */
	gv11b_gr_intr_handle_l1_data_exception(g, gpc, tpc, post_event, fault_ch, hww_global_esr);

	/* Check for L0 && L1 icache ECC errors. */
	gv11b_gr_intr_handle_icache_exception(g, gpc, tpc, post_event, fault_ch, hww_global_esr);
}

void gv11b_gr_intr_get_esr_sm_sel(struct gk20a *g, u32 gpc, u32 tpc,
				u32 *esr_sm_sel)
{
	u32 reg_val;
	u32 offset;

	offset = nvgpu_safe_add_u32(nvgpu_gr_gpc_offset(g, gpc),
				    nvgpu_gr_tpc_offset(g, tpc));

	reg_val = nvgpu_readl(g, nvgpu_safe_add_u32(
			gr_gpc0_tpc0_sm_tpc_esr_sm_sel_r(), offset));
	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
			"sm tpc esr sm sel reg val: 0x%x", reg_val);
	*esr_sm_sel = 0;
	if (gr_gpc0_tpc0_sm_tpc_esr_sm_sel_sm0_error_v(reg_val) != 0U) {
		*esr_sm_sel = 1;
	}
	if (gr_gpc0_tpc0_sm_tpc_esr_sm_sel_sm1_error_v(reg_val) != 0U) {
		*esr_sm_sel |= BIT32(1);
	}
	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
			"esr_sm_sel bitmask: 0x%x", *esr_sm_sel);
}

void gv11b_gr_intr_clear_sm_hww(struct gk20a *g, u32 gpc, u32 tpc, u32 sm,
				u32 global_esr)
{
	u32 offset;

	offset = nvgpu_safe_add_u32(nvgpu_gr_gpc_offset(g, gpc),
			nvgpu_safe_add_u32(nvgpu_gr_tpc_offset(g, tpc),
					   nvgpu_gr_sm_offset(g, sm)));

	nvgpu_writel(g, nvgpu_safe_add_u32(
				gr_gpc0_tpc0_sm0_hww_global_esr_r(), offset),
			global_esr);
	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
			"Cleared HWW global esr, current reg val: 0x%x",
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_gpc0_tpc0_sm0_hww_global_esr_r(), offset)));

	nvgpu_writel(g, nvgpu_safe_add_u32(
			gr_gpc0_tpc0_sm0_hww_warp_esr_r(), offset), 0);
	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
			"Cleared HWW warp esr, current reg val: 0x%x",
			nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_gpc0_tpc0_sm0_hww_warp_esr_r(), offset)));
}

int gv11b_gr_intr_handle_ssync_hww(struct gk20a *g, u32 *ssync_esr)
{
	u32 ssync = nvgpu_readl(g, gr_ssync_hww_esr_r());

	if (ssync_esr != NULL) {
		*ssync_esr = ssync;
	}
	nvgpu_err(g, "ssync exception: esr 0x%08x", ssync);
	nvgpu_writel(g, gr_ssync_hww_esr_r(),
			 gr_ssync_hww_esr_reset_active_f());
	return -EFAULT;
}

static void gv11b_gr_intr_read_sm_error_state(struct gk20a *g,
			u32 offset,
			struct nvgpu_tsg_sm_error_state *sm_error_states)
{
	sm_error_states->hww_global_esr = nvgpu_readl(g, nvgpu_safe_add_u32(
		gr_gpc0_tpc0_sm0_hww_global_esr_r(), offset));

	sm_error_states->hww_warp_esr = nvgpu_readl(g, nvgpu_safe_add_u32(
		gr_gpc0_tpc0_sm0_hww_warp_esr_r(), offset));

	sm_error_states->hww_warp_esr_pc = hi32_lo32_to_u64(
		nvgpu_readl(g, nvgpu_safe_add_u32(
		gr_gpc0_tpc0_sm0_hww_warp_esr_pc_hi_r(), offset)),
		nvgpu_readl(g, nvgpu_safe_add_u32(
		gr_gpc0_tpc0_sm0_hww_warp_esr_pc_r(), offset)));

	sm_error_states->hww_global_esr_report_mask = nvgpu_readl(g,
		nvgpu_safe_add_u32(
			gr_gpc0_tpc0_sm0_hww_global_esr_report_mask_r(),
			offset));

	sm_error_states->hww_warp_esr_report_mask = nvgpu_readl(g,
		nvgpu_safe_add_u32(
			gr_gpc0_tpc0_sm0_hww_warp_esr_report_mask_r(),
			offset));
}

u32 gv11b_gr_intr_record_sm_error_state(struct gk20a *g, u32 gpc, u32 tpc, u32 sm,
				struct nvgpu_channel *fault_ch)
{
	u32 sm_id;
	u32 offset, sm_per_tpc, tpc_id;
	u32 gpc_offset, gpc_tpc_offset;
	struct nvgpu_tsg_sm_error_state *sm_error_states = NULL;
	struct nvgpu_tsg *tsg = NULL;

	nvgpu_mutex_acquire(&g->dbg_sessions_lock);

	sm_per_tpc = nvgpu_get_litter_value(g, GPU_LIT_NUM_SM_PER_TPC);
	gpc_offset = nvgpu_gr_gpc_offset(g, gpc);
	gpc_tpc_offset = nvgpu_safe_add_u32(gpc_offset,
				nvgpu_gr_tpc_offset(g, tpc));

	tpc_id = nvgpu_readl(g, nvgpu_safe_add_u32(
			gr_gpc0_gpm_pd_sm_id_r(tpc), gpc_offset));
	sm_id = nvgpu_safe_add_u32(
			nvgpu_safe_mult_u32(tpc_id, sm_per_tpc),
			sm);

	offset = nvgpu_safe_add_u32(gpc_tpc_offset,
			nvgpu_gr_sm_offset(g, sm));

	if (fault_ch != NULL) {
		tsg = nvgpu_tsg_from_ch(fault_ch);
	}

	if (tsg == NULL) {
		nvgpu_err(g, "no valid tsg");
		goto record_fail;
	}

	sm_error_states = tsg->sm_error_states + sm_id;
	gv11b_gr_intr_read_sm_error_state(g, offset, sm_error_states);

record_fail:
	nvgpu_mutex_release(&g->dbg_sessions_lock);

	return sm_id;
}

u32 gv11b_gr_intr_get_sm_hww_warp_esr(struct gk20a *g,
			u32 gpc, u32 tpc, u32 sm)
{
	u32 offset = nvgpu_safe_add_u32(nvgpu_gr_gpc_offset(g, gpc),
			nvgpu_safe_add_u32(nvgpu_gr_tpc_offset(g, tpc),
					   nvgpu_gr_sm_offset(g, sm)));

	u32 hww_warp_esr = nvgpu_readl(g, nvgpu_safe_add_u32(
				gr_gpc0_tpc0_sm0_hww_warp_esr_r(), offset));
	return hww_warp_esr;
}

u32 gv11b_gr_intr_get_sm_hww_global_esr(struct gk20a *g,
			u32 gpc, u32 tpc, u32 sm)
{
	u32 offset = nvgpu_safe_add_u32(nvgpu_gr_gpc_offset(g, gpc),
			nvgpu_safe_add_u32(nvgpu_gr_tpc_offset(g, tpc),
					   nvgpu_gr_sm_offset(g, sm)));

	u32 hww_global_esr = nvgpu_readl(g, nvgpu_safe_add_u32(
				 gr_gpc0_tpc0_sm0_hww_global_esr_r(), offset));

	return hww_global_esr;
}

u32 gv11b_gr_intr_get_sm_no_lock_down_hww_global_esr_mask(struct gk20a *g)
{
	/*
	 * These three interrupts don't require locking down the SM. They can
	 * be handled by usermode clients as they aren't fatal. Additionally,
	 * usermode clients may wish to allow some warps to execute while others
	 * are at breakpoints, as opposed to fatal errors where all warps should
	 * halt.
	 */
	u32 global_esr_mask =
		gr_gpc0_tpc0_sm0_hww_global_esr_bpt_int_pending_f()   |
		gr_gpc0_tpc0_sm0_hww_global_esr_bpt_pause_pending_f() |
		gr_gpc0_tpc0_sm0_hww_global_esr_single_step_complete_pending_f();

	return global_esr_mask;
}

u64 gv11b_gr_intr_get_sm_hww_warp_esr_pc(struct gk20a *g, u32 offset)
{
	u64 hww_warp_esr_pc;

	hww_warp_esr_pc = hi32_lo32_to_u64(
		nvgpu_readl(g, nvgpu_safe_add_u32(
			gr_gpc0_tpc0_sm0_hww_warp_esr_pc_hi_r(), offset)),
		nvgpu_readl(g, nvgpu_safe_add_u32(
			gr_gpc0_tpc0_sm0_hww_warp_esr_pc_r(), offset)));

	return hww_warp_esr_pc;
}

u32 gv11b_gr_intr_ctxsw_checksum_mismatch_mailbox_val(void)
{
	return gr_fecs_ctxsw_mailbox_value_ctxsw_checksum_mismatch_v();
}
