/*
 * Copyright (c) 2017, NVIDIA Corporation.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <nvgpu/enabled.h>
#include <nvgpu/debug.h>
#include <nvgpu/ltc.h>

/*
 * This is required for nvgpu_vm_find_buf() which is used in the tracing
 * code. Once we can get and access userspace buffers without requiring
 * direct dma_buf usage this can be removed.
 */
#include <nvgpu/linux/vm.h>

#include "gk20a/gk20a.h"

#include "channel.h"
#include "os_linux.h"

#include <nvgpu/hw/gk20a/hw_pbdma_gk20a.h>

#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <trace/events/gk20a.h>
#include <uapi/linux/nvgpu.h>

static void gk20a_channel_update_runcb_fn(struct work_struct *work)
{
	struct nvgpu_channel_completion_cb *completion_cb =
		container_of(work, struct nvgpu_channel_completion_cb, work);
	struct nvgpu_channel_linux *priv =
		container_of(completion_cb,
				struct nvgpu_channel_linux, completion_cb);
	struct channel_gk20a *ch = priv->ch;
	void (*fn)(struct channel_gk20a *, void *);
	void *user_data;

	nvgpu_spinlock_acquire(&completion_cb->lock);
	fn = completion_cb->fn;
	user_data = completion_cb->user_data;
	nvgpu_spinlock_release(&completion_cb->lock);

	if (fn)
		fn(ch, user_data);
}

static void nvgpu_channel_work_completion_init(struct channel_gk20a *ch)
{
	struct nvgpu_channel_linux *priv = ch->os_priv;

	priv->completion_cb.fn = NULL;
	priv->completion_cb.user_data = NULL;
	nvgpu_spinlock_init(&priv->completion_cb.lock);
	INIT_WORK(&priv->completion_cb.work, gk20a_channel_update_runcb_fn);
}

static void nvgpu_channel_work_completion_clear(struct channel_gk20a *ch)
{
	struct nvgpu_channel_linux *priv = ch->os_priv;

	nvgpu_spinlock_acquire(&priv->completion_cb.lock);
	priv->completion_cb.fn = NULL;
	priv->completion_cb.user_data = NULL;
	nvgpu_spinlock_release(&priv->completion_cb.lock);
	cancel_work_sync(&priv->completion_cb.work);
}

static void nvgpu_channel_work_completion_signal(struct channel_gk20a *ch)
{
	struct nvgpu_channel_linux *priv = ch->os_priv;

	if (priv->completion_cb.fn)
		schedule_work(&priv->completion_cb.work);
}

static void nvgpu_channel_work_completion_cancel_sync(struct channel_gk20a *ch)
{
	struct nvgpu_channel_linux *priv = ch->os_priv;

	if (priv->completion_cb.fn)
		cancel_work_sync(&priv->completion_cb.work);
}

struct channel_gk20a *gk20a_open_new_channel_with_cb(struct gk20a *g,
		void (*update_fn)(struct channel_gk20a *, void *),
		void *update_fn_data,
		int runlist_id,
		bool is_privileged_channel)
{
	struct channel_gk20a *ch;
	struct nvgpu_channel_linux *priv;

	ch = gk20a_open_new_channel(g, runlist_id, is_privileged_channel);

	if (ch) {
		priv = ch->os_priv;
		nvgpu_spinlock_acquire(&priv->completion_cb.lock);
		priv->completion_cb.fn = update_fn;
		priv->completion_cb.user_data = update_fn_data;
		nvgpu_spinlock_release(&priv->completion_cb.lock);
	}

	return ch;
}

static void nvgpu_channel_open_linux(struct channel_gk20a *ch)
{
}

static void nvgpu_channel_close_linux(struct channel_gk20a *ch)
{
	nvgpu_channel_work_completion_clear(ch);
}

static int nvgpu_channel_alloc_linux(struct gk20a *g, struct channel_gk20a *ch)
{
	struct nvgpu_channel_linux *priv;

	priv = nvgpu_kzalloc(g, sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	ch->os_priv = priv;
	priv->ch = ch;

	nvgpu_channel_work_completion_init(ch);

	return 0;
}

static void nvgpu_channel_free_linux(struct gk20a *g, struct channel_gk20a *ch)
{
	nvgpu_kfree(g, ch->os_priv);
}

int nvgpu_init_channel_support_linux(struct nvgpu_os_linux *l)
{
	struct gk20a *g = &l->g;
	struct fifo_gk20a *f = &g->fifo;
	int chid;
	int err;

	for (chid = 0; chid < (int)f->num_channels; chid++) {
		struct channel_gk20a *ch = &f->channel[chid];

		err = nvgpu_channel_alloc_linux(g, ch);
		if (err)
			goto err_clean;
	}

	g->os_channel.open = nvgpu_channel_open_linux;
	g->os_channel.close = nvgpu_channel_close_linux;
	g->os_channel.work_completion_signal =
		nvgpu_channel_work_completion_signal;
	g->os_channel.work_completion_cancel_sync =
		nvgpu_channel_work_completion_cancel_sync;
	return 0;

err_clean:
	for (; chid >= 0; chid--) {
		struct channel_gk20a *ch = &f->channel[chid];

		nvgpu_channel_free_linux(g, ch);
	}
	return err;
}

void nvgpu_remove_channel_support_linux(struct nvgpu_os_linux *l)
{
	struct gk20a *g = &l->g;
	struct fifo_gk20a *f = &g->fifo;
	unsigned int chid;

	for (chid = 0; chid < f->num_channels; chid++) {
		struct channel_gk20a *ch = &f->channel[chid];

		nvgpu_channel_free_linux(g, ch);
	}
}

u32 nvgpu_get_gpfifo_entry_size(void)
{
	return sizeof(struct nvgpu_gpfifo);
}

#ifdef CONFIG_DEBUG_FS
static void trace_write_pushbuffer(struct channel_gk20a *c,
				   struct nvgpu_gpfifo *g)
{
	void *mem = NULL;
	unsigned int words;
	u64 offset;
	struct dma_buf *dmabuf = NULL;

	if (gk20a_debug_trace_cmdbuf) {
		u64 gpu_va = (u64)g->entry0 |
			(u64)((u64)pbdma_gp_entry1_get_hi_v(g->entry1) << 32);
		int err;

		words = pbdma_gp_entry1_length_v(g->entry1);
		err = nvgpu_vm_find_buf(c->vm, gpu_va, &dmabuf, &offset);
		if (!err)
			mem = dma_buf_vmap(dmabuf);
	}

	if (mem) {
		u32 i;
		/*
		 * Write in batches of 128 as there seems to be a limit
		 * of how much you can output to ftrace at once.
		 */
		for (i = 0; i < words; i += 128U) {
			trace_gk20a_push_cmdbuf(
				c->g->name,
				0,
				min(words - i, 128U),
				offset + i * sizeof(u32),
				mem);
		}
		dma_buf_vunmap(dmabuf, mem);
	}
}
#endif

static void trace_write_pushbuffer_range(struct channel_gk20a *c,
					 struct nvgpu_gpfifo *g,
					 struct nvgpu_gpfifo __user *user_gpfifo,
					 int offset,
					 int count)
{
#ifdef CONFIG_DEBUG_FS
	u32 size;
	int i;
	struct nvgpu_gpfifo *gp;
	bool gpfifo_allocated = false;

	if (!gk20a_debug_trace_cmdbuf)
		return;

	if (!g && !user_gpfifo)
		return;

	if (!g) {
		size = count * sizeof(struct nvgpu_gpfifo);
		if (size) {
			g = nvgpu_big_malloc(c->g, size);
			if (!g)
				return;

			if (copy_from_user(g, user_gpfifo, size)) {
				nvgpu_big_free(c->g, g);
				return;
			}
		}
		gpfifo_allocated = true;
	}

	gp = g + offset;
	for (i = 0; i < count; i++, gp++)
		trace_write_pushbuffer(c, gp);

	if (gpfifo_allocated)
		nvgpu_big_free(c->g, g);
#endif
}

/*
 * Handle the submit synchronization - pre-fences and post-fences.
 */
static int gk20a_submit_prepare_syncs(struct channel_gk20a *c,
				      struct nvgpu_fence *fence,
				      struct channel_gk20a_job *job,
				      struct priv_cmd_entry **wait_cmd,
				      struct priv_cmd_entry **incr_cmd,
				      struct gk20a_fence **pre_fence,
				      struct gk20a_fence **post_fence,
				      bool force_need_sync_fence,
				      bool register_irq,
				      u32 flags)
{
	struct gk20a *g = c->g;
	bool need_sync_fence = false;
	bool new_sync_created = false;
	int wait_fence_fd = -1;
	int err = 0;
	bool need_wfi = !(flags & NVGPU_SUBMIT_GPFIFO_FLAGS_SUPPRESS_WFI);
	bool pre_alloc_enabled = channel_gk20a_is_prealloc_enabled(c);

	/*
	 * If user wants to always allocate sync_fence_fds then respect that;
	 * otherwise, allocate sync_fence_fd based on user flags.
	 */
	if (force_need_sync_fence)
		need_sync_fence = true;

	if (g->aggressive_sync_destroy_thresh) {
		nvgpu_mutex_acquire(&c->sync_lock);
		if (!c->sync) {
			c->sync = gk20a_channel_sync_create(c);
			if (!c->sync) {
				err = -ENOMEM;
				nvgpu_mutex_release(&c->sync_lock);
				goto fail;
			}
			new_sync_created = true;
		}
		nvgpu_atomic_inc(&c->sync->refcount);
		nvgpu_mutex_release(&c->sync_lock);
	}

	if (g->ops.fifo.resetup_ramfc && new_sync_created) {
		err = g->ops.fifo.resetup_ramfc(c);
		if (err)
			goto fail;
	}

	/*
	 * Optionally insert syncpt wait in the beginning of gpfifo submission
	 * when user requested and the wait hasn't expired. Validate that the id
	 * makes sense, elide if not. The only reason this isn't being
	 * unceremoniously killed is to keep running some tests which trigger
	 * this condition.
	 */
	if (flags & NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_WAIT) {
		job->pre_fence = gk20a_alloc_fence(c);
		if (!job->pre_fence) {
			err = -ENOMEM;
			goto fail;
		}

		if (!pre_alloc_enabled)
			job->wait_cmd = nvgpu_kzalloc(g,
				sizeof(struct priv_cmd_entry));

		if (!job->wait_cmd) {
			err = -ENOMEM;
			goto clean_up_pre_fence;
		}

		if (flags & NVGPU_SUBMIT_GPFIFO_FLAGS_SYNC_FENCE) {
			wait_fence_fd = fence->id;
			err = c->sync->wait_fd(c->sync, wait_fence_fd,
					       job->wait_cmd, job->pre_fence);
		} else {
			err = c->sync->wait_syncpt(c->sync, fence->id,
						   fence->value, job->wait_cmd,
						   job->pre_fence);
		}

		if (!err) {
			if (job->wait_cmd->valid)
				*wait_cmd = job->wait_cmd;
			*pre_fence = job->pre_fence;
		} else
			goto clean_up_wait_cmd;
	}

	if ((flags & NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_GET) &&
	    (flags & NVGPU_SUBMIT_GPFIFO_FLAGS_SYNC_FENCE))
		need_sync_fence = true;

	/*
	 * Always generate an increment at the end of a GPFIFO submission. This
	 * is used to keep track of method completion for idle railgating. The
	 * sync_pt/semaphore PB is added to the GPFIFO later on in submit.
	 */
	job->post_fence = gk20a_alloc_fence(c);
	if (!job->post_fence) {
		err = -ENOMEM;
		goto clean_up_wait_cmd;
	}
	if (!pre_alloc_enabled)
		job->incr_cmd = nvgpu_kzalloc(g, sizeof(struct priv_cmd_entry));

	if (!job->incr_cmd) {
		err = -ENOMEM;
		goto clean_up_post_fence;
	}

	if (flags & NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_GET)
		err = c->sync->incr_user(c->sync, wait_fence_fd, job->incr_cmd,
				 job->post_fence, need_wfi, need_sync_fence,
				 register_irq);
	else
		err = c->sync->incr(c->sync, job->incr_cmd,
				    job->post_fence, need_sync_fence,
				    register_irq);
	if (!err) {
		*incr_cmd = job->incr_cmd;
		*post_fence = job->post_fence;
	} else
		goto clean_up_incr_cmd;

	return 0;

clean_up_incr_cmd:
	free_priv_cmdbuf(c, job->incr_cmd);
	if (!pre_alloc_enabled)
		job->incr_cmd = NULL;
clean_up_post_fence:
	gk20a_fence_put(job->post_fence);
	job->post_fence = NULL;
clean_up_wait_cmd:
	free_priv_cmdbuf(c, job->wait_cmd);
	if (!pre_alloc_enabled)
		job->wait_cmd = NULL;
clean_up_pre_fence:
	gk20a_fence_put(job->pre_fence);
	job->pre_fence = NULL;
fail:
	*wait_cmd = NULL;
	*pre_fence = NULL;
	return err;
}

static void gk20a_submit_append_priv_cmdbuf(struct channel_gk20a *c,
		struct priv_cmd_entry *cmd)
{
	struct gk20a *g = c->g;
	struct nvgpu_mem *gpfifo_mem = &c->gpfifo.mem;
	struct nvgpu_gpfifo x = {
		.entry0 = u64_lo32(cmd->gva),
		.entry1 = u64_hi32(cmd->gva) |
			pbdma_gp_entry1_length_f(cmd->size)
	};

	nvgpu_mem_wr_n(g, gpfifo_mem, c->gpfifo.put * sizeof(x),
			&x, sizeof(x));

	if (cmd->mem->aperture == APERTURE_SYSMEM)
		trace_gk20a_push_cmdbuf(g->name, 0, cmd->size, 0,
				cmd->mem->cpu_va + cmd->off * sizeof(u32));

	c->gpfifo.put = (c->gpfifo.put + 1) & (c->gpfifo.entry_num - 1);
}

/*
 * Copy source gpfifo entries into the gpfifo ring buffer, potentially
 * splitting into two memcpys to handle wrap-around.
 */
static int gk20a_submit_append_gpfifo(struct channel_gk20a *c,
		struct nvgpu_gpfifo *kern_gpfifo,
		struct nvgpu_gpfifo __user *user_gpfifo,
		u32 num_entries)
{
	/* byte offsets */
	u32 gpfifo_size = c->gpfifo.entry_num * sizeof(struct nvgpu_gpfifo);
	u32 len = num_entries * sizeof(struct nvgpu_gpfifo);
	u32 start = c->gpfifo.put * sizeof(struct nvgpu_gpfifo);
	u32 end = start + len; /* exclusive */
	struct nvgpu_mem *gpfifo_mem = &c->gpfifo.mem;
	struct nvgpu_gpfifo *cpu_src;
	int err;

	if (user_gpfifo && !c->gpfifo.pipe) {
		/*
		 * This path (from userspace to sysmem) is special in order to
		 * avoid two copies unnecessarily (from user to pipe, then from
		 * pipe to gpu sysmem buffer).
		 *
		 * As a special case, the pipe buffer exists if PRAMIN writes
		 * are forced, although the buffers may not be in vidmem in
		 * that case.
		 */
		if (end > gpfifo_size) {
			/* wrap-around */
			int length0 = gpfifo_size - start;
			int length1 = len - length0;
			void __user *user2 = (u8 __user *)user_gpfifo + length0;

			err = copy_from_user(gpfifo_mem->cpu_va + start,
					user_gpfifo, length0);
			if (err)
				return err;

			err = copy_from_user(gpfifo_mem->cpu_va,
					user2, length1);
			if (err)
				return err;
		} else {
			err = copy_from_user(gpfifo_mem->cpu_va + start,
					user_gpfifo, len);
			if (err)
				return err;
		}

		trace_write_pushbuffer_range(c, NULL, user_gpfifo,
				0, num_entries);
		goto out;
	} else if (user_gpfifo) {
		/* from userspace to vidmem or sysmem when pramin forced, use
		 * the common copy path below */
		err = copy_from_user(c->gpfifo.pipe, user_gpfifo, len);
		if (err)
			return err;

		cpu_src = c->gpfifo.pipe;
	} else {
		/* from kernel to either sysmem or vidmem, don't need
		 * copy_from_user so use the common path below */
		cpu_src = kern_gpfifo;
	}

	if (end > gpfifo_size) {
		/* wrap-around */
		int length0 = gpfifo_size - start;
		int length1 = len - length0;
		void *src2 = (u8 *)cpu_src + length0;

		nvgpu_mem_wr_n(c->g, gpfifo_mem, start, cpu_src, length0);
		nvgpu_mem_wr_n(c->g, gpfifo_mem, 0, src2, length1);
	} else {
		nvgpu_mem_wr_n(c->g, gpfifo_mem, start, cpu_src, len);

	}

	trace_write_pushbuffer_range(c, cpu_src, NULL, 0, num_entries);

out:
	c->gpfifo.put = (c->gpfifo.put + num_entries) &
		(c->gpfifo.entry_num - 1);

	return 0;
}

int gk20a_submit_channel_gpfifo(struct channel_gk20a *c,
				struct nvgpu_gpfifo *gpfifo,
				struct nvgpu_submit_gpfifo_args *args,
				u32 num_entries,
				u32 flags,
				struct nvgpu_fence *fence,
				struct gk20a_fence **fence_out,
				bool force_need_sync_fence,
				struct fifo_profile_gk20a *profile)
{
	struct gk20a *g = c->g;
	struct priv_cmd_entry *wait_cmd = NULL;
	struct priv_cmd_entry *incr_cmd = NULL;
	struct gk20a_fence *pre_fence = NULL;
	struct gk20a_fence *post_fence = NULL;
	struct channel_gk20a_job *job = NULL;
	/* we might need two extra gpfifo entries - one for pre fence
	 * and one for post fence. */
	const int extra_entries = 2;
	bool skip_buffer_refcounting = (flags &
			NVGPU_SUBMIT_GPFIFO_FLAGS_SKIP_BUFFER_REFCOUNTING);
	int err = 0;
	bool need_job_tracking;
	bool need_deferred_cleanup = false;
	struct nvgpu_gpfifo __user *user_gpfifo = args ?
		(struct nvgpu_gpfifo __user *)(uintptr_t)args->gpfifo : NULL;

	if (nvgpu_is_enabled(g, NVGPU_DRIVER_IS_DYING))
		return -ENODEV;

	if (c->has_timedout)
		return -ETIMEDOUT;

	if (!nvgpu_mem_is_valid(&c->gpfifo.mem))
		return -ENOMEM;

	/* fifo not large enough for request. Return error immediately.
	 * Kernel can insert gpfifo entries before and after user gpfifos.
	 * So, add extra_entries in user request. Also, HW with fifo size N
	 * can accept only N-1 entreis and so the below condition */
	if (c->gpfifo.entry_num - 1 < num_entries + extra_entries) {
		nvgpu_err(g, "not enough gpfifo space allocated");
		return -ENOMEM;
	}

	if (!gpfifo && !args)
		return -EINVAL;

	if ((flags & (NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_WAIT |
		      NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_GET)) &&
	    !fence)
		return -EINVAL;

	/* an address space needs to have been bound at this point. */
	if (!gk20a_channel_as_bound(c)) {
		nvgpu_err(g,
			    "not bound to an address space at time of gpfifo"
			    " submission.");
		return -EINVAL;
	}

	if (profile)
		profile->timestamp[PROFILE_ENTRY] = sched_clock();

	/* update debug settings */
	nvgpu_ltc_sync_enabled(g);

	gk20a_dbg_info("channel %d", c->chid);

	/*
	 * Job tracking is necessary for any of the following conditions:
	 *  - pre- or post-fence functionality
	 *  - channel wdt
	 *  - GPU rail-gating with non-deterministic channels
	 *  - buffer refcounting
	 *
	 * If none of the conditions are met, then job tracking is not
	 * required and a fast submit can be done (ie. only need to write
	 * out userspace GPFIFO entries and update GP_PUT).
	 */
	need_job_tracking = (flags & NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_WAIT) ||
			(flags & NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_GET) ||
			c->wdt_enabled ||
			(g->can_railgate && !c->deterministic) ||
			!skip_buffer_refcounting;

	if (need_job_tracking) {
		bool need_sync_framework = false;

		/*
		 * If the channel is to have deterministic latency and
		 * job tracking is required, the channel must have
		 * pre-allocated resources. Otherwise, we fail the submit here
		 */
		if (c->deterministic && !channel_gk20a_is_prealloc_enabled(c))
			return -EINVAL;

		need_sync_framework = force_need_sync_fence ||
			gk20a_channel_sync_needs_sync_framework(g) ||
			(flags & NVGPU_SUBMIT_GPFIFO_FLAGS_SYNC_FENCE &&
			(flags & NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_WAIT ||
			 flags & NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_GET));

		/*
		 * Deferred clean-up is necessary for any of the following
		 * conditions:
		 * - channel's deterministic flag is not set
		 * - dependency on sync framework, which could make the
		 *   behavior of the clean-up operation non-deterministic
		 *   (should not be performed in the submit path)
		 * - channel wdt
		 * - GPU rail-gating with non-deterministic channels
		 * - buffer refcounting
		 *
		 * If none of the conditions are met, then deferred clean-up
		 * is not required, and we clean-up one job-tracking
		 * resource in the submit path.
		 */
		need_deferred_cleanup = !c->deterministic ||
					need_sync_framework ||
					c->wdt_enabled ||
					(g->can_railgate &&
					 !c->deterministic) ||
					!skip_buffer_refcounting;

		/*
		 * For deterministic channels, we don't allow deferred clean_up
		 * processing to occur. In cases we hit this, we fail the submit
		 */
		if (c->deterministic && need_deferred_cleanup)
			return -EINVAL;

		if (!c->deterministic) {
			/*
			 * Get a power ref unless this is a deterministic
			 * channel that holds them during the channel lifetime.
			 * This one is released by gk20a_channel_clean_up_jobs,
			 * via syncpt or sema interrupt, whichever is used.
			 */
			err = gk20a_busy(g);
			if (err) {
				nvgpu_err(g,
					"failed to host gk20a to submit gpfifo, process %s",
					current->comm);
				return err;
			}
		}

		if (!need_deferred_cleanup) {
			/* clean up a single job */
			gk20a_channel_clean_up_jobs(c, false);
		}
	}


	/* Grab access to HW to deal with do_idle */
	if (c->deterministic)
		nvgpu_rwsem_down_read(&g->deterministic_busy);

	if (c->deterministic && c->deterministic_railgate_allowed) {
		/*
		 * Nope - this channel has dropped its own power ref. As
		 * deterministic submits don't hold power on per each submitted
		 * job like normal ones do, the GPU might railgate any time now
		 * and thus submit is disallowed.
		 */
		err = -EINVAL;
		goto clean_up;
	}

	trace_gk20a_channel_submit_gpfifo(g->name,
					  c->chid,
					  num_entries,
					  flags,
					  fence ? fence->id : 0,
					  fence ? fence->value : 0);

	gk20a_dbg_info("pre-submit put %d, get %d, size %d",
		c->gpfifo.put, c->gpfifo.get, c->gpfifo.entry_num);

	/*
	 * Make sure we have enough space for gpfifo entries. Check cached
	 * values first and then read from HW. If no space, return EAGAIN
	 * and let userpace decide to re-try request or not.
	 */
	if (nvgpu_gp_free_count(c) < num_entries + extra_entries) {
		if (nvgpu_get_gp_free_count(c) < num_entries + extra_entries) {
			err = -EAGAIN;
			goto clean_up;
		}
	}

	if (c->has_timedout) {
		err = -ETIMEDOUT;
		goto clean_up;
	}

	if (need_job_tracking) {
		err = channel_gk20a_alloc_job(c, &job);
		if (err)
			goto clean_up;

		err = gk20a_submit_prepare_syncs(c, fence, job,
						 &wait_cmd, &incr_cmd,
						 &pre_fence, &post_fence,
						 force_need_sync_fence,
						 need_deferred_cleanup,
						 flags);
		if (err)
			goto clean_up_job;
	}

	if (profile)
		profile->timestamp[PROFILE_JOB_TRACKING] = sched_clock();

	if (wait_cmd)
		gk20a_submit_append_priv_cmdbuf(c, wait_cmd);

	if (gpfifo || user_gpfifo)
		err = gk20a_submit_append_gpfifo(c, gpfifo, user_gpfifo,
				num_entries);
	if (err)
		goto clean_up_job;

	/*
	 * And here's where we add the incr_cmd we generated earlier. It should
	 * always run!
	 */
	if (incr_cmd)
		gk20a_submit_append_priv_cmdbuf(c, incr_cmd);

	if (fence_out)
		*fence_out = gk20a_fence_get(post_fence);

	if (need_job_tracking)
		/* TODO! Check for errors... */
		gk20a_channel_add_job(c, job, skip_buffer_refcounting);
	if (profile)
		profile->timestamp[PROFILE_APPEND] = sched_clock();

	g->ops.fifo.userd_gp_put(g, c);

	if ((NVGPU_SUBMIT_GPFIFO_FLAGS_RESCHEDULE_RUNLIST & flags) &&
		g->ops.fifo.reschedule_runlist)
		g->ops.fifo.reschedule_runlist(g, c->runlist_id);

	/* No hw access beyond this point */
	if (c->deterministic)
		nvgpu_rwsem_up_read(&g->deterministic_busy);

	trace_gk20a_channel_submitted_gpfifo(g->name,
				c->chid,
				num_entries,
				flags,
				post_fence ? post_fence->syncpt_id : 0,
				post_fence ? post_fence->syncpt_value : 0);

	gk20a_dbg_info("post-submit put %d, get %d, size %d",
		c->gpfifo.put, c->gpfifo.get, c->gpfifo.entry_num);

	if (profile)
		profile->timestamp[PROFILE_END] = sched_clock();
	gk20a_dbg_fn("done");
	return err;

clean_up_job:
	channel_gk20a_free_job(c, job);
clean_up:
	gk20a_dbg_fn("fail");
	gk20a_fence_put(pre_fence);
	gk20a_fence_put(post_fence);
	if (c->deterministic)
		nvgpu_rwsem_up_read(&g->deterministic_busy);
	else if (need_deferred_cleanup)
		gk20a_idle(g);

	return err;
}

