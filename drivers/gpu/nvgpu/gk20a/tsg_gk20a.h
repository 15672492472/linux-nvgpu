/*
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved.
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
#ifndef __TSG_GK20A_H_
#define __TSG_GK20A_H_

#include <nvgpu/lock.h>
#include <nvgpu/kref.h>
#include <nvgpu/rwsem.h>

#ifdef CONFIG_TEGRA_19x_GPU
#include "tsg_t19x.h"
#endif
#define NVGPU_INVALID_TSG_ID (-1)

struct channel_gk20a;

bool gk20a_is_channel_marked_as_tsg(struct channel_gk20a *ch);
struct tsg_gk20a *gk20a_tsg_open(struct gk20a *g);
void gk20a_tsg_release(struct nvgpu_ref *ref);

int gk20a_init_tsg_support(struct gk20a *g, u32 tsgid);
struct tsg_gk20a *tsg_gk20a_from_ch(struct channel_gk20a *ch);

struct tsg_gk20a {
	struct gk20a *g;

	bool in_use;
	int tsgid;

	struct nvgpu_ref refcount;

	struct nvgpu_list_node ch_list;
	int num_active_channels;
	struct nvgpu_rwsem ch_list_lock;

	unsigned int timeslice_us;
	unsigned int timeslice_timeout;
	unsigned int timeslice_scale;

	struct gr_ctx_desc *tsg_gr_ctx;

	struct vm_gk20a *vm;

	u32 interleave_level;

	struct nvgpu_list_node event_id_list;
	struct nvgpu_mutex event_id_list_lock;

	u32 runlist_id;
	pid_t tgid;
	struct nvgpu_mem *eng_method_buffers;
#ifdef CONFIG_TEGRA_19x_GPU
	struct tsg_t19x t19x;
#endif
};

int gk20a_enable_tsg(struct tsg_gk20a *tsg);
int gk20a_disable_tsg(struct tsg_gk20a *tsg);
int gk20a_tsg_bind_channel(struct tsg_gk20a *tsg,
			struct channel_gk20a *ch);
int gk20a_tsg_unbind_channel(struct channel_gk20a *ch);

void gk20a_tsg_event_id_post_event(struct tsg_gk20a *tsg,
				       int event_id);
int gk20a_tsg_set_runlist_interleave(struct tsg_gk20a *tsg, u32 level);
int gk20a_tsg_set_timeslice(struct tsg_gk20a *tsg, u32 timeslice);
u32 gk20a_tsg_get_timeslice(struct tsg_gk20a *tsg);
int gk20a_tsg_set_priority(struct gk20a *g, struct tsg_gk20a *tsg,
				u32 priority);


#endif /* __TSG_GK20A_H_ */
