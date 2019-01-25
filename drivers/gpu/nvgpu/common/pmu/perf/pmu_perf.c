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

#include <nvgpu/bug.h>
#include <nvgpu/pmu.h>
#include <nvgpu/clk_arb.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/pmu/perf.h>

#include "pmu_perf.h"

struct perfrpc_pmucmdhandler_params {
	struct nv_pmu_perf_rpc *prpccall;
	u32 success;
};

static void perfrpc_pmucmdhandler(struct gk20a *g, struct pmu_msg *msg,
				  void *param, u32 handle, u32 status)
{
	struct perfrpc_pmucmdhandler_params *phandlerparams =
		(struct perfrpc_pmucmdhandler_params *)param;

	nvgpu_log_info(g, " ");

	if (msg->msg.perf.msg_type != NV_PMU_PERF_MSG_ID_RPC) {
		nvgpu_err(g, "unsupported msg for VFE LOAD RPC %x",
		msg->msg.perf.msg_type);
		return;
	}

	if (phandlerparams->prpccall->b_supported) {
		phandlerparams->success = 1;
	}
}

static int pmu_handle_perf_event(struct gk20a *g, void *pmu_msg)
{
	struct nv_pmu_perf_msg *msg = (struct nv_pmu_perf_msg *)pmu_msg;

	nvgpu_log_fn(g, " ");
	switch (msg->msg_type) {
	case NV_PMU_PERF_MSG_ID_VFE_CALLBACK:
		nvgpu_clk_arb_schedule_vf_table_update(g);
		break;
	default:
		WARN_ON(true);
		break;
	}
	return 0;
}

int perf_pmu_vfe_load(struct gk20a *g)
{
	struct pmu_cmd cmd;
	struct pmu_payload payload;
	int status;
	u32 seqdesc;
	struct nv_pmu_perf_rpc rpccall;
	struct perfrpc_pmucmdhandler_params handler;

	(void) memset(&payload, 0, sizeof(struct pmu_payload));
	(void) memset(&rpccall, 0, sizeof(struct nv_pmu_perf_rpc));
	(void) memset(&handler, 0,
		sizeof(struct perfrpc_pmucmdhandler_params));

	/*register call back for future VFE updates*/
	g->ops.pmu_perf.handle_pmu_perf_event = pmu_handle_perf_event;

	rpccall.function = NV_PMU_PERF_RPC_ID_VFE_LOAD;
	rpccall.params.vfe_load.b_load = true;
	cmd.hdr.unit_id = PMU_UNIT_PERF;
	cmd.hdr.size = (u32)sizeof(struct nv_pmu_perf_cmd) +
		       (u32)sizeof(struct pmu_hdr);

	cmd.cmd.perf.cmd_type = NV_PMU_PERF_CMD_ID_RPC;

	payload.in.buf = (u8 *)&rpccall;
	payload.in.size = (u32)sizeof(struct nv_pmu_perf_rpc);
	payload.in.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	payload.in.offset = NV_PMU_PERF_CMD_RPC_ALLOC_OFFSET;

	payload.out.buf = (u8 *)&rpccall;
	payload.out.size = (u32)sizeof(struct nv_pmu_perf_rpc);
	payload.out.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	payload.out.offset = NV_PMU_PERF_MSG_RPC_ALLOC_OFFSET;

	handler.prpccall = &rpccall;
	handler.success = 0;

	status = nvgpu_pmu_cmd_post(g, &cmd, NULL, &payload,
			PMU_COMMAND_QUEUE_LPQ,
			perfrpc_pmucmdhandler, (void *)&handler,
			&seqdesc);

	if (status != 0) {
		nvgpu_err(g, "unable to post perf RPC cmd %x",
			  cmd.cmd.perf.cmd_type);
		goto done;
	}

	pmu_wait_message_cond(&g->pmu,
			gk20a_get_gr_idle_timeout(g),
			&handler.success, 1);

	if (handler.success == 0U) {
		status = -EINVAL;
		nvgpu_err(g, "rpc call to load VFE failed");
	}
done:
	return status;
}

int perf_pmu_init_pmupstate(struct gk20a *g)
{
	/* If already allocated, do not re-allocate */
	if (g->perf_pmu != NULL) {
		return 0;
	}

	g->perf_pmu = nvgpu_kzalloc(g, sizeof(*g->perf_pmu));
	if (g->perf_pmu == NULL) {
		return -ENOMEM;
	}

	return 0;
}

static void vfe_thread_stop_cb(void *data)
{
	struct nvgpu_cond *cond = (struct nvgpu_cond *)data;

	(void)nvgpu_cond_signal(cond);
}

void perf_pmu_free_pmupstate(struct gk20a *g)
{
	if (nvgpu_thread_is_running(&g->perf_pmu->vfe_init.state_task)) {
		nvgpu_thread_stop_graceful(&g->perf_pmu->vfe_init.state_task,
				vfe_thread_stop_cb, &g->perf_pmu->vfe_init.wq);
	}
	nvgpu_cond_destroy(&g->perf_pmu->vfe_init.wq);
	nvgpu_mutex_destroy(&g->perf_pmu->pstatesobjs.pstate_mutex);
	nvgpu_kfree(g, g->perf_pmu);
	g->perf_pmu = NULL;
}
