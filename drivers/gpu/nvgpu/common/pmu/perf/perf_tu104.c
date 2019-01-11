/*
 * GV100 PERF
 *
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/pmu.h>
#include <nvgpu/bug.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/clk_arb.h>
#include <nvgpu/pmu/perf.h>

#include "perf_tu104.h"
#include "pmu_perf.h"

static int pmu_set_boot_clk_runcb_fn(void *arg)
{
	struct gk20a *g = (struct gk20a *)arg;
	struct nvgpu_pmu *pmu = &g->pmu;
	struct nv_pmu_rpc_struct_perf_load rpc;
	struct perf_pmupstate *perf_pmu = g->perf_pmu;
	struct nvgpu_vfe_invalidate *vfe_init = &perf_pmu->vfe_init;
	int status = 0;

	nvgpu_log_fn(g, "thread start");

	while (true) {
		NVGPU_COND_WAIT_INTERRUPTIBLE(&vfe_init->wq,
			(vfe_init->state_change == true), 0);

		vfe_init->state_change = false;

		(void) memset(&rpc, 0,
			sizeof(struct nv_pmu_rpc_struct_perf_load));
		PMU_RPC_EXECUTE_CPB(status, pmu, PERF, LOAD, &rpc, 0);
		if (status != 0) {
			nvgpu_err(g, "Failed to execute RPC status=0x%x",
					status);
		}
	}

	return 0;
}

static int tu104_pmu_handle_perf_event(struct gk20a *g, void *pmumsg)
{
	struct nv_pmu_perf_msg *msg = (struct nv_pmu_perf_msg *)pmumsg;
	struct perf_pmupstate *perf_pmu = g->perf_pmu;

	nvgpu_log_fn(g, " ");
	switch (msg->msg_type) {
	case NV_PMU_PERF_MSG_ID_VFE_CALLBACK:
		perf_pmu->vfe_init.state_change = true;
		(void) nvgpu_cond_signal(&perf_pmu->vfe_init.wq);
		nvgpu_clk_arb_schedule_vf_table_update(g);
		break;
	case NV_PMU_PERF_MSG_ID_CHANGE_SEQ_COMPLETION:
		nvgpu_log_fn(g, "Change Seq Completed");
		break;
	default:
		WARN_ON(true);
		break;
	}
	return 0;
}

int tu104_perf_pmu_init_vfe_perf_event(struct gk20a *g)
{
	struct perf_pmupstate *perf_pmu = g->perf_pmu;
	char thread_name[64];
	int err = 0;

	nvgpu_log_fn(g, " ");

	nvgpu_cond_init(&perf_pmu->vfe_init.wq);

	(void) snprintf(thread_name, sizeof(thread_name),
				"nvgpu_vfe_invalidate_init_%s", g->name);

	err = nvgpu_thread_create(&perf_pmu->vfe_init.state_task, g,
			pmu_set_boot_clk_runcb_fn, thread_name);
	if (err != 0U) {
		nvgpu_err(g, "failed to start nvgpu_vfe_invalidate_init thread");
	}

	return err;

}

int tu104_perf_pmu_vfe_load(struct gk20a *g)
{
	struct nvgpu_pmu *pmu = &g->pmu;
	struct nv_pmu_rpc_struct_perf_load rpc;
	int status = 0;

	(void) memset(&rpc, 0, sizeof(struct nv_pmu_rpc_struct_perf_load));
	rpc.b_load = true;
	PMU_RPC_EXECUTE_CPB(status, pmu, PERF, LOAD, &rpc, 0);
	if (status != 0) {
		nvgpu_err(g, "Failed to execute RPC status=0x%x",
			status);
	}

	status = tu104_perf_pmu_init_vfe_perf_event(g);

	/*register call back for future VFE updates*/
	g->ops.pmu_perf.handle_pmu_perf_event = tu104_pmu_handle_perf_event;

	return status;
}
