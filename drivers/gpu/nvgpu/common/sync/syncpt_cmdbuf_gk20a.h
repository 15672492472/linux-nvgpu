/*
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
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
#ifndef NVGPU_SYNC_SYNCPT_CMDBUF_GK20A_H
#define NVGPU_SYNC_SYNCPT_CMDBUF_GK20A_H

#include <nvgpu/types.h>

struct gk20a;
struct priv_cmd_entry;
struct nvgpu_mem;

#ifdef CONFIG_TEGRA_GK20A_NVHOST

void gk20a_add_syncpt_wait_cmd(struct gk20a *g,
		struct priv_cmd_entry *cmd, u32 off,
		u32 id, u32 thresh, u64 gpu_va);
u32 gk20a_get_syncpt_wait_cmd_size(void);
u32 gk20a_get_syncpt_incr_per_release(void);
void gk20a_add_syncpt_incr_cmd(struct gk20a *g,
		bool wfi_cmd, struct priv_cmd_entry *cmd,
		u32 id, u64 gpu_va);
u32 gk20a_get_syncpt_incr_cmd_size(bool wfi_cmd);
void gk20a_free_syncpt_buf(struct channel_gk20a *c,
		struct nvgpu_mem *syncpt_buf);

int gk20a_alloc_syncpt_buf(struct channel_gk20a *c,
		u32 syncpt_id, struct nvgpu_mem *syncpt_buf);

#else

static inline void gk20a_add_syncpt_wait_cmd(struct gk20a *g,
		struct priv_cmd_entry *cmd, u32 off,
		u32 id, u32 thresh, u64 gpu_va)
{
}
static inline u32 gk20a_get_syncpt_wait_cmd_size(void)
{
	return 0U;
}
static inline u32 gk20a_get_syncpt_incr_per_release(void)
{
	return 0U;
}
static inline void gk20a_add_syncpt_incr_cmd(struct gk20a *g,
		bool wfi_cmd, struct priv_cmd_entry *cmd,
		u32 id, u64 gpu_va)
{
}
static inline u32 gk20a_get_syncpt_incr_cmd_size(bool wfi_cmd)
{
	return 0U;
}
static inline void gk20a_free_syncpt_buf(struct channel_gk20a *c,
		struct nvgpu_mem *syncpt_buf)
{
}

static inline int gk20a_alloc_syncpt_buf(struct channel_gk20a *c,
		u32 syncpt_id, struct nvgpu_mem *syncpt_buf)
{
	return -ENOSYS;
}

#endif

#endif /* NVGPU_SYNC_SYNCPT_CMDBUF_GK20A_H */