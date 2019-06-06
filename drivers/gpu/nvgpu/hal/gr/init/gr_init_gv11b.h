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

#ifndef NVGPU_GR_INIT_GV11B_H
#define NVGPU_GR_INIT_GV11B_H

#include <nvgpu/types.h>

struct gk20a;
struct nvgpu_gr_config;
struct nvgpu_gr_ctx;
struct netlist_av_list;

u32 gv11b_gr_init_get_nonpes_aware_tpc(struct gk20a *g, u32 gpc, u32 tpc,
				       struct nvgpu_gr_config *gr_config);
void gv11b_gr_init_ecc_scrub_reg(struct gk20a *g,
				 struct nvgpu_gr_config *gr_config);
void gv11b_gr_init_gpc_mmu(struct gk20a *g);
void gv11b_gr_init_get_access_map(struct gk20a *g,
				   u32 **whitelist, u32 *num_entries);
void gv11b_gr_init_sm_id_numbering(struct gk20a *g, u32 gpc, u32 tpc, u32 smid,
				   struct nvgpu_gr_config *gr_config);
int gv11b_gr_init_sm_id_config(struct gk20a *g, u32 *tpc_sm_id,
			       struct nvgpu_gr_config *gr_config);
void gv11b_gr_init_tpc_mask(struct gk20a *g, u32 gpc_index, u32 pes_tpc_mask);
#ifdef NVGPU_GRAPHICS
void gv11b_gr_init_rop_mapping(struct gk20a *g,
			      struct nvgpu_gr_config *gr_config);
#endif
int gv11b_gr_init_fs_state(struct gk20a *g);
int gv11b_gr_init_preemption_state(struct gk20a *g);
void gv11b_gr_init_commit_global_timeslice(struct gk20a *g);

u32 gv11b_gr_init_get_bundle_cb_default_size(struct gk20a *g);
u32 gv11b_gr_init_get_min_gpm_fifo_depth(struct gk20a *g);
u32 gv11b_gr_init_get_bundle_cb_token_limit(struct gk20a *g);
u32 gv11b_gr_init_get_attrib_cb_default_size(struct gk20a *g);
u32 gv11b_gr_init_get_alpha_cb_default_size(struct gk20a *g);
u32 gv11b_gr_init_get_attrib_cb_gfxp_default_size(struct gk20a *g);
u32 gv11b_gr_init_get_attrib_cb_gfxp_size(struct gk20a *g);
u32 gv11b_gr_init_get_attrib_cb_size(struct gk20a *g, u32 tpc_count);
u32 gv11b_gr_init_get_alpha_cb_size(struct gk20a *g, u32 tpc_count);
u32 gv11b_gr_init_get_global_attr_cb_size(struct gk20a *g, u32 tpc_count,
	u32 max_tpc);

void gv11b_gr_init_commit_global_attrib_cb(struct gk20a *g,
	struct nvgpu_gr_ctx *gr_ctx, u32 tpc_count, u32 max_tpc, u64 addr,
	bool patch);

int gv11b_gr_init_load_sw_veid_bundle(struct gk20a *g,
	struct netlist_av_list *sw_veid_bundle_init);

u32 gv11b_gr_init_get_ctx_spill_size(struct gk20a *g);
u32 gv11b_gr_init_get_ctx_betacb_size(struct gk20a *g);

void gv11b_gr_init_commit_ctxsw_spill(struct gk20a *g,
	struct nvgpu_gr_ctx *gr_ctx, u64 addr, u32 size, bool patch);
void gv11b_gr_init_commit_cbes_reserve(struct gk20a *g,
	struct nvgpu_gr_ctx *gr_ctx, bool patch);
void gv11b_gr_init_commit_gfxp_wfi_timeout(struct gk20a *g,
	struct nvgpu_gr_ctx *gr_ctx, bool patch);

u32 gv11b_gr_init_get_max_subctx_count(void);
u32 gv11b_gr_init_get_patch_slots(struct gk20a *g,
	struct nvgpu_gr_config *config);
void gv11b_gr_init_detect_sm_arch(struct gk20a *g);

#endif /* NVGPU_GR_INIT_GV11B_H */
