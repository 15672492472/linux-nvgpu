/*
 * GK20A Graphics Engine
 *
 * Copyright (c) 2011-2019, NVIDIA CORPORATION.  All rights reserved.
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
#ifndef GR_GK20A_H
#define GR_GK20A_H

#include <nvgpu/types.h>

struct gk20a;
struct channel_gk20a;
struct nvgpu_warpstate;
struct dbg_session_gk20a;
struct nvgpu_dbg_reg_op;

enum ctxsw_addr_type;

/* sm */
bool gk20a_gr_sm_debugger_attached(struct gk20a *g);
u32 gk20a_gr_get_sm_no_lock_down_hww_global_esr_mask(struct gk20a *g);
int gr_gk20a_exec_ctx_ops(struct channel_gk20a *ch,
			  struct nvgpu_dbg_reg_op *ctx_ops, u32 num_ops,
			  u32 num_ctx_wr_ops, u32 num_ctx_rd_ops,
			  bool *is_curr_ctx);
int gr_gk20a_get_ctx_buffer_offsets(struct gk20a *g,
				    u32 addr, u32 max_offsets,
				    u32 *offsets, u32 *offset_addrs,
				    u32 *num_offsets, bool is_quad, u32 quad);
int gr_gk20a_get_pm_ctx_buffer_offsets(struct gk20a *g,
				       u32 addr, u32 max_offsets,
				       u32 *offsets, u32 *offset_addrs,
				       u32 *num_offsets);
int gr_gk20a_update_smpc_ctxsw_mode(struct gk20a *g,
				    struct channel_gk20a *c,
				    bool enable_smpc_ctxsw);
int gr_gk20a_update_hwpm_ctxsw_mode(struct gk20a *g,
				  struct channel_gk20a *c,
				  u64 gpu_va, u32 mode);
void gk20a_gr_resume_single_sm(struct gk20a *g,
				u32 gpc, u32 tpc, u32 sm);
void gk20a_gr_resume_all_sms(struct gk20a *g);
void gk20a_gr_suspend_single_sm(struct gk20a *g,
		u32 gpc, u32 tpc, u32 sm,
		u32 global_esr_mask, bool check_errors);
void gk20a_gr_suspend_all_sms(struct gk20a *g,
		u32 global_esr_mask, bool check_errors);
int gr_gk20a_set_sm_debug_mode(struct gk20a *g,
	struct channel_gk20a *ch, u64 sms, bool enable);
bool gk20a_is_channel_ctx_resident(struct channel_gk20a *ch);
int gk20a_gr_lock_down_sm(struct gk20a *g,
			 u32 gpc, u32 tpc, u32 sm, u32 global_esr_mask,
			 bool check_errors);
int gk20a_gr_wait_for_sm_lock_down(struct gk20a *g, u32 gpc, u32 tpc, u32 sm,
		u32 global_esr_mask, bool check_errors);
u32 gk20a_gr_get_sm_hww_warp_esr(struct gk20a *g, u32 gpc, u32 tpc, u32 sm);
u32 gk20a_gr_get_sm_hww_global_esr(struct gk20a *g, u32 gpc, u32 tpc, u32 sm);
bool gr_gk20a_suspend_context(struct channel_gk20a *ch);
bool gr_gk20a_resume_context(struct channel_gk20a *ch);
int gr_gk20a_suspend_contexts(struct gk20a *g,
			      struct dbg_session_gk20a *dbg_s,
			      int *ctx_resident_ch_fd);
int gr_gk20a_resume_contexts(struct gk20a *g,
			      struct dbg_session_gk20a *dbg_s,
			      int *ctx_resident_ch_fd);
int gr_gk20a_trigger_suspend(struct gk20a *g);
int gr_gk20a_wait_for_pause(struct gk20a *g, struct nvgpu_warpstate *w_state);
int gr_gk20a_resume_from_pause(struct gk20a *g);
int gr_gk20a_clear_sm_errors(struct gk20a *g);
u32 gr_gk20a_tpc_enabled_exceptions(struct gk20a *g);
void gk20a_gr_get_esr_sm_sel(struct gk20a *g, u32 gpc, u32 tpc,
				u32 *esr_sm_sel);
void gk20a_gr_init_ovr_sm_dsm_perf(void);
void gk20a_gr_get_ovr_perf_regs(struct gk20a *g, u32 *num_ovr_perf_regs,
					       u32 **ovr_perf_regs);
int gr_gk20a_decode_priv_addr(struct gk20a *g, u32 addr,
	enum ctxsw_addr_type *addr_type,
	u32 *gpc_num, u32 *tpc_num, u32 *ppc_num, u32 *be_num,
	u32 *broadcast_flags);
int gr_gk20a_split_ppc_broadcast_addr(struct gk20a *g, u32 addr,
	u32 gpc_num,
	u32 *priv_addr_table, u32 *t);
int gr_gk20a_create_priv_addr_table(struct gk20a *g,
	u32 addr,
	u32 *priv_addr_table,
	u32 *num_registers);
void gr_gk20a_split_fbpa_broadcast_addr(struct gk20a *g, u32 addr,
	u32 num_fbpas,
	u32 *priv_addr_table, u32 *t);
int gr_gk20a_get_offset_in_gpccs_segment(struct gk20a *g,
	enum ctxsw_addr_type addr_type, u32 num_tpcs, u32 num_ppcs,
	u32 reg_list_ppc_count, u32 *__offset_in_segment);

#endif /*__GR_GK20A_H__*/
