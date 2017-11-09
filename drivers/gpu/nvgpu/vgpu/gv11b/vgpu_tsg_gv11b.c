/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/tegra_vgpu.h>
#include <gk20a/gk20a.h>
#include <vgpu/vgpu.h>

#include "vgpu_tsg_gv11b.h"

int vgpu_gv11b_tsg_bind_channel(struct tsg_gk20a *tsg,
				struct channel_gk20a *ch)
{
	struct tegra_vgpu_cmd_msg msg = {};
	struct tegra_vgpu_tsg_bind_channel_ex_params *p =
				&msg.params.t19x.tsg_bind_channel_ex;
	int err;

	gk20a_dbg_fn("");

	err = gk20a_tsg_bind_channel(tsg, ch);
	if (err)
		return err;

	msg.cmd = TEGRA_VGPU_CMD_TSG_BIND_CHANNEL_EX;
	msg.handle = vgpu_get_handle(tsg->g);
	p->tsg_id = tsg->tsgid;
	p->ch_handle = ch->virt_ctx;
	p->subctx_id = ch->t19x.subctx_id;
	p->runqueue_sel = ch->t19x.runqueue_sel;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	err = err ? err : msg.ret;
	if (err) {
		nvgpu_err(tsg->g,
			"vgpu_gv11b_tsg_bind_channel failed, ch %d tsgid %d",
			ch->chid, tsg->tsgid);
		gk20a_tsg_unbind_channel(ch);
	}

	return err;
}
