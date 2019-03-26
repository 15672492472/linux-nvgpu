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
#include <nvgpu/log.h>
#include <nvgpu/io.h>
#include <nvgpu/soc.h>
#include <nvgpu/ptimer.h>
#include <nvgpu/channel.h>
#include <nvgpu/tsg.h>
#include <nvgpu/nvgpu_err.h>
#include <nvgpu/error_notifier.h>
#include <nvgpu/pbdma_status.h>

#include <hal/fifo/fifo_intr_gk20a.h>

#include <nvgpu/hw/gk20a/hw_fifo_gk20a.h>
#include <nvgpu/hw/gk20a/hw_pbdma_gk20a.h> /* TODO: remove */

static u32 gk20a_fifo_intr_0_error_mask(struct gk20a *g)
{
	u32 intr_0_error_mask =
		fifo_intr_0_bind_error_pending_f() |
		fifo_intr_0_sched_error_pending_f() |
		fifo_intr_0_chsw_error_pending_f() |
		fifo_intr_0_fb_flush_timeout_pending_f() |
		fifo_intr_0_dropped_mmu_fault_pending_f() |
		fifo_intr_0_mmu_fault_pending_f() |
		fifo_intr_0_lb_error_pending_f() |
		fifo_intr_0_pio_error_pending_f();

	return intr_0_error_mask;
}

static u32 gk20a_fifo_intr_0_en_mask(struct gk20a *g)
{
	u32 intr_0_en_mask;

	intr_0_en_mask = gk20a_fifo_intr_0_error_mask(g);

	intr_0_en_mask |= fifo_intr_0_runlist_event_pending_f() |
				 fifo_intr_0_pbdma_intr_pending_f();

	return intr_0_en_mask;
}


void gk20a_fifo_intr_0_enable(struct gk20a *g, bool enable)
{
	unsigned int i;
	u32 intr_stall, timeout, mask;
	u32 host_num_pbdma = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_PBDMA);

	if (!enable) {
		nvgpu_writel(g, fifo_intr_en_0_r(), 0U);
		return;
	}

	if (g->ops.fifo.apply_ctxsw_timeout_intr != NULL) {
		g->ops.fifo.apply_ctxsw_timeout_intr(g);
	} else {
		/* timeout is in us. Enable ctxsw timeout */
		timeout = g->ctxsw_timeout_period_ms * 1000U;
		timeout = scale_ptimer(timeout,
			ptimer_scalingfactor10x(g->ptimer_src_freq));
		timeout |= fifo_eng_timeout_detection_enabled_f();
		nvgpu_writel(g, fifo_eng_timeout_r(), timeout);
	}

	/* clear and enable pbdma interrupt */
	for (i = 0; i < host_num_pbdma; i++) {
		nvgpu_writel(g, pbdma_intr_0_r(i), U32_MAX);
		nvgpu_writel(g, pbdma_intr_1_r(i), U32_MAX);

		intr_stall = nvgpu_readl(g, pbdma_intr_stall_r(i));
		intr_stall &= ~pbdma_intr_stall_lbreq_enabled_f();
		nvgpu_writel(g, pbdma_intr_stall_r(i), intr_stall);
		nvgpu_log_info(g, "pbdma id:%u, intr_en_0 0x%08x", i,
				intr_stall);
		nvgpu_writel(g, pbdma_intr_en_0_r(i), intr_stall);
		intr_stall = nvgpu_readl(g, pbdma_intr_stall_1_r(i));
		/*
		 * For bug 2082123
		 * Mask the unused HCE_RE_ILLEGAL_OP bit from the interrupt.
		 */
		intr_stall &= ~pbdma_intr_stall_1_hce_illegal_op_enabled_f();
		nvgpu_log_info(g, "pbdma id:%u, intr_en_1 0x%08x", i,
				intr_stall);
		nvgpu_writel(g, pbdma_intr_en_1_r(i), intr_stall);
	}

	/* reset runlist interrupts */
	nvgpu_writel(g, fifo_intr_runlist_r(), ~U32(0U));

	/* clear and enable pfifo interrupt */
	nvgpu_writel(g, fifo_intr_0_r(), U32_MAX);
	mask = gk20a_fifo_intr_0_en_mask(g);
	nvgpu_log_info(g, "fifo_intr_en_0 0x%08x", mask);
	nvgpu_writel(g, fifo_intr_en_0_r(), mask);
}

void gk20a_fifo_intr_1_enable(struct gk20a *g, bool enable)
{
	if (enable) {
		nvgpu_writel(g, fifo_intr_en_1_r(),
			fifo_intr_0_channel_intr_pending_f());
		nvgpu_log_info(g, "fifo_intr_en_1 = 0x%08x",
			nvgpu_readl(g, fifo_intr_en_1_r()));
	} else {
		nvgpu_writel(g, fifo_intr_en_1_r(), 0U);
	}
}

u32 gk20a_fifo_intr_1_isr(struct gk20a *g)
{
	u32 fifo_intr = nvgpu_readl(g, fifo_intr_0_r());
	u32 clear_intr = 0U;

	nvgpu_log(g, gpu_dbg_intr, "fifo nonstall isr %08x\n", fifo_intr);

	if ((fifo_intr & fifo_intr_0_channel_intr_pending_f()) != 0U) {
		clear_intr = fifo_intr_0_channel_intr_pending_f();
	}

	nvgpu_writel(g, fifo_intr_0_r(), clear_intr);

	return GK20A_NONSTALL_OPS_WAKEUP_SEMAPHORE;
}

void gk20a_fifo_intr_handle_chsw_error(struct gk20a *g)
{
	u32 intr;

	intr = nvgpu_readl(g, fifo_intr_chsw_error_r());
	nvgpu_report_host_error(g, 0,
			GPU_HOST_PFIFO_CHSW_ERROR, intr);
	nvgpu_err(g, "chsw: %08x", intr);
	g->ops.gr.dump_gr_falcon_stats(g);
	nvgpu_writel(g, fifo_intr_chsw_error_r(), intr);
}

static u32 gk20a_fifo_intr_handle_errors(struct gk20a *g, u32 fifo_intr)
{
	u32 handled = 0U;

	nvgpu_log_fn(g, "fifo_intr=0x%08x", fifo_intr);

	if ((fifo_intr & fifo_intr_0_pio_error_pending_f()) != 0U) {
		/* pio mode is unused.  this shouldn't happen, ever. */
		/* should we clear it or just leave it pending? */
		nvgpu_err(g, "fifo pio error!");
		BUG();
	}

	if ((fifo_intr & fifo_intr_0_bind_error_pending_f()) != 0U) {
		u32 bind_error = nvgpu_readl(g, fifo_intr_bind_error_r());

		nvgpu_report_host_error(g, 0,
				GPU_HOST_PFIFO_BIND_ERROR, bind_error);
		nvgpu_err(g, "fifo bind error: 0x%08x", bind_error);
		handled |= fifo_intr_0_bind_error_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_chsw_error_pending_f()) != 0U) {
		gk20a_fifo_intr_handle_chsw_error(g);
		handled |= fifo_intr_0_chsw_error_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_fb_flush_timeout_pending_f()) != 0U) {
		nvgpu_err(g, "fifo fb flush timeout error");
		handled |= fifo_intr_0_fb_flush_timeout_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_lb_error_pending_f()) != 0U) {
		nvgpu_err(g, "fifo lb error");
		handled |= fifo_intr_0_lb_error_pending_f();
	}

	return handled;
}

void gk20a_fifo_intr_handle_runlist_event(struct gk20a *g)
{
	u32 runlist_event = nvgpu_readl(g, fifo_intr_runlist_r());

	nvgpu_log(g, gpu_dbg_intr, "runlist event %08x",
		  runlist_event);

	nvgpu_writel(g, fifo_intr_runlist_r(), runlist_event);
}

void gk20a_fifo_intr_0_isr(struct gk20a *g)
{
	u32 clear_intr = 0U;
	u32 fifo_intr = nvgpu_readl(g, fifo_intr_0_r());

	/* TODO: sw_ready is needed only for recovery part */
	if (!g->fifo.sw_ready) {
		nvgpu_err(g, "unhandled fifo intr: 0x%08x", fifo_intr);
		nvgpu_writel(g, fifo_intr_0_r(), fifo_intr);
		return;
	}
	/* note we're not actually in an "isr", but rather
	 * in a threaded interrupt context... */
	nvgpu_mutex_acquire(&g->fifo.intr.isr.mutex);

	nvgpu_log(g, gpu_dbg_intr, "fifo isr %08x", fifo_intr);

	if (unlikely((fifo_intr & gk20a_fifo_intr_0_error_mask(g)) !=
							0U)) {
		clear_intr |= gk20a_fifo_intr_handle_errors(g,
				fifo_intr);
	}

	if ((fifo_intr & fifo_intr_0_runlist_event_pending_f()) != 0U) {
		gk20a_fifo_intr_handle_runlist_event(g);
		clear_intr |= fifo_intr_0_runlist_event_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_pbdma_intr_pending_f()) != 0U) {
		clear_intr |= fifo_pbdma_isr(g, fifo_intr);
	}

	if ((fifo_intr & fifo_intr_0_mmu_fault_pending_f()) != 0U) {
		(void) gk20a_fifo_handle_mmu_fault(g, 0, INVAL_ID, false);
		clear_intr |= fifo_intr_0_mmu_fault_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_sched_error_pending_f()) != 0U) {
		(void) g->ops.fifo.handle_sched_error(g);
		clear_intr |= fifo_intr_0_sched_error_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_dropped_mmu_fault_pending_f()) != 0U) {
		gk20a_fifo_handle_dropped_mmu_fault(g);
		clear_intr |= fifo_intr_0_dropped_mmu_fault_pending_f();
	}

	nvgpu_mutex_release(&g->fifo.intr.isr.mutex);

	nvgpu_writel(g, fifo_intr_0_r(), clear_intr);
}
