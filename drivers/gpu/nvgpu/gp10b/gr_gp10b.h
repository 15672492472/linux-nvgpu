/*
 * GP10B GPU GR
 *
 * Copyright (c) 2015-2019, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef NVGPU_GR_GP10B_H
#define NVGPU_GR_GP10B_H

#include "gk20a/mm_gk20a.h"

struct gk20a;
struct nvgpu_gr_isr_data;
struct nvgpu_gr_ctx;
struct nvgpu_preemption_modes_rec;
struct gk20a_debug_output;

#define NVC097_BES_CROP_DEBUG4_CLAMP_FP_BLEND_TO_INF	0x0U
#define NVC097_BES_CROP_DEBUG4_CLAMP_FP_BLEND_TO_MAXVAL 0x1U

void gr_gp10b_create_sysfs(struct gk20a *g);
void gr_gp10b_remove_sysfs(struct gk20a *g);
int gr_gp10b_handle_fecs_error(struct gk20a *g,
			struct channel_gk20a *__ch,
			struct nvgpu_gr_isr_data *isr_data);
int gr_gp10b_set_cilp_preempt_pending(struct gk20a *g,
		struct channel_gk20a *fault_ch);
int gr_gp10b_commit_global_cb_manager(struct gk20a *g,
			struct nvgpu_gr_ctx *gr_ctx, bool patch);
void gr_gp10b_set_bes_crop_debug3(struct gk20a *g, u32 data);
void gr_gp10b_set_bes_crop_debug4(struct gk20a *g, u32 data);
void gr_gp10b_set_alpha_circular_buffer_size(struct gk20a *g, u32 data);
void gr_gp10b_set_circular_buffer_size(struct gk20a *g, u32 data);
int gr_gp10b_dump_gr_status_regs(struct gk20a *g,
			   struct gk20a_debug_output *o);
void gr_gp10b_set_gpc_tpc_mask(struct gk20a *g, u32 gpc_index);
int gr_gp10b_pre_process_sm_exception(struct gk20a *g,
		u32 gpc, u32 tpc, u32 sm, u32 global_esr, u32 warp_esr,
		bool sm_debugger_attached, struct channel_gk20a *fault_ch,
		bool *early_exit, bool *ignore_debugger);
u32 gp10b_gr_get_sm_hww_warp_esr(struct gk20a *g,
			u32 gpc, u32 tpc, u32 sm);
u32 get_ecc_override_val(struct gk20a *g);
int gr_gp10b_suspend_contexts(struct gk20a *g,
				struct dbg_session_gk20a *dbg_s,
				int *ctx_resident_ch_fd);
int gr_gp10b_set_boosted_ctx(struct channel_gk20a *ch,
				    bool boost);
int gp10b_gr_fuse_override(struct gk20a *g);
int gr_gp10b_init_preemption_state(struct gk20a *g);
bool gr_gp10b_suspend_context(struct channel_gk20a *ch,
				bool *cilp_preempt_pending);
#endif /* NVGPU_GR_GP10B_H */
