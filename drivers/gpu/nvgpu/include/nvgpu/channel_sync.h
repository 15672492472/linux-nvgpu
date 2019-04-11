/*
 *
 * Nvgpu Channel Synchronization Abstraction
 *
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

#ifndef NVGPU_CHANNEL_SYNC_H
#define NVGPU_CHANNEL_SYNC_H

#include <nvgpu/atomic.h>

struct nvgpu_channel_sync;
struct priv_cmd_entry;
struct channel_gk20a;
struct nvgpu_fence_type;
struct gk20a;

/* Public APIS for channel_sync below */

/*
 * Generate a gpu wait cmdbuf from sync fd.
 * Returns a gpu cmdbuf that performs the wait when executed
 */
int nvgpu_channel_sync_wait_fence_fd(struct nvgpu_channel_sync *s, int fd,
	struct priv_cmd_entry *entry, u32 max_wait_cmds);

/*
 * Increment syncpoint/semaphore.
 * Returns
 *  - a gpu cmdbuf that performs the increment when executed,
 *  - a fence that can be passed to wait_cpu() and is_expired().
 */
int nvgpu_channel_sync_incr(struct nvgpu_channel_sync *s,
	struct priv_cmd_entry *entry, struct nvgpu_fence_type *fence,
	bool need_sync_fence, bool register_irq);

/*
 * Increment syncpoint/semaphore, so that the returned fence represents
 * work completion (may need wfi) and can be returned to user space.
 * Returns
 *  - a gpu cmdbuf that performs the increment when executed,
 *  - a fence that can be passed to wait_cpu() and is_expired(),
 *  - a nvgpu_fence_type that signals when the incr has happened.
 */
int nvgpu_channel_sync_incr_user(struct nvgpu_channel_sync *s,
	int wait_fence_fd, struct priv_cmd_entry *entry,
	struct nvgpu_fence_type *fence, bool wfi, bool need_sync_fence,
	bool register_irq);
/*
 * Reset the channel syncpoint/semaphore. Syncpoint increments generally
 * wrap around the range of integer values. Current max value encompasses
 * all jobs tracked by the channel. In order to reset the syncpoint,
 * the min_value is advanced and set to the global max. Similarly,
 * for semaphores.
 */
void nvgpu_channel_sync_set_min_eq_max(struct nvgpu_channel_sync *s);
/*
 * Set the channel syncpoint/semaphore to safe state
 * This should be used to reset User managed syncpoint since we don't
 * track threshold values for those syncpoints
 */
void nvgpu_channel_sync_set_safe_state(struct nvgpu_channel_sync *s);

/*
 * Free the resources allocated by nvgpu_channel_sync_create.
 */
void nvgpu_channel_sync_destroy(struct nvgpu_channel_sync *sync,
	bool set_safe_state);

/*
 * Increment the usage_counter for this instance.
 */
void nvgpu_channel_sync_get_ref(struct nvgpu_channel_sync *s);

/*
 * Decrement the usage_counter for this instance and return if equals 0.
 */
bool nvgpu_channel_sync_put_ref_and_check(struct nvgpu_channel_sync *s);

/*
 * Construct a channel_sync backed by either a syncpoint or a semaphore.
 * A channel_sync is by default constructed as backed by a syncpoint
 * if CONFIG_TEGRA_GK20A_NVHOST is defined, otherwise the channel_sync
 * is constructed as backed by a semaphore.
 */
struct nvgpu_channel_sync *nvgpu_channel_sync_create(struct channel_gk20a *c,
	bool user_managed);
bool nvgpu_channel_sync_needs_os_fence_framework(struct gk20a *g);

#endif /* NVGPU_CHANNEL_SYNC_H */
