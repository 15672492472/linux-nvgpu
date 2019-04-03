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
#include <nvgpu/nvgpu_err.h>

#include <nvgpu/gr/config.h>
#include <nvgpu/gr/gr.h>

#include "gr_intr_gv11b.h"

#include <nvgpu/hw/gv11b/hw_gr_gv11b.h>

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
		gr_pri_gpc0_gcc_l15_ecc_status_r() + offset);
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
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_GCC, gpc, tpc,
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
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_GCC, gpc, tpc,
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

	hww_esr = nvgpu_readl(g, gr_gpc0_mmu_gpcmmu_global_esr_r() + offset);

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
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_MMU, gpc, 0,
				GPU_MMU_L1TLB_SA_DATA_ECC_CORRECTED,
				0, (u32)*corrected_err);
		nvgpu_log(g, gpu_dbg_intr, "corrected ecc sa data error");
	}
	if ((ecc_status &
	     gr_gpc0_mmu_l1tlb_ecc_status_uncorrected_err_l1tlb_sa_data_m()) !=
									 0U) {
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_MMU, gpc, 0,
				GPU_MMU_L1TLB_SA_DATA_ECC_UNCORRECTED,
				0, (u32)*uncorrected_err);
		nvgpu_log(g, gpu_dbg_intr, "uncorrected ecc sa data error");
	}
	if ((ecc_status &
	     gr_gpc0_mmu_l1tlb_ecc_status_corrected_err_l1tlb_fa_data_m()) !=
									0U) {
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_MMU, gpc, 0,
				GPU_MMU_L1TLB_FA_DATA_ECC_CORRECTED,
				0, (u32)*corrected_err);
		nvgpu_log(g, gpu_dbg_intr, "corrected ecc fa data error");
	}
	if ((ecc_status &
	     gr_gpc0_mmu_l1tlb_ecc_status_uncorrected_err_l1tlb_fa_data_m()) !=
									0U) {
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_MMU, gpc, 0,
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

	hww_esr = nvgpu_readl(g, gr_gpc0_gpccs_hww_esr_r() + offset);

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
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_GPCCS, gpc, 0,
				GPU_GPCCS_FALCON_IMEM_ECC_CORRECTED,
				ecc_addr, (u32)*corrected_err);
		nvgpu_log(g, gpu_dbg_intr, "imem ecc error corrected");
	}
	if ((ecc_status &
	     gr_gpc0_gpccs_falcon_ecc_status_uncorrected_err_imem_m()) != 0U) {
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_GPCCS, gpc, 0,
				GPU_GPCCS_FALCON_IMEM_ECC_UNCORRECTED,
				ecc_addr, (u32)*uncorrected_err);
		nvgpu_log(g, gpu_dbg_intr, "imem ecc error uncorrected");
	}
	if ((ecc_status &
	     gr_gpc0_gpccs_falcon_ecc_status_corrected_err_dmem_m()) != 0U) {
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_GPCCS, gpc, 0,
				GPU_GPCCS_FALCON_DMEM_ECC_CORRECTED,
				ecc_addr, (u32)*corrected_err);
		nvgpu_log(g, gpu_dbg_intr, "dmem ecc error corrected");
	}
	if ((ecc_status &
	     gr_gpc0_gpccs_falcon_ecc_status_uncorrected_err_dmem_m()) != 0U) {
		nvgpu_gr_report_ecc_error(g, NVGPU_ERR_MODULE_GPCCS, gpc, 0,
				GPU_GPCCS_FALCON_DMEM_ECC_UNCORRECTED,
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
	u32 offset = nvgpu_gr_gpc_offset(g, gpc) + nvgpu_gr_tpc_offset(g, tpc);

	esr = nvgpu_readl(g, gr_gpc0_tpc0_mpc_hww_esr_r() + offset);
	nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg, "mpc hww esr 0x%08x", esr);

	nvgpu_report_gr_exception(g, ((gpc << 8U) | tpc),
			GPU_PGRAPH_MPC_EXCEPTION,
			esr);

	esr = nvgpu_readl(g, gr_gpc0_tpc0_mpc_hww_esr_info_r() + offset);
	nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			"mpc hww esr info: veid 0x%08x",
			gr_gpc0_tpc0_mpc_hww_esr_info_veid_v(esr));

	nvgpu_writel(g, gr_gpc0_tpc0_mpc_hww_esr_r() + offset,
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
	nvgpu_writel(g, gr_exception1_en_r(), (reg_val - 1U));

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
		gr_gpcs_gpccs_gpc_exception_en_tpc_f(tpc_mask_calc - 1U);

	nvgpu_writel(g, gr_gpcs_gpccs_gpc_exception_en_r(),
		(tpc_mask | gr_gpcs_gpccs_gpc_exception_en_gcc_f(1U) |
			    gr_gpcs_gpccs_gpc_exception_en_gpccs_f(1U) |
			    gr_gpcs_gpccs_gpc_exception_en_gpcmmu_f(1U)));
}
