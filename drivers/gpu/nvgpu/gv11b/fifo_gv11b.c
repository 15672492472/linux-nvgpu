/*
 * GV11B fifo
 *
 * Copyright (c) 2015-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/bug.h>
#include <nvgpu/semaphore.h>
#include <nvgpu/timers.h>
#include <nvgpu/log.h>
#include <nvgpu/dma.h>
#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/gmmu.h>
#include <nvgpu/soc.h>
#include <nvgpu/debug.h>
#include <nvgpu/nvhost.h>
#include <nvgpu/barrier.h>
#include <nvgpu/mm.h>
#include <nvgpu/log2.h>
#include <nvgpu/io_usermode.h>
#include <nvgpu/ptimer.h>
#include <nvgpu/io.h>
#include <nvgpu/utils.h>
#include <nvgpu/fifo.h>
#include <nvgpu/rc.h>
#include <nvgpu/runlist.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/unit.h>
#include <nvgpu/nvgpu_err.h>
#include <nvgpu/pbdma_status.h>
#include <nvgpu/engine_status.h>
#include <nvgpu/power_features/cg.h>
#include <nvgpu/power_features/pg.h>
#include <nvgpu/power_features/power_features.h>
#include <nvgpu/gr/fecs_trace.h>
#include <nvgpu/preempt.h>

#include "gk20a/fifo_gk20a.h"

#include <nvgpu/hw/gv11b/hw_pbdma_gv11b.h>
#include <nvgpu/hw/gv11b/hw_fifo_gv11b.h>
#include <nvgpu/hw/gv11b/hw_ram_gv11b.h>
#include <nvgpu/hw/gv11b/hw_top_gv11b.h>
#include <nvgpu/hw/gv11b/hw_gmmu_gv11b.h>

#include "fifo_gv11b.h"
#include "gr_gv11b.h"

static void gv11b_fifo_locked_abort_runlist_active_tsgs(struct gk20a *g,
			unsigned int rc_type,
			u32 runlists_mask)
{
	struct fifo_gk20a *f = &g->fifo;
	struct tsg_gk20a *tsg = NULL;
	unsigned long tsgid;
	struct fifo_runlist_info_gk20a *runlist = NULL;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret = 0;
	int err;
	u32 i;

	nvgpu_err(g, "runlist id unknown, abort active tsgs in runlists");

	/* runlist_lock  are locked by teardown */
	mutex_ret = nvgpu_pmu_lock_acquire(g, &g->pmu,
			PMU_MUTEX_ID_FIFO, &token);

	for (i = 0U; i < f->num_runlists; i++) {
		runlist = &f->active_runlist_info[i];

		if ((runlists_mask & BIT32(runlist->runlist_id)) == 0U) {
			continue;
		}
		nvgpu_log(g, gpu_dbg_info, "abort runlist id %d",
				runlist->runlist_id);

		for_each_set_bit(tsgid, runlist->active_tsgs,
			g->fifo.num_channels) {
			tsg = &g->fifo.tsg[tsgid];

			if (!tsg->abortable) {
				nvgpu_log(g, gpu_dbg_info,
					  "tsg %lu is not abortable, skipping",
					  tsgid);
				continue;
			}
			nvgpu_log(g, gpu_dbg_info, "abort tsg id %lu", tsgid);

			g->ops.tsg.disable(tsg);

			nvgpu_tsg_reset_faulted_eng_pbdma(g, tsg, true, true);

#ifdef CONFIG_GK20A_CTXSW_TRACE
			nvgpu_gr_fecs_trace_add_tsg_reset(g, tsg);
#endif
			if (!g->fifo.deferred_reset_pending) {
				if (rc_type == RC_TYPE_MMU_FAULT) {
					nvgpu_tsg_set_ctx_mmu_error(g, tsg);
					/*
					 * Mark error (returned verbose flag is
					 * ignored since it is not needed here)
					 */
					(void) nvgpu_tsg_mark_error(g, tsg);
				}
			}

			/*
			 * remove all entries from this runlist; don't wait for
			 * the update to finish on hw.
			 */
			err = gk20a_runlist_update_locked(g, runlist->runlist_id,
					NULL, false, false);
			if (err != 0) {
				nvgpu_err(g, "runlist id %d is not cleaned up",
					runlist->runlist_id);
			}

			nvgpu_tsg_abort(g, tsg, false);

			nvgpu_log(g, gpu_dbg_info, "aborted tsg id %lu", tsgid);
		}
	}
	if (mutex_ret == 0) {
		err = nvgpu_pmu_lock_release(g, &g->pmu, PMU_MUTEX_ID_FIFO,
				&token);
		if (err != 0) {
			nvgpu_err(g, "PMU_MUTEX_ID_FIFO not released err=%d",
					err);
		}
	}
}

void gv11b_fifo_teardown_ch_tsg(struct gk20a *g, u32 act_eng_bitmask,
			u32 id, unsigned int id_type, unsigned int rc_type,
			 struct mmu_fault_info *mmfault)
{
	struct tsg_gk20a *tsg = NULL;
	u32 runlists_mask, i;
	unsigned long bit;
	u32 pbdma_bitmask = 0U;
	struct fifo_runlist_info_gk20a *runlist = NULL;
	u32 engine_id;
	u32 client_type = ~U32(0U);
	struct fifo_gk20a *f = &g->fifo;
	u32 runlist_id = FIFO_INVAL_RUNLIST_ID;
	u32 num_runlists = 0U;
	bool deferred_reset_pending = false;

	nvgpu_log_info(g, "acquire engines_reset_mutex");
	nvgpu_mutex_acquire(&g->fifo.engines_reset_mutex);

	nvgpu_fifo_lock_active_runlists(g);

	g->ops.fifo.intr_set_recover_mask(g);

	/* get runlist id and tsg */
	if (id_type == ID_TYPE_TSG) {
		if (id != FIFO_INVAL_TSG_ID) {
			tsg = &g->fifo.tsg[id];
			runlist_id = tsg->runlist_id;
			if (runlist_id != FIFO_INVAL_RUNLIST_ID) {
				num_runlists++;
			} else {
				nvgpu_log_fn(g, "tsg runlist id is invalid");
			}
		} else {
			nvgpu_log_fn(g, "id type is tsg but tsg id is inval");
		}
	} else {
		/*
		 * id type is unknown, get runlist_id if eng mask is such that
		 * it corresponds to single runlist id. If eng mask corresponds
		 * to multiple runlists, then abort all runlists
		 */
		for (i = 0U; i < f->num_runlists; i++) {
			runlist = &f->active_runlist_info[i];

			if ((runlist->eng_bitmask & act_eng_bitmask) != 0U) {
				runlist_id = runlist->runlist_id;
				num_runlists++;
			}
		}
		if (num_runlists > 1U) {
			/* abort all runlists */
			runlist_id = FIFO_INVAL_RUNLIST_ID;
		}
	}

	/* if runlist_id is valid and there is only single runlist to be
	 * aborted, release runlist lock that are not
	 * needed for this recovery
	 */
	if (runlist_id != FIFO_INVAL_RUNLIST_ID && num_runlists == 1U) {
		for (i = 0U; i < f->num_runlists; i++) {
			runlist = &f->active_runlist_info[i];

			if (runlist->runlist_id != runlist_id) {
				nvgpu_log_fn(g, "release runlist_lock for "
						"unused runlist id: %d",
						runlist->runlist_id);
				nvgpu_mutex_release(&runlist->runlist_lock);
			}
		}
	}

	nvgpu_log(g, gpu_dbg_info, "id = %d, id_type = %d, rc_type = %d, "
			"act_eng_bitmask = 0x%x, mmfault ptr = 0x%p",
			 id, id_type, rc_type, act_eng_bitmask, mmfault);

	if (rc_type == RC_TYPE_MMU_FAULT && mmfault != NULL) {
		if (mmfault->faulted_pbdma != INVAL_ID) {
			pbdma_bitmask = BIT32(mmfault->faulted_pbdma);
		}
	}
	runlists_mask = nvgpu_fifo_get_runlists_mask(g, id, id_type,
				act_eng_bitmask, pbdma_bitmask);

	/* Disable runlist scheduler */
	nvgpu_fifo_runlist_set_state(g, runlists_mask, RUNLIST_DISABLED);

	if (nvgpu_cg_pg_disable(g) != 0) {
		nvgpu_warn(g, "fail to disable power mgmt");
	}

	if (rc_type == RC_TYPE_MMU_FAULT) {
		gk20a_debug_dump(g);
		client_type = mmfault->client_type;
		nvgpu_tsg_reset_faulted_eng_pbdma(g, tsg, true, true);
	}

	if (tsg != NULL) {
		g->ops.tsg.disable(tsg);
	}

	/*
	 * Even though TSG preempt timed out, the RC sequence would by design
	 * require s/w to issue another preempt.
	 * If recovery includes an ENGINE_RESET, to not have race conditions,
	 * use RUNLIST_PREEMPT to kick all work off, and cancel any context
	 * load which may be pending. This is also needed to make sure
	 * that all PBDMAs serving the engine are not loaded when engine is
	 * reset.
	 */
	g->ops.fifo.preempt_runlists_for_rc(g, runlists_mask);
	/*
	 * For each PBDMA which serves the runlist, poll to verify the TSG is no
	 * longer on the PBDMA and the engine phase of the preempt has started.
	 */
	if (tsg != NULL) {
		nvgpu_preempt_poll_tsg_on_pbdma(g, tsg);
	}

	nvgpu_mutex_acquire(&f->deferred_reset_mutex);
	g->fifo.deferred_reset_pending = false;
	nvgpu_mutex_release(&f->deferred_reset_mutex);

	/* check if engine reset should be deferred */
	for (i = 0U; i < f->num_runlists; i++) {
		runlist = &f->active_runlist_info[i];

		if (((runlists_mask & BIT32(runlist->runlist_id)) != 0U) &&
		    (runlist->reset_eng_bitmask != 0U)) {

			unsigned long __reset_eng_bitmask =
				 runlist->reset_eng_bitmask;

			for_each_set_bit(bit, &__reset_eng_bitmask,
							g->fifo.max_engines) {
				engine_id = U32(bit);
				if ((tsg != NULL) &&
					 nvgpu_engine_should_defer_reset(g,
					engine_id, client_type, false)) {

					g->fifo.deferred_fault_engines |=
							 BIT64(engine_id);

					/* handled during channel free */
					nvgpu_mutex_acquire(&f->deferred_reset_mutex);
					g->fifo.deferred_reset_pending = true;
					nvgpu_mutex_release(&f->deferred_reset_mutex);

					deferred_reset_pending = true;

					nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
					"sm debugger attached,"
					" deferring channel recovery to channel free");
				} else {
					nvgpu_engine_reset(g, engine_id);
				}
			}
		}
	}

#ifdef CONFIG_GK20A_CTXSW_TRACE
	if (tsg != NULL)
		nvgpu_gr_fecs_trace_add_tsg_reset(g, tsg);
#endif
	if (tsg != NULL) {
		if (deferred_reset_pending) {
			g->ops.tsg.disable(tsg);
		} else {
			if (rc_type == RC_TYPE_MMU_FAULT) {
				nvgpu_tsg_set_ctx_mmu_error(g, tsg);
			}
			(void)nvgpu_tsg_mark_error(g, tsg);
			nvgpu_tsg_abort(g, tsg, false);
		}
	} else {
		gv11b_fifo_locked_abort_runlist_active_tsgs(g, rc_type,
			runlists_mask);
	}

	nvgpu_fifo_runlist_set_state(g, runlists_mask, RUNLIST_ENABLED);

	if (nvgpu_cg_pg_enable(g) != 0) {
		nvgpu_warn(g, "fail to enable power mgmt");
	}

	g->ops.fifo.intr_unset_recover_mask(g);

	/* release runlist_lock */
	if (runlist_id != FIFO_INVAL_RUNLIST_ID) {
		nvgpu_log_fn(g, "release runlist_lock runlist_id = %d",
				runlist_id);
		runlist = f->runlist_info[runlist_id];
		nvgpu_mutex_release(&runlist->runlist_lock);
	} else {
		nvgpu_fifo_unlock_active_runlists(g);
	}

	nvgpu_log_info(g, "release engines_reset_mutex");
	nvgpu_mutex_release(&g->fifo.engines_reset_mutex);
}

int gv11b_init_fifo_reset_enable_hw(struct gk20a *g)
{
	u32 timeout;

	nvgpu_log_fn(g, " ");

	/* enable pmc pfifo */
	g->ops.mc.reset(g, g->ops.mc.reset_mask(g, NVGPU_UNIT_FIFO));

	nvgpu_cg_slcg_ce2_load_enable(g);

	nvgpu_cg_slcg_fifo_load_enable(g);

	nvgpu_cg_blcg_fifo_load_enable(g);

	timeout = gk20a_readl(g, fifo_fb_timeout_r());
	nvgpu_log_info(g, "fifo_fb_timeout reg val = 0x%08x", timeout);
	if (!nvgpu_platform_is_silicon(g)) {
		timeout = set_field(timeout, fifo_fb_timeout_period_m(),
					fifo_fb_timeout_period_max_f());
		timeout = set_field(timeout, fifo_fb_timeout_detection_m(),
					fifo_fb_timeout_detection_disabled_f());
		nvgpu_log_info(g, "new fifo_fb_timeout reg val = 0x%08x",
					timeout);
		gk20a_writel(g, fifo_fb_timeout_r(), timeout);
	}

	g->ops.pbdma.setup_hw(g);

	g->ops.fifo.intr_0_enable(g, true);
	g->ops.fifo.intr_1_enable(g, true);

	nvgpu_log_fn(g, "done");

	return 0;
}

int gv11b_init_fifo_setup_hw(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;

	f->max_subctx_count = g->ops.gr.init.get_max_subctx_count();

	/* configure userd writeback timer */
	nvgpu_writel(g, fifo_userd_writeback_r(),
		fifo_userd_writeback_timer_f(
			fifo_userd_writeback_timer_100us_v()));

	return 0;
}

u32 gv11b_fifo_mmu_fault_id_to_pbdma_id(struct gk20a *g, u32 mmu_fault_id)
{
	u32 num_pbdma, reg_val, fault_id_pbdma0;

	reg_val = nvgpu_readl(g, fifo_cfg0_r());
	num_pbdma = fifo_cfg0_num_pbdma_v(reg_val);
	fault_id_pbdma0 = fifo_cfg0_pbdma_fault_id_v(reg_val);

	if (mmu_fault_id >= fault_id_pbdma0 &&
			mmu_fault_id <= fault_id_pbdma0 + num_pbdma - 1U) {
		return mmu_fault_id - fault_id_pbdma0;
	}

	return INVAL_ID;
}
