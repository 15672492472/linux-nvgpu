/*
 * Copyright (c) 2016-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/bitops.h>
#include <nvgpu/lock.h>
#include <nvgpu/kmem.h>
#include <nvgpu/atomic.h>
#include <nvgpu/bug.h>
#include <nvgpu/kref.h>
#include <nvgpu/log.h>
#include <nvgpu/barrier.h>
#include <nvgpu/cond.h>
#include <nvgpu/list.h>
#include <nvgpu/clk_arb.h>
#include <nvgpu/timers.h>
#include <nvgpu/worker.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/pmu/perf_pstate.h>
#include <nvgpu/pmu/volt.h>
#include <nvgpu/pmu/clk/clk.h>
#include <nvgpu/boardobjgrp_e255.h>

int nvgpu_clk_notification_queue_alloc(struct gk20a *g,
				struct nvgpu_clk_notification_queue *queue,
				u32 events_number) {
	queue->notifications = nvgpu_kcalloc(g, events_number,
		sizeof(struct nvgpu_clk_notification));
	if (!queue->notifications) {
		return -ENOMEM;
	}
	queue->size = events_number;

	nvgpu_atomic_set(&queue->head, 0);
	nvgpu_atomic_set(&queue->tail, 0);

	return 0;
}

void nvgpu_clk_notification_queue_free(struct gk20a *g,
		struct nvgpu_clk_notification_queue *queue) {
	if (queue->size > 0) {
		nvgpu_kfree(g, queue->notifications);
		queue->size = 0;
		nvgpu_atomic_set(&queue->head, 0);
		nvgpu_atomic_set(&queue->tail, 0);
	}
}

static void nvgpu_clk_arb_queue_notification(struct gk20a *g,
				struct nvgpu_clk_notification_queue *queue,
				u32 alarm_mask) {

	u32 queue_index;
	u64 timestamp;

	queue_index = (nvgpu_atomic_inc_return(&queue->tail)) % queue->size;
	/* get current timestamp */
	timestamp = (u64) nvgpu_hr_timestamp();

	queue->notifications[queue_index].timestamp = timestamp;
	queue->notifications[queue_index].notification = alarm_mask;

}

void nvgpu_clk_arb_set_global_alarm(struct gk20a *g, u32 alarm)
{
	struct nvgpu_clk_arb *arb = g->clk_arb;

	u64 current_mask;
	u32 refcnt;
	u32 alarm_mask;
	u64 new_mask;

	do {
		current_mask = (u64)nvgpu_atomic64_read(&arb->alarm_mask);
		/* atomic operations are strong so they do not need masks */

		refcnt = ((u32) (current_mask >> 32)) + 1;
		alarm_mask =  (u32) (current_mask &  ~0) | alarm;
		new_mask = ((u64) refcnt << 32) | alarm_mask;

	} while (unlikely(current_mask !=
			(u64)nvgpu_atomic64_cmpxchg(&arb->alarm_mask,
				(long int)current_mask, (long int)new_mask)));

	nvgpu_clk_arb_queue_notification(g, &arb->notification_queue, alarm);
}


int nvgpu_clk_arb_update_vf_table(struct nvgpu_clk_arb *arb)
{
	struct gk20a *g = arb->g;
	struct nvgpu_clk_vf_table *table;

	u32 i, j;
	int status = -EINVAL;
	u16 clk_cur;
	u32 num_points;

	struct clk_set_info *p0_info;

	table = NV_ACCESS_ONCE(arb->current_vf_table);
	/* make flag visible when all data has resolved in the tables */
	nvgpu_smp_rmb();
	table = (table == &arb->vf_table_pool[0]) ? &arb->vf_table_pool[1] :
		&arb->vf_table_pool[0];

	/* Get allowed memory ranges */
	if (g->ops.clk_arb.get_arbiter_clk_range(g, CTRL_CLK_DOMAIN_GPCCLK,
						&arb->gpc2clk_min,
						&arb->gpc2clk_max) < 0) {
		nvgpu_err(g, "failed to fetch GPC2CLK range");
		goto exit_vf_table;
	}

	if (g->ops.clk_arb.get_arbiter_clk_range(g, CTRL_CLK_DOMAIN_MCLK,
						&arb->mclk_min,
						&arb->mclk_max) < 0) {
		nvgpu_err(g, "failed to fetch MCLK range");
		goto exit_vf_table;
	}

	table->gpc2clk_num_points = MAX_F_POINTS;
	table->mclk_num_points = MAX_F_POINTS;
	if (g->ops.clk.clk_domain_get_f_points(arb->g, CTRL_CLK_DOMAIN_GPCCLK,
		&table->gpc2clk_num_points, arb->gpc2clk_f_points)) {
		nvgpu_err(g, "failed to fetch GPC2CLK frequency points");
		goto exit_vf_table;
	}
	if (!table->gpc2clk_num_points) {
		nvgpu_err(g, "empty queries to f points gpc2clk %d", table->gpc2clk_num_points);
		status = -EINVAL;
		goto exit_vf_table;
	}

	(void) memset(table->gpc2clk_points, 0,
		table->gpc2clk_num_points*sizeof(struct nvgpu_clk_vf_point));

	p0_info = nvgpu_pmu_perf_pstate_get_clk_set_info(g,
			CTRL_PERF_PSTATE_P0, CLKWHICH_GPCCLK);
	if (!p0_info) {
		status = -EINVAL;
		nvgpu_err(g, "failed to get GPC2CLK P0 info");
		goto exit_vf_table;
	}

	/* GPC2CLK needs to be checked in two passes. The first determines the
	 * relationships between GPC2CLK, SYS2CLK and XBAR2CLK, while the
	 * second verifies that the clocks minimum is satisfied and sets
	 * the voltages,the later part is done in nvgpu_clk_set_req_fll_clk_ps35
	 */
	for (i = 0, j = 0, num_points = 0, clk_cur = 0;
			i < table->gpc2clk_num_points; i++) {
		struct nvgpu_set_fll_clk setfllclk;

		if ((arb->gpc2clk_f_points[i] >= arb->gpc2clk_min) &&
			(arb->gpc2clk_f_points[i] <= arb->gpc2clk_max) &&
			(arb->gpc2clk_f_points[i] != clk_cur)) {

			table->gpc2clk_points[j].gpc_mhz =
				arb->gpc2clk_f_points[i];
			setfllclk.gpc2clkmhz = arb->gpc2clk_f_points[i];

			status = nvgpu_clk_get_fll_clks(g, &setfllclk);
			if (status < 0) {
				nvgpu_err(g,
					"failed to get GPC2CLK slave clocks");
				goto exit_vf_table;
			}

			table->gpc2clk_points[j].sys_mhz =
				setfllclk.sys2clkmhz;
			table->gpc2clk_points[j].xbar_mhz =
				setfllclk.xbar2clkmhz;
			table->gpc2clk_points[j].nvd_mhz =
				setfllclk.nvdclkmhz;
			table->gpc2clk_points[j].host_mhz =
				setfllclk.hostclkmhz;

			clk_cur = table->gpc2clk_points[j].gpc_mhz;

			if ((clk_cur >= p0_info->min_mhz) &&
					(clk_cur <= p0_info->max_mhz)) {
				VF_POINT_SET_PSTATE_SUPPORTED(
					&table->gpc2clk_points[j],
					CTRL_PERF_PSTATE_P0);
			}

			j++;
			num_points++;
		}
	}
	table->gpc2clk_num_points = num_points;

	/* make table visible when all data has resolved in the tables */
	nvgpu_smp_wmb();
	arb->current_vf_table = table;

exit_vf_table:

	if (status < 0) {
		nvgpu_clk_arb_set_global_alarm(g,
			EVENT(ALARM_VF_TABLE_UPDATE_FAILED));
	}
	nvgpu_clk_arb_worker_enqueue(g, &arb->update_arb_work_item);

	return status;
}


static void nvgpu_clk_arb_run_vf_table_cb(struct nvgpu_clk_arb *arb)
{
	struct gk20a *g = arb->g;
	int err;

	/* get latest vf curve from pmu */
	err = g->clk_pmu->nvgpu_clk_vf_point_cache(g);
	if (err != 0) {
		nvgpu_err(g, "failed to cache VF table");
		nvgpu_clk_arb_set_global_alarm(g,
			EVENT(ALARM_VF_TABLE_UPDATE_FAILED));
		nvgpu_clk_arb_worker_enqueue(g, &arb->update_arb_work_item);

		return;
	}
	nvgpu_clk_arb_update_vf_table(arb);
}

u32 nvgpu_clk_arb_notify(struct nvgpu_clk_dev *dev,
				struct nvgpu_clk_arb_target *target,
				u32 alarm) {

	struct nvgpu_clk_session *session = dev->session;
	struct nvgpu_clk_arb *arb = session->g->clk_arb;
	struct nvgpu_clk_notification *notification;

	u32 queue_alarm_mask = 0;
	u32 enabled_mask = 0;
	u32 new_alarms_reported = 0;
	u32 poll_mask = 0;
	u32 tail, head, index;
	u32 queue_index;
	size_t size;

	enabled_mask = (u32)nvgpu_atomic_read(&dev->enabled_mask);
	size = arb->notification_queue.size;

	/* queue global arbiter notifications in buffer */
	do {
		tail = (u32)nvgpu_atomic_read(&arb->notification_queue.tail);
		/* copy items to the queue */
		queue_index = (u32)nvgpu_atomic_read(&dev->queue.tail);
		head = dev->arb_queue_head;
		head = (tail - head) < arb->notification_queue.size ?
			head : tail - arb->notification_queue.size;

		for (index = head; _WRAPGTEQ(tail, index); index++) {
			u32 alarm_detected;

			notification = &arb->notification_queue.
					notifications[(index+1U) % size];
			alarm_detected =
				NV_ACCESS_ONCE(notification->notification);

			if (!(enabled_mask & alarm_detected)) {
				continue;
			}

			queue_index++;
			dev->queue.notifications[
				queue_index % dev->queue.size].timestamp =
					NV_ACCESS_ONCE(notification->timestamp);

			dev->queue.notifications[
				queue_index % dev->queue.size].notification =
					alarm_detected;

			queue_alarm_mask |= alarm_detected;
		}
	} while (unlikely(nvgpu_atomic_read(&arb->notification_queue.tail) !=
			(int)tail));

	nvgpu_atomic_set(&dev->queue.tail, (int)queue_index);
	/* update the last notification we processed from global queue */

	dev->arb_queue_head = tail;

	/* Check if current session targets are met */
	if (enabled_mask & EVENT(ALARM_LOCAL_TARGET_VF_NOT_POSSIBLE)) {
		if ((target->gpc2clk < session->target->gpc2clk)
			|| (target->mclk < session->target->mclk)) {

			poll_mask |= (NVGPU_POLLIN | NVGPU_POLLPRI);
			nvgpu_clk_arb_queue_notification(arb->g, &dev->queue,
				EVENT(ALARM_LOCAL_TARGET_VF_NOT_POSSIBLE));
		}
	}

	/* Check if there is a new VF update */
	if (queue_alarm_mask & EVENT(VF_UPDATE)) {
		poll_mask |= (NVGPU_POLLIN | NVGPU_POLLRDNORM);
	}

	/* Notify sticky alarms that were not reported on previous run*/
	new_alarms_reported = (queue_alarm_mask |
			(alarm & ~dev->alarms_reported & queue_alarm_mask));

	if (new_alarms_reported & ~LOCAL_ALARM_MASK) {
		/* check that we are not re-reporting */
		if (new_alarms_reported & EVENT(ALARM_GPU_LOST)) {
			poll_mask |= NVGPU_POLLHUP;
		}

		poll_mask |= (NVGPU_POLLIN | NVGPU_POLLPRI);
		/* On next run do not report global alarms that were already
		 * reported, but report SHUTDOWN always
		 */
		dev->alarms_reported = new_alarms_reported & ~LOCAL_ALARM_MASK &
							~EVENT(ALARM_GPU_LOST);
	}

	if (poll_mask) {
		nvgpu_atomic_set(&dev->poll_mask, (int)poll_mask);
		nvgpu_clk_arb_event_post_event(dev);
	}

	return new_alarms_reported;
}

void nvgpu_clk_arb_clear_global_alarm(struct gk20a *g, u32 alarm)
{
	struct nvgpu_clk_arb *arb = g->clk_arb;

	u64 current_mask;
	u32 refcnt;
	u32 alarm_mask;
	u64 new_mask;

	do {
		current_mask = (u64)nvgpu_atomic64_read(&arb->alarm_mask);
		/* atomic operations are strong so they do not need masks */

		refcnt = ((u32) (current_mask >> 32)) + 1;
		alarm_mask =  (u32) (current_mask & ~alarm);
		new_mask = ((u64) refcnt << 32) | alarm_mask;

	} while (unlikely(current_mask !=
			(u64)nvgpu_atomic64_cmpxchg(&arb->alarm_mask,
				(long int)current_mask, (long int)new_mask)));
}

static inline struct nvgpu_clk_arb_worker *
nvgpu_clk_arb_worker_from_worker(struct nvgpu_worker *worker)
{
	return (struct nvgpu_clk_arb_worker *)
	   ((uintptr_t)worker - offsetof(struct nvgpu_clk_arb_worker, worker));
};

/*
 * Process one scheduled work item.
 */
static void nvgpu_clk_arb_worker_poll_wakeup_process_item(
		struct nvgpu_list_node *work_item)
{
	struct nvgpu_clk_arb_work_item *clk_arb_work_item =
		nvgpu_clk_arb_work_item_from_worker_item(work_item);

	struct gk20a *g = clk_arb_work_item->arb->g;

	clk_arb_dbg(g, " ");

	if (clk_arb_work_item->item_type == CLK_ARB_WORK_UPDATE_VF_TABLE) {
		nvgpu_clk_arb_run_vf_table_cb(clk_arb_work_item->arb);
	} else if (clk_arb_work_item->item_type == CLK_ARB_WORK_UPDATE_ARB) {
		g->ops.clk_arb.clk_arb_run_arbiter_cb(clk_arb_work_item->arb);
	}
}

static void nvgpu_clk_arb_worker_poll_init(struct nvgpu_worker *worker)
{
	struct gk20a *g = worker->g;

	clk_arb_dbg(g, " ");
}

const struct nvgpu_worker_ops clk_arb_worker_ops = {
	.pre_process = nvgpu_clk_arb_worker_poll_init,
	.wakeup_early_exit = nvgpu_worker_should_stop,
	.wakeup_post_process = NULL,
	.wakeup_process_item =
		nvgpu_clk_arb_worker_poll_wakeup_process_item,
	.wakeup_condition =
		nvgpu_worker_should_stop,
	.wakeup_timeout = NULL,
};

/**
 * Append a work item to the worker's list.
 *
 * This adds work item to the end of the list and wakes the worker
 * up immediately. If the work item already existed in the list, it's not added,
 * because in that case it has been scheduled already but has not yet been
 * processed.
 */
void nvgpu_clk_arb_worker_enqueue(struct gk20a *g,
		struct nvgpu_clk_arb_work_item *work_item)
{
	clk_arb_dbg(g, " ");

	(void)nvgpu_worker_enqueue(&g->clk_arb_worker.worker,
			&work_item->worker_item);
}

/**
 * Initialize the clk arb worker's metadata and start the background thread.
 */
int nvgpu_clk_arb_worker_init(struct gk20a *g)
{
	struct nvgpu_worker *worker = &g->clk_arb_worker.worker;

	nvgpu_worker_init_name(worker, "nvgpu_clk_arb_poll", g->name);

	return nvgpu_worker_init(g, worker, &clk_arb_worker_ops);
}

int nvgpu_clk_arb_init_arbiter(struct gk20a *g)
{
	int err = 0;

	if (g->ops.clk_arb.check_clk_arb_support != NULL) {
		if (!g->ops.clk_arb.check_clk_arb_support(g)) {
			return 0;
		}
	}
	else {
		return 0;
	}

	nvgpu_mutex_acquire(&g->clk_arb_enable_lock);

	err = g->ops.clk_arb.arbiter_clk_init(g);

	nvgpu_mutex_release(&g->clk_arb_enable_lock);

	return err;
}

bool nvgpu_clk_arb_has_active_req(struct gk20a *g)
{
	return (nvgpu_atomic_read(&g->clk_arb_global_nr) > 0);
}

static void nvgpu_clk_arb_schedule_alarm(struct gk20a *g, u32 alarm)
{
	struct nvgpu_clk_arb *arb = g->clk_arb;

	nvgpu_clk_arb_set_global_alarm(g, alarm);
	nvgpu_clk_arb_worker_enqueue(g, &arb->update_arb_work_item);
}

void nvgpu_clk_arb_send_thermal_alarm(struct gk20a *g)
{
	nvgpu_clk_arb_schedule_alarm(g,
		BIT32(NVGPU_EVENT_ALARM_THERMAL_ABOVE_THRESHOLD));
}

void nvgpu_clk_arb_worker_deinit(struct gk20a *g)
{
	struct nvgpu_worker *worker = &g->clk_arb_worker.worker;

	nvgpu_worker_deinit(worker);
}

void nvgpu_clk_arb_cleanup_arbiter(struct gk20a *g)
{
	struct nvgpu_clk_arb *arb = g->clk_arb;

	nvgpu_mutex_acquire(&g->clk_arb_enable_lock);

	if (arb) {
		g->ops.clk_arb.clk_arb_cleanup(g->clk_arb);
	}

	nvgpu_mutex_release(&g->clk_arb_enable_lock);
}

int nvgpu_clk_arb_init_session(struct gk20a *g,
		struct nvgpu_clk_session **_session)
{
	struct nvgpu_clk_arb *arb = g->clk_arb;
	struct nvgpu_clk_session *session = *(_session);

	clk_arb_dbg(g, " ");

	if (g->ops.clk_arb.check_clk_arb_support != NULL) {
		if (!g->ops.clk_arb.check_clk_arb_support(g)) {
			return 0;
		}
	}
	else {
		return 0;
	}

	session = nvgpu_kzalloc(g, sizeof(struct nvgpu_clk_session));
	if (!session) {
		return -ENOMEM;
	}
	session->g = g;

	nvgpu_ref_init(&session->refcount);

	session->zombie = false;
	session->target_pool[0].pstate = CTRL_PERF_PSTATE_P8;
	/* make sure that the initialization of the pool is visible
	 * before the update
	 */
	nvgpu_smp_wmb();
	session->target = &session->target_pool[0];

	nvgpu_init_list_node(&session->targets);
	nvgpu_spinlock_init(&session->session_lock);

	nvgpu_spinlock_acquire(&arb->sessions_lock);
	nvgpu_list_add_tail(&session->link, &arb->sessions);
	nvgpu_spinlock_release(&arb->sessions_lock);

	*_session = session;

	return 0;
}

static struct nvgpu_clk_dev *
nvgpu_clk_dev_from_refcount(struct nvgpu_ref *refcount)
{
	return (struct nvgpu_clk_dev *)
	   ((uintptr_t)refcount - offsetof(struct nvgpu_clk_dev, refcount));
};

void nvgpu_clk_arb_free_fd(struct nvgpu_ref *refcount)
{
	struct nvgpu_clk_dev *dev = nvgpu_clk_dev_from_refcount(refcount);
	struct nvgpu_clk_session *session = dev->session;
	struct gk20a *g = session->g;

	nvgpu_clk_notification_queue_free(g, &dev->queue);

	nvgpu_atomic_dec(&g->clk_arb_global_nr);
	nvgpu_kfree(g, dev);
}

static struct nvgpu_clk_session *
nvgpu_clk_session_from_refcount(struct nvgpu_ref *refcount)
{
	return (struct nvgpu_clk_session *)
	   ((uintptr_t)refcount - offsetof(struct nvgpu_clk_session, refcount));
};

void nvgpu_clk_arb_free_session(struct nvgpu_ref *refcount)
{
	struct nvgpu_clk_session *session =
		nvgpu_clk_session_from_refcount(refcount);
	struct nvgpu_clk_arb *arb = session->g->clk_arb;
	struct gk20a *g = session->g;
	struct nvgpu_clk_dev *dev, *tmp;

	clk_arb_dbg(g, " ");

	if (arb) {
		nvgpu_spinlock_acquire(&arb->sessions_lock);
		nvgpu_list_del(&session->link);
		nvgpu_spinlock_release(&arb->sessions_lock);
	}

	nvgpu_spinlock_acquire(&session->session_lock);
	nvgpu_list_for_each_entry_safe(dev, tmp, &session->targets,
			nvgpu_clk_dev, node) {
		nvgpu_ref_put(&dev->refcount, nvgpu_clk_arb_free_fd);
		nvgpu_list_del(&dev->node);
	}
	nvgpu_spinlock_release(&session->session_lock);

	nvgpu_kfree(g, session);
}

void nvgpu_clk_arb_release_session(struct gk20a *g,
	struct nvgpu_clk_session *session)
{
	struct nvgpu_clk_arb *arb = g->clk_arb;

	clk_arb_dbg(g, " ");

	session->zombie = true;
	nvgpu_ref_put(&session->refcount, nvgpu_clk_arb_free_session);
	if (arb) {
		nvgpu_clk_arb_worker_enqueue(g, &arb->update_arb_work_item);
	}
}

void nvgpu_clk_arb_schedule_vf_table_update(struct gk20a *g)
{
	struct nvgpu_clk_arb *arb = g->clk_arb;

	nvgpu_clk_arb_worker_enqueue(g, &arb->update_vf_table_work_item);
}

/* This function is inherently unsafe to call while arbiter is running
 * arbiter must be blocked before calling this function
 */
u32 nvgpu_clk_arb_get_current_pstate(struct gk20a *g)
{
	return NV_ACCESS_ONCE(g->clk_arb->actual->pstate);
}

void nvgpu_clk_arb_pstate_change_lock(struct gk20a *g, bool lock)
{
	struct nvgpu_clk_arb *arb = g->clk_arb;

	if (lock) {
		nvgpu_mutex_acquire(&arb->pstate_lock);
	} else {
		nvgpu_mutex_release(&arb->pstate_lock);
	}
}

bool nvgpu_clk_arb_is_valid_domain(struct gk20a *g, u32 api_domain)
{
	u32 clk_domains = g->ops.clk_arb.get_arbiter_clk_domains(g);

	switch (api_domain) {
	case NVGPU_CLK_DOMAIN_MCLK:
		return (clk_domains & CTRL_CLK_DOMAIN_MCLK) != 0;

	case NVGPU_CLK_DOMAIN_GPCCLK:
		return (clk_domains & CTRL_CLK_DOMAIN_GPCCLK) != 0;

	default:
		return false;
	}
}

int nvgpu_clk_arb_get_arbiter_clk_range(struct gk20a *g, u32 api_domain,
		u16 *min_mhz, u16 *max_mhz)
{
	int ret;

	switch (api_domain) {
	case NVGPU_CLK_DOMAIN_MCLK:
		ret = g->ops.clk_arb.get_arbiter_clk_range(g,
				CTRL_CLK_DOMAIN_MCLK, min_mhz, max_mhz);
		return ret;

	case NVGPU_CLK_DOMAIN_GPCCLK:
		ret = g->ops.clk_arb.get_arbiter_clk_range(g,
				CTRL_CLK_DOMAIN_GPCCLK, min_mhz, max_mhz);
		return ret;

	default:
		return -EINVAL;
	}
}

int nvgpu_clk_arb_get_arbiter_clk_f_points(struct gk20a *g,
	u32 api_domain, u32 *max_points, u16 *fpoints)
{
	int err;

	switch (api_domain) {
	case NVGPU_CLK_DOMAIN_GPCCLK:
		err = g->ops.clk_arb.get_arbiter_f_points(g,
			CTRL_CLK_DOMAIN_GPCCLK, max_points, fpoints);
		if (err || !fpoints) {
			return err;
		}
		return 0;
	case NVGPU_CLK_DOMAIN_MCLK:
		return g->ops.clk_arb.get_arbiter_f_points(g,
			CTRL_CLK_DOMAIN_MCLK, max_points, fpoints);
	default:
		return -EINVAL;
	}
}

int nvgpu_clk_arb_get_session_target_mhz(struct nvgpu_clk_session *session,
		u32 api_domain, u16 *freq_mhz)
{
	int err = 0;
	struct nvgpu_clk_arb_target *target = session->target;

	if (!nvgpu_clk_arb_is_valid_domain(session->g, api_domain)) {
		return -EINVAL;
	}

	switch (api_domain) {
		case NVGPU_CLK_DOMAIN_MCLK:
			*freq_mhz = target->mclk;
			break;

		case NVGPU_CLK_DOMAIN_GPCCLK:
			*freq_mhz = target->gpc2clk;
			break;

		default:
			*freq_mhz = 0;
			err = -EINVAL;
	}
	return err;
}

int nvgpu_clk_arb_get_arbiter_actual_mhz(struct gk20a *g,
		u32 api_domain, u16 *freq_mhz)
{
	struct nvgpu_clk_arb *arb = g->clk_arb;
	int err = 0;
	struct nvgpu_clk_arb_target *actual = arb->actual;

	if (!nvgpu_clk_arb_is_valid_domain(g, api_domain)) {
		return -EINVAL;
	}

	switch (api_domain) {
		case NVGPU_CLK_DOMAIN_MCLK:
			*freq_mhz = actual->mclk;
			break;

		case NVGPU_CLK_DOMAIN_GPCCLK:
			*freq_mhz = actual->gpc2clk ;
			break;

		default:
			*freq_mhz = 0;
			err = -EINVAL;
	}
	return err;
}

unsigned long nvgpu_clk_measure_freq(struct gk20a *g, u32 api_domain)
{
	unsigned long freq = 0UL;

	switch (api_domain) {
	/*
	 * Incase of iGPU clocks to each parition (GPC, SYS, LTC, XBAR) are
	 * generated using 1X GPCCLK and hence should be the same.
	 */
	case CTRL_CLK_DOMAIN_GPCCLK:
	case CTRL_CLK_DOMAIN_SYSCLK:
	case CTRL_CLK_DOMAIN_XBARCLK:
		freq = g->ops.clk.get_rate(g, CTRL_CLK_DOMAIN_GPCCLK);
		break;
	default:
		break;
	}
	return freq;
}

int nvgpu_clk_arb_get_arbiter_effective_mhz(struct gk20a *g,
		u32 api_domain, u16 *freq_mhz)
{
	u64 freq_mhz_u64;
	if (!nvgpu_clk_arb_is_valid_domain(g, api_domain)) {
		return -EINVAL;
	}

	switch (api_domain) {
	case NVGPU_CLK_DOMAIN_MCLK:
		freq_mhz_u64 = g->ops.clk.measure_freq(g,
					CTRL_CLK_DOMAIN_MCLK) /	1000000ULL;
		break;

	case NVGPU_CLK_DOMAIN_GPCCLK:
		freq_mhz_u64 = g->ops.clk.measure_freq(g,
					CTRL_CLK_DOMAIN_GPCCLK) / 1000000ULL;
		break;

	default:
		return -EINVAL;
	}

	nvgpu_assert(freq_mhz_u64 <= (u64)U16_MAX);
	*freq_mhz = (u16)freq_mhz_u64;
	return 0;
}
