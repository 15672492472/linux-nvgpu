/*
 * Copyright (c) 2014-2019, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef NVGPU_PBDMA_GM20B_H
#define NVGPU_PBDMA_GM20B_H

#include <nvgpu/types.h>

struct gk20a;
struct gk20a_debug_output;
struct nvgpu_channel_dump_info;
struct nvgpu_gpfifo_entry;

void gm20b_pbdma_intr_enable(struct gk20a *g, bool enable);

bool gm20b_pbdma_handle_intr_0(struct gk20a *g, u32 pbdma_id,
			u32 pbdma_intr_0, u32 *error_notifier);
bool gm20b_pbdma_handle_intr_1(struct gk20a *g, u32 pbdma_id,
			u32 pbdma_intr_1, u32 *error_notifier);
bool gm20b_pbdma_handle_intr(struct gk20a *g, u32 pbdma_id,
			u32 *error_notifier);
u32 gm20b_pbdma_get_signature(struct gk20a *g);
u32 gm20b_pbdma_read_data(struct gk20a *g, u32 pbdma_id);
void gm20b_pbdma_reset_header(struct gk20a *g, u32 pbdma_id);
void gm20b_pbdma_reset_method(struct gk20a *g, u32 pbdma_id,
			u32 pbdma_method_index);
u32 gm20b_pbdma_acquire_val(u64 timeout);
void gm20b_pbdma_dump_status(struct gk20a *g, struct gk20a_debug_output *o);

void gm20b_pbdma_format_gpfifo_entry(struct gk20a *g,
		struct nvgpu_gpfifo_entry *gpfifo_entry,
		u64 pb_gpu_va, u32 method_size);

u32 gm20b_pbdma_device_fatal_0_intr_descs(void);
u32 gm20b_pbdma_channel_fatal_0_intr_descs(void);
u32 gm20b_pbdma_restartable_0_intr_descs(void);

void gm20b_pbdma_clear_all_intr(struct gk20a *g, u32 pbdma_id);
void gm20b_pbdma_disable_and_clear_all_intr(struct gk20a *g);
void gm20b_pbdma_syncpoint_debug_dump(struct gk20a *g,
		     struct gk20a_debug_output *o,
		     struct nvgpu_channel_dump_info *info);
void gm20b_pbdma_setup_hw(struct gk20a *g);

#endif /* NVGPU_PBDMA_GM20B_H */
