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

#include <nvgpu/pmu.h>
#include <nvgpu/log.h>
#include <nvgpu/pmu/pmuif/nvgpu_cmdif.h>
#include <nvgpu/barrier.h>
#include <nvgpu/bug.h>
#include <nvgpu/utils.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/string.h>
#include <nvgpu/engines.h>
#include <nvgpu/pmu/cmd.h>
#include <nvgpu/dma.h>
#include <nvgpu/pmu/fw.h>
#include <nvgpu/pmu/debug.h>
#include <nvgpu/pmu/pmu_pg.h>

#include "pg_sw_gm20b.h"
#include "pg_sw_gv11b.h"
#include "pg_sw_gp10b.h"

/* state transition :
 * OFF => [OFF_ON_PENDING optional] => ON_PENDING => ON => OFF
 * ON => OFF is always synchronized
 */
/* elpg is off */
#define PMU_ELPG_STAT_OFF	0U
/* elpg is on */
#define PMU_ELPG_STAT_ON	1U
/* elpg is off, ALLOW cmd has been sent, wait for ack */
#define PMU_ELPG_STAT_ON_PENDING	2U
/* elpg is on, DISALLOW cmd has been sent, wait for ack */
#define PMU_ELPG_STAT_OFF_PENDING	3U
/* elpg is off, caller has requested on, but ALLOW
 * cmd hasn't been sent due to ENABLE_ALLOW delay
 */
#define PMU_ELPG_STAT_OFF_ON_PENDING	4U

#define PMU_PGENG_GR_BUFFER_IDX_INIT	(0)
#define PMU_PGENG_GR_BUFFER_IDX_ZBC		(1)
#define PMU_PGENG_GR_BUFFER_IDX_FECS	(2)

static bool is_pg_supported(struct gk20a *g, struct nvgpu_pmu_pg *pg)
{
	if (!g->support_ls_pmu || !g->can_elpg || pg == NULL) {
		return false;
	}

	return true;
}

static int pmu_pg_setup_hw_enable_elpg(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct nvgpu_pmu_pg *pg)
{
	int err = 0;
	nvgpu_log_fn(g, " ");

	pg->initialized = true;

	nvgpu_pmu_fw_state_change(g, pmu, PMU_FW_STATE_STARTED, false);

	if (nvgpu_is_enabled(g, NVGPU_PMU_ZBC_SAVE)) {
		/* Save zbc table after PMU is initialized. */
		pg->zbc_ready = true;
		nvgpu_pmu_save_zbc(g, 0xf);
	}

	if (g->elpg_enabled) {
		/* Init reg with prod values*/
		if (g->ops.pmu.pmu_setup_elpg != NULL) {
			g->ops.pmu.pmu_setup_elpg(g);
		}
		err = nvgpu_pmu_enable_elpg(g);
		if (err != 0) {
			nvgpu_err(g, "nvgpu_pmu_enable_elpg failed err=%d",
				err);
			return err;
		}
	}

	nvgpu_udelay(50);

	/* Enable AELPG */
	if (g->aelpg_enabled) {
		err = nvgpu_aelpg_init(g);
		if (err != 0) {
			nvgpu_err(g, "nvgpu_aelpg_init failed err=%d", err);
			return err;
		}

		err = nvgpu_aelpg_init_and_enable(g, PMU_AP_CTRL_ID_GRAPHICS);
		if (err != 0) {
			nvgpu_err(g, "aelpg_init_and_enable failed err=%d",
				err);
			return err;
		}
	}

	return err;
}

static void pmu_handle_pg_elpg_msg(struct gk20a *g, struct pmu_msg *msg,
			void *param, u32 status)
{
	struct nvgpu_pmu *pmu = param;
	struct pmu_pg_msg_elpg_msg *elpg_msg = &msg->msg.pg.elpg_msg;

	nvgpu_log_fn(g, " ");

	if (status != 0U) {
		nvgpu_err(g, "ELPG cmd aborted");
		return;
	}

	switch (elpg_msg->msg) {
	case PMU_PG_ELPG_MSG_INIT_ACK:
		nvgpu_pmu_dbg(g, "INIT_PG is ack from PMU, eng - %d",
			elpg_msg->engine_id);
		break;
	case PMU_PG_ELPG_MSG_ALLOW_ACK:
		nvgpu_pmu_dbg(g, "ALLOW is ack from PMU, eng - %d",
			elpg_msg->engine_id);
		if (elpg_msg->engine_id == PMU_PG_ELPG_ENGINE_ID_MS) {
			pmu->pg->mscg_transition_state = PMU_ELPG_STAT_ON;
		} else {
			pmu->pg->elpg_stat = PMU_ELPG_STAT_ON;
		}
		break;
	case PMU_PG_ELPG_MSG_DISALLOW_ACK:
		nvgpu_pmu_dbg(g, "DISALLOW is ack from PMU, eng - %d",
			elpg_msg->engine_id);

		if (elpg_msg->engine_id == PMU_PG_ELPG_ENGINE_ID_MS) {
			pmu->pg->mscg_transition_state = PMU_ELPG_STAT_OFF;
		} else {
			pmu->pg->elpg_stat = PMU_ELPG_STAT_OFF;
		}

		if (nvgpu_pmu_get_fw_state(g, pmu) ==
			PMU_FW_STATE_ELPG_BOOTING) {
			if (pmu->pg->engines_feature_list != NULL &&
				pmu->pg->engines_feature_list(g,
					PMU_PG_ELPG_ENGINE_ID_GRAPHICS) !=
				NVGPU_PMU_GR_FEATURE_MASK_POWER_GATING) {
				pmu->pg->initialized = true;
				nvgpu_pmu_fw_state_change(g, pmu, PMU_FW_STATE_STARTED,
					true);
				WRITE_ONCE(pmu->pg->mscg_stat, PMU_MSCG_DISABLED);
				/* make status visible */
				nvgpu_smp_mb();
			} else {
				nvgpu_pmu_fw_state_change(g, pmu,
					PMU_FW_STATE_ELPG_BOOTED, true);
			}
		}
		break;
	default:
		nvgpu_err(g,
			"unsupported ELPG message : 0x%04x", elpg_msg->msg);
		break;
	}
}

/* PG enable/disable */
int nvgpu_pmu_pg_global_enable(struct gk20a *g, bool enable_pg)
{
	struct nvgpu_pmu *pmu = g->pmu;
	int status = 0;

	if (!is_pg_supported(g, pmu->pg)) {
		return status;
	}

	if (enable_pg) {
		if (pmu->pg->engines_feature_list != NULL &&
			pmu->pg->engines_feature_list(g,
				PMU_PG_ELPG_ENGINE_ID_GRAPHICS) !=
			NVGPU_PMU_GR_FEATURE_MASK_POWER_GATING) {
			if (pmu->pg->lpwr_enable_pg != NULL) {
				status = pmu->pg->lpwr_enable_pg(g,
						true);
			}
		} else if (g->can_elpg) {
			status = nvgpu_pmu_enable_elpg(g);
		}
	} else {
		if (pmu->pg->engines_feature_list != NULL &&
			pmu->pg->engines_feature_list(g,
				PMU_PG_ELPG_ENGINE_ID_GRAPHICS) !=
			NVGPU_PMU_GR_FEATURE_MASK_POWER_GATING) {
			if (pmu->pg->lpwr_disable_pg != NULL) {
				status = pmu->pg->lpwr_disable_pg(g,
						true);
			}
		} else if (g->can_elpg) {
			status = nvgpu_pmu_disable_elpg(g);
		}
	}

	return status;
}

static int pmu_enable_elpg_locked(struct gk20a *g, u8 pg_engine_id)
{
	struct nvgpu_pmu *pmu = g->pmu;
	struct pmu_cmd cmd;
	int status;
	u64 tmp;

	nvgpu_log_fn(g, " ");

	(void) memset(&cmd, 0, sizeof(struct pmu_cmd));
	cmd.hdr.unit_id = PMU_UNIT_PG;
	tmp = PMU_CMD_HDR_SIZE + sizeof(struct pmu_pg_cmd_elpg_cmd);
	nvgpu_assert(tmp <= U8_MAX);
	cmd.hdr.size = (u8)tmp;
	cmd.cmd.pg.elpg_cmd.cmd_type = PMU_PG_CMD_ID_ELPG_CMD;
	cmd.cmd.pg.elpg_cmd.engine_id = pg_engine_id;
	cmd.cmd.pg.elpg_cmd.cmd = PMU_PG_ELPG_CMD_ALLOW;

	/* no need to wait ack for ELPG enable but set
	* pending to sync with follow up ELPG disable
	*/
	if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_GRAPHICS) {
		pmu->pg->elpg_stat = PMU_ELPG_STAT_ON_PENDING;
	} else if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_MS) {
		pmu->pg->mscg_transition_state = PMU_ELPG_STAT_ON_PENDING;
	}

	nvgpu_pmu_dbg(g, "cmd post PMU_PG_ELPG_CMD_ALLOW");
	status = nvgpu_pmu_cmd_post(g, &cmd, NULL,
			PMU_COMMAND_QUEUE_HPQ, pmu_handle_pg_elpg_msg,
			pmu);

	if (status != 0) {
		nvgpu_log_fn(g, "pmu_enable_elpg_locked FAILED err=%d",
			status);
	} else {
		nvgpu_log_fn(g, "done");
	}

	return status;
}

int nvgpu_pmu_enable_elpg(struct gk20a *g)
{
	struct nvgpu_pmu *pmu = g->pmu;
	u8 pg_engine_id;
	u32 pg_engine_id_list = 0;

	int ret = 0;

	nvgpu_log_fn(g, " ");

	if (!is_pg_supported(g, g->pmu->pg)) {
		return ret;
	}

	nvgpu_mutex_acquire(&pmu->pg->elpg_mutex);

	pmu->pg->elpg_refcnt++;
	if (pmu->pg->elpg_refcnt <= 0) {
		goto exit_unlock;
	}

	/* something is not right if we end up in following code path */
	if (unlikely(pmu->pg->elpg_refcnt > 1)) {
		nvgpu_warn(g,
			"%s(): possible elpg refcnt mismatch. elpg refcnt=%d",
			__func__, pmu->pg->elpg_refcnt);
		WARN_ON(true);
	}

	/* do NOT enable elpg until golden ctx is created,
	 * which is related with the ctx that ELPG save and restore.
	*/
	if (unlikely(!pmu->pg->golden_image_initialized)) {
		goto exit_unlock;
	}

	/* return if ELPG is already on or on_pending or off_on_pending */
	if (pmu->pg->elpg_stat != PMU_ELPG_STAT_OFF) {
		goto exit_unlock;
	}

	if (pmu->pg->supported_engines_list != NULL) {
		pg_engine_id_list = pmu->pg->supported_engines_list(g);
	}

	for (pg_engine_id = PMU_PG_ELPG_ENGINE_ID_GRAPHICS;
		pg_engine_id < PMU_PG_ELPG_ENGINE_ID_INVALID_ENGINE;
		pg_engine_id++) {

		if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_MS &&
			pmu->pg->mscg_stat == PMU_MSCG_DISABLED) {
			continue;
		}

		if ((BIT32(pg_engine_id) & pg_engine_id_list) != 0U) {
			ret = pmu_enable_elpg_locked(g, pg_engine_id);
		}
	}

exit_unlock:
	nvgpu_mutex_release(&pmu->pg->elpg_mutex);
	nvgpu_log_fn(g, "done");
	return ret;
}

static void pmu_dump_elpg_stats(struct nvgpu_pmu *pmu)
{
	struct gk20a *g = pmu->g;

	if (!is_pg_supported(g, pmu->pg)) {
		return;
	}

	/* Print PG stats */
	nvgpu_err(g, "Print PG stats");
	nvgpu_falcon_print_dmem(pmu->flcn,
		pmu->pg->stat_dmem_offset[PMU_PG_ELPG_ENGINE_ID_GRAPHICS],
		(u32)sizeof(struct pmu_pg_stats_v2));

	/* Print ELPG stats */
	g->ops.pmu.pmu_dump_elpg_stats(pmu);
}

int nvgpu_pmu_disable_elpg(struct gk20a *g)
{
	struct nvgpu_pmu *pmu = g->pmu;
	struct pmu_cmd cmd;
	int ret = 0;
	u8 pg_engine_id;
	u32 pg_engine_id_list = 0;
	u32 *ptr = NULL;
	u64 tmp;

	nvgpu_log_fn(g, " ");

	if (!is_pg_supported(g, pmu->pg)) {
		return ret;
	}

	if (pmu->pg->supported_engines_list != NULL) {
		pg_engine_id_list = pmu->pg->supported_engines_list(g);
	}

	nvgpu_mutex_acquire(&pmu->pg->elpg_mutex);

	pmu->pg->elpg_refcnt--;
	if (pmu->pg->elpg_refcnt > 0) {
		nvgpu_warn(g,
			"%s(): possible elpg refcnt mismatch. elpg refcnt=%d",
			__func__, pmu->pg->elpg_refcnt);
		WARN_ON(true);
		ret = 0;
		goto exit_unlock;
	}

	/* cancel off_on_pending and return */
	if (pmu->pg->elpg_stat == PMU_ELPG_STAT_OFF_ON_PENDING) {
		pmu->pg->elpg_stat = PMU_ELPG_STAT_OFF;
		ret = 0;
		goto exit_reschedule;
	}
	/* wait if on_pending */
	else if (pmu->pg->elpg_stat == PMU_ELPG_STAT_ON_PENDING) {

		pmu_wait_message_cond(pmu, nvgpu_get_poll_timeout(g),
				      &pmu->pg->elpg_stat, PMU_ELPG_STAT_ON);

		if (pmu->pg->elpg_stat != PMU_ELPG_STAT_ON) {
			nvgpu_err(g, "ELPG_ALLOW_ACK failed, elpg_stat=%d",
				pmu->pg->elpg_stat);
			pmu_dump_elpg_stats(pmu);
			nvgpu_pmu_dump_falcon_stats(pmu);
			ret = -EBUSY;
			goto exit_unlock;
		}
	}
	/* return if ELPG is already off */
	else if (pmu->pg->elpg_stat != PMU_ELPG_STAT_ON) {
		ret = 0;
		goto exit_reschedule;
	}

	for (pg_engine_id = PMU_PG_ELPG_ENGINE_ID_GRAPHICS;
		pg_engine_id < PMU_PG_ELPG_ENGINE_ID_INVALID_ENGINE;
		pg_engine_id++) {

		if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_MS &&
			pmu->pg->mscg_stat == PMU_MSCG_DISABLED) {
			continue;
		}

		if ((BIT32(pg_engine_id) & pg_engine_id_list) != 0U) {
			(void) memset(&cmd, 0, sizeof(struct pmu_cmd));
			cmd.hdr.unit_id = PMU_UNIT_PG;
			tmp = PMU_CMD_HDR_SIZE +
				sizeof(struct pmu_pg_cmd_elpg_cmd);
			nvgpu_assert(tmp <= U8_MAX);
			cmd.hdr.size = (u8)tmp;
			cmd.cmd.pg.elpg_cmd.cmd_type = PMU_PG_CMD_ID_ELPG_CMD;
			cmd.cmd.pg.elpg_cmd.engine_id = pg_engine_id;
			cmd.cmd.pg.elpg_cmd.cmd = PMU_PG_ELPG_CMD_DISALLOW;

			if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_GRAPHICS) {
				pmu->pg->elpg_stat = PMU_ELPG_STAT_OFF_PENDING;
			} else if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_MS) {
				pmu->pg->mscg_transition_state =
					PMU_ELPG_STAT_OFF_PENDING;
			}
			if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_GRAPHICS) {
				ptr = &pmu->pg->elpg_stat;
			} else if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_MS) {
				ptr = &pmu->pg->mscg_transition_state;
			}

			nvgpu_pmu_dbg(g, "cmd post PMU_PG_ELPG_CMD_DISALLOW");
			ret = nvgpu_pmu_cmd_post(g, &cmd, NULL,
				PMU_COMMAND_QUEUE_HPQ, pmu_handle_pg_elpg_msg,
				pmu);
			if (ret != 0) {
				nvgpu_err(g, "PMU_PG_ELPG_CMD_DISALLOW \
					cmd post failed");
				goto exit_unlock;
			}

			pmu_wait_message_cond(pmu,
				nvgpu_get_poll_timeout(g),
				ptr, PMU_ELPG_STAT_OFF);
			if (*ptr != PMU_ELPG_STAT_OFF) {
				nvgpu_err(g, "ELPG_DISALLOW_ACK failed");
				pmu_dump_elpg_stats(pmu);
				nvgpu_pmu_dump_falcon_stats(pmu);
				ret = -EBUSY;
				goto exit_unlock;
			}
		}
	}

exit_reschedule:
exit_unlock:
	nvgpu_mutex_release(&pmu->pg->elpg_mutex);
	nvgpu_log_fn(g, "done");
	return ret;
}

/* PG init */
static void pmu_handle_pg_stat_msg(struct gk20a *g, struct pmu_msg *msg,
			void *param, u32 status)
{
	struct nvgpu_pmu *pmu = param;

	nvgpu_log_fn(g, " ");

	if (status != 0U) {
		nvgpu_err(g, "ELPG cmd aborted");
		return;
	}

	switch (msg->msg.pg.stat.sub_msg_id) {
	case PMU_PG_STAT_MSG_RESP_DMEM_OFFSET:
		nvgpu_pmu_dbg(g, "ALLOC_DMEM_OFFSET is acknowledged from PMU");
		pmu->pg->stat_dmem_offset[msg->msg.pg.stat.engine_id] =
			msg->msg.pg.stat.data;
		break;
	default:
		nvgpu_err(g, "Invalid msg id:%u",
			msg->msg.pg.stat.sub_msg_id);
		break;
	}
}

static int pmu_pg_init_send(struct gk20a *g, u8 pg_engine_id)
{
	struct nvgpu_pmu *pmu = g->pmu;
	struct pmu_cmd cmd;
	int err = 0;
	u64 tmp;

	nvgpu_log_fn(g, " ");

	g->ops.pmu.pmu_pg_idle_counter_config(g, pg_engine_id);

	if (pmu->pg->init_param != NULL) {
		err = pmu->pg->init_param(g, pg_engine_id);
		if (err != 0) {
			nvgpu_err(g, "init_param failed err=%d", err);
			return err;
		}
	}

	/* init ELPG */
	(void) memset(&cmd, 0, sizeof(struct pmu_cmd));
	cmd.hdr.unit_id = PMU_UNIT_PG;
	tmp = PMU_CMD_HDR_SIZE + sizeof(struct pmu_pg_cmd_elpg_cmd);
	nvgpu_assert(tmp <= U8_MAX);
	cmd.hdr.size = (u8)tmp;
	cmd.cmd.pg.elpg_cmd.cmd_type = PMU_PG_CMD_ID_ELPG_CMD;
	cmd.cmd.pg.elpg_cmd.engine_id = (u8)pg_engine_id;
	cmd.cmd.pg.elpg_cmd.cmd = PMU_PG_ELPG_CMD_INIT;

	nvgpu_pmu_dbg(g, "cmd post PMU_PG_ELPG_CMD_INIT");
	err = nvgpu_pmu_cmd_post(g, &cmd, NULL, PMU_COMMAND_QUEUE_HPQ,
			pmu_handle_pg_elpg_msg, pmu);
	if (err != 0) {
		nvgpu_err(g, "PMU_PG_ELPG_CMD_INIT cmd failed\n");
		return err;
	}

	/* alloc dmem for powergating state log */
	pmu->pg->stat_dmem_offset[pg_engine_id] = 0;
	(void) memset(&cmd, 0, sizeof(struct pmu_cmd));
	cmd.hdr.unit_id = PMU_UNIT_PG;
	tmp = PMU_CMD_HDR_SIZE + sizeof(struct pmu_pg_cmd_stat);
	nvgpu_assert(tmp <= U8_MAX);
	cmd.hdr.size = (u8)tmp;
	cmd.cmd.pg.stat.cmd_type = PMU_PG_CMD_ID_PG_STAT;
	cmd.cmd.pg.stat.engine_id = pg_engine_id;
	cmd.cmd.pg.stat.sub_cmd_id = PMU_PG_STAT_CMD_ALLOC_DMEM;
	cmd.cmd.pg.stat.data = 0;

	nvgpu_pmu_dbg(g, "cmd post PMU_PG_STAT_CMD_ALLOC_DMEM");
	err = nvgpu_pmu_cmd_post(g, &cmd, NULL, PMU_COMMAND_QUEUE_LPQ,
			pmu_handle_pg_stat_msg, pmu);
	if (err != 0) {
		nvgpu_err(g, "PMU_PG_STAT_CMD_ALLOC_DMEM cmd failed\n");
		return err;
	}

	/* disallow ELPG initially
	 * PMU ucode requires a disallow cmd before allow cmd
	*/
	/* set for wait_event PMU_ELPG_STAT_OFF */
	if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_GRAPHICS) {
		pmu->pg->elpg_stat = PMU_ELPG_STAT_OFF;
	} else if (pg_engine_id == PMU_PG_ELPG_ENGINE_ID_MS) {
		pmu->pg->mscg_transition_state = PMU_ELPG_STAT_OFF;
	}
	(void) memset(&cmd, 0, sizeof(struct pmu_cmd));
	cmd.hdr.unit_id = PMU_UNIT_PG;
	tmp = PMU_CMD_HDR_SIZE + sizeof(struct pmu_pg_cmd_elpg_cmd);
	nvgpu_assert(tmp <= U8_MAX);
	cmd.hdr.size = (u8)tmp;
	cmd.cmd.pg.elpg_cmd.cmd_type = PMU_PG_CMD_ID_ELPG_CMD;
	cmd.cmd.pg.elpg_cmd.engine_id = pg_engine_id;
	cmd.cmd.pg.elpg_cmd.cmd = PMU_PG_ELPG_CMD_DISALLOW;

	nvgpu_pmu_dbg(g, "cmd post PMU_PG_ELPG_CMD_DISALLOW");
	err = nvgpu_pmu_cmd_post(g, &cmd, NULL, PMU_COMMAND_QUEUE_HPQ,
		pmu_handle_pg_elpg_msg, pmu);
	if (err != 0) {
		nvgpu_err(g, "PMU_PG_ELPG_CMD_DISALLOW cmd failed\n");
		return err;
	}

	if (pmu->pg->set_sub_feature_mask != NULL) {
		err = pmu->pg->set_sub_feature_mask(g, pg_engine_id);
		if (err != 0) {
			nvgpu_err(g, "set_sub_feature_mask failed err=%d",
				err);
			return err;
		}
	}

	return err;
}

static int pmu_pg_init_powergating(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct nvgpu_pmu_pg *pg)
{
	u8 pg_engine_id;
	u32 pg_engine_id_list = 0;
	int err = 0;

	nvgpu_log_fn(g, " ");

	if (pmu->pg->supported_engines_list != NULL) {
		pg_engine_id_list = pmu->pg->supported_engines_list(g);
	}

	g->ops.gr.init.wait_initialized(g);

	for (pg_engine_id = PMU_PG_ELPG_ENGINE_ID_GRAPHICS;
		pg_engine_id < PMU_PG_ELPG_ENGINE_ID_INVALID_ENGINE;
			pg_engine_id++) {

		if ((BIT32(pg_engine_id) & pg_engine_id_list) != 0U) {
			if (pmu != NULL &&
				nvgpu_pmu_get_fw_state(g, pmu) ==
					PMU_FW_STATE_INIT_RECEIVED) {
				nvgpu_pmu_fw_state_change(g, pmu,
					PMU_FW_STATE_ELPG_BOOTING, false);
			}
			/* Error print handled by pmu_pg_init_send */
			err = pmu_pg_init_send(g, pg_engine_id);
			if (err != 0) {
				return err;
			}
		}
	}

	if (pmu->pg->param_post_init != NULL) {
		/* Error print handled by param_post_init */
		err = pmu->pg->param_post_init(g);
	}

	return err;
}

static void pmu_handle_pg_buf_config_msg(struct gk20a *g, struct pmu_msg *msg,
			void *param, u32 status)
{
	struct nvgpu_pmu *pmu = param;
	struct pmu_pg_msg_eng_buf_stat *eng_buf_stat =
		&msg->msg.pg.eng_buf_stat;

	nvgpu_log_fn(g, " ");

	nvgpu_pmu_dbg(g,
		"reply PMU_PG_CMD_ID_ENG_BUF_LOAD PMU_PGENG_GR_BUFFER_IDX_FECS");
	if (status != 0U) {
		nvgpu_err(g, "PGENG cmd aborted");
		return;
	}

	pmu->pg->buf_loaded = (eng_buf_stat->status == PMU_PG_MSG_ENG_BUF_LOADED);
	if ((!pmu->pg->buf_loaded) &&
		(nvgpu_pmu_get_fw_state(g, pmu) ==
			PMU_FW_STATE_LOADING_PG_BUF)) {
		nvgpu_err(g, "failed to load PGENG buffer");
	} else {
		nvgpu_pmu_fw_state_change(g, pmu,
			nvgpu_pmu_get_fw_state(g, pmu), true);
	}
}

static int pmu_pg_init_bind_fecs(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct nvgpu_pmu_pg *pg)
{
	struct pmu_cmd cmd;
	int err = 0;
	u32 gr_engine_id;

	nvgpu_log_fn(g, " ");

	gr_engine_id = nvgpu_engine_get_gr_id(g);

	(void) memset(&cmd, 0, sizeof(struct pmu_cmd));
	cmd.hdr.unit_id = PMU_UNIT_PG;
	nvgpu_assert(PMU_CMD_HDR_SIZE < U32(U8_MAX));
	cmd.hdr.size = U8(PMU_CMD_HDR_SIZE) +
			pmu->fw->ops.pg_cmd_eng_buf_load_size(&cmd.cmd.pg);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_cmd_type(&cmd.cmd.pg,
			PMU_PG_CMD_ID_ENG_BUF_LOAD);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_engine_id(&cmd.cmd.pg,
			gr_engine_id);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_buf_idx(&cmd.cmd.pg,
			PMU_PGENG_GR_BUFFER_IDX_FECS);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_buf_size(&cmd.cmd.pg,
			pmu->pg->pg_buf.size);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_dma_base(&cmd.cmd.pg,
			u64_lo32(pmu->pg->pg_buf.gpu_va));
	pmu->fw->ops.pg_cmd_eng_buf_load_set_dma_offset(&cmd.cmd.pg,
			(u8)(pmu->pg->pg_buf.gpu_va & 0xFFU));
	pmu->fw->ops.pg_cmd_eng_buf_load_set_dma_idx(&cmd.cmd.pg,
			PMU_DMAIDX_VIRT);

	pg->buf_loaded = false;
	nvgpu_pmu_dbg(g,
		"cmd post PMU_PG_CMD_ID_ENG_BUF_LOAD PMU_PGENG_GR_BUFFER_IDX_FECS");
	nvgpu_pmu_fw_state_change(g, pmu, PMU_FW_STATE_LOADING_PG_BUF, false);
	err = nvgpu_pmu_cmd_post(g, &cmd, NULL, PMU_COMMAND_QUEUE_LPQ,
			pmu_handle_pg_buf_config_msg, pmu);
	if (err != 0) {
		nvgpu_err(g, "cmd LOAD PMU_PGENG_GR_BUFFER_IDX_FECS failed\n");
	}

	return err;
}

static void pmu_pg_setup_hw_load_zbc(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct nvgpu_pmu_pg *pg)
{
	struct pmu_cmd cmd;
	u32 gr_engine_id;
	int err = 0;

	gr_engine_id = nvgpu_engine_get_gr_id(g);

	(void) memset(&cmd, 0, sizeof(struct pmu_cmd));
	cmd.hdr.unit_id = PMU_UNIT_PG;
	nvgpu_assert(PMU_CMD_HDR_SIZE < U32(U8_MAX));
	cmd.hdr.size = U8(PMU_CMD_HDR_SIZE) +
			pmu->fw->ops.pg_cmd_eng_buf_load_size(&cmd.cmd.pg);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_cmd_type(&cmd.cmd.pg,
			PMU_PG_CMD_ID_ENG_BUF_LOAD);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_engine_id(&cmd.cmd.pg,
			gr_engine_id);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_buf_idx(&cmd.cmd.pg,
			PMU_PGENG_GR_BUFFER_IDX_ZBC);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_buf_size(&cmd.cmd.pg,
			pmu->pg->seq_buf.size);
	pmu->fw->ops.pg_cmd_eng_buf_load_set_dma_base(&cmd.cmd.pg,
			u64_lo32(pmu->pg->seq_buf.gpu_va));
	pmu->fw->ops.pg_cmd_eng_buf_load_set_dma_offset(&cmd.cmd.pg,
			(u8)(pmu->pg->seq_buf.gpu_va & 0xFFU));
	pmu->fw->ops.pg_cmd_eng_buf_load_set_dma_idx(&cmd.cmd.pg,
			PMU_DMAIDX_VIRT);

	pg->buf_loaded = false;
	nvgpu_pmu_dbg(g,
		"cmd post PMU_PG_CMD_ID_ENG_BUF_LOAD PMU_PGENG_GR_BUFFER_IDX_ZBC");
	nvgpu_pmu_fw_state_change(g, pmu, PMU_FW_STATE_LOADING_ZBC, false);
	err = nvgpu_pmu_cmd_post(g, &cmd, NULL, PMU_COMMAND_QUEUE_LPQ,
			pmu_handle_pg_buf_config_msg, pmu);
	if (err != 0) {
		nvgpu_err(g, "CMD LOAD PMU_PGENG_GR_BUFFER_IDX_ZBC failed\n");
	}
}

/* stats */
int nvgpu_pmu_get_pg_stats(struct gk20a *g, u32 pg_engine_id,
		struct pmu_pg_stats_data *pg_stat_data)
{
	struct nvgpu_pmu *pmu = g->pmu;
	u32 pg_engine_id_list = 0;
	int err = 0;

	if (!is_pg_supported(g, pmu->pg) || !pmu->pg->initialized) {
		pg_stat_data->ingating_time = 0;
		pg_stat_data->ungating_time = 0;
		pg_stat_data->gating_cnt = 0;
		return 0;
	}

	if (pmu->pg->supported_engines_list != NULL) {
		pg_engine_id_list = pmu->pg->supported_engines_list(g);
	}

	if ((BIT32(pg_engine_id) & pg_engine_id_list) != 0U) {
		err = nvgpu_pmu_elpg_statistics(g, pg_engine_id, pg_stat_data);
	}

	return err;
}

/* PG state machine */
static void pmu_pg_kill_task(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct nvgpu_pmu_pg *pg)
{
	struct nvgpu_timeout timeout;
	int err = 0;

	/* make sure the pending operations are finished before we continue */
	if (nvgpu_thread_is_running(&pg->pg_init.state_task)) {

		/* post PMU_FW_STATE_EXIT to exit PMU state machine loop */
		nvgpu_pmu_fw_state_change(g, pmu, PMU_FW_STATE_EXIT, true);

		/* Make thread stop*/
		nvgpu_thread_stop(&pg->pg_init.state_task);

		/* wait to confirm thread stopped */
		err = nvgpu_timeout_init(g, &timeout, 1000,
			NVGPU_TIMER_RETRY_TIMER);
		if (err != 0) {
			nvgpu_err(g, "timeout_init failed err=%d", err);
			return;
		}
		do {
			if (!nvgpu_thread_is_running(&pg->pg_init.state_task)) {
				break;
			}
			nvgpu_udelay(2);
		} while (nvgpu_timeout_expired_msg(&timeout,
			"timeout - waiting PMU state machine thread stop") == 0);
	} else {
		nvgpu_thread_join(&pg->pg_init.state_task);
	}
}

static int pmu_pg_task(void *arg)
{
	struct gk20a *g = (struct gk20a *)arg;
	struct nvgpu_pmu *pmu = g->pmu;
	struct nvgpu_pg_init *pg_init = &pmu->pg->pg_init;
	u32 pmu_state = 0;
	int err = 0;

	nvgpu_log_fn(g, "thread start");

	while (true) {

		NVGPU_COND_WAIT_INTERRUPTIBLE(&pg_init->wq,
			(pg_init->state_change == true), 0U);

		pmu->pg->pg_init.state_change = false;
		pmu_state = nvgpu_pmu_get_fw_state(g, pmu);

		if (pmu_state == PMU_FW_STATE_EXIT) {
			nvgpu_pmu_dbg(g, "pmu state exit");
			break;
		}

		switch (pmu_state) {
		case PMU_FW_STATE_INIT_RECEIVED:
			nvgpu_pmu_dbg(g, "pmu starting");
			if (g->can_elpg) {
				err = pmu_pg_init_powergating(g, pmu, pmu->pg);
			}
			break;
		case PMU_FW_STATE_ELPG_BOOTED:
			nvgpu_pmu_dbg(g, "elpg booted");
			err = pmu_pg_init_bind_fecs(g, pmu, pmu->pg);
			break;
		case PMU_FW_STATE_LOADING_PG_BUF:
			nvgpu_pmu_dbg(g, "loaded pg buf");
			pmu_pg_setup_hw_load_zbc(g, pmu, pmu->pg);
			break;
		case PMU_FW_STATE_LOADING_ZBC:
			nvgpu_pmu_dbg(g, "loaded zbc");
			err = pmu_pg_setup_hw_enable_elpg(g, pmu, pmu->pg);
			nvgpu_pmu_dbg(g, "PMU booted, thread exiting");
			return 0;
		default:
			nvgpu_pmu_dbg(g, "invalid state");
			err = -EINVAL;
			break;
		}

	}
	/*
	* If an operation above failed, the error was already logged by the
	* operation itself and this thread will end just like in the normal case
	*/
	if (err != 0) {
		nvgpu_err(g, "pg_init_task failed err=%d", err);
	}

	while (!nvgpu_thread_should_stop(&pg_init->state_task)) {
		nvgpu_usleep_range(5000, 5100);
	}

	nvgpu_log_fn(g, "thread exit");

	return err;
}

static int pmu_pg_task_init(struct gk20a *g, struct nvgpu_pmu_pg *pg)
{
	char thread_name[64];
	int err = 0;

	nvgpu_log_fn(g, " ");

	nvgpu_cond_init(&pg->pg_init.wq);

	(void) snprintf(thread_name, sizeof(thread_name),
				"nvgpu_pg_init_%s", g->name);

	err = nvgpu_thread_create(&pg->pg_init.state_task, g,
			pmu_pg_task, thread_name);
	if (err != 0) {
		nvgpu_err(g, "failed to start nvgpu_pg_init thread");
	}

	return err;
}

static int pmu_pg_init_seq_buf(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct nvgpu_pmu_pg *pg)
{
	struct mm_gk20a *mm = &g->mm;
	struct vm_gk20a *vm = mm->pmu.vm;
	int err;
	u8 *ptr;

	err = nvgpu_dma_alloc_map_sys(vm, GK20A_PMU_SEQ_BUF_SIZE,
				&pg->seq_buf);
	if (err != 0) {
		return err;
	}

	ptr = (u8 *)pg->seq_buf.cpu_va;

	ptr[0] = 0x16; /* opcode EXIT */
	ptr[1] = 0; ptr[2] = 1; ptr[3] = 0;
	ptr[4] = 0; ptr[5] = 0; ptr[6] = 0; ptr[7] = 0;

	pg->seq_buf.size = GK20A_PMU_SEQ_BUF_SIZE;

	return err;
}

int nvgpu_pmu_pg_sw_setup(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct nvgpu_pmu_pg *pg)
{
	int err;

	if (!is_pg_supported(g, pg)) {
		return 0;
	}

	/* start with elpg disabled until first enable call */
	pg->elpg_refcnt = 0;

	/* skip seq_buf alloc during unrailgate path */
	if (!nvgpu_mem_is_valid(&pg->seq_buf)) {
		err = pmu_pg_init_seq_buf(g, pmu, pg);
		if (err != 0) {
			nvgpu_err(g, "failed to allocate memory");
			return err;
		}
	}

	/* Create thread to handle PMU state machine */
	return pmu_pg_task_init(g, pg);
}

void nvgpu_pmu_pg_destroy(struct gk20a *g, struct nvgpu_pmu *pmu,
		struct nvgpu_pmu_pg *pg)
{
	struct pmu_pg_stats_data pg_stat_data = { 0 };

	if (!is_pg_supported(g, pg)) {
		return;
	}

	pmu_pg_kill_task(g, pmu, pg);

	nvgpu_pmu_get_pg_stats(g,
		PMU_PG_ELPG_ENGINE_ID_GRAPHICS, &pg_stat_data);

	if (nvgpu_pmu_disable_elpg(g) != 0) {
		nvgpu_err(g, "failed to set disable elpg");
	}

	pg->initialized = false;

	/* update the s/w ELPG residency counters */
	g->pg_ingating_time_us += (u64)pg_stat_data.ingating_time;
	g->pg_ungating_time_us += (u64)pg_stat_data.ungating_time;
	g->pg_gating_cnt += pg_stat_data.gating_cnt;

	pg->zbc_ready = false;
}

int nvgpu_pmu_pg_init(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct nvgpu_pmu_pg **pg_p)
{
	struct nvgpu_pmu_pg *pg;
	int err = 0;
	u32 ver = g->params.gpu_arch + g->params.gpu_impl;

	if (*pg_p != NULL) {
		/* skip alloc/reinit for unrailgate sequence */
		nvgpu_pmu_dbg(g, "skip lsfm init for unrailgate sequence");
		goto exit;
	}

	pg = (struct nvgpu_pmu_pg *)
		nvgpu_kzalloc(g, sizeof(struct nvgpu_pmu_pg));
	if (pg == NULL) {
		err = -ENOMEM;
		goto exit;
	}

	/* set default values to aelpg parameters */
	pg->aelpg_param[0] = APCTRL_SAMPLING_PERIOD_PG_DEFAULT_US;
	pg->aelpg_param[1] = APCTRL_MINIMUM_IDLE_FILTER_DEFAULT_US;
	pg->aelpg_param[2] = APCTRL_MINIMUM_TARGET_SAVING_DEFAULT_US;
	pg->aelpg_param[3] = APCTRL_POWER_BREAKEVEN_DEFAULT_US;
	pg->aelpg_param[4] = APCTRL_CYCLES_PER_SAMPLE_MAX_DEFAULT;

	err = nvgpu_mutex_init(&pg->elpg_mutex);
	if (err != 0) {
		nvgpu_kfree(g, pg);
		goto exit;
	}

	err = nvgpu_mutex_init(&pg->pg_mutex);
	if (err != 0) {
		nvgpu_mutex_destroy(&pg->elpg_mutex);
		nvgpu_kfree(g, pg);
		goto exit;
	}

	*pg_p = pg;

	switch (ver) {
	case GK20A_GPUID_GM20B:
	case GK20A_GPUID_GM20B_B:
		nvgpu_gm20b_pg_sw_init(g, *pg_p);
		break;

	case NVGPU_GPUID_GP10B:
		nvgpu_gp10b_pg_sw_init(g, *pg_p);
		break;

	case NVGPU_GPUID_GV11B:
		nvgpu_gv11b_pg_sw_init(g, *pg_p);
		break;

	default:
		nvgpu_kfree(g, *pg_p);
		err = -EINVAL;
		nvgpu_err(g, "no support for GPUID %x", ver);
		break;
	}
exit:
	return err;
}

void nvgpu_pmu_pg_deinit(struct gk20a *g, struct nvgpu_pmu *pmu,
	struct nvgpu_pmu_pg *pg)
{
	struct mm_gk20a *mm = &g->mm;
	struct vm_gk20a *vm = mm->pmu.vm;

	if (!is_pg_supported(g, pg)) {
		return;
	}

	if (nvgpu_mem_is_valid(&pg->seq_buf)) {
		nvgpu_dma_unmap_free(vm, &pg->seq_buf);
	}
	nvgpu_mutex_destroy(&pg->elpg_mutex);
	nvgpu_mutex_destroy(&pg->pg_mutex);
	nvgpu_kfree(g, pg);
}

void nvgpu_pmu_set_golden_image_initialized(struct gk20a *g, bool initialized)
{
	struct nvgpu_pmu *pmu = g->pmu;

	if (!is_pg_supported(g, pmu->pg)) {
		return;
	}

	pmu->pg->golden_image_initialized = initialized;
}

int nvgpu_pmu_elpg_statistics(struct gk20a *g, u32 pg_engine_id,
			struct pmu_pg_stats_data *pg_stat_data)
{
	struct nvgpu_pmu *pmu = g->pmu;

	if (!is_pg_supported(g, pmu->pg)) {
		return 0;
	}

	return pmu->pg->elpg_statistics(g, pg_engine_id, pg_stat_data);
}

void nvgpu_pmu_save_zbc(struct gk20a *g, u32 entries)
{
	struct nvgpu_pmu *pmu = g->pmu;

	if (!is_pg_supported(g, pmu->pg)) {
		return;
	}

	return pmu->pg->save_zbc(g, entries);
}

bool nvgpu_pmu_is_lpwr_feature_supported(struct gk20a *g, u32 feature_id)
{
	struct nvgpu_pmu *pmu = g->pmu;

	if (!is_pg_supported(g, pmu->pg)) {
		return false;
	}

	return pmu->pg->is_lpwr_feature_supported(g, feature_id);
}

u64 nvgpu_pmu_pg_buf_get_gpu_va(struct nvgpu_pmu *pmu)
{
	return pmu->pg->pg_buf.gpu_va;
}

struct nvgpu_mem *nvgpu_pmu_pg_buf(struct nvgpu_pmu *pmu)
{
	return &pmu->pg->pg_buf;
}

void *nvgpu_pmu_pg_buf_get_cpu_va(struct nvgpu_pmu *pmu)
{
	return pmu->pg->pg_buf.cpu_va;
}
