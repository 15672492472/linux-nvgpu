/*
 * GK20A Channel Synchronization Abstraction
 *
 * Copyright (c) 2014-2018, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/semaphore.h>
#include <nvgpu/kmem.h>
#include <nvgpu/log.h>
#include <nvgpu/atomic.h>
#include <nvgpu/bug.h>
#include <nvgpu/list.h>
#include <nvgpu/nvhost.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/os_fence.h>
#include <nvgpu/os_fence_semas.h>
#include <nvgpu/channel.h>
#include <nvgpu/channel_sync.h>
#include <nvgpu/channel_sync_semaphore.h>

#include "channel_sync_priv.h"
#include "gk20a/fence_gk20a.h"
#include "gk20a/mm_gk20a.h"

struct nvgpu_channel_sync_semaphore {
	struct nvgpu_channel_sync ops;
	struct channel_gk20a *c;

	/* A semaphore pool owned by this channel. */
	struct nvgpu_semaphore_pool *pool;
};


static void add_sema_cmd(struct gk20a *g, struct channel_gk20a *c,
			 struct nvgpu_semaphore *s, struct priv_cmd_entry *cmd,
			 u32 offset, bool acquire, bool wfi)
{
	int ch = c->chid;
	u32 ob, off = cmd->off + offset;
	u64 va;

	ob = off;

	/*
	 * RO for acquire (since we just need to read the mem) and RW for
	 * release since we will need to write back to the semaphore memory.
	 */
	va = acquire ? nvgpu_semaphore_gpu_ro_va(s) :
		       nvgpu_semaphore_gpu_rw_va(s);

	/*
	 * If the op is not an acquire (so therefor a release) we should
	 * incr the underlying sema next_value.
	 */
	if (!acquire) {
		nvgpu_semaphore_prepare(s, c->hw_sema);
	}

	g->ops.fifo.add_sema_cmd(g, s, va, cmd, off, acquire, wfi);

	if (acquire) {
		gpu_sema_verbose_dbg(g, "(A) c=%d ACQ_GE %-4u pool=%-3llu"
				     "va=0x%llx cmd_mem=0x%llx b=0x%llx off=%u",
				     ch, nvgpu_semaphore_get_value(s),
				     s->location.pool->page_idx, va, cmd->gva,
				     cmd->mem->gpu_va, ob);
	} else {
		gpu_sema_verbose_dbg(g, "(R) c=%d INCR %u (%u) pool=%-3llu"
				     "va=0x%llx cmd_mem=0x%llx b=0x%llx off=%u",
				     ch, nvgpu_semaphore_get_value(s),
				     nvgpu_semaphore_read(s),
				     s->location.pool->page_idx,
				     va, cmd->gva, cmd->mem->gpu_va, ob);
	}
}

static void channel_sync_semaphore_gen_wait_cmd(struct channel_gk20a *c,
	struct nvgpu_semaphore *sema, struct priv_cmd_entry *wait_cmd,
	u32 wait_cmd_size, u32 pos)
{
	if (sema == NULL) {
		/* expired */
		nvgpu_memset(c->g, wait_cmd->mem,
			(wait_cmd->off + pos * wait_cmd_size) * (u32)sizeof(u32),
			0, wait_cmd_size * (u32)sizeof(u32));
	} else {
		WARN_ON(!sema->incremented);
		add_sema_cmd(c->g, c, sema, wait_cmd,
			pos * wait_cmd_size, true, false);
		nvgpu_semaphore_put(sema);
	}
}

static int channel_sync_semaphore_wait_fd(
		struct nvgpu_channel_sync *s, int fd,
		struct priv_cmd_entry *entry, u32 max_wait_cmds)
{
	struct nvgpu_channel_sync_semaphore *sema =
		container_of(s, struct nvgpu_channel_sync_semaphore, ops);
	struct channel_gk20a *c = sema->c;

	struct nvgpu_os_fence os_fence = {0};
	struct nvgpu_os_fence_sema os_fence_sema = {0};
	int err;
	u32 wait_cmd_size, i, num_fences;
	struct nvgpu_semaphore *semaphore = NULL;

	err = nvgpu_os_fence_fdget(&os_fence, c, fd);
	if (err != 0) {
		return err;
	}

	err = nvgpu_os_fence_get_semas(&os_fence_sema, &os_fence);
	if (err != 0) {
		goto cleanup;
	}

	num_fences = nvgpu_os_fence_sema_get_num_semaphores(&os_fence_sema);

	if (num_fences == 0U) {
		goto cleanup;
	}

	if ((max_wait_cmds != 0U) && (num_fences > max_wait_cmds)) {
		err = -EINVAL;
		goto cleanup;
	}

	wait_cmd_size = c->g->ops.fifo.get_sema_wait_cmd_size();
	err = gk20a_channel_alloc_priv_cmdbuf(c,
		wait_cmd_size * num_fences, entry);
	if (err != 0) {
		nvgpu_err(c->g, "not enough priv cmd buffer space");
		goto cleanup;
	}

	for (i = 0; i < num_fences; i++) {
		nvgpu_os_fence_sema_extract_nth_semaphore(
			&os_fence_sema, i, &semaphore);
		channel_sync_semaphore_gen_wait_cmd(c, semaphore, entry,
				wait_cmd_size, i);
	}

cleanup:
	os_fence.ops->drop_ref(&os_fence);
	return err;
}

static int channel_sync_semaphore_incr_common(
		struct nvgpu_channel_sync *s, bool wfi_cmd,
		struct priv_cmd_entry *incr_cmd,
		struct gk20a_fence *fence,
		bool need_sync_fence)
{
	u32 incr_cmd_size;
	struct nvgpu_channel_sync_semaphore *sp =
		container_of(s, struct nvgpu_channel_sync_semaphore, ops);
	struct channel_gk20a *c = sp->c;
	struct nvgpu_semaphore *semaphore;
	int err = 0;
	struct nvgpu_os_fence os_fence = {0};

	semaphore = nvgpu_semaphore_alloc(c);
	if (semaphore == NULL) {
		nvgpu_err(c->g,
				"ran out of semaphores");
		return -ENOMEM;
	}

	incr_cmd_size = c->g->ops.fifo.get_sema_incr_cmd_size();
	err = gk20a_channel_alloc_priv_cmdbuf(c, incr_cmd_size, incr_cmd);
	if (err) {
		nvgpu_err(c->g,
				"not enough priv cmd buffer space");
		goto clean_up_sema;
	}

	/* Release the completion semaphore. */
	add_sema_cmd(c->g, c, semaphore, incr_cmd, 0, false, wfi_cmd);

	if (need_sync_fence) {
		err = nvgpu_os_fence_sema_create(&os_fence, c,
			semaphore);

		if (err) {
			goto clean_up_sema;
		}
	}

	err = gk20a_fence_from_semaphore(fence,
		semaphore,
		&c->semaphore_wq,
		os_fence);

	if (err != 0) {
		if (nvgpu_os_fence_is_initialized(&os_fence)) {
			os_fence.ops->drop_ref(&os_fence);
		}
		goto clean_up_sema;
	}

	return 0;

clean_up_sema:
	nvgpu_semaphore_put(semaphore);
	return err;
}

static int channel_sync_semaphore_incr(
		struct nvgpu_channel_sync *s,
		struct priv_cmd_entry *entry,
		struct gk20a_fence *fence,
		bool need_sync_fence,
		bool register_irq)
{
	/* Don't put wfi cmd to this one since we're not returning
	 * a fence to user space. */
	return channel_sync_semaphore_incr_common(s,
			false /* no wfi */,
			entry, fence, need_sync_fence);
}

static int channel_sync_semaphore_incr_user(
		struct nvgpu_channel_sync *s,
		int wait_fence_fd,
		struct priv_cmd_entry *entry,
		struct gk20a_fence *fence,
		bool wfi,
		bool need_sync_fence,
		bool register_irq)
{
#ifdef CONFIG_SYNC
	int err;

	err = channel_sync_semaphore_incr_common(s, wfi, entry, fence,
			need_sync_fence);
	if (err != 0) {
		return err;
	}

	return 0;
#else
	struct nvgpu_channel_sync_semaphore *sema =
		container_of(s, struct nvgpu_channel_sync_semaphore, ops);
	nvgpu_err(sema->c->g,
		  "trying to use sync fds with CONFIG_SYNC disabled");
	return -ENODEV;
#endif
}

static void channel_sync_semaphore_set_min_eq_max(struct nvgpu_channel_sync *s)
{
	struct nvgpu_channel_sync_semaphore *sp =
		container_of(s, struct nvgpu_channel_sync_semaphore, ops);
	struct channel_gk20a *c = sp->c;
	bool updated;

	if (c->hw_sema == NULL) {
		return;
	}

	updated = nvgpu_semaphore_reset(c->hw_sema);

	if (updated) {
		nvgpu_cond_broadcast_interruptible(&c->semaphore_wq);
	}
}

static void channel_sync_semaphore_set_safe_state(struct nvgpu_channel_sync *s)
{
	/* Nothing to do. */
}

static void channel_sync_semaphore_destroy(struct nvgpu_channel_sync *s)
{
	struct nvgpu_channel_sync_semaphore *sema =
		container_of(s, struct nvgpu_channel_sync_semaphore, ops);

	struct channel_gk20a *c = sema->c;
	struct gk20a *g = c->g;

	if (c->has_os_fence_framework_support &&
		g->os_channel.os_fence_framework_inst_exists(c)) {
			g->os_channel.destroy_os_fence_framework(c);
	}

	/* The sema pool is cleaned up by the VM destroy. */
	sema->pool = NULL;

	nvgpu_kfree(sema->c->g, sema);
}

/* Converts a valid struct nvgpu_channel_sync ptr to
 * struct nvgpu_channel_sync_syncpt ptr else return NULL.
 */
struct nvgpu_channel_sync_semaphore *
	nvgpu_channel_sync_to_semaphore(struct nvgpu_channel_sync *sync)
{
	struct nvgpu_channel_sync_semaphore *sema = NULL;
	if (sync->wait_fence_fd == channel_sync_semaphore_wait_fd) {
		sema = container_of(sync, struct nvgpu_channel_sync_semaphore, ops);
	}

	return sema;
}

struct nvgpu_channel_sync *
nvgpu_channel_sync_semaphore_create(
	struct channel_gk20a *c, bool user_managed)
{
	struct nvgpu_channel_sync_semaphore *sema;
	struct gk20a *g = c->g;
	char pool_name[20];
	int asid = -1;
	int err;

	if (WARN_ON(c->vm == NULL)) {
		return NULL;
	}

	sema = nvgpu_kzalloc(c->g, sizeof(*sema));
	if (sema == NULL) {
		return NULL;
	}
	sema->c = c;

	sprintf(pool_name, "semaphore_pool-%d", c->chid);
	sema->pool = c->vm->sema_pool;

	if (c->vm->as_share != NULL) {
		asid = c->vm->as_share->id;
	}

	if (c->has_os_fence_framework_support) {
		/*Init the sync_timeline for this channel */
		err = g->os_channel.init_os_fence_framework(c,
			"gk20a_ch%d_as%d", c->chid, asid);

		if (err != 0) {
			nvgpu_kfree(g, sema);
			return NULL;
		}
	}

	nvgpu_atomic_set(&sema->ops.refcount, 0);
	sema->ops.wait_fence_fd	= channel_sync_semaphore_wait_fd;
	sema->ops.incr		= channel_sync_semaphore_incr;
	sema->ops.incr_user	= channel_sync_semaphore_incr_user;
	sema->ops.set_min_eq_max = channel_sync_semaphore_set_min_eq_max;
	sema->ops.set_safe_state = channel_sync_semaphore_set_safe_state;
	sema->ops.destroy	= channel_sync_semaphore_destroy;

	return &sema->ops;
}