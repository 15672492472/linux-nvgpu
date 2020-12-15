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

#include <nvgpu/gk20a.h>
#include <nvgpu/io.h>
#include <nvgpu/class.h>
#include <nvgpu/channel.h>
#include <nvgpu/static_analysis.h>

#include <nvgpu/gr/config.h>
#include <nvgpu/gr/gr.h>
#include <nvgpu/gr/ctx.h>
#include <nvgpu/gr/gr_falcon.h>
#include <nvgpu/gr/gr_intr.h>
#include <nvgpu/gr/gr_utils.h>

#include "gr_intr_gp10b.h"

#include <nvgpu/hw/gp10b/hw_gr_gp10b.h>

#ifdef CONFIG_NVGPU_CILP
static int gp10b_gr_intr_clear_cilp_preempt_pending(struct gk20a *g,
					       struct nvgpu_channel *fault_ch)
{
	struct nvgpu_tsg *tsg;
	struct nvgpu_gr_ctx *gr_ctx;

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr, " ");

	tsg = nvgpu_tsg_from_ch(fault_ch);
	if (tsg == NULL) {
		return -EINVAL;
	}

	gr_ctx = tsg->gr_ctx;

	/*
	 * The ucode is self-clearing, so all we need to do here is
	 * to clear cilp_preempt_pending.
	 */
	if (!nvgpu_gr_ctx_get_cilp_preempt_pending(gr_ctx)) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
				"CILP is already cleared for chid %d\n",
				fault_ch->chid);
		return 0;
	}

	nvgpu_gr_ctx_set_cilp_preempt_pending(gr_ctx, false);
	nvgpu_gr_clear_cilp_preempt_pending_chid(g);

	return 0;
}

static int gp10b_gr_intr_get_cilp_preempt_pending_chid(struct gk20a *g,
					u32 *chid_ptr)
{
	struct nvgpu_gr_ctx *gr_ctx;
	struct nvgpu_channel *ch;
	struct nvgpu_tsg *tsg;
	u32 chid;
	int ret = -EINVAL;

	chid = nvgpu_gr_get_cilp_preempt_pending_chid(g);
	if (chid == NVGPU_INVALID_CHANNEL_ID) {
		return ret;
	}

	ch = nvgpu_channel_from_id(g, chid);
	if (ch == NULL) {
		return ret;
	}

	tsg = nvgpu_tsg_from_ch(ch);
	if (tsg == NULL) {
		nvgpu_channel_put(ch);
		return -EINVAL;
	}

	gr_ctx = tsg->gr_ctx;

	if (nvgpu_gr_ctx_get_cilp_preempt_pending(gr_ctx)) {
		*chid_ptr = chid;
		ret = 0;
	}

	nvgpu_channel_put(ch);

	return ret;
}
#endif /* CONFIG_NVGPU_CILP */

int gp10b_gr_intr_handle_fecs_error(struct gk20a *g,
				struct nvgpu_channel *ch_ptr,
				struct nvgpu_gr_isr_data *isr_data)
{
#ifdef CONFIG_NVGPU_CILP
	struct nvgpu_channel *ch;
	u32 chid = NVGPU_INVALID_CHANNEL_ID;
	int ret = 0;
#ifdef CONFIG_NVGPU_CHANNEL_TSG_CONTROL
	struct nvgpu_tsg *tsg;
#endif
#endif
	struct nvgpu_fecs_host_intr_status fecs_host_intr;
	u32 gr_fecs_intr = g->ops.gr.falcon.fecs_host_intr_status(g,
						&fecs_host_intr);

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr, " ");

	if (gr_fecs_intr == 0U) {
		return 0;
	}

#ifdef CONFIG_NVGPU_CILP
	/*
	 * INTR1 (bit 1 of the HOST_INT_STATUS_CTXSW_INTR)
	 * indicates that a CILP ctxsw save has finished
	 */
	if (fecs_host_intr.ctxsw_intr1 != 0U) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
				"CILP: ctxsw save completed!\n");

		/* now clear the interrupt */
		g->ops.gr.falcon.fecs_host_clear_intr(g,
					fecs_host_intr.ctxsw_intr1);

		ret = gp10b_gr_intr_get_cilp_preempt_pending_chid(g, &chid);
		if ((ret != 0) || (chid == NVGPU_INVALID_CHANNEL_ID)) {
			goto clean_up;
		}

		ch = nvgpu_channel_from_id(g, chid);
		if (ch == NULL) {
			goto clean_up;
		}

		/* set preempt_pending to false */
		ret = gp10b_gr_intr_clear_cilp_preempt_pending(g, ch);
		if (ret != 0) {
			nvgpu_err(g, "CILP: error while unsetting CILP preempt pending!");
			nvgpu_channel_put(ch);
			goto clean_up;
		}

#ifdef CONFIG_NVGPU_DEBUGGER
		/* Post events to UMD */
		g->ops.debugger.post_events(ch);
#endif

#ifdef CONFIG_NVGPU_CHANNEL_TSG_CONTROL
		tsg = &g->fifo.tsg[ch->tsgid];
		g->ops.tsg.post_event_id(tsg,
				NVGPU_EVENT_ID_CILP_PREEMPTION_COMPLETE);
#endif

		nvgpu_channel_put(ch);
	}

clean_up:
#endif /* CONFIG_NVGPU_CILP */

	/* handle any remaining interrupts */
	return nvgpu_gr_intr_handle_fecs_error(g, ch_ptr, isr_data);
}

void gp10b_gr_intr_set_go_idle_timeout(struct gk20a *g, u32 data)
{
	nvgpu_writel(g, gr_fe_go_idle_timeout_r(), data);
}

void gp10b_gr_intr_set_coalesce_buffer_size(struct gk20a *g, u32 data)
{
	u32 val;

	nvgpu_log_fn(g, " ");

	val = nvgpu_readl(g, gr_gpcs_tc_debug0_r());
	val = set_field(val, gr_gpcs_tc_debug0_limit_coalesce_buffer_size_m(),
			gr_gpcs_tc_debug0_limit_coalesce_buffer_size_f(data));
	nvgpu_writel(g, gr_gpcs_tc_debug0_r(), val);

	nvgpu_log_fn(g, "done");
}
