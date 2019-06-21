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

#ifndef NVGPU_GR_UTILS_H
#define NVGPU_GR_UTILS_H

#include <nvgpu/types.h>
#include <nvgpu/safe_ops.h>

struct gk20a;
struct nvgpu_gr_falcon;
struct nvgpu_gr_obj_ctx_golden_image;
struct nvgpu_gr_config;

#ifdef CONFIG_NVGPU_GRAPHICS
struct nvgpu_gr_zbc;
struct nvgpu_gr_zcull;
#endif
struct nvgpu_gr_hwpm_map;
struct nvgpu_gr_intr;
struct nvgpu_gr_global_ctx_buffer_desc;

static inline u32 nvgpu_gr_checksum_u32(u32 a, u32 b)
{
	return nvgpu_safe_cast_u64_to_u32(((u64)a + (u64)b) & (U32_MAX));
}

/* gr struct pointers */
struct nvgpu_gr_falcon *nvgpu_gr_get_falcon_ptr(struct gk20a *g);
struct nvgpu_gr_obj_ctx_golden_image *nvgpu_gr_get_golden_image_ptr(
							struct gk20a *g);
struct nvgpu_gr_config *nvgpu_gr_get_config_ptr(struct gk20a *g);
struct nvgpu_gr_intr *nvgpu_gr_get_intr_ptr(struct gk20a *g);
/* gr variables */
u32 nvgpu_gr_get_override_ecc_val(struct gk20a *g);
void nvgpu_gr_override_ecc_val(struct gk20a *g, u32 ecc_val);
#ifdef CONFIG_NVGPU_GRAPHICS
struct nvgpu_gr_zcull *nvgpu_gr_get_zcull_ptr(struct gk20a *g);
struct nvgpu_gr_zbc *nvgpu_gr_get_zbc_ptr(struct gk20a *g);
#endif
#ifdef CONFIG_NVGPU_CILP
u32 nvgpu_gr_get_cilp_preempt_pending_chid(struct gk20a *g);
void nvgpu_gr_clear_cilp_preempt_pending_chid(struct gk20a *g);
#endif
#ifdef CONFIG_NVGPU_DEBUGGER
struct nvgpu_gr_hwpm_map *nvgpu_gr_get_hwpm_map_ptr(struct gk20a *g);
void nvgpu_gr_reset_falcon_ptr(struct gk20a *g);
void nvgpu_gr_reset_golden_image_ptr(struct gk20a *g);
#endif
#ifdef CONFIG_NVGPU_FECS_TRACE
struct nvgpu_gr_global_ctx_buffer_desc *nvgpu_gr_get_global_ctx_buffer_ptr(
							struct gk20a *g);
#endif

#endif /* NVGPU_GR_UTILS_H */
