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

#ifndef NVGPU_GR_INIT_GM20B_H
#define NVGPU_GR_INIT_GM20B_H

#include <nvgpu/types.h>

struct gk20a;

void gm20b_gr_init_pd_tpc_per_gpc(struct gk20a *g);
void gm20b_gr_init_pd_skip_table_gpc(struct gk20a *g);
void gm20b_gr_init_cwd_gpcs_tpcs_num(struct gk20a *g,
				     u32 gpc_count, u32 tpc_count);
int gm20b_gr_init_wait_idle(struct gk20a *g);
int gm20b_gr_init_wait_fe_idle(struct gk20a *g);
int gm20b_gr_init_fe_pwr_mode_force_on(struct gk20a *g, bool force_on);
void gm20b_gr_init_override_context_reset(struct gk20a *g);
void gm20b_gr_init_fe_go_idle_timeout(struct gk20a *g, bool enable);

#endif /* NVGPU_GR_INIT_GM20B_H */
