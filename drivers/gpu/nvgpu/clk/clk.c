/*
 * Copyright (c) 2016-2018, NVIDIA CORPORATION.  All rights reserved.
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
#include <nvgpu/pmuif/nvgpu_gpmu_cmdif.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/pmuif/ctrlclk.h>
#include <nvgpu/pmuif/ctrlvolt.h>
#include <nvgpu/bug.h>
#include <nvgpu/pmuif/ctrlperf.h>
#include <nvgpu/pmu/pstate.h>
#include <nvgpu/pmu/volt.h>

#include "clk.h"
#include <nvgpu/timers.h>

#define BOOT_GPC2CLK_MHZ  2581U
#define BOOT_MCLK_MHZ     3003U

struct clkrpc_pmucmdhandler_params {
	struct nv_pmu_clk_rpc *prpccall;
	u32 success;
};

static void clkrpc_pmucmdhandler(struct gk20a *g, struct pmu_msg *msg,
				 void *param, u32 handle, u32 status)
{
	struct clkrpc_pmucmdhandler_params *phandlerparams =
		(struct clkrpc_pmucmdhandler_params *)param;

	nvgpu_log_info(g, " ");

	if (msg->msg.clk.msg_type != NV_PMU_CLK_MSG_ID_RPC) {
		nvgpu_err(g, "unsupported msg for VFE LOAD RPC %x",
			  msg->msg.clk.msg_type);
		return;
	}

	if (phandlerparams->prpccall->b_supported) {
		phandlerparams->success = 1;
	}
}


int clk_pmu_freq_effective_avg_load(struct gk20a *g, bool bload)
{
	struct pmu_cmd cmd;
	struct pmu_payload payload;
	int status;
	u32 seqdesc;
	struct nv_pmu_clk_rpc rpccall;
	struct clkrpc_pmucmdhandler_params handler;
	struct nv_pmu_clk_load *clkload;

	(void) memset(&payload, 0, sizeof(struct pmu_payload));
	(void) memset(&rpccall, 0, sizeof(struct nv_pmu_clk_rpc));
	(void) memset(&handler, 0, sizeof(struct clkrpc_pmucmdhandler_params));
	(void) memset(&cmd, 0, sizeof(struct pmu_cmd));

	rpccall.function = NV_PMU_CLK_RPC_ID_LOAD;
	clkload = &rpccall.params.clk_load;
	clkload->feature = NV_NV_PMU_CLK_LOAD_FEATURE_FREQ_EFFECTIVE_AVG;
	clkload->action_mask = bload ?
		NV_NV_PMU_CLK_LOAD_ACTION_MASK_FREQ_EFFECTIVE_AVG_CALLBACK_YES :
		NV_NV_PMU_CLK_LOAD_ACTION_MASK_FREQ_EFFECTIVE_AVG_CALLBACK_NO;

	cmd.hdr.unit_id = PMU_UNIT_CLK;
	cmd.hdr.size =  (u32)sizeof(struct nv_pmu_clk_cmd) +
			(u32)sizeof(struct pmu_hdr);

	cmd.cmd.clk.cmd_type = NV_PMU_CLK_CMD_ID_RPC;

	payload.in.buf = (u8 *)&rpccall;
	payload.in.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.in.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.in.offset = (u32)NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET;

	payload.out.buf = (u8 *)&rpccall;
	payload.out.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.out.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.out.offset = (u32)NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET;

	handler.prpccall = &rpccall;
	handler.success = 0;

	status = nvgpu_pmu_cmd_post(g, &cmd, NULL, &payload,
			PMU_COMMAND_QUEUE_LPQ,
			clkrpc_pmucmdhandler, (void *)&handler,
			&seqdesc);
	if (status != 0) {
		nvgpu_err(g, "unable to post clk RPC cmd %x",
			cmd.cmd.clk.cmd_type);
		goto done;
	}

	pmu_wait_message_cond(&g->pmu,
			gk20a_get_gr_idle_timeout(g),
			&handler.success, 1);
	if (handler.success == 0U) {
		nvgpu_err(g, "rpc call to load Effective avg clk domain freq failed");
		status = -EINVAL;
	}

done:
	return status;
}

int clk_freq_effective_avg(struct gk20a *g, u32 *freqkHz, u32 clkDomainMask) {

	struct pmu_cmd cmd;
	struct pmu_payload payload;
	int status = 0;
	u32 seqdesc;
	struct nv_pmu_clk_rpc rpccall;
	struct clkrpc_pmucmdhandler_params handler;
	struct nv_pmu_clk_freq_effective_avg *clk_freq_effective_avg;

	(void) memset(&payload, 0, sizeof(struct pmu_payload));
	(void) memset(&rpccall, 0, sizeof(struct nv_pmu_clk_rpc));
	(void) memset(&handler, 0, sizeof(struct clkrpc_pmucmdhandler_params));
	(void) memset(&cmd, 0, sizeof(struct pmu_cmd));

	rpccall.function = NV_PMU_CLK_RPC_ID_CLK_FREQ_EFF_AVG;
	clk_freq_effective_avg = &rpccall.params.clk_freq_effective_avg;
	clk_freq_effective_avg->clkDomainMask = clkDomainMask;

	cmd.hdr.unit_id = PMU_UNIT_CLK;
	cmd.hdr.size =  (u32)sizeof(struct nv_pmu_clk_cmd) +
			(u32)sizeof(struct pmu_hdr);

	cmd.cmd.clk.cmd_type = NV_PMU_CLK_CMD_ID_RPC;

	payload.in.buf = (u8 *)&rpccall;
	payload.in.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.in.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.in.offset = (u32)NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET;

	payload.out.buf = (u8 *)&rpccall;
	payload.out.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.out.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.out.offset = (u32)NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET;

	handler.prpccall = &rpccall;
	handler.success = 0;

	status = nvgpu_pmu_cmd_post(g, &cmd, NULL, &payload,
			PMU_COMMAND_QUEUE_LPQ,
			clkrpc_pmucmdhandler, (void *)&handler,
			&seqdesc);
	if (status != 0) {
		nvgpu_err(g, "unable to post clk RPC cmd %x",
			cmd.cmd.clk.cmd_type);
		goto done;
	}

	pmu_wait_message_cond(&g->pmu,
			gk20a_get_gr_idle_timeout(g),
			&handler.success, 1);
	if (handler.success == 0U) {
		nvgpu_err(g, "rpc call to get clk frequency average failed");
		status = -EINVAL;
		goto done;
	}

	*freqkHz = rpccall.params.clk_freq_effective_avg.freqkHz[clkDomainMask];

done:
	return status;
}

int clk_pmu_freq_controller_load(struct gk20a *g, bool bload, u8 bit_idx)
{
	struct pmu_cmd cmd;
	struct pmu_payload payload;
	int status;
	u32 seqdesc;
	struct nv_pmu_clk_rpc rpccall;
	struct clkrpc_pmucmdhandler_params handler;
	struct nv_pmu_clk_load *clkload;
	struct clk_freq_controllers *pclk_freq_controllers;
	struct ctrl_boardobjgrp_mask_e32 *load_mask;
	struct boardobjgrpmask_e32 isolate_cfc_mask;

	(void) memset(&payload, 0, sizeof(struct pmu_payload));
	(void) memset(&rpccall, 0, sizeof(struct nv_pmu_clk_rpc));
	(void) memset(&handler, 0, sizeof(struct clkrpc_pmucmdhandler_params));

	pclk_freq_controllers = &g->clk_pmu->clk_freq_controllers;
	rpccall.function = NV_PMU_CLK_RPC_ID_LOAD;
	clkload = &rpccall.params.clk_load;
	clkload->feature = NV_NV_PMU_CLK_LOAD_FEATURE_FREQ_CONTROLLER;
	clkload->action_mask = bload ?
		NV_NV_PMU_CLK_LOAD_ACTION_MASK_FREQ_CONTROLLER_CALLBACK_YES :
		NV_NV_PMU_CLK_LOAD_ACTION_MASK_FREQ_CONTROLLER_CALLBACK_NO;

	load_mask = &rpccall.params.clk_load.payload.freq_controllers.load_mask;

	status = boardobjgrpmask_e32_init(&isolate_cfc_mask, NULL);

	if (bit_idx == CTRL_CLK_CLK_FREQ_CONTROLLER_ID_ALL) {
		status = boardobjgrpmask_export(
				&pclk_freq_controllers->
					freq_ctrl_load_mask.super,
				pclk_freq_controllers->
					freq_ctrl_load_mask.super.bitcount,
				&load_mask->super);


	} else {
		status = boardobjgrpmask_bitset(&isolate_cfc_mask.super,
						bit_idx);
		status = boardobjgrpmask_export(&isolate_cfc_mask.super,
					isolate_cfc_mask.super.bitcount,
					&load_mask->super);
		if (bload) {
			status = boardobjgrpmask_bitset(
					&pclk_freq_controllers->
						freq_ctrl_load_mask.super,
					bit_idx);
		} else {
			status = boardobjgrpmask_bitclr(
					&pclk_freq_controllers->
						freq_ctrl_load_mask.super,
					bit_idx);
		}
	}

	if (status != 0) {
		nvgpu_err(g, "Error in generating mask used to select CFC");
		goto done;
	}

	cmd.hdr.unit_id = PMU_UNIT_CLK;
	cmd.hdr.size =  (u32)sizeof(struct nv_pmu_clk_cmd) +
			(u32)sizeof(struct pmu_hdr);

	cmd.cmd.clk.cmd_type = NV_PMU_CLK_CMD_ID_RPC;

	payload.in.buf = (u8 *)&rpccall;
	payload.in.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.in.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.in.offset = (u32)NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET;

	payload.out.buf = (u8 *)&rpccall;
	payload.out.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.out.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.out.offset = (u32)NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET;

	handler.prpccall = &rpccall;
	handler.success = 0;
	status = nvgpu_pmu_cmd_post(g, &cmd, NULL, &payload,
			PMU_COMMAND_QUEUE_LPQ,
			clkrpc_pmucmdhandler, (void *)&handler,
			&seqdesc);

	if (status != 0) {
		nvgpu_err(g, "unable to post clk RPC cmd %x",
			cmd.cmd.clk.cmd_type);
		goto done;
	}

	pmu_wait_message_cond(&g->pmu,
			gk20a_get_gr_idle_timeout(g),
			&handler.success, 1);

	if (handler.success == 0U) {
		nvgpu_err(g, "rpc call to load freq cntlr cal failed");
		status = -EINVAL;
	}

done:
	return status;
}

int clk_pmu_vin_load(struct gk20a *g)
{
	struct pmu_cmd cmd;
	struct pmu_payload payload;
	int status;
	u32 seqdesc;
	struct nv_pmu_clk_rpc rpccall;
	struct clkrpc_pmucmdhandler_params handler;
	struct nv_pmu_clk_load *clkload;

	(void) memset(&payload, 0, sizeof(struct pmu_payload));
	(void) memset(&rpccall, 0, sizeof(struct nv_pmu_clk_rpc));
	(void) memset(&handler, 0, sizeof(struct clkrpc_pmucmdhandler_params));

	rpccall.function = NV_PMU_CLK_RPC_ID_LOAD;
	clkload = &rpccall.params.clk_load;
	clkload->feature = NV_NV_PMU_CLK_LOAD_FEATURE_VIN;
	clkload->action_mask = NV_NV_PMU_CLK_LOAD_ACTION_MASK_VIN_HW_CAL_PROGRAM_YES << 4;

	cmd.hdr.unit_id = PMU_UNIT_CLK;
	cmd.hdr.size =  (u32)sizeof(struct nv_pmu_clk_cmd) +
			(u32)sizeof(struct pmu_hdr);

	cmd.cmd.clk.cmd_type = NV_PMU_CLK_CMD_ID_RPC;
	cmd.cmd.clk.generic.b_perf_daemon_cmd =false;

	payload.in.buf = (u8 *)&rpccall;
	payload.in.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.in.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.in.offset = (u32)NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET;

	payload.out.buf = (u8 *)&rpccall;
	payload.out.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.out.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.out.offset = (u32)NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET;

	handler.prpccall = &rpccall;
	handler.success = 0;
	status = nvgpu_pmu_cmd_post(g, &cmd, NULL, &payload,
			PMU_COMMAND_QUEUE_LPQ,
			clkrpc_pmucmdhandler, (void *)&handler,
			&seqdesc);

	if (status != 0) {
		nvgpu_err(g, "unable to post clk RPC cmd %x",
			cmd.cmd.clk.cmd_type);
		goto done;
	}

	pmu_wait_message_cond(&g->pmu,
			gk20a_get_gr_idle_timeout(g),
			&handler.success, 1);

	if (handler.success == 0U) {
		nvgpu_err(g, "rpc call to load vin cal failed");
		status = -EINVAL;
	}

done:
	return status;
}

int clk_pmu_clk_domains_load(struct gk20a *g)
{
	struct pmu_cmd cmd;
	struct pmu_payload payload;
	struct nv_pmu_clk_rpc rpccall;
	struct clkrpc_pmucmdhandler_params handler;
	struct nv_pmu_clk_load *clkload;
	int status;
	u32 seqdesc;

	(void) memset(&payload, 0, sizeof(struct pmu_payload));
	(void) memset(&rpccall, 0, sizeof(struct nv_pmu_clk_rpc));
	(void) memset(&handler, 0, sizeof(struct clkrpc_pmucmdhandler_params));

	rpccall.function = NV_PMU_CLK_RPC_ID_LOAD;
	clkload = &rpccall.params.clk_load;
	clkload->feature = NV_NV_PMU_CLK_LOAD_FEATURE_CLK_DOMAIN;

	cmd.hdr.unit_id = PMU_UNIT_CLK;
	cmd.hdr.size =  (u32)sizeof(struct nv_pmu_clk_cmd) +
			(u32)sizeof(struct pmu_hdr);

	cmd.cmd.clk.cmd_type = NV_PMU_CLK_CMD_ID_RPC;
	cmd.cmd.clk.generic.b_perf_daemon_cmd = false;

	payload.in.buf = (u8 *)&rpccall;
	payload.in.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.in.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.in.offset = (u32)NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET;

	payload.out.buf = (u8 *)&rpccall;
	payload.out.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.out.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.out.offset = (u32)NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET;

	handler.prpccall = &rpccall;
	handler.success = 0;
	status = nvgpu_pmu_cmd_post(g, &cmd, NULL, &payload,
			PMU_COMMAND_QUEUE_LPQ,
			clkrpc_pmucmdhandler, (void *)&handler,
			&seqdesc);

	if (status != 0) {
		nvgpu_err(g, "unable to post clk RPC cmd %x",
			cmd.cmd.clk.cmd_type);
		goto done;
	}

	(void) pmu_wait_message_cond(&g->pmu,
			gk20a_get_gr_idle_timeout(g),
			&handler.success, 1);

	if (handler.success == 0U) {
		nvgpu_err(g, "rpc call to load clk_domains cal failed");
		status = -EINVAL;
	}

done:
	return status;
}

u32 nvgpu_clk_vf_change_inject_data_fill_gp10x(struct gk20a *g,
	struct nv_pmu_clk_rpc *rpccall,
	struct set_fll_clk *setfllclk)
{
	struct nv_pmu_clk_vf_change_inject *vfchange;

	vfchange = &rpccall->params.clk_vf_change_inject;
	vfchange->flags = 0;
	vfchange->clk_list.num_domains = 3;
	vfchange->clk_list.clk_domains[0].clk_domain = CTRL_CLK_DOMAIN_GPCCLK;
	vfchange->clk_list.clk_domains[0].clk_freq_khz =
					(u32)setfllclk->gpc2clkmhz * 1000U;
	vfchange->clk_list.clk_domains[0].clk_flags = 0;
	vfchange->clk_list.clk_domains[0].current_regime_id =
		setfllclk->current_regime_id_gpc;
	vfchange->clk_list.clk_domains[0].target_regime_id =
		setfllclk->target_regime_id_gpc;
	vfchange->clk_list.clk_domains[1].clk_domain = CTRL_CLK_DOMAIN_XBARCLK;
	vfchange->clk_list.clk_domains[1].clk_freq_khz =
					(u32)setfllclk->xbar2clkmhz * 1000U;
	vfchange->clk_list.clk_domains[1].clk_flags = 0;
	vfchange->clk_list.clk_domains[1].current_regime_id =
		setfllclk->current_regime_id_xbar;
	vfchange->clk_list.clk_domains[1].target_regime_id =
		setfllclk->target_regime_id_xbar;
	vfchange->clk_list.clk_domains[2].clk_domain = CTRL_CLK_DOMAIN_SYSCLK;
	vfchange->clk_list.clk_domains[2].clk_freq_khz =
					(u32)setfllclk->sys2clkmhz * 1000U;
	vfchange->clk_list.clk_domains[2].clk_flags = 0;
	vfchange->clk_list.clk_domains[2].current_regime_id =
		setfllclk->current_regime_id_sys;
	vfchange->clk_list.clk_domains[2].target_regime_id =
		setfllclk->target_regime_id_sys;
	vfchange->volt_list.num_rails = 1;
	vfchange->volt_list.rails[0].volt_domain = CTRL_VOLT_DOMAIN_LOGIC;
	vfchange->volt_list.rails[0].voltage_uv = setfllclk->voltuv;
	vfchange->volt_list.rails[0].voltage_min_noise_unaware_uv =
				setfllclk->voltuv;

	return 0;
}

u32 nvgpu_clk_vf_change_inject_data_fill_gv10x(struct gk20a *g,
	struct nv_pmu_clk_rpc *rpccall,
	struct set_fll_clk *setfllclk)
{
	struct nv_pmu_clk_vf_change_inject_v1 *vfchange;

	vfchange = &rpccall->params.clk_vf_change_inject_v1;
	vfchange->flags = 0;
	vfchange->clk_list.num_domains = 4;
	vfchange->clk_list.clk_domains[0].clk_domain = CTRL_CLK_DOMAIN_GPCCLK;
	vfchange->clk_list.clk_domains[0].clk_freq_khz =
			(u32)setfllclk->gpc2clkmhz * 1000U;

	vfchange->clk_list.clk_domains[1].clk_domain = CTRL_CLK_DOMAIN_XBARCLK;
	vfchange->clk_list.clk_domains[1].clk_freq_khz =
			(u32)setfllclk->xbar2clkmhz * 1000U;

	vfchange->clk_list.clk_domains[2].clk_domain = CTRL_CLK_DOMAIN_SYSCLK;
	vfchange->clk_list.clk_domains[2].clk_freq_khz =
			(u32)setfllclk->sys2clkmhz * 1000U;

	vfchange->clk_list.clk_domains[3].clk_domain = CTRL_CLK_DOMAIN_NVDCLK;
	vfchange->clk_list.clk_domains[3].clk_freq_khz = 855 * 1000;

	vfchange->volt_list.num_rails = 1;
	vfchange->volt_list.rails[0].rail_idx = 0;
	vfchange->volt_list.rails[0].voltage_uv = setfllclk->voltuv;
	vfchange->volt_list.rails[0].voltage_min_noise_unaware_uv =
			setfllclk->voltuv;

	return 0;
}

static int clk_pmu_vf_inject(struct gk20a *g, struct set_fll_clk *setfllclk)
{
	struct pmu_cmd cmd;
	struct pmu_payload payload;
	int status;
	u32 seqdesc;
	struct nv_pmu_clk_rpc rpccall;
	struct clkrpc_pmucmdhandler_params handler;

	(void) memset(&payload, 0, sizeof(struct pmu_payload));
	(void) memset(&rpccall, 0, sizeof(struct nv_pmu_clk_rpc));
	(void) memset(&handler, 0, sizeof(struct clkrpc_pmucmdhandler_params));
	(void) memset(&cmd, 0, sizeof(struct pmu_cmd));

	if ((setfllclk->gpc2clkmhz == 0U) || (setfllclk->xbar2clkmhz == 0U) ||
		(setfllclk->sys2clkmhz == 0U) || (setfllclk->voltuv == 0U)) {
		return -EINVAL;
	}

	if ((setfllclk->target_regime_id_gpc > CTRL_CLK_FLL_REGIME_ID_FR) ||
		(setfllclk->target_regime_id_sys > CTRL_CLK_FLL_REGIME_ID_FR) ||
		(setfllclk->target_regime_id_xbar > CTRL_CLK_FLL_REGIME_ID_FR)) {
		return -EINVAL;
	}

	rpccall.function = NV_PMU_CLK_RPC_ID_CLK_VF_CHANGE_INJECT;

	g->ops.pmu_ver.clk.clk_vf_change_inject_data_fill(g,
		&rpccall, setfllclk);

	cmd.hdr.unit_id = PMU_UNIT_CLK;
	cmd.hdr.size =  (u32)sizeof(struct nv_pmu_clk_cmd) +
			(u32)sizeof(struct pmu_hdr);

	cmd.cmd.clk.cmd_type = NV_PMU_CLK_CMD_ID_RPC;

	payload.in.buf = (u8 *)&rpccall;
	payload.in.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.in.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.in.offset = (u32)NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET;

	payload.out.buf = (u8 *)&rpccall;
	payload.out.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.out.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.out.offset = (u32)NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET;

	handler.prpccall = &rpccall;
	handler.success = 0;

	status = nvgpu_pmu_cmd_post(g, &cmd, NULL, &payload,
			PMU_COMMAND_QUEUE_LPQ,
			clkrpc_pmucmdhandler, (void *)&handler,
			&seqdesc);

	if (status != 0) {
		nvgpu_err(g, "unable to post clk RPC cmd %x",
			  cmd.cmd.clk.cmd_type);
		goto done;
	}

	pmu_wait_message_cond(&g->pmu,
			gk20a_get_gr_idle_timeout(g),
			&handler.success, 1);

	if (handler.success == 0U) {
		nvgpu_err(g, "rpc call to inject clock failed");
		status = -EINVAL;
	}
done:
	return status;
}

static u8 find_regime_id(struct gk20a *g, u32 domain, u16 clkmhz)
{
	struct fll_device *pflldev;
	u8 j;
	struct clk_pmupstate *pclk = g->clk_pmu;

	BOARDOBJGRP_FOR_EACH(&(pclk->avfs_fllobjs.super.super),
		struct fll_device *, pflldev, j) {
		if (pflldev->clk_domain == domain) {
			if (pflldev->regime_desc.fixed_freq_regime_limit_mhz >=
							clkmhz) {
				return CTRL_CLK_FLL_REGIME_ID_FFR;
			} else {
				return CTRL_CLK_FLL_REGIME_ID_FR;
			}
		}
	}
	return CTRL_CLK_FLL_REGIME_ID_INVALID;
}

static int set_regime_id(struct gk20a *g, u32 domain, u8 regimeid)
{
	struct fll_device *pflldev;
	u8 j;
	struct clk_pmupstate *pclk = g->clk_pmu;

	BOARDOBJGRP_FOR_EACH(&(pclk->avfs_fllobjs.super.super),
		struct fll_device *, pflldev, j) {
		if (pflldev->clk_domain == domain) {
			pflldev->regime_desc.regime_id = regimeid;
			return 0;
		}
	}
	return -EINVAL;
}

static int get_regime_id(struct gk20a *g, u32 domain, u8 *regimeid)
{
	struct fll_device *pflldev;
	u8 j;
	struct clk_pmupstate *pclk = g->clk_pmu;

	BOARDOBJGRP_FOR_EACH(&(pclk->avfs_fllobjs.super.super),
		struct fll_device *, pflldev, j) {
		if (pflldev->clk_domain == domain) {
			*regimeid = pflldev->regime_desc.regime_id;
			return 0;
		}
	}
	return -EINVAL;
}

int clk_set_fll_clks(struct gk20a *g, struct set_fll_clk *setfllclk)
{
	int status = -EINVAL;

	/*set regime ids */
	status = get_regime_id(g, CTRL_CLK_DOMAIN_GPCCLK,
			&setfllclk->current_regime_id_gpc);
	if (status != 0) {
		goto done;
	}

	setfllclk->target_regime_id_gpc = find_regime_id(g,
			CTRL_CLK_DOMAIN_GPCCLK, setfllclk->gpc2clkmhz);

	status = get_regime_id(g, CTRL_CLK_DOMAIN_SYSCLK,
			&setfllclk->current_regime_id_sys);
	if (status != 0) {
		goto done;
	}

	setfllclk->target_regime_id_sys = find_regime_id(g,
			CTRL_CLK_DOMAIN_SYSCLK, setfllclk->sys2clkmhz);

	status = get_regime_id(g, CTRL_CLK_DOMAIN_XBARCLK,
			&setfllclk->current_regime_id_xbar);
	if (status != 0) {
		goto done;
	}

	setfllclk->target_regime_id_xbar = find_regime_id(g,
			CTRL_CLK_DOMAIN_XBARCLK, setfllclk->xbar2clkmhz);

	status = clk_pmu_vf_inject(g, setfllclk);

	if (status != 0) {
		nvgpu_err(g, "vf inject to change clk failed");
	}

	/* save regime ids */
	status = set_regime_id(g, CTRL_CLK_DOMAIN_XBARCLK,
			setfllclk->target_regime_id_xbar);
	if (status != 0) {
		goto done;
	}

	status = set_regime_id(g, CTRL_CLK_DOMAIN_GPCCLK,
			setfllclk->target_regime_id_gpc);
	if (status != 0) {
		goto done;
	}

	status = set_regime_id(g, CTRL_CLK_DOMAIN_SYSCLK,
			setfllclk->target_regime_id_sys);
	if (status != 0) {
		goto done;
	}
done:
	return status;
}

int clk_get_fll_clks(struct gk20a *g, struct set_fll_clk *setfllclk)
{
	int status = -EINVAL;
	struct clk_domain *pdomain;
	u8 i;
	struct clk_pmupstate *pclk = g->clk_pmu;
	u16 clkmhz = 0;
	struct clk_domain_35_master *p35master;
	struct clk_domain_35_slave *p35slave;
	unsigned long slaveidxmask;

	if (setfllclk->gpc2clkmhz == 0U) {
		return -EINVAL;
	}

	BOARDOBJGRP_FOR_EACH(&(pclk->clk_domainobjs.super.super),
			struct clk_domain *, pdomain, i) {

		if (pdomain->api_domain == CTRL_CLK_DOMAIN_GPCCLK) {
			if (!pdomain->super.implements(g, &pdomain->super,
				CTRL_CLK_CLK_DOMAIN_TYPE_35_MASTER)) {
				status = -EINVAL;
				goto done;
			}
			p35master = (struct clk_domain_35_master *)pdomain;
			slaveidxmask = p35master->master.slave_idxs_mask;
			for_each_set_bit(i, &slaveidxmask, 32U) {
				p35slave = (struct clk_domain_35_slave *)
						CLK_CLK_DOMAIN_GET(pclk, i);

				clkmhz = 0;
				status = p35slave->slave.clkdomainclkgetslaveclk(g,
						pclk,
						(struct clk_domain *)(void *)p35slave,
						&clkmhz,
						setfllclk->gpc2clkmhz);
				if (status != 0) {
					status = -EINVAL;
					goto done;
				}
				if (p35slave->super.super.super.super.api_domain ==
				     CTRL_CLK_DOMAIN_XBARCLK) {
					setfllclk->xbar2clkmhz = clkmhz;
				}
				if (p35slave->super.super.super.super.api_domain ==
				     CTRL_CLK_DOMAIN_SYSCLK) {
					setfllclk->sys2clkmhz = clkmhz;
				}
				if (p35slave->super.super.super.super.api_domain ==
				     CTRL_CLK_DOMAIN_NVDCLK) {
					setfllclk->nvdclkmhz = clkmhz;
				}
				if (p35slave->super.super.super.super.api_domain ==
				     CTRL_CLK_DOMAIN_HOSTCLK) {
					setfllclk->hostclkmhz = clkmhz;
				}
			}
		}
	}
done:
	return status;
}

int clk_domain_print_vf_table(struct gk20a *g, u32 clkapidomain)
{
	int status = -EINVAL;
	struct clk_domain *pdomain;
	u8 i;
	struct clk_pmupstate *pclk = g->clk_pmu;
	u16 clkmhz = 0;
	u32 volt = 0;

	BOARDOBJGRP_FOR_EACH(&(pclk->clk_domainobjs.super.super),
			struct clk_domain *, pdomain, i) {
		if (pdomain->api_domain == clkapidomain) {
			status = pdomain->clkdomainclkvfsearch(g, pclk,
				pdomain, &clkmhz, &volt,
				CLK_PROG_VFE_ENTRY_LOGIC);
			status = pdomain->clkdomainclkvfsearch(g, pclk,
				pdomain, &clkmhz, &volt,
				CLK_PROG_VFE_ENTRY_SRAM);
		}
	}
	return status;
}

static int clk_program_fllclks(struct gk20a *g, struct change_fll_clk *fllclk)
{
	int status = -EINVAL;
	struct clk_domain *pdomain;
	u8 i;
	struct clk_pmupstate *pclk = g->clk_pmu;
	u16 clkmhz = 0;
	struct clk_domain_3x_master *p3xmaster;
	struct clk_domain_3x_slave *p3xslave;
	unsigned long slaveidxmask;
	struct set_fll_clk setfllclk;

	if (fllclk->api_clk_domain != CTRL_CLK_DOMAIN_GPCCLK) {
		return -EINVAL;
	}
	if (fllclk->voltuv == 0U) {
		return -EINVAL;
	}
	if (fllclk->clkmhz == 0U) {
		return -EINVAL;
	}

	setfllclk.voltuv = fllclk->voltuv;
	setfllclk.gpc2clkmhz = fllclk->clkmhz;

	BOARDOBJGRP_FOR_EACH(&(pclk->clk_domainobjs.super.super),
			struct clk_domain *, pdomain, i) {

		if (pdomain->api_domain == fllclk->api_clk_domain) {

			if (!pdomain->super.implements(g, &pdomain->super,
				CTRL_CLK_CLK_DOMAIN_TYPE_3X_MASTER)) {
				status = -EINVAL;
				goto done;
			}
			p3xmaster = (struct clk_domain_3x_master *)pdomain;
			slaveidxmask = p3xmaster->slave_idxs_mask;
			for_each_set_bit(i, &slaveidxmask, 32U) {
				p3xslave = (struct clk_domain_3x_slave *)
						CLK_CLK_DOMAIN_GET(pclk, i);
				if ((p3xslave->super.super.super.api_domain !=
				     CTRL_CLK_DOMAIN_XBARCLK) &&
				    (p3xslave->super.super.super.api_domain !=
				     CTRL_CLK_DOMAIN_SYSCLK)) {
					continue;
				}
				clkmhz = 0;
				status = p3xslave->clkdomainclkgetslaveclk(g,
						pclk,
						(struct clk_domain *)p3xslave,
						&clkmhz,
						fllclk->clkmhz);
				if (status != 0) {
					status = -EINVAL;
					goto done;
				}
				if (p3xslave->super.super.super.api_domain ==
				     CTRL_CLK_DOMAIN_XBARCLK) {
					setfllclk.xbar2clkmhz = clkmhz;
				}
				if (p3xslave->super.super.super.api_domain ==
				     CTRL_CLK_DOMAIN_SYSCLK) {
					setfllclk.sys2clkmhz = clkmhz;
				}
			}
		}
	}
	/*set regime ids */
	status = get_regime_id(g, CTRL_CLK_DOMAIN_GPCCLK,
			&setfllclk.current_regime_id_gpc);
	if (status != 0) {
		goto done;
	}

	setfllclk.target_regime_id_gpc = find_regime_id(g,
			CTRL_CLK_DOMAIN_GPCCLK, setfllclk.gpc2clkmhz);

	status = get_regime_id(g, CTRL_CLK_DOMAIN_SYSCLK,
			&setfllclk.current_regime_id_sys);
	if (status != 0) {
		goto done;
	}

	setfllclk.target_regime_id_sys = find_regime_id(g,
			CTRL_CLK_DOMAIN_SYSCLK, setfllclk.sys2clkmhz);

	status = get_regime_id(g, CTRL_CLK_DOMAIN_XBARCLK,
			&setfllclk.current_regime_id_xbar);
	if (status != 0) {
		goto done;
	}

	setfllclk.target_regime_id_xbar = find_regime_id(g,
			CTRL_CLK_DOMAIN_XBARCLK, setfllclk.xbar2clkmhz);

	status = clk_pmu_vf_inject(g, &setfllclk);

	if (status != 0) {
		nvgpu_err(g,
			"vf inject to change clk failed");
	}

	/* save regime ids */
	status = set_regime_id(g, CTRL_CLK_DOMAIN_XBARCLK,
			setfllclk.target_regime_id_xbar);
	if (status != 0) {
		goto done;
	}

	status = set_regime_id(g, CTRL_CLK_DOMAIN_GPCCLK,
			setfllclk.target_regime_id_gpc);
	if (status != 0) {
		goto done;
	}

	status = set_regime_id(g, CTRL_CLK_DOMAIN_SYSCLK,
			setfllclk.target_regime_id_sys);
	if (status != 0) {
		goto done;
	}
done:
	return status;
}

int nvgpu_clk_set_boot_fll_clk_gv10x(struct gk20a *g)
{
	int status;
	struct change_fll_clk bootfllclk;
	u16 gpcclk_clkmhz = BOOT_GPCCLK_MHZ;
	u32 gpcclk_voltuv = 0;
	u32 voltuv = 0;

	status = clk_vf_point_cache(g);
	if (status != 0) {
		nvgpu_err(g,"caching failed");
		return status;
	}

	status = clk_domain_get_f_or_v(g, CTRL_CLK_DOMAIN_GPCCLK,
		&gpcclk_clkmhz, &gpcclk_voltuv, CTRL_VOLT_DOMAIN_LOGIC);
	if (status != 0) {
		return status;
	}

	voltuv = gpcclk_voltuv;
	status = volt_set_voltage(g, voltuv, 0);
	if (status != 0) {
		nvgpu_err(g,
			"attempt to set boot voltage failed %d",
			voltuv);
	}
	bootfllclk.api_clk_domain = CTRL_CLK_DOMAIN_GPCCLK;
	bootfllclk.clkmhz = gpcclk_clkmhz;
	bootfllclk.voltuv = voltuv;
	status = clk_program_fllclks(g, &bootfllclk);
	if (status != 0) {
		nvgpu_err(g, "attempt to set boot gpcclk failed");
	}
	status = clk_pmu_freq_effective_avg_load(g, true);
	/*
	 * Read clocks after some delay with below method
	 * & extract clock data from buffer
	 * u32 freqkHz;
	 * status = clk_freq_effective_avg(g, &freqkHz, CTRL_CLK_DOMAIN_GPCCLK |
	 * 				CTRL_CLK_DOMAIN_XBARCLK |
	 * 				CTRL_CLK_DOMAIN_SYSCLK |
	 * 				CTRL_CLK_DOMAIN_NVDCLK)
	 * */

	return status;
}

int nvgpu_clk_set_fll_clk_gv10x(struct gk20a *g)
{
	int status;
	struct change_fll_clk bootfllclk;
	u16 gpcclk_clkmhz = BOOT_GPCCLK_MHZ;
	u32 gpcclk_voltuv = 0U;
	u32 voltuv = 0U;

	status = clk_vf_point_cache(g);
	if (status != 0) {
		nvgpu_err(g, "caching failed");
		return status;
	}

	status = clk_domain_get_f_or_v(g, CTRL_CLK_DOMAIN_GPCCLK,
		&gpcclk_clkmhz, &gpcclk_voltuv, CTRL_VOLT_DOMAIN_LOGIC);
	if (status != 0) {
		return status;
	}

	voltuv = gpcclk_voltuv;

	status = volt_set_voltage(g, voltuv, 0U);
	if (status != 0) {
		nvgpu_err(g, "attempt to set max voltage failed %d", voltuv);
	}

	bootfllclk.api_clk_domain = CTRL_CLK_DOMAIN_GPCCLK;
	bootfllclk.clkmhz = gpcclk_clkmhz;
	bootfllclk.voltuv = voltuv;
	status = clk_program_fllclks(g, &bootfllclk);
	if (status != 0) {
		nvgpu_err(g, "attempt to set max gpcclk failed");
	}
	return status;
}

int nvgpu_clk_set_boot_fll_clk_tu10x(struct gk20a *g)
{
	struct nvgpu_pmu *pmu = &g->pmu;
	struct nv_pmu_rpc_perf_change_seq_queue_change rpc;
	struct ctrl_perf_change_seq_change_input change_input;
	struct clk_set_info *p0_clk_set_info;
	struct clk_domain *pclk_domain;
	int status = 0;
	u8 i = 0, gpcclk_domain=0;
	u32 gpcclk_clkmhz=0, gpcclk_voltuv=0;

	(void) memset(&change_input, 0,
		sizeof(struct ctrl_perf_change_seq_change_input));

	BOARDOBJGRP_FOR_EACH(&(g->clk_pmu->clk_domainobjs.super.super),
		struct clk_domain *, pclk_domain, i) {

		p0_clk_set_info = pstate_get_clk_set_info(g, CTRL_PERF_PSTATE_P0,
				pclk_domain->domain);

		switch (pclk_domain->api_domain) {
		case CTRL_CLK_DOMAIN_GPCCLK:
			gpcclk_domain = i;
			gpcclk_clkmhz = p0_clk_set_info->max_mhz;
			change_input.clk[i].clk_freq_khz =
				p0_clk_set_info->max_mhz * 1000U;
			change_input.clk_domains_mask.super.data[0] |= (u32) BIT(i);
			break;
		case CTRL_CLK_DOMAIN_XBARCLK:
		case CTRL_CLK_DOMAIN_SYSCLK:
		case CTRL_CLK_DOMAIN_NVDCLK:
		case CTRL_CLK_DOMAIN_HOSTCLK:
			change_input.clk[i].clk_freq_khz =
				p0_clk_set_info->max_mhz * 1000U;
			change_input.clk_domains_mask.super.data[0] |= (u32) BIT(i);
			break;
		default:
			nvgpu_pmu_dbg(g, "Fixed clock domain");
			break;
		}
	}

	change_input.pstate_index = 0U;
	change_input.flags = CTRL_PERF_CHANGE_SEQ_CHANGE_FORCE;
	change_input.vf_points_cache_counter = 0xFFFFFFFFU;

	status = clk_domain_freq_to_volt(g, gpcclk_domain,
		&gpcclk_clkmhz, &gpcclk_voltuv, CTRL_VOLT_DOMAIN_LOGIC);

	change_input.volt[0].voltage_uv = gpcclk_voltuv;
	change_input.volt[0].voltage_min_noise_unaware_uv = gpcclk_voltuv;
	change_input.volt_rails_mask.super.data[0] = 1U;

	/* RPC to PMU to queue to execute change sequence request*/
	(void) memset(&rpc, 0, sizeof(struct nv_pmu_rpc_perf_change_seq_queue_change ));
	rpc.change = change_input;
	rpc.change.pstate_index =  0;
	PMU_RPC_EXECUTE_CPB(status, pmu, PERF, CHANGE_SEQ_QUEUE_CHANGE, &rpc, 0);
	if (status != 0) {
		nvgpu_err(g, "Failed to execute Change Seq RPC status=0x%x",
			status);
	}

	/* Wait for sync change to complete. */
	if ((rpc.change.flags & CTRL_PERF_CHANGE_SEQ_CHANGE_ASYNC) == 0U) {
		nvgpu_msleep(20);
	}

	return status;
}

int clk_domain_volt_to_freq(struct gk20a *g, u8 clkdomain_idx,
	u32 *pclkmhz, u32 *pvoltuv, u8 railidx)
{
	struct nv_pmu_rpc_clk_domain_35_prog_freq_to_volt  rpc;
	struct nvgpu_pmu *pmu = &g->pmu;
	int status = -EINVAL;

	(void)memset(&rpc, 0, sizeof(struct nv_pmu_rpc_clk_domain_35_prog_freq_to_volt ));
	rpc.volt_rail_idx = volt_rail_volt_domain_convert_to_idx(g, railidx);
	rpc.clk_domain_idx = clkdomain_idx;
	rpc.voltage_type = CTRL_VOLT_DOMAIN_LOGIC;
	rpc.input.value = *pvoltuv;
	PMU_RPC_EXECUTE_CPB(status, pmu, CLK, CLK_DOMAIN_35_PROG_VOLT_TO_FREQ, &rpc, 0);
	if (status != 0) {
		nvgpu_err(g, "Failed to execute Freq to Volt RPC status=0x%x",
			status);
	}
	*pclkmhz = rpc.output.value;
	return status;
}

int clk_domain_freq_to_volt(struct gk20a *g, u8 clkdomain_idx,
	u32 *pclkmhz, u32 *pvoltuv, u8 railidx)
{
	struct nv_pmu_rpc_clk_domain_35_prog_freq_to_volt  rpc;
	struct nvgpu_pmu *pmu = &g->pmu;
	int status = -EINVAL;

	(void)memset(&rpc, 0, sizeof(struct nv_pmu_rpc_clk_domain_35_prog_freq_to_volt ));
	rpc.volt_rail_idx = volt_rail_volt_domain_convert_to_idx(g, railidx);
	rpc.clk_domain_idx = clkdomain_idx;
	rpc.voltage_type = CTRL_VOLT_DOMAIN_LOGIC;
	rpc.input.value = *pclkmhz;
	PMU_RPC_EXECUTE_CPB(status, pmu, CLK, CLK_DOMAIN_35_PROG_FREQ_TO_VOLT, &rpc, 0);
	if (status != 0) {
		nvgpu_err(g, "Failed to execute Freq to Volt RPC status=0x%x",
			status);
	}
	*pvoltuv = rpc.output.value;
	return status;
}


int clk_domain_get_f_or_v(struct gk20a *g, u32 clkapidomain,
	u16 *pclkmhz, u32 *pvoltuv, u8 railidx)
{
	int status = -EINVAL;
	struct clk_domain *pdomain;
	u8 i;
	struct clk_pmupstate *pclk = g->clk_pmu;
	u8 rail;

	if ((pclkmhz == NULL) || (pvoltuv == NULL)) {
		return -EINVAL;
	}

	if (railidx == CTRL_VOLT_DOMAIN_LOGIC) {
		rail = CLK_PROG_VFE_ENTRY_LOGIC;
	} else if (railidx == CTRL_VOLT_DOMAIN_SRAM) {
		rail = CLK_PROG_VFE_ENTRY_SRAM;
	} else {
		return -EINVAL;
	}

	BOARDOBJGRP_FOR_EACH(&(pclk->clk_domainobjs.super.super),
			struct clk_domain *, pdomain, i) {
		if (pdomain->api_domain == clkapidomain) {
			status = pdomain->clkdomainclkvfsearch(g, pclk,
				pdomain, pclkmhz, pvoltuv, rail);
			return status;
		}
	}
	return status;
}

int clk_init_pmupstate(struct gk20a *g)
{
	/* If already allocated, do not re-allocate */
	if (g->clk_pmu != NULL) {
		return 0;
	}

	g->clk_pmu = nvgpu_kzalloc(g, sizeof(*g->clk_pmu));
	if (g->clk_pmu == NULL) {
		return -ENOMEM;
	}

	return 0;
}

void clk_free_pmupstate(struct gk20a *g)
{
	nvgpu_kfree(g, g->clk_pmu);
	g->clk_pmu = NULL;
}
