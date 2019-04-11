/*
 * GK20A Channel Synchronization Abstraction
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

#include <nvgpu/kmem.h>
#include <nvgpu/log.h>
#include <nvgpu/atomic.h>
#include <nvgpu/bug.h>
#include <nvgpu/list.h>
#include <nvgpu/nvhost.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/os_fence.h>
#include <nvgpu/os_fence_syncpts.h>
#include <nvgpu/channel.h>
#include <nvgpu/channel_sync.h>
#include <nvgpu/channel_sync_syncpt.h>
#include <nvgpu/fence.h>

#include "channel_sync_priv.h"
#include "gk20a/mm_gk20a.h"

struct nvgpu_channel_sync_syncpt {
	struct nvgpu_channel_sync ops;
	struct channel_gk20a *c;
	struct nvgpu_nvhost_dev *nvhost_dev;
	u32 id;
	struct nvgpu_mem syncpt_buf;
};

static struct nvgpu_channel_sync_syncpt *
nvgpu_channel_sync_syncpt_from_ops(struct nvgpu_channel_sync *ops)
{
	return (struct nvgpu_channel_sync_syncpt *)
		((uintptr_t)ops -
			offsetof(struct nvgpu_channel_sync_syncpt, ops));
}

static int channel_sync_syncpt_gen_wait_cmd(struct channel_gk20a *c,
	u32 id, u32 thresh, struct priv_cmd_entry *wait_cmd,
	u32 wait_cmd_size, u32 pos, bool preallocated)
{
	int err = 0;
	bool is_expired = nvgpu_nvhost_syncpt_is_expired_ext(
		c->g->nvhost_dev, id, thresh);

	if (is_expired) {
		if (preallocated) {
			nvgpu_memset(c->g, wait_cmd->mem,
			(wait_cmd->off + pos * wait_cmd_size) * (u32)sizeof(u32),
				0, wait_cmd_size * (u32)sizeof(u32));
		}
	} else {
		if (!preallocated) {
			err = gk20a_channel_alloc_priv_cmdbuf(c,
				c->g->ops.sync.get_syncpt_wait_cmd_size(),
				wait_cmd);
			if (err != 0) {
				nvgpu_err(c->g, "not enough priv cmd buffer space");
				return err;
			}
		}
		nvgpu_log(c->g, gpu_dbg_info, "sp->id %d gpu va %llx",
				id, c->vm->syncpt_ro_map_gpu_va);
		c->g->ops.sync.add_syncpt_wait_cmd(c->g, wait_cmd,
			pos * wait_cmd_size, id, thresh,
			c->vm->syncpt_ro_map_gpu_va);
	}

	return 0;
}

static int channel_sync_syncpt_wait_raw(struct nvgpu_channel_sync_syncpt *s,
		u32 id, u32 thresh, struct priv_cmd_entry *wait_cmd)
{
	struct channel_gk20a *c = s->c;
	int err = 0;
	u32 wait_cmd_size = c->g->ops.sync.get_syncpt_wait_cmd_size();

	if (!nvgpu_nvhost_syncpt_is_valid_pt_ext(s->nvhost_dev, id)) {
		return -EINVAL;
	}

	err = channel_sync_syncpt_gen_wait_cmd(c, id, thresh,
			wait_cmd, wait_cmd_size, 0, false);

	return err;
}

static int channel_sync_syncpt_wait_fd(struct nvgpu_channel_sync *s, int fd,
	struct priv_cmd_entry *wait_cmd, u32 max_wait_cmds)
{
	struct nvgpu_os_fence os_fence = {0};
	struct nvgpu_os_fence_syncpt os_fence_syncpt = {0};
	struct nvgpu_channel_sync_syncpt *sp =
		nvgpu_channel_sync_syncpt_from_ops(s);
	struct channel_gk20a *c = sp->c;
	int err = 0;
	u32 i, num_fences, wait_cmd_size;
	u32 syncpt_id = 0U;
	u32 syncpt_thresh = 0U;

	err = nvgpu_os_fence_fdget(&os_fence, c, fd);
	if (err != 0) {
		return -EINVAL;
	}

	err = nvgpu_os_fence_get_syncpts(&os_fence_syncpt, &os_fence);
	if (err != 0) {
		goto cleanup;
	}

	num_fences = nvgpu_os_fence_syncpt_get_num_syncpoints(&os_fence_syncpt);

	if (num_fences == 0U) {
		goto cleanup;
	}

	if ((max_wait_cmds != 0U) && (num_fences > max_wait_cmds)) {
		err = -EINVAL;
		goto cleanup;
	}

	for (i = 0; i < num_fences; i++) {
		nvgpu_os_fence_syncpt_extract_nth_syncpt(
			&os_fence_syncpt, i, &syncpt_id, &syncpt_thresh);
		if ((syncpt_id == 0U) || !nvgpu_nvhost_syncpt_is_valid_pt_ext(
			c->g->nvhost_dev, syncpt_id)) {
				err = -EINVAL;
				goto cleanup;
		}
	}

	wait_cmd_size = c->g->ops.sync.get_syncpt_wait_cmd_size();
	err = gk20a_channel_alloc_priv_cmdbuf(c,
		wait_cmd_size * num_fences, wait_cmd);
	if (err != 0) {
		nvgpu_err(c->g, "not enough priv cmd buffer space");
		err = -EINVAL;
		goto cleanup;
	}

	for (i = 0; i < num_fences; i++) {
		nvgpu_os_fence_syncpt_extract_nth_syncpt(
			&os_fence_syncpt, i, &syncpt_id, &syncpt_thresh);
		err = channel_sync_syncpt_gen_wait_cmd(c, syncpt_id,
			syncpt_thresh, wait_cmd, wait_cmd_size, i, true);
	}

cleanup:
	os_fence.ops->drop_ref(&os_fence);
	return err;
}

static void channel_sync_syncpt_update(void *priv, int nr_completed)
{
	struct channel_gk20a *ch = priv;

	gk20a_channel_update(ch);

	/* note: channel_get() is in channel_sync_syncpt_incr_common() */
	gk20a_channel_put(ch);
}

static int channel_sync_syncpt_incr_common(struct nvgpu_channel_sync *s,
				       bool wfi_cmd,
				       bool register_irq,
				       struct priv_cmd_entry *incr_cmd,
				       struct nvgpu_fence_type *fence,
				       bool need_sync_fence)
{
	u32 thresh;
	int err;
	struct nvgpu_channel_sync_syncpt *sp =
		nvgpu_channel_sync_syncpt_from_ops(s);
	struct channel_gk20a *c = sp->c;
	struct nvgpu_os_fence os_fence = {0};

	err = gk20a_channel_alloc_priv_cmdbuf(c,
			c->g->ops.sync.get_syncpt_incr_cmd_size(wfi_cmd),
			incr_cmd);
	if (err != 0) {
		return err;
	}

	nvgpu_log(c->g, gpu_dbg_info, "sp->id %d gpu va %llx",
				sp->id, sp->syncpt_buf.gpu_va);
	c->g->ops.sync.add_syncpt_incr_cmd(c->g, wfi_cmd,
			incr_cmd, sp->id, sp->syncpt_buf.gpu_va);

	thresh = nvgpu_nvhost_syncpt_incr_max_ext(sp->nvhost_dev, sp->id,
			c->g->ops.sync.get_syncpt_incr_per_release());

	if (register_irq) {
		struct channel_gk20a *referenced = gk20a_channel_get(c);

		WARN_ON(!referenced);

		if (referenced) {
			/* note: channel_put() is in
			 * channel_sync_syncpt_update() */

			err = nvgpu_nvhost_intr_register_notifier(
				sp->nvhost_dev,
				sp->id, thresh,
				channel_sync_syncpt_update, c);
			if (err != 0) {
				gk20a_channel_put(referenced);
			}

			/* Adding interrupt action should
			 * never fail. A proper error handling
			 * here would require us to decrement
			 * the syncpt max back to its original
			 * value. */
			WARN(err,
			     "failed to set submit complete interrupt");
		}
	}

	if (need_sync_fence) {
		err = nvgpu_os_fence_syncpt_create(&os_fence, c, sp->nvhost_dev,
			sp->id, thresh);

		if (err != 0) {
			goto clean_up_priv_cmd;
		}
	}

	err = nvgpu_fence_from_syncpt(fence, sp->nvhost_dev,
	 sp->id, thresh, os_fence);

	if (err != 0) {
		if (nvgpu_os_fence_is_initialized(&os_fence) != 0) {
			os_fence.ops->drop_ref(&os_fence);
		}
		goto clean_up_priv_cmd;
	}

	return 0;

clean_up_priv_cmd:
	gk20a_free_priv_cmdbuf(c, incr_cmd);
	return err;
}

static int channel_sync_syncpt_incr(struct nvgpu_channel_sync *s,
			      struct priv_cmd_entry *entry,
			      struct nvgpu_fence_type *fence,
			      bool need_sync_fence,
			      bool register_irq)
{
	/* Don't put wfi cmd to this one since we're not returning
	 * a fence to user space. */
	return channel_sync_syncpt_incr_common(s,
			false /* no wfi */,
			register_irq /* register irq */,
			entry, fence, need_sync_fence);
}

static int channel_sync_syncpt_incr_user(struct nvgpu_channel_sync *s,
				   int wait_fence_fd,
				   struct priv_cmd_entry *entry,
				   struct nvgpu_fence_type *fence,
				   bool wfi,
				   bool need_sync_fence,
				   bool register_irq)
{
	/* Need to do 'wfi + host incr' since we return the fence
	 * to user space. */
	return channel_sync_syncpt_incr_common(s,
			wfi,
			register_irq /* register irq */,
			entry, fence, need_sync_fence);
}

static void channel_sync_syncpt_set_min_eq_max(struct nvgpu_channel_sync *s)
{
	struct nvgpu_channel_sync_syncpt *sp =
		nvgpu_channel_sync_syncpt_from_ops(s);
	nvgpu_nvhost_syncpt_set_min_eq_max_ext(sp->nvhost_dev, sp->id);
}

static void channel_sync_syncpt_set_safe_state(struct nvgpu_channel_sync *s)
{
	struct nvgpu_channel_sync_syncpt *sp =
		nvgpu_channel_sync_syncpt_from_ops(s);
	nvgpu_nvhost_syncpt_set_safe_state(sp->nvhost_dev, sp->id);
}

static u32 channel_sync_syncpt_get_id(struct nvgpu_channel_sync_syncpt *sp)
{
	return sp->id;
}

static u64 channel_sync_syncpt_get_address(struct nvgpu_channel_sync_syncpt *sp)
{
	return sp->syncpt_buf.gpu_va;
}

static void channel_sync_syncpt_destroy(struct nvgpu_channel_sync *s)
{
	struct nvgpu_channel_sync_syncpt *sp =
		nvgpu_channel_sync_syncpt_from_ops(s);


	sp->c->g->ops.sync.free_syncpt_buf(sp->c, &sp->syncpt_buf);

	nvgpu_nvhost_syncpt_set_min_eq_max_ext(sp->nvhost_dev, sp->id);
	nvgpu_nvhost_syncpt_put_ref_ext(sp->nvhost_dev, sp->id);
	nvgpu_kfree(sp->c->g, sp);
}

u32 nvgpu_channel_sync_get_syncpt_id(struct nvgpu_channel_sync_syncpt *s)
{
	return channel_sync_syncpt_get_id(s);
}

u64 nvgpu_channel_sync_get_syncpt_address(struct nvgpu_channel_sync_syncpt *s)
{
	return channel_sync_syncpt_get_address(s);
}

int nvgpu_channel_sync_wait_syncpt(struct nvgpu_channel_sync_syncpt *s,
	u32 id, u32 thresh, struct priv_cmd_entry *entry)
{
	return channel_sync_syncpt_wait_raw(s, id, thresh, entry);
}

struct nvgpu_channel_sync_syncpt *
nvgpu_channel_sync_to_syncpt(struct nvgpu_channel_sync *sync)
{
	struct nvgpu_channel_sync_syncpt *syncpt = NULL;

	if (sync->wait_fence_fd == channel_sync_syncpt_wait_fd) {
		syncpt = nvgpu_channel_sync_syncpt_from_ops(sync);
	}

	return syncpt;
}

struct nvgpu_channel_sync *
nvgpu_channel_sync_syncpt_create(struct channel_gk20a *c, bool user_managed)
{
	struct nvgpu_channel_sync_syncpt *sp;
	char syncpt_name[32];

	sp = nvgpu_kzalloc(c->g, sizeof(*sp));
	if (sp == NULL) {
		return NULL;
	}

	sp->c = c;
	sp->nvhost_dev = c->g->nvhost_dev;

	if (user_managed) {
		snprintf(syncpt_name, sizeof(syncpt_name),
			"%s_%d_user", c->g->name, c->chid);

		sp->id = nvgpu_nvhost_get_syncpt_client_managed(sp->nvhost_dev,
						syncpt_name);
	} else {
		snprintf(syncpt_name, sizeof(syncpt_name),
			"%s_%d", c->g->name, c->chid);

		sp->id = nvgpu_nvhost_get_syncpt_host_managed(sp->nvhost_dev,
						c->chid, syncpt_name);
	}
	if (sp->id == 0) {
		nvgpu_kfree(c->g, sp);
		nvgpu_err(c->g, "failed to get free syncpt");
		return NULL;
	}

	sp->c->g->ops.sync.alloc_syncpt_buf(sp->c, sp->id,
				&sp->syncpt_buf);

	nvgpu_nvhost_syncpt_set_min_eq_max_ext(sp->nvhost_dev, sp->id);

	nvgpu_atomic_set(&sp->ops.refcount, 0);
	sp->ops.wait_fence_fd		= channel_sync_syncpt_wait_fd;
	sp->ops.incr			= channel_sync_syncpt_incr;
	sp->ops.incr_user		= channel_sync_syncpt_incr_user;
	sp->ops.set_min_eq_max		= channel_sync_syncpt_set_min_eq_max;
	sp->ops.set_safe_state		= channel_sync_syncpt_set_safe_state;
	sp->ops.destroy			= channel_sync_syncpt_destroy;

	return &sp->ops;
}


