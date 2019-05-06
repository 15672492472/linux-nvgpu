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

#include <nvgpu/gr/config.h>
#include <nvgpu/gr/fs_state.h>

static int gr_load_sm_id_config(struct gk20a *g, struct nvgpu_gr_config *config)
{
	int err;
	u32 *tpc_sm_id;
	u32 sm_id_size = g->ops.gr.init.get_sm_id_size();

	tpc_sm_id = nvgpu_kcalloc(g, sm_id_size, sizeof(u32));
	if (tpc_sm_id == NULL) {
		return -ENOMEM;
	}

	err = g->ops.gr.init.sm_id_config(g, tpc_sm_id, config);

	nvgpu_kfree(g, tpc_sm_id);

	return err;
}

static void gr_load_tpc_mask(struct gk20a *g, struct nvgpu_gr_config *config)
{
	u32 pes_tpc_mask = 0, fuse_tpc_mask;
	u32 gpc, pes, val;
	u32 num_tpc_per_gpc = nvgpu_get_litter_value(g,
						     GPU_LIT_NUM_TPC_PER_GPC);
	u32 max_tpc_count = nvgpu_gr_config_get_max_tpc_count(config);

	/* gv11b has 1 GPC and 4 TPC/GPC, so mask will not overflow u32 */
	for (gpc = 0; gpc < nvgpu_gr_config_get_gpc_count(config); gpc++) {
		for (pes = 0;
		     pes < nvgpu_gr_config_get_pe_count_per_gpc(config);
		     pes++) {
			pes_tpc_mask |= nvgpu_gr_config_get_pes_tpc_mask(
						config, gpc, pes) <<
					num_tpc_per_gpc * gpc;
		}
	}

	nvgpu_log_info(g, "pes_tpc_mask %u\n", pes_tpc_mask);

	fuse_tpc_mask = g->ops.gr.config.get_gpc_tpc_mask(g, config, 0);
	if ((g->tpc_fs_mask_user != 0U) &&
	    (g->tpc_fs_mask_user != fuse_tpc_mask) &&
	    (fuse_tpc_mask == BIT32(max_tpc_count) - U32(1))) {
		val = g->tpc_fs_mask_user;
		val &= BIT32(max_tpc_count) - U32(1);
		/* skip tpc to disable the other tpc cause channel timeout */
		val = BIT32(hweight32(val)) - U32(1);
		pes_tpc_mask = val;
	}
	g->ops.gr.init.tpc_mask(g, 0, pes_tpc_mask);
}

int nvgpu_gr_fs_state_init(struct gk20a *g, struct nvgpu_gr_config *config)
{
	u32 tpc_index, gpc_index;
	u32 sm_id = 0;
	u32 fuse_tpc_mask;
	u32 gpc_cnt, tpc_cnt, max_tpc_cnt;
	int err = 0;

	nvgpu_log_fn(g, " ");

	err = g->ops.gr.init.fs_state(g);
	if (err != 0) {
		return err;
	}

	if (g->ops.gr.config.init_sm_id_table != NULL) {
		err = g->ops.gr.config.init_sm_id_table(g, config);
		if (err != 0) {
			return err;
		}

		/* Is table empty ? */
		if (g->ops.gr.init.get_no_of_sm(g) == 0U) {
			return -EINVAL;
		}
	}

	for (sm_id = 0; sm_id < g->ops.gr.init.get_no_of_sm(g);
	     sm_id++) {
		struct nvgpu_sm_info *sm_info =
			nvgpu_gr_config_get_sm_info(config, sm_id);
		tpc_index = nvgpu_gr_config_get_sm_info_tpc_index(sm_info);
		gpc_index = nvgpu_gr_config_get_sm_info_gpc_index(sm_info);

		g->ops.gr.init.sm_id_numbering(g, gpc_index, tpc_index, sm_id,
					       config);
	}

	g->ops.gr.init.pd_tpc_per_gpc(g, config);

	/* gr__setup_pd_mapping */
	g->ops.gr.init.rop_mapping(g, config);

	g->ops.gr.init.pd_skip_table_gpc(g, config);

	fuse_tpc_mask = g->ops.gr.config.get_gpc_tpc_mask(g, config, 0);
	gpc_cnt = nvgpu_gr_config_get_gpc_count(config);
	tpc_cnt = nvgpu_gr_config_get_tpc_count(config);
	max_tpc_cnt = nvgpu_gr_config_get_max_tpc_count(config);

	if ((g->tpc_fs_mask_user != 0U) &&
		(fuse_tpc_mask == BIT32(max_tpc_cnt) - 1U)) {
		u32 val = g->tpc_fs_mask_user;
		val &= BIT32(max_tpc_cnt) - U32(1);
		tpc_cnt = (u32)hweight32(val);
	}
	g->ops.gr.init.cwd_gpcs_tpcs_num(g, gpc_cnt, tpc_cnt);

	gr_load_tpc_mask(g, config);

	err = gr_load_sm_id_config(g, config);
	if (err != 0) {
		nvgpu_err(g, "load_smid_config failed err=%d", err);
	}

	return err;
}

