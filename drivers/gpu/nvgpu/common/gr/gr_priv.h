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

#ifndef NVGPU_GR_PRIV_H
#define NVGPU_GR_PRIV_H

#include <nvgpu/types.h>
#include <nvgpu/cond.h>

struct nvgpu_gr_ctx_desc;
struct nvgpu_gr_global_ctx_buffer_desc;
struct nvgpu_gr_obj_ctx_golden_image;
struct nvgpu_gr_config;
#ifdef CONFIG_NVGPU_GRAPHICS
struct nvgpu_gr_zbc;
struct nvgpu_gr_zcull;
#endif
struct nvgpu_gr_hwpm_map;
struct gk20a_cs_snapshot;

struct nvgpu_gr {
	struct gk20a *g;

	struct nvgpu_cond init_wq;
	bool initialized;

	struct nvgpu_gr_global_ctx_buffer_desc *global_ctx_buffer;

	struct nvgpu_gr_obj_ctx_golden_image *golden_image;

	struct nvgpu_gr_ctx_desc *gr_ctx_desc;

	struct nvgpu_gr_config *config;

#ifdef CONFIG_NVGPU_DEBUGGER
	struct nvgpu_gr_hwpm_map *hwpm_map;
#endif

#ifdef CONFIG_NVGPU_GRAPHICS
	struct nvgpu_gr_zcull *zcull;

	struct nvgpu_gr_zbc *zbc;
#endif

	struct nvgpu_gr_falcon *falcon;

	struct nvgpu_gr_intr *intr;

	void (*remove_support)(struct gk20a *g);
	bool sw_ready;

#ifdef CONFIG_NVGPU_NON_FUSA
	u32 fecs_feature_override_ecc_val;
#endif

#ifdef CONFIG_NVGPU_CILP
	u32 cilp_preempt_pending_chid;
#endif

	struct nvgpu_mutex ctxsw_disable_mutex;
	int ctxsw_disable_count;
};

#endif /* NVGPU_GR_PRIV_H */

