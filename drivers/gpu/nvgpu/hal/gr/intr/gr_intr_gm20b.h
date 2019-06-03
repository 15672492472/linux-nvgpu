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

#ifndef NVGPU_GR_INTR_GM20B_H
#define NVGPU_GR_INTR_GM20B_H

#include <nvgpu/types.h>

struct gk20a;
struct nvgpu_gr_config;
struct nvgpu_gr_tpc_exception;
struct nvgpu_gr_isr_data;
struct nvgpu_gr_intr_info;

#define NVB197_SET_ALPHA_CIRCULAR_BUFFER_SIZE	0x02dc
#define NVB197_SET_CIRCULAR_BUFFER_SIZE		0x1280
#define NVB197_SET_SHADER_EXCEPTIONS		0x1528
#define NVB197_SET_RD_COALESCE			0x102c
#define NVB1C0_SET_SHADER_EXCEPTIONS		0x1528
#define NVB1C0_SET_RD_COALESCE			0x0228

#define NVA297_SET_SHADER_EXCEPTIONS_ENABLE_FALSE	U32(0)

int gm20b_gr_intr_handle_sw_method(struct gk20a *g, u32 addr,
				  u32 class_num, u32 offset, u32 data);
void gm20b_gr_intr_set_shader_exceptions(struct gk20a *g, u32 data);
void gm20b_gr_intr_handle_class_error(struct gk20a *g, u32 chid,
				       struct nvgpu_gr_isr_data *isr_data);
void gm20b_gr_intr_clear_pending_interrupts(struct gk20a *g, u32 gr_intr);
u32 gm20b_gr_intr_read_pending_interrupts(struct gk20a *g,
					struct nvgpu_gr_intr_info *intr_info);
bool gm20b_gr_intr_handle_exceptions(struct gk20a *g, bool *is_gpc_exception);
u32 gm20b_gr_intr_read_gpc_tpc_exception(u32 gpc_exception);
u32 gm20b_gr_intr_read_gpc_exception(struct gk20a *g, u32 gpc);
u32 gm20b_gr_intr_read_exception1(struct gk20a *g);
void gm20b_gr_intr_get_trapped_method_info(struct gk20a *g,
				    struct nvgpu_gr_isr_data *isr_data);
u32 gm20b_gr_intr_get_tpc_exception(struct gk20a *g, u32 offset,
			struct nvgpu_gr_tpc_exception *pending_tpc);
void gm20b_gr_intr_handle_tex_exception(struct gk20a *g, u32 gpc, u32 tpc);
void gm20b_gr_intr_enable_hww_exceptions(struct gk20a *g);
void gm20b_gr_intr_enable_interrupts(struct gk20a *g, bool enable);
void gm20b_gr_intr_enable_exceptions(struct gk20a *g,
				     struct nvgpu_gr_config *gr_config,
				     bool enable);
void gm20b_gr_intr_enable_gpc_exceptions(struct gk20a *g,
					 struct nvgpu_gr_config *gr_config);
u32 gm20b_gr_intr_nonstall_isr(struct gk20a *g);
void gm20ab_gr_intr_tpc_exception_sm_disable(struct gk20a *g, u32 offset);
void gm20ab_gr_intr_tpc_exception_sm_enable(struct gk20a *g);

void gm20b_gr_intr_set_hww_esr_report_mask(struct gk20a *g);
void gm20b_gr_intr_get_esr_sm_sel(struct gk20a *g, u32 gpc, u32 tpc,
				u32 *esr_sm_sel);
void gm20b_gr_intr_clear_sm_hww(struct gk20a *g, u32 gpc, u32 tpc, u32 sm,
			u32 global_esr);
u32 gm20b_gr_intr_record_sm_error_state(struct gk20a *g, u32 gpc, u32 tpc, u32 sm,
				struct nvgpu_channel *fault_ch);

u32 gm20b_gr_intr_get_sm_hww_global_esr(struct gk20a *g, u32 gpc, u32 tpc,
		u32 sm);
u32 gm20b_gr_intr_get_sm_hww_warp_esr(struct gk20a *g, u32 gpc, u32 tpc, u32 sm);
u32 gm20b_gr_intr_get_sm_no_lock_down_hww_global_esr_mask(struct gk20a *g);
u64 gm20b_gr_intr_tpc_enabled_exceptions(struct gk20a *g);

#endif /* NVGPU_GR_INTR_GM20B_H */
