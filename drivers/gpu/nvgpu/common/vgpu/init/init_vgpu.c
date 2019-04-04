/*
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

#include <nvgpu/enabled.h>
#include <nvgpu/ptimer.h>
#include <nvgpu/vgpu/vgpu_ivc.h>
#include <nvgpu/vgpu/vgpu.h>
#include <nvgpu/timers.h>
#include <nvgpu/fifo.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/clk_arb.h>
#include <nvgpu/string.h>
#include <nvgpu/ltc.h>
#include <nvgpu/cbc.h>

#include "init_vgpu.h"
#include "init_hal_vgpu.h"
#include "common/vgpu/gr/fecs_trace_vgpu.h"
#include "common/vgpu/fifo/fifo_vgpu.h"
#include "common/vgpu/mm/mm_vgpu.h"
#include "common/vgpu/gr/gr_vgpu.h"
#include "common/vgpu/ivc/comm_vgpu.h"

u64 vgpu_connect(void)
{
	struct tegra_vgpu_cmd_msg msg;
	struct tegra_vgpu_connect_params *p = &msg.params.connect;
	int err;

	msg.cmd = TEGRA_VGPU_CMD_CONNECT;
	p->module = TEGRA_VGPU_MODULE_GPU;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));

	return (err || msg.ret) ? 0 : p->handle;
}

void vgpu_remove_support_common(struct gk20a *g)
{
	struct vgpu_priv_data *priv = vgpu_get_priv_data(g);
	struct tegra_vgpu_intr_msg msg;
	int err;

	if (g->dbg_regops_tmp_buf) {
		nvgpu_kfree(g, g->dbg_regops_tmp_buf);
	}

	if (g->pmu.remove_support) {
		g->pmu.remove_support(&g->pmu);
	}

	if (g->gr.remove_support) {
		g->gr.remove_support(&g->gr);
	}

	if (g->fifo.remove_support) {
		g->fifo.remove_support(&g->fifo);
	}

	if (g->mm.remove_support) {
		g->mm.remove_support(&g->mm);
	}

	msg.event = TEGRA_VGPU_EVENT_ABORT;
	err = vgpu_ivc_send(vgpu_ivc_get_peer_self(), TEGRA_VGPU_QUEUE_INTR,
				&msg, sizeof(msg));
	WARN_ON(err);
	nvgpu_thread_stop(&priv->intr_handler);

	nvgpu_clk_arb_cleanup_arbiter(g);

	nvgpu_mutex_destroy(&g->clk_arb_enable_lock);
	nvgpu_mutex_destroy(&priv->vgpu_clk_get_freq_lock);

	nvgpu_kfree(g, priv->freqs);
}

void vgpu_init_gpu_characteristics(struct gk20a *g)
{
	nvgpu_log_fn(g, " ");

	gk20a_init_gpu_characteristics(g);

	nvgpu_set_enabled(g, NVGPU_SUPPORT_PREEMPTION_GFXP, true);

	/* features vgpu does not support */
	nvgpu_set_enabled(g, NVGPU_SUPPORT_MAP_BUFFER_BATCH, false);
	nvgpu_set_enabled(g, NVGPU_SUPPORT_RESCHEDULE_RUNLIST, false);
	nvgpu_set_enabled(g, NVGPU_SUPPORT_SPARSE_ALLOCS, false);
}

int vgpu_get_constants(struct gk20a *g)
{
	struct tegra_vgpu_cmd_msg msg = {};
	struct tegra_vgpu_constants_params *p = &msg.params.constants;
	struct vgpu_priv_data *priv = vgpu_get_priv_data(g);
	int err;

	nvgpu_log_fn(g, " ");

	msg.cmd = TEGRA_VGPU_CMD_GET_CONSTANTS;
	msg.handle = vgpu_get_handle(g);
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	err = err ? err : msg.ret;

	if (unlikely(err)) {
		nvgpu_err(g, "%s failed, err=%d", __func__, err);
		return err;
	}

	if (unlikely(p->gpc_count > TEGRA_VGPU_MAX_GPC_COUNT ||
		p->max_tpc_per_gpc_count > TEGRA_VGPU_MAX_TPC_COUNT_PER_GPC)) {
		nvgpu_err(g, "gpc_count %d max_tpc_per_gpc %d overflow",
			(int)p->gpc_count, (int)p->max_tpc_per_gpc_count);
		return -EINVAL;
	}

	priv->constants = *p;
	return 0;
}

int vgpu_finalize_poweron_common(struct gk20a *g)
{
	int err;

	nvgpu_log_fn(g, " ");

	vgpu_detect_chip(g);
	err = vgpu_init_hal(g);
	if (err != 0) {
		return err;
	}

	err = nvgpu_init_ltc_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init ltc");
		return err;
	}

	err = vgpu_init_mm_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a mm");
		return err;
	}

	err = nvgpu_fifo_init_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a fifo");
		return err;
	}

	err = vgpu_init_gr_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a gr");
		return err;
	}

	err = nvgpu_clk_arb_init_arbiter(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init clk arb");
		return err;
	}

	err = nvgpu_cbc_init_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init cbc");
		return err;
	}

	g->ops.chip_init_gpu_characteristics(g);

	g->ops.fifo.channel_resume(g);

	return 0;
}
