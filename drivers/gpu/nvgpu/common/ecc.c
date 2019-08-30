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

#include <nvgpu/gk20a.h>
#include <nvgpu/gr/config.h>
#include <nvgpu/gr/gr.h>
#include <nvgpu/gr/gr_utils.h>
#include <nvgpu/ltc.h>
#include <nvgpu/nvgpu_err.h>
#include <nvgpu/safe_ops.h>

static void nvgpu_ecc_stat_add(struct gk20a *g, struct nvgpu_ecc_stat *stat)
{
	struct nvgpu_ecc *ecc = &g->ecc;

	nvgpu_init_list_node(&stat->node);

	nvgpu_list_add_tail(&stat->node, &ecc->stats_list);
	ecc->stats_count = nvgpu_safe_add_s32(ecc->stats_count, 1);
}

static void nvgpu_ecc_init(struct gk20a *g)
{
	struct nvgpu_ecc *ecc = &g->ecc;

	nvgpu_init_list_node(&ecc->stats_list);
}

int nvgpu_ecc_counter_init_per_tpc(struct gk20a *g,
		struct nvgpu_ecc_stat ***stat, const char *name)
{
	struct nvgpu_ecc_stat **stats;
	struct nvgpu_gr_config *gr_config = nvgpu_gr_get_config_ptr(g);
	u32 gpc_count = nvgpu_gr_config_get_gpc_count(gr_config);
	u32 gpc, tpc;
	int err = 0;

	stats = nvgpu_kzalloc(g, nvgpu_safe_mult_u64(sizeof(*stats),
			      gpc_count));
	if (stats == NULL) {
		return -ENOMEM;
	}
	for (gpc = 0; gpc < gpc_count; gpc++) {
		stats[gpc] = nvgpu_kzalloc(g,
			nvgpu_safe_mult_u64(sizeof(*stats[gpc]),
				nvgpu_gr_config_get_gpc_tpc_count(gr_config,
								  gpc)));
		if (stats[gpc] == NULL) {
			err = -ENOMEM;
			break;
		}
	}

	if (err != 0) {
		while (gpc-- != 0u) {
			nvgpu_kfree(g, stats[gpc]);
		}

		nvgpu_kfree(g, stats);
		return err;
	}

	for (gpc = 0; gpc < gpc_count; gpc++) {
		for (tpc = 0;
		     tpc < nvgpu_gr_config_get_gpc_tpc_count(gr_config, gpc);
		     tpc++) {
#ifdef CONFIG_NVGPU_LOGGING
			(void) snprintf(stats[gpc][tpc].name,
					NVGPU_ECC_STAT_NAME_MAX_SIZE,
					"gpc%u_tpc%u_%s", gpc, tpc, name);
#endif
			nvgpu_ecc_stat_add(g, &stats[gpc][tpc]);
		}
	}

	*stat = stats;
	return 0;
}

int nvgpu_ecc_counter_init_per_gpc(struct gk20a *g,
		struct nvgpu_ecc_stat **stat, const char *name)
{
	struct nvgpu_ecc_stat *stats;
	struct nvgpu_gr_config *gr_config = nvgpu_gr_get_config_ptr(g);
	u32 gpc_count = nvgpu_gr_config_get_gpc_count(gr_config);
	u32 gpc;

	stats = nvgpu_kzalloc(g, nvgpu_safe_mult_u64(sizeof(*stats),
						     gpc_count));
	if (stats == NULL) {
		return -ENOMEM;
	}
	for (gpc = 0; gpc < gpc_count; gpc++) {
#ifdef CONFIG_NVGPU_LOGGING
		(void) snprintf(stats[gpc].name, NVGPU_ECC_STAT_NAME_MAX_SIZE,
				"gpc%u_%s", gpc, name);
#endif
		nvgpu_ecc_stat_add(g, &stats[gpc]);
	}

	*stat = stats;
	return 0;
}

int nvgpu_ecc_counter_init(struct gk20a *g,
		struct nvgpu_ecc_stat **stat, const char *name)
{
	struct nvgpu_ecc_stat *stats;

	stats = nvgpu_kzalloc(g, sizeof(*stats));
	if (stats == NULL) {
		return -ENOMEM;
	}

	(void)strncpy(stats->name, name, NVGPU_ECC_STAT_NAME_MAX_SIZE - 1);
	nvgpu_ecc_stat_add(g, stats);
	*stat = stats;
	return 0;
}

int nvgpu_ecc_counter_init_per_lts(struct gk20a *g,
		struct nvgpu_ecc_stat ***stat, const char *name)
{
	struct nvgpu_ecc_stat **stats;
	u32 ltc, lts;
	int err = 0;
	u32 ltc_count = nvgpu_ltc_get_ltc_count(g);
	u32 slices_per_ltc = nvgpu_ltc_get_slices_per_ltc(g);

	stats = nvgpu_kzalloc(g, nvgpu_safe_mult_u64(sizeof(*stats),
						     ltc_count));
	if (stats == NULL) {
		return -ENOMEM;
	}
	for (ltc = 0; ltc < ltc_count; ltc++) {
		stats[ltc] = nvgpu_kzalloc(g,
			nvgpu_safe_mult_u64(sizeof(*stats[ltc]),
					    slices_per_ltc));
		if (stats[ltc] == NULL) {
			err = -ENOMEM;
			break;
		}
	}

	if (err != 0) {
		while (ltc-- > 0u) {
			nvgpu_kfree(g, stats[ltc]);
		}

		nvgpu_kfree(g, stats);
		return err;
	}

	for (ltc = 0; ltc < ltc_count; ltc++) {
		for (lts = 0; lts < slices_per_ltc; lts++) {
#ifdef CONFIG_NVGPU_LOGGING
			(void) snprintf(stats[ltc][lts].name,
					NVGPU_ECC_STAT_NAME_MAX_SIZE,
					"ltc%u_lts%u_%s", ltc, lts, name);
#endif
			nvgpu_ecc_stat_add(g, &stats[ltc][lts]);
		}
	}

	*stat = stats;
	return 0;
}

int nvgpu_ecc_counter_init_per_fbpa(struct gk20a *g,
		struct nvgpu_ecc_stat **stat, const char *name)
{
	u32 i;
	u32 num_fbpa = nvgpu_get_litter_value(g, GPU_LIT_NUM_FBPAS);
	struct nvgpu_ecc_stat *stats;

	stats = nvgpu_kzalloc(g, nvgpu_safe_mult_u64(sizeof(*stats),
						     (size_t)num_fbpa));
	if (stats == NULL) {
		return -ENOMEM;
	}

	for (i = 0; i < num_fbpa; i++) {
#ifdef CONFIG_NVGPU_LOGGING
		(void) snprintf(stats[i].name, NVGPU_ECC_STAT_NAME_MAX_SIZE,
				"fbpa%u_%s", i, name);
#endif
		nvgpu_ecc_stat_add(g, &stats[i]);
	}

	*stat = stats;
	return 0;
}

/* release all ecc_stat */
void nvgpu_ecc_free(struct gk20a *g)
{
	struct nvgpu_ecc *ecc = &g->ecc;
	struct nvgpu_gr_config *gr_config = nvgpu_gr_get_config_ptr(g);
	u32 gpc_count;
	u32 i;

	if (gr_config == NULL) {
		return;
	}

	gpc_count = nvgpu_gr_config_get_gpc_count(gr_config);

	for (i = 0; i < gpc_count; i++) {
		if (ecc->gr.sm_lrf_ecc_single_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_lrf_ecc_single_err_count[i]);
		}

		if (ecc->gr.sm_lrf_ecc_double_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_lrf_ecc_double_err_count[i]);
		}

		if (ecc->gr.sm_shm_ecc_sec_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_shm_ecc_sec_count[i]);
		}

		if (ecc->gr.sm_shm_ecc_sed_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_shm_ecc_sed_count[i]);
		}

		if (ecc->gr.sm_shm_ecc_ded_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_shm_ecc_ded_count[i]);
		}

		if (ecc->gr.tex_ecc_total_sec_pipe0_count != NULL) {
			nvgpu_kfree(g, ecc->gr.tex_ecc_total_sec_pipe0_count[i]);
		}

		if (ecc->gr.tex_ecc_total_ded_pipe0_count != NULL) {
			nvgpu_kfree(g, ecc->gr.tex_ecc_total_ded_pipe0_count[i]);
		}

		if (ecc->gr.tex_unique_ecc_sec_pipe0_count != NULL) {
			nvgpu_kfree(g, ecc->gr.tex_unique_ecc_sec_pipe0_count[i]);
		}

		if (ecc->gr.tex_unique_ecc_ded_pipe0_count != NULL) {
			nvgpu_kfree(g, ecc->gr.tex_unique_ecc_ded_pipe0_count[i]);
		}

		if (ecc->gr.tex_ecc_total_sec_pipe1_count != NULL) {
			nvgpu_kfree(g, ecc->gr.tex_ecc_total_sec_pipe1_count[i]);
		}

		if (ecc->gr.tex_ecc_total_ded_pipe1_count != NULL) {
			nvgpu_kfree(g, ecc->gr.tex_ecc_total_ded_pipe1_count[i]);
		}

		if (ecc->gr.tex_unique_ecc_sec_pipe1_count != NULL) {
			nvgpu_kfree(g, ecc->gr.tex_unique_ecc_sec_pipe1_count[i]);
		}

		if (ecc->gr.tex_unique_ecc_ded_pipe1_count != NULL) {
			nvgpu_kfree(g, ecc->gr.tex_unique_ecc_ded_pipe1_count[i]);
		}

		if (ecc->gr.sm_l1_tag_ecc_corrected_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_l1_tag_ecc_corrected_err_count[i]);
		}

		if (ecc->gr.sm_l1_tag_ecc_uncorrected_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_l1_tag_ecc_uncorrected_err_count[i]);
		}

		if (ecc->gr.sm_cbu_ecc_corrected_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_cbu_ecc_corrected_err_count[i]);
		}

		if (ecc->gr.sm_cbu_ecc_uncorrected_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_cbu_ecc_uncorrected_err_count[i]);
		}

		if (ecc->gr.sm_l1_data_ecc_corrected_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_l1_data_ecc_corrected_err_count[i]);
		}

		if (ecc->gr.sm_l1_data_ecc_uncorrected_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_l1_data_ecc_uncorrected_err_count[i]);
		}

		if (ecc->gr.sm_icache_ecc_corrected_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_icache_ecc_corrected_err_count[i]);
		}

		if (ecc->gr.sm_icache_ecc_uncorrected_err_count != NULL) {
			nvgpu_kfree(g, ecc->gr.sm_icache_ecc_uncorrected_err_count[i]);
		}
	}
	nvgpu_kfree(g, ecc->gr.sm_lrf_ecc_single_err_count);
	nvgpu_kfree(g, ecc->gr.sm_lrf_ecc_double_err_count);
	nvgpu_kfree(g, ecc->gr.sm_shm_ecc_sec_count);
	nvgpu_kfree(g, ecc->gr.sm_shm_ecc_sed_count);
	nvgpu_kfree(g, ecc->gr.sm_shm_ecc_ded_count);
	nvgpu_kfree(g, ecc->gr.tex_ecc_total_sec_pipe0_count);
	nvgpu_kfree(g, ecc->gr.tex_ecc_total_ded_pipe0_count);
	nvgpu_kfree(g, ecc->gr.tex_unique_ecc_sec_pipe0_count);
	nvgpu_kfree(g, ecc->gr.tex_unique_ecc_ded_pipe0_count);
	nvgpu_kfree(g, ecc->gr.tex_ecc_total_sec_pipe1_count);
	nvgpu_kfree(g, ecc->gr.tex_ecc_total_ded_pipe1_count);
	nvgpu_kfree(g, ecc->gr.tex_unique_ecc_sec_pipe1_count);
	nvgpu_kfree(g, ecc->gr.tex_unique_ecc_ded_pipe1_count);
	nvgpu_kfree(g, ecc->gr.sm_l1_tag_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->gr.sm_l1_tag_ecc_uncorrected_err_count);
	nvgpu_kfree(g, ecc->gr.sm_cbu_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->gr.sm_cbu_ecc_uncorrected_err_count);
	nvgpu_kfree(g, ecc->gr.sm_l1_data_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->gr.sm_l1_data_ecc_uncorrected_err_count);
	nvgpu_kfree(g, ecc->gr.sm_icache_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->gr.sm_icache_ecc_uncorrected_err_count);

	nvgpu_kfree(g, ecc->gr.gcc_l15_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->gr.gcc_l15_ecc_uncorrected_err_count);
	nvgpu_kfree(g, ecc->gr.gpccs_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->gr.gpccs_ecc_uncorrected_err_count);
	nvgpu_kfree(g, ecc->gr.mmu_l1tlb_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->gr.mmu_l1tlb_ecc_uncorrected_err_count);
	nvgpu_kfree(g, ecc->gr.fecs_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->gr.fecs_ecc_uncorrected_err_count);

	for (i = 0; i < nvgpu_ltc_get_ltc_count(g); i++) {
		if (ecc->ltc.ecc_sec_count != NULL) {
			nvgpu_kfree(g, ecc->ltc.ecc_sec_count[i]);
		}

		if (ecc->ltc.ecc_ded_count != NULL) {
			nvgpu_kfree(g, ecc->ltc.ecc_ded_count[i]);
		}
	}
	nvgpu_kfree(g, ecc->ltc.ecc_sec_count);
	nvgpu_kfree(g, ecc->ltc.ecc_ded_count);

	nvgpu_kfree(g, ecc->fb.mmu_l2tlb_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->fb.mmu_l2tlb_ecc_uncorrected_err_count);
	nvgpu_kfree(g, ecc->fb.mmu_hubtlb_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->fb.mmu_hubtlb_ecc_uncorrected_err_count);
	nvgpu_kfree(g, ecc->fb.mmu_fillunit_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->fb.mmu_fillunit_ecc_uncorrected_err_count);

	nvgpu_kfree(g, ecc->pmu.pmu_ecc_corrected_err_count);
	nvgpu_kfree(g, ecc->pmu.pmu_ecc_uncorrected_err_count);

	nvgpu_kfree(g, ecc->fbpa.fbpa_ecc_sec_err_count);
	nvgpu_kfree(g, ecc->fbpa.fbpa_ecc_ded_err_count);

	(void)memset(ecc, 0, sizeof(*ecc));

	ecc->initialized = false;
}

int nvgpu_ecc_init_support(struct gk20a *g)
{
	int err;

	if (g->ecc.initialized) {
		return 0;
	}

	if (g->ops.gr.ecc.init == NULL) {
		return 0;
	}

	nvgpu_ecc_init(g);
	err = g->ops.gr.ecc.init(g);
	if (err != 0) {
		return err;
	}

#ifdef CONFIG_NVGPU_SYSFS
	err = nvgpu_ecc_sysfs_init(g);
	if (err != 0) {
		nvgpu_ecc_free(g);
		return err;
	}
#endif

	g->ecc.initialized = true;

	return 0;
}

void nvgpu_ecc_remove_support(struct gk20a *g)
{
	if (g->ops.gr.ecc.init == NULL) {
		return;
	}

#ifdef CONFIG_NVGPU_SYSFS
	nvgpu_ecc_sysfs_remove(g);
#endif
	nvgpu_ecc_free(g);
}
