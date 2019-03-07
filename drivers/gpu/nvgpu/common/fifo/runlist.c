/*
 * Copyright (c) 2011-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/fifo.h>
#include <nvgpu/runlist.h>
#include <nvgpu/bug.h>

void nvgpu_fifo_lock_active_runlists(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	struct fifo_runlist_info_gk20a *runlist;
	u32 i;

	nvgpu_log_info(g, "acquire runlist_lock for active runlists");
	for (i = 0; i < g->fifo.num_runlists; i++) {
		runlist = &f->active_runlist_info[i];
		nvgpu_mutex_acquire(&runlist->runlist_lock);
	}
}

void nvgpu_fifo_unlock_active_runlists(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	struct fifo_runlist_info_gk20a *runlist;
	u32 i;

	nvgpu_log_info(g, "release runlist_lock for active runlists");
	for (i = 0; i < g->fifo.num_runlists; i++) {
		runlist = &f->active_runlist_info[i];
		nvgpu_mutex_release(&runlist->runlist_lock);
	}
}

static u32 nvgpu_runlist_append_tsg(struct gk20a *g,
		struct fifo_runlist_info_gk20a *runlist,
		u32 **runlist_entry,
		u32 *entries_left,
		struct tsg_gk20a *tsg)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 runlist_entry_words = f->runlist_entry_size / (u32)sizeof(u32);
	struct channel_gk20a *ch;
	u32 count = 0;

	nvgpu_log_fn(f->g, " ");

	if (*entries_left == 0U) {
		return RUNLIST_APPEND_FAILURE;
	}

	/* add TSG entry */
	nvgpu_log_info(g, "add TSG %d to runlist", tsg->tsgid);
	g->ops.runlist.get_tsg_entry(tsg, *runlist_entry);
	nvgpu_log_info(g, "tsg rl entries left %d runlist [0] %x [1] %x",
			*entries_left,
			(*runlist_entry)[0], (*runlist_entry)[1]);
	*runlist_entry += runlist_entry_words;
	count++;
	(*entries_left)--;

	nvgpu_rwsem_down_read(&tsg->ch_list_lock);
	/* add runnable channels bound to this TSG */
	nvgpu_list_for_each_entry(ch, &tsg->ch_list,
			channel_gk20a, ch_entry) {
		if (!test_bit((int)ch->chid,
			      runlist->active_channels)) {
			continue;
		}

		if (*entries_left == 0U) {
			nvgpu_rwsem_up_read(&tsg->ch_list_lock);
			return RUNLIST_APPEND_FAILURE;
		}

		nvgpu_log_info(g, "add channel %d to runlist",
			ch->chid);
		g->ops.runlist.get_ch_entry(ch, *runlist_entry);
		nvgpu_log_info(g, "rl entries left %d runlist [0] %x [1] %x",
			*entries_left,
			(*runlist_entry)[0], (*runlist_entry)[1]);
		count++;
		*runlist_entry += runlist_entry_words;
		(*entries_left)--;
	}
	nvgpu_rwsem_up_read(&tsg->ch_list_lock);

	return count;
}


static u32 nvgpu_runlist_append_prio(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left,
				u32 interleave_level)
{
	u32 count = 0;
	unsigned long tsgid;

	nvgpu_log_fn(f->g, " ");

	for_each_set_bit(tsgid, runlist->active_tsgs, f->num_channels) {
		struct tsg_gk20a *tsg = &f->tsg[tsgid];
		u32 entries;

		if (tsg->interleave_level == interleave_level) {
			entries = nvgpu_runlist_append_tsg(f->g, runlist,
					runlist_entry, entries_left, tsg);
			if (entries == RUNLIST_APPEND_FAILURE) {
				return RUNLIST_APPEND_FAILURE;
			}
			count += entries;
		}
	}

	return count;
}

static u32 nvgpu_runlist_append_hi(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left)
{
	nvgpu_log_fn(f->g, " ");

	/*
	 * No higher levels - this is where the "recursion" ends; just add all
	 * active TSGs at this level.
	 */
	return nvgpu_runlist_append_prio(f, runlist, runlist_entry,
			entries_left,
			NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_HIGH);
}

static u32 nvgpu_runlist_append_med(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left)
{
	u32 count = 0;
	unsigned long tsgid;

	nvgpu_log_fn(f->g, " ");

	for_each_set_bit(tsgid, runlist->active_tsgs, f->num_channels) {
		struct tsg_gk20a *tsg = &f->tsg[tsgid];
		u32 entries;

		if (tsg->interleave_level !=
				NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_MEDIUM) {
			continue;
		}

		/* LEVEL_MEDIUM list starts with a LEVEL_HIGH, if any */

		entries = nvgpu_runlist_append_hi(f, runlist,
				runlist_entry, entries_left);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;

		entries = nvgpu_runlist_append_tsg(f->g, runlist,
				runlist_entry, entries_left, tsg);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;
	}

	return count;
}

static u32 nvgpu_runlist_append_low(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left)
{
	u32 count = 0;
	unsigned long tsgid;

	nvgpu_log_fn(f->g, " ");

	for_each_set_bit(tsgid, runlist->active_tsgs, f->num_channels) {
		struct tsg_gk20a *tsg = &f->tsg[tsgid];
		u32 entries;

		if (tsg->interleave_level !=
				NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_LOW) {
			continue;
		}

		/* The medium level starts with the highs, if any. */

		entries = nvgpu_runlist_append_med(f, runlist,
				runlist_entry, entries_left);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;

		entries = nvgpu_runlist_append_hi(f, runlist,
				runlist_entry, entries_left);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;

		entries = nvgpu_runlist_append_tsg(f->g, runlist,
				runlist_entry, entries_left, tsg);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;
	}

	if (count == 0U) {
		/*
		 * No transitions to fill with higher levels, so add
		 * the next level once. If that's empty too, we have only
		 * LEVEL_HIGH jobs.
		 */
		count = nvgpu_runlist_append_med(f, runlist,
				runlist_entry, entries_left);
		if (count == 0U) {
			count = nvgpu_runlist_append_hi(f, runlist,
					runlist_entry, entries_left);
		}
	}

	return count;
}

static u32 nvgpu_runlist_append_flat(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left)
{
	u32 count = 0, entries, i;

	nvgpu_log_fn(f->g, " ");

	/* Group by priority but don't interleave. High comes first. */

	for (i = 0; i < NVGPU_FIFO_RUNLIST_INTERLEAVE_NUM_LEVELS; i++) {
		u32 level = NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_HIGH - i;

		entries = nvgpu_runlist_append_prio(f, runlist, runlist_entry,
				entries_left, level);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;
	}

	return count;
}

u32 nvgpu_runlist_construct_locked(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 buf_id,
				u32 max_entries)
{
	u32 *runlist_entry_base = runlist->mem[buf_id].cpu_va;

	nvgpu_log_fn(f->g, " ");

	/*
	 * The entry pointer and capacity counter that live on the stack here
	 * keep track of the current position and the remaining space when tsg
	 * and channel entries are ultimately appended.
	 */
	if (f->g->runlist_interleave) {
		return nvgpu_runlist_append_low(f, runlist,
				&runlist_entry_base, &max_entries);
	} else {
		return nvgpu_runlist_append_flat(f, runlist,
				&runlist_entry_base, &max_entries);
	}
}

static bool gk20a_runlist_modify_active_locked(struct gk20a *g, u32 runlist_id,
					    struct channel_gk20a *ch, bool add)
{
	struct fifo_gk20a *f = &g->fifo;
	struct fifo_runlist_info_gk20a *runlist = NULL;
	struct tsg_gk20a *tsg = NULL;

	runlist = f->runlist_info[runlist_id];
	tsg = tsg_gk20a_from_ch(ch);

	if (tsg == NULL) {
		/*
		 * Unsupported condition, but shouldn't break anything. Warn
		 * and tell the caller that nothing has changed.
		 */
		nvgpu_warn(g, "Bare channel in runlist update");
		return false;
	}

	if (add) {
		if (test_and_set_bit((int)ch->chid,
				runlist->active_channels)) {
			/* was already there */
			return false;
		} else {
			/* new, and belongs to a tsg */
			set_bit((int)tsg->tsgid, runlist->active_tsgs);
			tsg->num_active_channels++;
		}
	} else {
		if (!test_and_clear_bit((int)ch->chid,
				runlist->active_channels)) {
			/* wasn't there */
			return false;
		} else {
			if (--tsg->num_active_channels == 0U) {
				/* was the only member of this tsg */
				clear_bit((int)tsg->tsgid,
						runlist->active_tsgs);
			}
		}
	}

	return true;
}

static int gk20a_runlist_reconstruct_locked(struct gk20a *g, u32 runlist_id,
				     u32 buf_id, bool add_entries)
{
	struct fifo_gk20a *f = &g->fifo;
	struct fifo_runlist_info_gk20a *runlist = NULL;

	runlist = f->runlist_info[runlist_id];

	nvgpu_log_info(g, "runlist_id : %d, switch to new buffer 0x%16llx",
		runlist_id, (u64)nvgpu_mem_get_addr(g, &runlist->mem[buf_id]));

	if (add_entries) {
		u32 num_entries = nvgpu_runlist_construct_locked(f,
						runlist,
						buf_id,
						f->num_runlist_entries);
		if (num_entries == RUNLIST_APPEND_FAILURE) {
			return -E2BIG;
		}
		runlist->count = num_entries;
		WARN_ON(runlist->count > f->num_runlist_entries);
	} else {
		runlist->count = 0;
	}

	return 0;
}

int gk20a_runlist_update_locked(struct gk20a *g, u32 runlist_id,
					    struct channel_gk20a *ch, bool add,
					    bool wait_for_finish)
{
	int ret = 0;
	struct fifo_gk20a *f = &g->fifo;
	struct fifo_runlist_info_gk20a *runlist = NULL;
	u32 buf_id;
	bool add_entries;

	if (ch != NULL) {
		bool update = gk20a_runlist_modify_active_locked(g, runlist_id,
				ch, add);
		if (!update) {
			/* no change in runlist contents */
			return 0;
		}
		/* had a channel to update, so reconstruct */
		add_entries = true;
	} else {
		/* no channel; add means update all, !add means clear all */
		add_entries = add;
	}

	runlist = f->runlist_info[runlist_id];
	/* double buffering, swap to next */
	buf_id = runlist->cur_buffer == 0U ? 1U : 0U;

	ret = gk20a_runlist_reconstruct_locked(g, runlist_id, buf_id,
			add_entries);
	if (ret != 0) {
		return ret;
	}

	g->ops.runlist.hw_submit(g, runlist_id, runlist->count, buf_id);

	if (wait_for_finish) {
		ret = g->ops.runlist.wait_pending(g, runlist_id);

		if (ret == -ETIMEDOUT) {
			nvgpu_err(g, "runlist %d update timeout", runlist_id);
			/* trigger runlist update timeout recovery */
			return ret;

		} else if (ret == -EINTR) {
			nvgpu_err(g, "runlist update interrupted");
		}
	}

	runlist->cur_buffer = buf_id;

	return ret;
}

/* trigger host to expire current timeslice and reschedule runlist from front */
int nvgpu_fifo_reschedule_runlist(struct channel_gk20a *ch, bool preempt_next,
		bool wait_preempt)
{
	struct gk20a *g = ch->g;
	struct fifo_runlist_info_gk20a *runlist;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret = 0;
	int ret = 0;

	runlist = g->fifo.runlist_info[ch->runlist_id];
	if (nvgpu_mutex_tryacquire(&runlist->runlist_lock) == 0) {
		return -EBUSY;
	}

	mutex_ret = nvgpu_pmu_mutex_acquire(
		&g->pmu, PMU_MUTEX_ID_FIFO, &token);


	g->ops.runlist.hw_submit(
		g, ch->runlist_id, runlist->count, runlist->cur_buffer);

	if (preempt_next) {
		g->ops.runlist.reschedule_preempt_next_locked(ch, wait_preempt);
	}

	g->ops.runlist.wait_pending(g, ch->runlist_id);

	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(
			&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}
	nvgpu_mutex_release(&runlist->runlist_lock);

	return ret;
}

static void gk20a_fifo_runlist_reset_engines(struct gk20a *g, u32 runlist_id)
{
	u32 engines = g->ops.fifo.runlist_busy_engines(g, runlist_id);

	if (engines != 0U) {
		gk20a_fifo_recover(g, engines, ~(u32)0, false, false, true,
				RC_TYPE_RUNLIST_UPDATE_TIMEOUT);
	}
}

/* add/remove a channel from runlist
   special cases below: runlist->active_channels will NOT be changed.
   (ch == NULL && !add) means remove all active channels from runlist.
   (ch == NULL &&  add) means restore all active channels on runlist. */
static int gk20a_runlist_update(struct gk20a *g, u32 runlist_id,
			      struct channel_gk20a *ch,
			      bool add, bool wait_for_finish)
{
	struct fifo_runlist_info_gk20a *runlist = NULL;
	struct fifo_gk20a *f = &g->fifo;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret = 0;
	int ret = 0;

	nvgpu_log_fn(g, " ");

	runlist = f->runlist_info[runlist_id];

	nvgpu_mutex_acquire(&runlist->runlist_lock);

	mutex_ret = nvgpu_pmu_mutex_acquire(&g->pmu,
		PMU_MUTEX_ID_FIFO, &token);

	ret = gk20a_runlist_update_locked(g, runlist_id, ch, add,
					       wait_for_finish);

	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}

	nvgpu_mutex_release(&runlist->runlist_lock);

	if (ret == -ETIMEDOUT) {
		gk20a_fifo_runlist_reset_engines(g, runlist_id);
	}

	return ret;
}

int gk20a_runlist_update_for_channel(struct gk20a *g, u32 runlist_id,
			      struct channel_gk20a *ch,
			      bool add, bool wait_for_finish)
{
	nvgpu_assert(ch != NULL);

	return gk20a_runlist_update(g, runlist_id, ch, add, wait_for_finish);
}

int gk20a_runlist_reload(struct gk20a *g, u32 runlist_id,
			      bool add, bool wait_for_finish)
{
	return gk20a_runlist_update(g, runlist_id, NULL, add, wait_for_finish);
}

int nvgpu_runlist_reload_ids(struct gk20a *g, u32 runlist_ids, bool add)
{
	int ret = -EINVAL;
	unsigned long runlist_id = 0;
	int errcode;
	unsigned long ulong_runlist_ids = (unsigned long)runlist_ids;

	if (g == NULL) {
		goto end;
	}

	ret = 0;
	for_each_set_bit(runlist_id, &ulong_runlist_ids, 32U) {
		/* Capture the last failure error code */
		errcode = g->ops.runlist.reload(g, (u32)runlist_id, add, true);
		if (errcode != 0) {
			nvgpu_err(g,
				"failed to update_runlist %lu %d",
				runlist_id, errcode);
			ret = errcode;
		}
	}
end:
	return ret;
}

const char *gk20a_fifo_interleave_level_name(u32 interleave_level)
{
	const char *ret_string = NULL;

	switch (interleave_level) {
	case NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_LOW:
		ret_string = "LOW";
		break;

	case NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_MEDIUM:
		ret_string = "MEDIUM";
		break;

	case NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_HIGH:
		ret_string = "HIGH";
		break;

	default:
		ret_string = "?";
		break;
	}

	return ret_string;
}

void gk20a_fifo_set_runlist_state(struct gk20a *g, u32 runlists_mask,
		u32 runlist_state)
{
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret = 0;

	nvgpu_log(g, gpu_dbg_info, "runlist mask = 0x%08x state = 0x%08x",
			runlists_mask, runlist_state);


	mutex_ret = nvgpu_pmu_mutex_acquire(&g->pmu,
		PMU_MUTEX_ID_FIFO, &token);

	g->ops.runlist.write_state(g, runlists_mask, runlist_state);

	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}
}

void gk20a_fifo_delete_runlist(struct fifo_gk20a *f)
{
	u32 i, j;
	struct fifo_runlist_info_gk20a *runlist;
	struct gk20a *g = NULL;

	if ((f == NULL) || (f->runlist_info == NULL)) {
		return;
	}

	g = f->g;

	for (i = 0; i < f->num_runlists; i++) {
		runlist = &f->active_runlist_info[i];
		for (j = 0; j < MAX_RUNLIST_BUFFERS; j++) {
			nvgpu_dma_free(g, &runlist->mem[j]);
		}

		nvgpu_kfree(g, runlist->active_channels);
		runlist->active_channels = NULL;

		nvgpu_kfree(g, runlist->active_tsgs);
		runlist->active_tsgs = NULL;

		nvgpu_mutex_destroy(&runlist->runlist_lock);
		f->runlist_info[runlist->runlist_id] = NULL;
	}

	nvgpu_kfree(g, f->active_runlist_info);
	f->active_runlist_info = NULL;
	f->num_runlists = 0;
	nvgpu_kfree(g, f->runlist_info);
	f->runlist_info = NULL;
	f->max_runlists = 0;
}

int nvgpu_init_runlist(struct gk20a *g, struct fifo_gk20a *f)
{
	struct fifo_runlist_info_gk20a *runlist;
	unsigned int runlist_id;
	u32 i, j;
	u32 num_runlists = 0U;
	size_t runlist_size;
	int err = 0;

	nvgpu_log_fn(g, " ");

	f->max_runlists = g->ops.runlist.count_max();
	f->runlist_info = nvgpu_kzalloc(g,
			sizeof(*f->runlist_info) * f->max_runlists);
	if (f->runlist_info == NULL) {
		goto clean_up_runlist;
	}

	for (runlist_id = 0; runlist_id < f->max_runlists; runlist_id++) {
		if (gk20a_fifo_is_valid_runlist_id(g, runlist_id)) {
			num_runlists++;
		}
	}
	f->num_runlists = num_runlists;

	f->active_runlist_info = nvgpu_kzalloc(g,
			 sizeof(*f->active_runlist_info) * num_runlists);
	if (f->active_runlist_info == NULL) {
		goto clean_up_runlist;
	}
	nvgpu_log_info(g, "num_runlists=%u", num_runlists);

	/* In most case we want to loop through active runlists only. Here
	 * we need to loop through all possible runlists, to build the mapping
	 * between runlist_info[runlist_id] and active_runlist_info[i].
	 */
	i = 0U;
	for (runlist_id = 0; runlist_id < f->max_runlists; runlist_id++) {
		if (!gk20a_fifo_is_valid_runlist_id(g, runlist_id)) {
			/* skip inactive runlist */
			continue;
		}
		runlist = &f->active_runlist_info[i];
		runlist->runlist_id = runlist_id;
		f->runlist_info[runlist_id] = runlist;
		i++;

		runlist->active_channels =
			nvgpu_kzalloc(g, DIV_ROUND_UP(f->num_channels,
						      BITS_PER_BYTE));
		if (runlist->active_channels == NULL) {
			goto clean_up_runlist;
		}

		runlist->active_tsgs =
			nvgpu_kzalloc(g, DIV_ROUND_UP(f->num_channels,
						      BITS_PER_BYTE));
		if (runlist->active_tsgs == NULL) {
			goto clean_up_runlist;
		}

		runlist_size = (size_t)f->runlist_entry_size *
				(size_t)f->num_runlist_entries;
		nvgpu_log(g, gpu_dbg_info,
				"runlist_entries %d runlist size %zu",
				f->num_runlist_entries, runlist_size);

		for (j = 0; j < MAX_RUNLIST_BUFFERS; j++) {
			err = nvgpu_dma_alloc_flags_sys(g,
					g->is_virtual ?
					  0 : NVGPU_DMA_PHYSICALLY_ADDRESSED,
					runlist_size,
					&runlist->mem[j]);
			if (err != 0) {
				nvgpu_err(g, "memory allocation failed");
				goto clean_up_runlist;
			}
		}

		err = nvgpu_mutex_init(&runlist->runlist_lock);
		if (err != 0) {
			nvgpu_err(g,
				"Error in runlist_lock mutex initialization");
			goto clean_up_runlist;
		}

		/* None of buffers is pinned if this value doesn't change.
		    Otherwise, one of them (cur_buffer) must have been pinned. */
		runlist->cur_buffer = MAX_RUNLIST_BUFFERS;
	}

	nvgpu_log_fn(g, "done");
	return 0;

clean_up_runlist:
	gk20a_fifo_delete_runlist(f);
	nvgpu_log_fn(g, "fail");
	return err;
}
