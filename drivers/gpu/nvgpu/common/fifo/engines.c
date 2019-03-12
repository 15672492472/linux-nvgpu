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


#include <nvgpu/log.h>
#include <nvgpu/errno.h>
#include <nvgpu/timers.h>
#include <nvgpu/bitops.h>
#include <nvgpu/pmu.h>
#include <nvgpu/runlist.h>
#include <nvgpu/engines.h>
#include <nvgpu/engine_status.h>
#include <nvgpu/pbdma_status.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>

#include "gk20a/fifo_gk20a.h"

enum nvgpu_fifo_engine nvgpu_engine_enum_from_type(struct gk20a *g,
					u32 engine_type)
{
	enum nvgpu_fifo_engine ret = NVGPU_ENGINE_INVAL_GK20A;

	if ((g->ops.top.is_engine_gr != NULL) &&
					(g->ops.top.is_engine_ce != NULL)) {
		if (g->ops.top.is_engine_gr(g, engine_type)) {
			ret = NVGPU_ENGINE_GR_GK20A;
		} else if (g->ops.top.is_engine_ce(g, engine_type)) {
			/* Lets consider all the CE engine have separate
			 * runlist at this point. We can identify the
			 * NVGPU_ENGINE_GRCE_GK20A type CE using runlist_id
			 * comparsion logic with GR runlist_id in
			 * init_engine_info()
			 */
			ret = NVGPU_ENGINE_ASYNC_CE_GK20A;
		} else {
			ret = NVGPU_ENGINE_INVAL_GK20A;
		}
	}

	return ret;
}

struct fifo_engine_info_gk20a *nvgpu_engine_get_active_eng_info(
	struct gk20a *g, u32 engine_id)
{
	struct fifo_gk20a *f = NULL;
	u32 engine_id_idx;
	struct fifo_engine_info_gk20a *info = NULL;

	if (g == NULL) {
		return info;
	}

	f = &g->fifo;

	if (engine_id < f->max_engines) {
		for (engine_id_idx = 0; engine_id_idx < f->num_engines;
				++engine_id_idx) {
			if (engine_id ==
					f->active_engines_list[engine_id_idx]) {
				info = &f->engine_info[engine_id];
				break;
			}
		}
	}

	if (info == NULL) {
		nvgpu_err(g, "engine_id is not in active list/invalid %d",
			engine_id);
	}

	return info;
}

u32 nvgpu_engine_get_ids(struct gk20a *g,
		u32 *engine_ids, u32 engine_id_sz,
		enum nvgpu_fifo_engine engine_enum)
{
	struct fifo_gk20a *f = NULL;
	u32 instance_cnt = 0;
	u32 engine_id_idx;
	u32 active_engine_id = 0;
	struct fifo_engine_info_gk20a *info = NULL;

	if ((g == NULL) || (engine_id_sz == 0U) ||
			(engine_enum == NVGPU_ENGINE_INVAL_GK20A)) {
		return instance_cnt;
	}

	f = &g->fifo;
	for (engine_id_idx = 0; engine_id_idx < f->num_engines;
			 ++engine_id_idx) {
		active_engine_id = f->active_engines_list[engine_id_idx];
		info = &f->engine_info[active_engine_id];

		if (info->engine_enum == engine_enum) {
			if (instance_cnt < engine_id_sz) {
				engine_ids[instance_cnt] = active_engine_id;
				++instance_cnt;
			} else {
				nvgpu_log_info(g, "warning engine_id table sz is small %d",
					engine_id_sz);
			}
		}
	}
	return instance_cnt;
}

bool nvgpu_engine_check_valid_id(struct gk20a *g, u32 engine_id)
{
	struct fifo_gk20a *f = NULL;
	u32 engine_id_idx;
	bool valid = false;

	if (g == NULL) {
		return valid;
	}

	f = &g->fifo;

	if (engine_id < f->max_engines) {
		for (engine_id_idx = 0; engine_id_idx < f->num_engines;
				++engine_id_idx) {
			if (engine_id == f->active_engines_list[engine_id_idx]) {
				valid = true;
				break;
			}
		}
	}

	if (!valid) {
		nvgpu_err(g, "engine_id is not in active list/invalid %d",
			engine_id);
	}

	return valid;
}

u32 nvgpu_engine_get_gr_id(struct gk20a *g)
{
	u32 gr_engine_cnt = 0;
	u32 gr_engine_id = FIFO_INVAL_ENGINE_ID;

	/* Consider 1st available GR engine */
	gr_engine_cnt = nvgpu_engine_get_ids(g, &gr_engine_id,
			1, NVGPU_ENGINE_GR_GK20A);

	if (gr_engine_cnt == 0U) {
		nvgpu_err(g, "No GR engine available on this device!");
	}

	return gr_engine_id;
}

u32 nvgpu_engine_act_interrupt_mask(struct gk20a *g, u32 act_eng_id)
{
	struct fifo_engine_info_gk20a *engine_info = NULL;

	engine_info = nvgpu_engine_get_active_eng_info(g, act_eng_id);
	if (engine_info != NULL) {
		return engine_info->intr_mask;
	}

	return 0;
}

u32 nvgpu_engine_interrupt_mask(struct gk20a *g)
{
	u32 eng_intr_mask = 0;
	unsigned int i;
	u32 active_engine_id = 0;
	enum nvgpu_fifo_engine engine_enum;

	for (i = 0; i < g->fifo.num_engines; i++) {
		u32 intr_mask;
		active_engine_id = g->fifo.active_engines_list[i];
		intr_mask = g->fifo.engine_info[active_engine_id].intr_mask;
		engine_enum = g->fifo.engine_info[active_engine_id].engine_enum;
		if (((engine_enum == NVGPU_ENGINE_GRCE_GK20A) ||
		     (engine_enum == NVGPU_ENGINE_ASYNC_CE_GK20A)) &&
		    ((g->ops.ce2.isr_stall == NULL) ||
		     (g->ops.ce2.isr_nonstall == NULL))) {
				continue;
		}

		eng_intr_mask |= intr_mask;
	}

	return eng_intr_mask;
}

u32 nvgpu_engine_get_all_ce_reset_mask(struct gk20a *g)
{
	u32 reset_mask = 0;
	enum nvgpu_fifo_engine engine_enum;
	struct fifo_gk20a *f = NULL;
	u32 engine_id_idx;
	struct fifo_engine_info_gk20a *engine_info;
	u32 active_engine_id = 0;

	if (g == NULL) {
		return reset_mask;
	}

	f = &g->fifo;

	for (engine_id_idx = 0; engine_id_idx < f->num_engines;
			++engine_id_idx) {
		active_engine_id = f->active_engines_list[engine_id_idx];
		engine_info = &f->engine_info[active_engine_id];
		engine_enum = engine_info->engine_enum;

		if ((engine_enum == NVGPU_ENGINE_GRCE_GK20A) ||
			(engine_enum == NVGPU_ENGINE_ASYNC_CE_GK20A)) {
				reset_mask |= engine_info->reset_mask;
		}
	}

	return reset_mask;
}

#ifdef NVGPU_ENGINE

int nvgpu_engine_enable_activity(struct gk20a *g,
				struct fifo_engine_info_gk20a *eng_info)
{
	nvgpu_log(g, gpu_dbg_info, "start");

	gk20a_fifo_set_runlist_state(g, BIT32(eng_info->runlist_id),
			RUNLIST_ENABLED);
	return 0;
}

int nvgpu_engine_enable_activity_all(struct gk20a *g)
{
	unsigned int i;
	int err = 0, ret = 0;

	for (i = 0; i < g->fifo.num_engines; i++) {
		u32 active_engine_id = g->fifo.active_engines_list[i];
		err = nvgpu_engine_enable_activity(g,
				&g->fifo.engine_info[active_engine_id]);
		if (err != 0) {
			nvgpu_err(g,
				"failed to enable engine %d activity", active_engine_id);
			ret = err;
		}
	}

	return ret;
}

int nvgpu_engine_disable_activity(struct gk20a *g,
				struct fifo_engine_info_gk20a *eng_info,
				bool wait_for_idle)
{
	u32 pbdma_chid = FIFO_INVAL_CHANNEL_ID;
	u32 engine_chid = FIFO_INVAL_CHANNEL_ID;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret = -EINVAL;
	struct channel_gk20a *ch = NULL;
	int err = 0;
	struct nvgpu_engine_status_info engine_status;
	struct nvgpu_pbdma_status_info pbdma_status;

	nvgpu_log_fn(g, " ");

	g->ops.engine_status.read_engine_status_info(g, eng_info->engine_id,
		 &engine_status);
	if (engine_status.is_busy && !wait_for_idle) {
		return -EBUSY;
	}

	if (g->ops.pmu.is_pmu_supported(g)) {
		mutex_ret = nvgpu_pmu_mutex_acquire(&g->pmu,
						PMU_MUTEX_ID_FIFO, &token);
	}

	gk20a_fifo_set_runlist_state(g, BIT32(eng_info->runlist_id),
			RUNLIST_DISABLED);

	/* chid from pbdma status */
	g->ops.pbdma_status.read_pbdma_status_info(g, eng_info->pbdma_id,
		&pbdma_status);
	if (nvgpu_pbdma_status_is_chsw_valid(&pbdma_status) ||
			nvgpu_pbdma_status_is_chsw_save(&pbdma_status)) {
		pbdma_chid = pbdma_status.id;
	} else if (nvgpu_pbdma_status_is_chsw_load(&pbdma_status) ||
			nvgpu_pbdma_status_is_chsw_switch(&pbdma_status)) {
		pbdma_chid = pbdma_status.next_id;
	}

	if (pbdma_chid != FIFO_INVAL_CHANNEL_ID) {
		ch = gk20a_channel_from_id(g, pbdma_chid);
		if (ch != NULL) {
			err = g->ops.fifo.preempt_channel(g, ch);
			gk20a_channel_put(ch);
		}
		if (err != 0) {
			goto clean_up;
		}
	}

	/* chid from engine status */
	g->ops.engine_status.read_engine_status_info(g, eng_info->engine_id,
		 &engine_status);
	if (nvgpu_engine_status_is_ctxsw_valid(&engine_status) ||
	    nvgpu_engine_status_is_ctxsw_save(&engine_status)) {
		engine_chid = engine_status.ctx_id;
	} else if (nvgpu_engine_status_is_ctxsw_switch(&engine_status) ||
	    nvgpu_engine_status_is_ctxsw_load(&engine_status)) {
		engine_chid = engine_status.ctx_next_id;
	}

	if (engine_chid != FIFO_INVAL_ENGINE_ID && engine_chid != pbdma_chid) {
		ch = gk20a_channel_from_id(g, engine_chid);
		if (ch != NULL) {
			err = g->ops.fifo.preempt_channel(g, ch);
			gk20a_channel_put(ch);
		}
		if (err != 0) {
			goto clean_up;
		}
	}

clean_up:
	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}

	if (err != 0) {
		nvgpu_log_fn(g, "failed");
		if (nvgpu_engine_enable_activity(g, eng_info) != 0) {
			nvgpu_err(g,
				"failed to enable gr engine activity");
		}
	} else {
		nvgpu_log_fn(g, "done");
	}
	return err;
}

int nvgpu_engine_disable_activity_all(struct gk20a *g,
					   bool wait_for_idle)
{
	unsigned int i;
	int err = 0, ret = 0;
	u32 active_engine_id;

	for (i = 0; i < g->fifo.num_engines; i++) {
		active_engine_id = g->fifo.active_engines_list[i];
		err = nvgpu_engine_disable_activity(g,
				&g->fifo.engine_info[active_engine_id],
				wait_for_idle);
		if (err != 0) {
			nvgpu_err(g, "failed to disable engine %d activity",
				active_engine_id);
			ret = err;
			break;
		}
	}

	if (err != 0) {
		while (i-- != 0U) {
			active_engine_id = g->fifo.active_engines_list[i];
			err = nvgpu_engine_enable_activity(g,
					&g->fifo.engine_info[active_engine_id]);
			if (err != 0) {
				nvgpu_err(g,
					"failed to re-enable engine %d activity",
					active_engine_id);
			}
		}
	}

	return ret;
}

int nvgpu_engine_wait_for_idle(struct gk20a *g)
{
	struct nvgpu_timeout timeout;
	u32 delay = GR_IDLE_CHECK_DEFAULT;
	int ret = 0;
	u32 i, host_num_engines;
	struct nvgpu_engine_status_info engine_status;

	nvgpu_log_fn(g, " ");

	host_num_engines =
		 nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_ENGINES);

	nvgpu_timeout_init(g, &timeout, gk20a_get_gr_idle_timeout(g),
			   NVGPU_TIMER_CPU_TIMER);

	for (i = 0; i < host_num_engines; i++) {
		ret = -ETIMEDOUT;
		do {
			g->ops.engine_status.read_engine_status_info(g, i,
				&engine_status);
			if (!engine_status.is_busy) {
				ret = 0;
				break;
			}

			nvgpu_usleep_range(delay, delay * 2U);
			delay = min_t(u32,
					delay << 1, GR_IDLE_CHECK_MAX);
		} while (nvgpu_timeout_expired(&timeout) == 0);

		if (ret != 0) {
			/* possible causes:
			 * check register settings programmed in hal set by
			 * elcg_init_idle_filters and init_therm_setup_hw
			 */
			nvgpu_err(g, "cannot idle engine: %u "
					"engine_status: 0x%08x", i,
					engine_status.reg_data);
			break;
		}
	}

	nvgpu_log_fn(g, "done");

	return ret;
}

#endif /* NVGPU_ENGINE */


