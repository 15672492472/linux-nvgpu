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
struct nvgpu_gr_zbc;
struct nvgpu_gr_hwpm_map;
struct nvgpu_gr_zcull;
struct gk20a_cs_snapshot;

struct gr_channel_map_tlb_entry {
	u32 curr_ctx;
	u32 chid;
	u32 tsgid;
};

struct nvgpu_gr {
	struct gk20a *g;
	struct {
		u32 golden_image_size;

		u32 pm_ctxsw_image_size;

		u32 preempt_image_size;

		u32 zcull_image_size;
	} ctx_vars;

	struct nvgpu_cond init_wq;
	bool initialized;

	u32 num_fbps;
	u32 max_fbps_count;

	struct nvgpu_gr_global_ctx_buffer_desc *global_ctx_buffer;

	struct nvgpu_gr_obj_ctx_golden_image *golden_image;

	struct nvgpu_gr_ctx_desc *gr_ctx_desc;

	struct nvgpu_gr_config *config;

	struct nvgpu_gr_hwpm_map *hwpm_map;

	struct nvgpu_gr_zcull *zcull;

	struct nvgpu_gr_zbc *zbc;

	struct nvgpu_gr_falcon *falcon;

#define GR_CHANNEL_MAP_TLB_SIZE		2U /* must of power of 2 */
	struct gr_channel_map_tlb_entry chid_tlb[GR_CHANNEL_MAP_TLB_SIZE];
	u32 channel_tlb_flush_index;
	struct nvgpu_spinlock ch_tlb_lock;

	void (*remove_support)(struct gk20a *g);
	bool sw_ready;

	u32 fecs_feature_override_ecc_val;

	u32 cilp_preempt_pending_chid;

	u32 fbp_en_mask;
	u32 *fbp_rop_l2_en_mask;

#if defined(CONFIG_GK20A_CYCLE_STATS)
	struct nvgpu_mutex		cs_lock;
	struct gk20a_cs_snapshot	*cs_data;
#endif
	u32 max_css_buffer_size;
	u32 max_ctxsw_ring_buffer_size;

	struct nvgpu_mutex ctxsw_disable_mutex;
	int ctxsw_disable_count;
};

#endif /* NVGPU_GR_PRIV_H */

