/*
 * Copyright (c) 2018-2019, NVIDIA CORPORATION.  All rights reserved.
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
#include <nvgpu/bug.h>
#include <nvgpu/timers.h>
#include <nvgpu/sec2.h>
#include <nvgpu/sec2/queue.h>
#include <nvgpu/sec2if/sec2_if_sec2.h>
#include <nvgpu/sec2if/sec2_if_cmn.h>

static void sec2_seq_init(struct nvgpu_sec2 *sec2)
{
	u32 i = 0;

	nvgpu_log_fn(sec2->g, " ");

	(void) memset(sec2->seq, 0,
		sizeof(struct sec2_sequence) * SEC2_MAX_NUM_SEQUENCES);

	(void) memset(sec2->sec2_seq_tbl, 0, sizeof(sec2->sec2_seq_tbl));

	for (i = 0; i < SEC2_MAX_NUM_SEQUENCES; i++) {
		sec2->seq[i].id = (u8)i;
	}
}

static void nvgpu_remove_sec2_support(struct nvgpu_sec2 *sec2)
{
	struct gk20a *g = sec2->g;

	nvgpu_log_fn(g, " ");

	nvgpu_kfree(g, sec2->seq);
	nvgpu_mutex_destroy(&sec2->sec2_seq_lock);
	nvgpu_mutex_destroy(&sec2->isr_mutex);
}

int nvgpu_init_sec2_setup_sw(struct gk20a *g, struct nvgpu_sec2 *sec2)
{
	int err = 0;

	nvgpu_log_fn(g, " ");

	sec2->g = g;

	sec2->seq = nvgpu_kzalloc(g, SEC2_MAX_NUM_SEQUENCES *
		sizeof(struct sec2_sequence));
	if (sec2->seq == NULL) {
		err = -ENOMEM;
		goto exit;
	}

	err = nvgpu_mutex_init(&sec2->sec2_seq_lock);
	if (err != 0) {
		goto free_seq_alloc;
	}

	sec2_seq_init(sec2);

	err = nvgpu_mutex_init(&sec2->isr_mutex);
	if (err != 0) {
		goto free_seq_mutex;
	}

	sec2->remove_support = nvgpu_remove_sec2_support;

	goto exit;

free_seq_mutex:
	nvgpu_mutex_destroy(&sec2->sec2_seq_lock);
free_seq_alloc:
	nvgpu_kfree(g, sec2->seq);

exit:
	return err;
}

int nvgpu_init_sec2_support(struct gk20a *g)
{
	struct nvgpu_sec2 *sec2 = &g->sec2;
	int err = 0;

	nvgpu_log_fn(g, " ");

	/* Enable irq*/
	nvgpu_mutex_acquire(&sec2->isr_mutex);
	g->ops.sec2.enable_irq(sec2, true);
	sec2->isr_enabled = true;
	nvgpu_mutex_release(&sec2->isr_mutex);

	/* execute SEC2 in secure mode to boot RTOS */
	g->ops.sec2.secured_sec2_start(g);

	return err;
}

int nvgpu_sec2_destroy(struct gk20a *g)
{
	struct nvgpu_sec2 *sec2 = &g->sec2;

	nvgpu_log_fn(g, " ");

	nvgpu_mutex_acquire(&sec2->isr_mutex);
	sec2->isr_enabled = false;
	nvgpu_mutex_release(&sec2->isr_mutex);

	nvgpu_sec2_queues_free(g, sec2->queues);

	sec2->sec2_ready = false;

	return 0;
}

/* Add code below to handle SEC2 RTOS commands */
/* LSF's bootstrap command */
static void sec2_handle_lsfm_boot_acr_msg(struct gk20a *g,
	struct nv_flcn_msg_sec2 *msg,
	void *param, u32 status)
{
	bool *command_ack = param;

	nvgpu_log_fn(g, " ");

	nvgpu_sec2_dbg(g, "reply NV_SEC2_ACR_CMD_ID_BOOTSTRAP_FALCON");

	nvgpu_sec2_dbg(g, "flcn %d: error code = %x",
		msg->msg.acr.msg_flcn.falcon_id,
		msg->msg.acr.msg_flcn.error_code);

	*command_ack = true;
}

static void sec2_load_ls_falcons(struct gk20a *g, struct nvgpu_sec2 *sec2,
	u32 falcon_id, u32 flags)
{
	struct nv_flcn_cmd_sec2 cmd;
	bool command_ack;
	int err = 0;
	size_t tmp_size;

	nvgpu_log_fn(g, " ");

	/* send message to load falcon */
	(void) memset(&cmd, 0, sizeof(struct nv_flcn_cmd_sec2));
	cmd.hdr.unit_id = NV_SEC2_UNIT_ACR;
	tmp_size = PMU_CMD_HDR_SIZE +
		sizeof(struct nv_sec2_acr_cmd_bootstrap_falcon);
	nvgpu_assert(tmp_size <= U64(U8_MAX));
	cmd.hdr.size = U8(tmp_size);

	cmd.cmd.acr.bootstrap_falcon.cmd_type =
		NV_SEC2_ACR_CMD_ID_BOOTSTRAP_FALCON;
	cmd.cmd.acr.bootstrap_falcon.flags = flags;
	cmd.cmd.acr.bootstrap_falcon.falcon_id = falcon_id;

	nvgpu_sec2_dbg(g, "NV_SEC2_ACR_CMD_ID_BOOTSTRAP_FALCON : %x",
		falcon_id);

	command_ack = false;
	err = nvgpu_sec2_cmd_post(g, &cmd, PMU_COMMAND_QUEUE_HPQ,
		sec2_handle_lsfm_boot_acr_msg, &command_ack, U32_MAX);
	if (err != 0) {
		nvgpu_err(g, "command post failed");
	}

	err = nvgpu_sec2_wait_message_cond(sec2, nvgpu_get_poll_timeout(g),
		&command_ack, U8(true));
	if (err != 0) {
		nvgpu_err(g, "command ack receive failed");
	}

	return;
}

int nvgpu_sec2_bootstrap_ls_falcons(struct gk20a *g, struct nvgpu_sec2 *sec2,
	u32 falcon_id)
{
	int err = 0;

	nvgpu_log_fn(g, " ");

	nvgpu_sec2_dbg(g, "Check SEC2 RTOS is ready else wait");
	err = nvgpu_sec2_wait_message_cond(&g->sec2, nvgpu_get_poll_timeout(g),
			&g->sec2.sec2_ready, U8(true));
	if (err != 0){
		nvgpu_err(g, "SEC2 RTOS not ready yet, failed to bootstrap flcn %d",
			falcon_id);
		goto exit;
	}

	nvgpu_sec2_dbg(g, "LS flcn %d bootstrap, blocked call", falcon_id);
	sec2_load_ls_falcons(g, sec2, falcon_id,
		NV_SEC2_ACR_CMD_BOOTSTRAP_FALCON_FLAGS_RESET_YES);

exit:
	nvgpu_sec2_dbg(g, "Done, err-%x", err);
	return err;
}
