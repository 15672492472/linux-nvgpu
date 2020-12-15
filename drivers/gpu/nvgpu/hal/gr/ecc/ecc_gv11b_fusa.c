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

#include <nvgpu/io.h>
#include <nvgpu/ecc.h>
#include <nvgpu/gk20a.h>

#include <nvgpu/hw/gv11b/hw_gr_gv11b.h>

#include "ecc_gv11b.h"

static void gv11b_ecc_enable_smlrf(struct gk20a *g,
				u32 fecs_feature_override_ecc, bool opt_ecc_en)
{
	if (gr_fecs_feature_override_ecc_sm_lrf_override_v(
			fecs_feature_override_ecc) == 1U) {
		if (gr_fecs_feature_override_ecc_sm_lrf_v(
			fecs_feature_override_ecc) == 1U) {
			nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_LRF, true);
		}
	} else {
		if (opt_ecc_en) {
			nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_LRF, true);
		}
	}
}

static void gv11b_ecc_enable_sml1data(struct gk20a *g,
				u32 fecs_feature_override_ecc, bool opt_ecc_en)
{
	if (gr_fecs_feature_override_ecc_sm_l1_data_override_v(
			fecs_feature_override_ecc) == 1U) {
		if (gr_fecs_feature_override_ecc_sm_l1_data_v(
			fecs_feature_override_ecc) == 1U) {
		  nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_L1_DATA, true);
		}
	} else {
		if (opt_ecc_en) {
		  nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_L1_DATA, true);
		}
	}
}

static void gv11b_ecc_enable_sml1tag(struct gk20a *g,
				u32 fecs_feature_override_ecc, bool opt_ecc_en)
{
	if (gr_fecs_feature_override_ecc_sm_l1_tag_override_v(
			fecs_feature_override_ecc) == 1U) {
		if (gr_fecs_feature_override_ecc_sm_l1_tag_v(
			fecs_feature_override_ecc) == 1U) {
		  nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_L1_TAG, true);
		}
	} else {
		if (opt_ecc_en) {
		  nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_L1_TAG, true);
		}
	}
}

static void gv11b_ecc_enable_smicache(struct gk20a *g,
				u32 fecs_feature_override_ecc, bool opt_ecc_en)
{
	if ((gr_fecs_feature_override_ecc_1_sm_l0_icache_override_v(
			fecs_feature_override_ecc) == 1U) &&
		(gr_fecs_feature_override_ecc_1_sm_l1_icache_override_v(
			fecs_feature_override_ecc) == 1U)) {
		if ((gr_fecs_feature_override_ecc_1_sm_l0_icache_v(
				fecs_feature_override_ecc) == 1U) &&
			(gr_fecs_feature_override_ecc_1_sm_l1_icache_v(
				fecs_feature_override_ecc) == 1U)) {
		  nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_ICACHE, true);
		}
	} else {
		if (opt_ecc_en) {
		  nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_ICACHE, true);
		}
	}
}

static void gv11b_ecc_enable_ltc(struct gk20a *g,
				u32 fecs_feature_override_ecc, bool opt_ecc_en)
{
	if (gr_fecs_feature_override_ecc_ltc_override_v(
			fecs_feature_override_ecc) == 1U) {
		if (gr_fecs_feature_override_ecc_ltc_v(
			fecs_feature_override_ecc) == 1U) {
			nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_LTC, true);
		}
	} else {
		if (opt_ecc_en) {
			nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_LTC, true);
		}
	}
}

static void gv11b_ecc_enable_smcbu(struct gk20a *g,
				u32 fecs_feature_override_ecc, bool opt_ecc_en)
{
	if (gr_fecs_feature_override_ecc_sm_cbu_override_v(
			fecs_feature_override_ecc) == 1U) {
		if (gr_fecs_feature_override_ecc_sm_cbu_v(
			fecs_feature_override_ecc) == 1U) {
			nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_CBU, true);
		}
	} else {
		if (opt_ecc_en) {
			nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_CBU, true);
		}
	}
}

void gv11b_ecc_detect_enabled_units(struct gk20a *g)
{
	bool opt_ecc_en = g->ops.fuse.is_opt_ecc_enable(g);
	bool opt_feature_fuses_override_disable =
		g->ops.fuse.is_opt_feature_override_disable(g);
	u32 fecs_feature_override_ecc =
			nvgpu_readl(g,
				gr_fecs_feature_override_ecc_r());

	if (opt_feature_fuses_override_disable) {
		if (opt_ecc_en) {
			nvgpu_set_enabled(g,
					NVGPU_ECC_ENABLED_SM_LRF, true);
			nvgpu_set_enabled(g,
					NVGPU_ECC_ENABLED_SM_L1_DATA, true);
			nvgpu_set_enabled(g,
					NVGPU_ECC_ENABLED_SM_L1_TAG, true);
			nvgpu_set_enabled(g,
					NVGPU_ECC_ENABLED_SM_ICACHE, true);
			nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_LTC, true);
			nvgpu_set_enabled(g, NVGPU_ECC_ENABLED_SM_CBU, true);
		}
	} else {
		/* SM LRF */
		gv11b_ecc_enable_smlrf(g,
				fecs_feature_override_ecc, opt_ecc_en);
		/* SM L1 DATA*/
		gv11b_ecc_enable_sml1data(g,
				fecs_feature_override_ecc, opt_ecc_en);
		/* SM L1 TAG*/
		gv11b_ecc_enable_sml1tag(g,
				fecs_feature_override_ecc, opt_ecc_en);
		/* SM ICACHE*/
		gv11b_ecc_enable_smicache(g,
				fecs_feature_override_ecc, opt_ecc_en);
		/* LTC */
		gv11b_ecc_enable_ltc(g,
				fecs_feature_override_ecc, opt_ecc_en);
		/* SM CBU */
		gv11b_ecc_enable_smcbu(g,
				fecs_feature_override_ecc, opt_ecc_en);
	}
}

static int gv11b_ecc_init_sm_corrected_err_count(struct gk20a *g)
{
	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_l1_tag_ecc_corrected_err_count);
	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_cbu_ecc_corrected_err_count);
	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_l1_data_ecc_corrected_err_count);
	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_icache_ecc_corrected_err_count);

	return 0;
}

static int gv11b_ecc_init_sm_uncorrected_err_count(struct gk20a *g)
{
	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_l1_tag_ecc_uncorrected_err_count);
	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_cbu_ecc_uncorrected_err_count);
	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_l1_data_ecc_uncorrected_err_count);
	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_icache_ecc_uncorrected_err_count);

	return 0;
}

static int gv11b_ecc_init_tpc(struct gk20a *g)
{
	int err;

	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_lrf_ecc_single_err_count);
	NVGPU_ECC_COUNTER_INIT_PER_TPC(sm_lrf_ecc_double_err_count);

	err = gv11b_ecc_init_sm_corrected_err_count(g);
	if (err != 0) {
		return err;
	}

	err = gv11b_ecc_init_sm_uncorrected_err_count(g);
	if (err != 0) {
		return err;
	}

	return 0;
}

static int gv11b_ecc_init_gpc(struct gk20a *g)
{
	int err = 0;

	err = NVGPU_ECC_COUNTER_INIT_PER_GPC(
			gcc_l15_ecc_corrected_err_count);
	if (err != 0) {
		goto init_gpc_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_PER_GPC(
			gcc_l15_ecc_uncorrected_err_count);
	if (err != 0) {
		goto init_gpc_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_PER_GPC(
			gpccs_ecc_uncorrected_err_count);
	if (err != 0) {
		goto init_gpc_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_PER_GPC(
			gpccs_ecc_corrected_err_count);
	if (err != 0) {
		goto init_gpc_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_PER_GPC(
			mmu_l1tlb_ecc_uncorrected_err_count);
	if (err != 0) {
		goto init_gpc_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_PER_GPC(
			mmu_l1tlb_ecc_corrected_err_count);

init_gpc_done:
	return err;
}

static int gv11b_ecc_init_fb(struct gk20a *g)
{
	int err = 0;

	err = NVGPU_ECC_COUNTER_INIT_FB(mmu_l2tlb_ecc_uncorrected_err_count);
	if (err != 0) {
		goto init_fb_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_FB(mmu_l2tlb_ecc_corrected_err_count);
	if (err != 0) {
		goto init_fb_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_FB(mmu_hubtlb_ecc_uncorrected_err_count);
	if (err != 0) {
		goto init_fb_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_FB(mmu_hubtlb_ecc_corrected_err_count);
	if (err != 0) {
		goto init_fb_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_FB(
			mmu_fillunit_ecc_uncorrected_err_count);
	if (err != 0) {
		goto init_fb_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_FB(
			mmu_fillunit_ecc_corrected_err_count);

init_fb_done:
	return err;
}

static int gv11b_ecc_init_other_units(struct gk20a *g)
{
	int err = 0;

	err = NVGPU_ECC_COUNTER_INIT_PER_LTS(ecc_sec_count);
	if (err != 0) {
		goto init_other_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_PER_LTS(ecc_ded_count);
	if (err != 0) {
		goto init_other_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_GR(fecs_ecc_uncorrected_err_count);
	if (err != 0) {
		goto init_other_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_GR(fecs_ecc_corrected_err_count);
	if (err != 0) {
		goto init_other_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_PMU(pmu_ecc_uncorrected_err_count);
	if (err != 0) {
		goto init_other_done;
	}
	err = NVGPU_ECC_COUNTER_INIT_PMU(pmu_ecc_corrected_err_count);

init_other_done:
	return err;
}

int gv11b_ecc_init(struct gk20a *g)
{
	int err;

	err = gv11b_ecc_init_tpc(g);
	if (err != 0) {
		goto done;
	}

	err = gv11b_ecc_init_gpc(g);
	if (err != 0) {
		goto done;
	}

	err = gv11b_ecc_init_fb(g);
	if (err != 0) {
		goto done;
	}

	err = gv11b_ecc_init_other_units(g);

done:
	if (err != 0) {
		nvgpu_err(g, "ecc counter allocate failed, err=%d", err);
		nvgpu_ecc_free(g);
	}

	return err;
}
