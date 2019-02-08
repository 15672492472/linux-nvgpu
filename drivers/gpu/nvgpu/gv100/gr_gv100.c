/*
 * GV100 GPU GR
 *
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

#include <nvgpu/log.h>
#include <nvgpu/debug.h>
#include <nvgpu/enabled.h>
#include <nvgpu/io.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/gr/ctx.h>
#include <nvgpu/gr/config.h>

#include "gk20a/gr_gk20a.h"
#include "gk20a/gr_pri_gk20a.h"

#include "gv100/gr_gv100.h"
#include "gv11b/subctx_gv11b.h"

#include <nvgpu/hw/gv100/hw_gr_gv100.h>
#include <nvgpu/hw/gv100/hw_proj_gv100.h>
#include <nvgpu/hw/gv100/hw_top_gv100.h>
#include <nvgpu/hw/gv100/hw_perf_gv100.h>


/*
 *  Estimate performance if the given logical TPC in the given logical GPC were
 * removed.
 */
static int gr_gv100_scg_estimate_perf(struct gk20a *g,
					unsigned long *gpc_tpc_mask,
					u32 disable_gpc_id, u32 disable_tpc_id,
					int *perf)
{
	struct gr_gk20a *gr = &g->gr;
	int err = 0;
	u32 scale_factor = 512U; /* Use fx23.9 */
	u32 pix_scale = 1024U*1024U;	/* Pix perf in [29:20] */
	u32 world_scale = 1024U;	/* World performance in [19:10] */
	u32 tpc_scale = 1U;		/* TPC balancing in [9:0] */
	u32 scg_num_pes = 0U;
	u32 min_scg_gpc_pix_perf = scale_factor; /* Init perf as maximum */
	u32 average_tpcs = 0U;		/* Average of # of TPCs per GPC */
	u32 deviation;			/* absolute diff between TPC# and
					 * average_tpcs, averaged across GPCs
					 */
	u32 norm_tpc_deviation;		/* deviation/max_tpc_per_gpc */
	u32 tpc_balance;
	u32 scg_gpc_pix_perf;
	u32 scg_world_perf;
	u32 gpc_id;
	u32 pes_id;
	int diff;
	bool is_tpc_removed_gpc = false;
	bool is_tpc_removed_pes = false;
	u32 max_tpc_gpc = 0U;
	u32 num_tpc_mask;
	u32 *num_tpc_gpc = nvgpu_kzalloc(g, sizeof(u32) *
				nvgpu_get_litter_value(g, GPU_LIT_NUM_GPCS));

	if (num_tpc_gpc == NULL) {
		return -ENOMEM;
	}

	/* Calculate pix-perf-reduction-rate per GPC and find bottleneck TPC */
	for (gpc_id = 0;
	     gpc_id < nvgpu_gr_config_get_gpc_count(gr->config);
	     gpc_id++) {
		num_tpc_mask = gpc_tpc_mask[gpc_id];

		if ((gpc_id == disable_gpc_id) &&
		    ((num_tpc_mask & BIT32(disable_tpc_id)) != 0U)) {
			/* Safety check if a TPC is removed twice */
			if (is_tpc_removed_gpc) {
				err = -EINVAL;
				goto free_resources;
			}
			/* Remove logical TPC from set */
			num_tpc_mask &= ~(BIT32(disable_tpc_id));
			is_tpc_removed_gpc = true;
		}

		/* track balancing of tpcs across gpcs */
		num_tpc_gpc[gpc_id] = hweight32(num_tpc_mask);
		average_tpcs += num_tpc_gpc[gpc_id];

		/* save the maximum numer of gpcs */
		max_tpc_gpc = num_tpc_gpc[gpc_id] > max_tpc_gpc ?
				num_tpc_gpc[gpc_id] : max_tpc_gpc;

		/*
		 * Calculate ratio between TPC count and post-FS and post-SCG
		 *
		 * ratio represents relative throughput of the GPC
		 */
		scg_gpc_pix_perf = scale_factor * num_tpc_gpc[gpc_id] /
				nvgpu_gr_config_get_gpc_tpc_count(gr->config, gpc_id);

		if (min_scg_gpc_pix_perf > scg_gpc_pix_perf) {
			min_scg_gpc_pix_perf = scg_gpc_pix_perf;
		}

		/* Calculate # of surviving PES */
		for (pes_id = 0;
		     pes_id < nvgpu_gr_config_get_gpc_ppc_count(gr->config, gpc_id);
		     pes_id++) {
			/* Count the number of TPC on the set */
			num_tpc_mask = nvgpu_gr_config_get_pes_tpc_mask(
						gr->config, gpc_id, pes_id) &
					gpc_tpc_mask[gpc_id];

			if ((gpc_id == disable_gpc_id) &&
			    ((num_tpc_mask & BIT32(disable_tpc_id)) != 0U)) {

				if (is_tpc_removed_pes) {
					err = -EINVAL;
					goto free_resources;
				}
				num_tpc_mask &= ~(BIT32(disable_tpc_id));
				is_tpc_removed_pes = true;
			}
			if (hweight32(num_tpc_mask) != 0UL) {
				scg_num_pes++;
			}
		}
	}

	if (!is_tpc_removed_gpc || !is_tpc_removed_pes) {
		err = -EINVAL;
		goto free_resources;
	}

	if (max_tpc_gpc == 0U) {
		*perf = 0;
		goto free_resources;
	}

	/* Now calculate perf */
	scg_world_perf = (scale_factor * scg_num_pes) /
		nvgpu_gr_config_get_ppc_count(gr->config);
	deviation = 0;
	average_tpcs = scale_factor * average_tpcs /
			nvgpu_gr_config_get_gpc_count(gr->config);
	for (gpc_id =0;
	     gpc_id < nvgpu_gr_config_get_gpc_count(gr->config);
	     gpc_id++) {
		diff = average_tpcs - scale_factor * num_tpc_gpc[gpc_id];
		if (diff < 0) {
			diff = -diff;
		}
		deviation += U32(diff);
	}

	deviation /= nvgpu_gr_config_get_gpc_count(gr->config);

	norm_tpc_deviation = deviation / max_tpc_gpc;

	tpc_balance = scale_factor - norm_tpc_deviation;

	if ((tpc_balance > scale_factor)          ||
	    (scg_world_perf > scale_factor)       ||
	    (min_scg_gpc_pix_perf > scale_factor) ||
	    (norm_tpc_deviation > scale_factor)) {
		err = -EINVAL;
		goto free_resources;
	}

	*perf = (pix_scale * min_scg_gpc_pix_perf) +
		(world_scale * scg_world_perf) +
		(tpc_scale * tpc_balance);
free_resources:
	nvgpu_kfree(g, num_tpc_gpc);
	return err;
}

void gr_gv100_bundle_cb_defaults(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;

	gr->bundle_cb_default_size =
		gr_scc_bundle_cb_size_div_256b__prod_v();
	gr->min_gpm_fifo_depth =
		gr_pd_ab_dist_cfg2_state_limit_min_gpm_fifo_depths_v();
	gr->bundle_cb_token_limit =
		gr_pd_ab_dist_cfg2_token_limit_init_v();
}

void gr_gv100_cb_size_default(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;

	if (gr->attrib_cb_default_size == 0U) {
		gr->attrib_cb_default_size =
			gr_gpc0_ppc0_cbm_beta_cb_size_v_default_v();
	}
	gr->alpha_cb_default_size =
		gr_gpc0_ppc0_cbm_alpha_cb_size_v_default_v();
}

void gr_gv100_set_gpc_tpc_mask(struct gk20a *g, u32 gpc_index)
{
}

int gr_gv100_init_sm_id_table(struct gk20a *g)
{
	unsigned long tpc;
	u32 gpc, sm, pes, gtpc;
	u32 sm_id = 0;
	u32 sm_per_tpc = nvgpu_get_litter_value(g, GPU_LIT_NUM_SM_PER_TPC);
	struct gr_gk20a *gr = &g->gr;
	u32 num_sm = sm_per_tpc * nvgpu_gr_config_get_tpc_count(gr->config);
	int perf, maxperf;
	int err = 0;
	unsigned long *gpc_tpc_mask;
	u32 *tpc_table, *gpc_table;

	gpc_table = nvgpu_kzalloc(g, nvgpu_gr_config_get_tpc_count(gr->config) *
					sizeof(u32));
	tpc_table = nvgpu_kzalloc(g, nvgpu_gr_config_get_tpc_count(gr->config) *
					sizeof(u32));
	gpc_tpc_mask = nvgpu_kzalloc(g, sizeof(unsigned long) *
			nvgpu_get_litter_value(g, GPU_LIT_NUM_GPCS));

	if ((gpc_table == NULL) ||
	    (tpc_table == NULL) ||
	    (gpc_tpc_mask == NULL)) {
		nvgpu_err(g, "Error allocating memory for sm tables");
		err = -ENOMEM;
		goto exit_build_table;
	}

	for (gpc = 0; gpc < nvgpu_gr_config_get_gpc_count(gr->config); gpc++) {
		for (pes = 0;
		     pes < nvgpu_gr_config_get_gpc_ppc_count(g->gr.config, gpc);
		     pes++) {
			gpc_tpc_mask[gpc] |= nvgpu_gr_config_get_pes_tpc_mask(
						g->gr.config, gpc, pes);
		}
	}

	for (gtpc = 0; gtpc < nvgpu_gr_config_get_tpc_count(gr->config); gtpc++) {
		maxperf = -1;
		for (gpc = 0; gpc < nvgpu_gr_config_get_gpc_count(gr->config); gpc++) {
			for_each_set_bit(tpc, &gpc_tpc_mask[gpc],
					nvgpu_gr_config_get_gpc_tpc_count(g->gr.config, gpc)) {
				perf = -1;
				err = gr_gv100_scg_estimate_perf(g,
						gpc_tpc_mask, gpc, tpc, &perf);

				if (err != 0) {
					nvgpu_err(g,
						"Error while estimating perf");
					goto exit_build_table;
				}

				if (perf >= maxperf) {
					maxperf = perf;
					gpc_table[gtpc] = gpc;
					tpc_table[gtpc] = tpc;
				}
			}
		}
		gpc_tpc_mask[gpc_table[gtpc]] &= ~(BIT64(tpc_table[gtpc]));
	}

	for (tpc = 0, sm_id = 0;  sm_id < num_sm; tpc++, sm_id += sm_per_tpc) {
		for (sm = 0; sm < sm_per_tpc; sm++) {
			u32 index = sm_id + sm;

			g->gr.sm_to_cluster[index].gpc_index = gpc_table[tpc];
			g->gr.sm_to_cluster[index].tpc_index = tpc_table[tpc];
			g->gr.sm_to_cluster[index].sm_index = sm;
			g->gr.sm_to_cluster[index].global_tpc_index = tpc;
			nvgpu_log_info(g,
				"gpc : %d tpc %d sm_index %d global_index: %d",
				g->gr.sm_to_cluster[index].gpc_index,
				g->gr.sm_to_cluster[index].tpc_index,
				g->gr.sm_to_cluster[index].sm_index,
				g->gr.sm_to_cluster[index].global_tpc_index);

		}
	}

	g->gr.no_of_sm = num_sm;
	nvgpu_log_info(g, " total number of sm = %d", g->gr.no_of_sm);
exit_build_table:
	nvgpu_kfree(g, gpc_table);
	nvgpu_kfree(g, tpc_table);
	nvgpu_kfree(g, gpc_tpc_mask);
	return err;
}

u32 gr_gv100_get_patch_slots(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;
	struct fifo_gk20a *f = &g->fifo;
	u32 size = 0;

	/*
	 * CMD to update PE table
	 */
	size++;

	/*
	 * Update PE table contents
	 * for PE table, each patch buffer update writes 32 TPCs
	 */
	size += DIV_ROUND_UP(nvgpu_gr_config_get_tpc_count(gr->config), 32U);

	/*
	 * Update the PL table contents
	 * For PL table, each patch buffer update configures 4 TPCs
	 */
	size += DIV_ROUND_UP(nvgpu_gr_config_get_tpc_count(gr->config), 4U);

	/*
	 * We need this for all subcontexts
	 */
	size *= f->max_subctx_count;

	/*
	 * Add space for a partition mode change as well
	 * reserve two slots since DYNAMIC -> STATIC requires
	 * DYNAMIC -> NONE -> STATIC
	 */
	size += 2U;

	/*
	 * Add current patch buffer size
	 */
	size += gr_gk20a_get_patch_slots(g);

	/*
	 * Align to 4K size
	 */
	size = ALIGN(size, PATCH_CTX_SLOTS_PER_PAGE);

	/*
	 * Increase the size to accommodate for additional TPC partition update
	 */
	size += 2U * PATCH_CTX_SLOTS_PER_PAGE;

	return size;
}

static u32 gr_gv100_get_active_fpba_mask(struct gk20a *g)
{
	u32 active_fbpa_mask;
	u32 num_fbpas, val;

	val = nvgpu_readl(g, top_num_fbpas_r());
	num_fbpas = top_num_fbpas_value_v(val);

	/*
	 * Read active fbpa mask from fuse
	 * Note that 0:enable and 1:disable in value read from fuse so we've to
	 * flip the bits.
	 * Also set unused bits to zero
	 */
	active_fbpa_mask = g->ops.fuse.fuse_status_opt_fbio(g);
	active_fbpa_mask = ~active_fbpa_mask;
	active_fbpa_mask = active_fbpa_mask & (BIT32(num_fbpas) - 1U);

	return active_fbpa_mask;
}

int gr_gv100_add_ctxsw_reg_pm_fbpa(struct gk20a *g,
				struct ctxsw_buf_offset_map_entry *map,
				struct netlist_aiv_list *regs,
				u32 *count, u32 *offset,
				u32 max_cnt, u32 base,
				u32 num_fbpas, u32 stride, u32 mask)
{
	u32 fbpa_id;
	u32 idx;
	u32 cnt = *count;
	u32 off = *offset;
	u32 active_fbpa_mask;

	if ((cnt + (regs->count * num_fbpas)) > max_cnt) {
		return -EINVAL;
	}

	active_fbpa_mask = gr_gv100_get_active_fpba_mask(g);

	for (idx = 0; idx < regs->count; idx++) {
		for (fbpa_id = 0; fbpa_id < num_fbpas; fbpa_id++) {
			if ((active_fbpa_mask & BIT32(fbpa_id)) != 0U) {
				map[cnt].addr = base +
						(regs->l[idx].addr & mask) +
						(fbpa_id * stride);
				map[cnt++].offset = off;
				off += 4U;
			}
		}
	}
	*count = cnt;
	*offset = off;
	return 0;
}

int gr_gv100_add_ctxsw_reg_perf_pma(struct ctxsw_buf_offset_map_entry *map,
	struct netlist_aiv_list *regs,
	u32 *count, u32 *offset,
	u32 max_cnt, u32 base, u32 mask)
{
	*offset = ALIGN(*offset, 256);
	return gr_gk20a_add_ctxsw_reg_perf_pma(map, regs,
			count, offset, max_cnt, base, mask);
}

void gr_gv100_split_fbpa_broadcast_addr(struct gk20a *g, u32 addr,
				      u32 num_fbpas,
				      u32 *priv_addr_table, u32 *t)
{
	u32 active_fbpa_mask;
	u32 fbpa_id;

	active_fbpa_mask = gr_gv100_get_active_fpba_mask(g);

	for (fbpa_id = 0; fbpa_id < num_fbpas; fbpa_id++) {
		if ((active_fbpa_mask & BIT32(fbpa_id)) != 0U) {
			priv_addr_table[(*t)++] = pri_fbpa_addr(g,
					pri_fbpa_addr_mask(g, addr), fbpa_id);
		}
	}
}

void gr_gv100_set_pmm_register(struct gk20a *g, u32 offset, u32 val,
				u32 num_chiplets, u32 num_perfmons)
{
	u32 perfmon_index = 0;
	u32 chiplet_index = 0;
	u32 reg_offset = 0;
	u32 chiplet_stride = g->ops.gr.get_pmm_per_chiplet_offset();

	for (chiplet_index = 0; chiplet_index < num_chiplets; chiplet_index++) {
		for (perfmon_index = 0; perfmon_index < num_perfmons;
						perfmon_index++) {
			reg_offset = offset + perfmon_index *
				perf_pmmsys_perdomain_offset_v() +
				chiplet_index * chiplet_stride;
			nvgpu_writel(g, reg_offset, val);
		}
	}
}

void gr_gv100_get_num_hwpm_perfmon(struct gk20a *g, u32 *num_sys_perfmon,
				u32 *num_fbp_perfmon, u32 *num_gpc_perfmon)
{
	int err;
	u32 buf_offset_lo, buf_offset_addr, num_offsets;
	u32 perfmon_index = 0;

	for (perfmon_index = 0; perfmon_index <
			perf_pmmsys_engine_sel__size_1_v();
			perfmon_index++) {
		err = gr_gk20a_get_pm_ctx_buffer_offsets(g,
				perf_pmmsys_engine_sel_r(perfmon_index),
				1,
				&buf_offset_lo,
				&buf_offset_addr,
				&num_offsets);
		if (err != 0) {
			break;
		}
	}
	*num_sys_perfmon = perfmon_index;

	for (perfmon_index = 0; perfmon_index <
			perf_pmmfbp_engine_sel__size_1_v();
			perfmon_index++) {
		err = gr_gk20a_get_pm_ctx_buffer_offsets(g,
				perf_pmmfbp_engine_sel_r(perfmon_index),
				1,
				&buf_offset_lo,
				&buf_offset_addr,
				&num_offsets);
		if (err != 0) {
			break;
		}
	}
	*num_fbp_perfmon = perfmon_index;

	for (perfmon_index = 0; perfmon_index <
			perf_pmmgpc_engine_sel__size_1_v();
			perfmon_index++) {
		err = gr_gk20a_get_pm_ctx_buffer_offsets(g,
				perf_pmmgpc_engine_sel_r(perfmon_index),
				1,
				&buf_offset_lo,
				&buf_offset_addr,
				&num_offsets);
		if (err != 0) {
			break;
		}
	}
	*num_gpc_perfmon = perfmon_index;
}

void gr_gv100_init_hwpm_pmm_register(struct gk20a *g)
{
	u32 num_sys_perfmon = 0;
	u32 num_fbp_perfmon = 0;
	u32 num_gpc_perfmon = 0;

	g->ops.gr.get_num_hwpm_perfmon(g, &num_sys_perfmon,
				&num_fbp_perfmon, &num_gpc_perfmon);

	g->ops.gr.set_pmm_register(g, perf_pmmsys_engine_sel_r(0),
		0xFFFFFFFFU, 1U, num_sys_perfmon);
	g->ops.gr.set_pmm_register(g, perf_pmmfbp_engine_sel_r(0),
		0xFFFFFFFFU, g->gr.num_fbps, num_fbp_perfmon);
	g->ops.gr.set_pmm_register(g, perf_pmmgpc_engine_sel_r(0),
		0xFFFFFFFFU, nvgpu_gr_config_get_gpc_count(g->gr.config),
		num_gpc_perfmon);
}
