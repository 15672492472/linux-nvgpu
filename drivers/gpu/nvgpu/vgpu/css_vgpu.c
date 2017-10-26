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
#if defined(CONFIG_GK20A_CYCLE_STATS)

#include <linux/tegra-ivc.h>
#include <linux/tegra_vgpu.h>

#include "gk20a/gk20a.h"
#include "gk20a/channel_gk20a.h"
#include "gk20a/platform_gk20a.h"
#include "gk20a/css_gr_gk20a.h"
#include "vgpu.h"
#include "css_vgpu.h"

static struct tegra_hv_ivm_cookie *css_cookie;

static struct tegra_hv_ivm_cookie *vgpu_css_reserve_mempool(struct gk20a *g)
{
	struct device *dev = dev_from_gk20a(g);
	struct device_node *np = dev->of_node;
	struct of_phandle_args args;
	struct device_node *hv_np;
	struct tegra_hv_ivm_cookie *cookie;
	u32 mempool;
	int err;

	err = of_parse_phandle_with_fixed_args(np,
			"mempool-css", 1, 0, &args);
	if (err) {
		nvgpu_err(g, "dt missing mempool-css");
		return ERR_PTR(err);
	}

	hv_np = args.np;
	mempool = args.args[0];
	cookie = tegra_hv_mempool_reserve(hv_np, mempool);
	if (IS_ERR_OR_NULL(cookie)) {
		nvgpu_err(g, "mempool  %u reserve failed", mempool);
		return ERR_PTR(-EINVAL);
	}
	return cookie;
}

u32 vgpu_css_get_buffer_size(struct gk20a *g)
{
	struct tegra_hv_ivm_cookie *cookie;
	u32 size;

	nvgpu_log_fn(g, " ");

	if (css_cookie) {
		nvgpu_log_info(g, "buffer size = %llu", css_cookie->size);
		return (u32)css_cookie->size;
	}

	cookie = vgpu_css_reserve_mempool(g);
	if (IS_ERR(css_cookie))
		return 0;

	size = cookie->size;

	tegra_hv_mempool_unreserve(cookie);
	nvgpu_log_info(g, "buffer size = %u", size);
	return size;
}

static int vgpu_css_init_snapshot_buffer(struct gr_gk20a *gr)
{
	struct gk20a *g = gr->g;
	struct gk20a_cs_snapshot *data = gr->cs_data;
	void *buf = NULL;
	int err;

	gk20a_dbg_fn("");

	if (data->hw_snapshot)
		return 0;

	css_cookie = vgpu_css_reserve_mempool(g);
	if (IS_ERR(css_cookie))
		return PTR_ERR(css_cookie);

	/* Make sure buffer size is large enough */
	if (css_cookie->size < CSS_MIN_HW_SNAPSHOT_SIZE) {
		nvgpu_info(g, "mempool size %lld too small",
			css_cookie->size);
		err = -ENOMEM;
		goto fail;
	}

	buf = ioremap_cache(css_cookie->ipa, css_cookie->size);
	if (!buf) {
		nvgpu_info(g, "ioremap_cache failed");
		err = -EINVAL;
		goto fail;
	}

	data->hw_snapshot = buf;
	data->hw_end = data->hw_snapshot +
		css_cookie->size / sizeof(struct gk20a_cs_snapshot_fifo_entry);
	data->hw_get = data->hw_snapshot;
	memset(data->hw_snapshot, 0xff, css_cookie->size);
	return 0;
fail:
	tegra_hv_mempool_unreserve(css_cookie);
	css_cookie = NULL;
	return err;
}

void vgpu_css_release_snapshot_buffer(struct gr_gk20a *gr)
{
	struct gk20a_cs_snapshot *data = gr->cs_data;

	if (!data->hw_snapshot)
		return;

	iounmap(data->hw_snapshot);
	data->hw_snapshot = NULL;

	tegra_hv_mempool_unreserve(css_cookie);
	css_cookie = NULL;

	gk20a_dbg_info("cyclestats(vgpu): buffer for snapshots released\n");
}

int vgpu_css_flush_snapshots(struct channel_gk20a *ch,
			u32 *pending, bool *hw_overflow)
{
	struct gk20a *g = ch->g;
	struct tegra_vgpu_cmd_msg msg = {};
	struct tegra_vgpu_channel_cyclestats_snapshot_params *p;
	struct gr_gk20a *gr = &g->gr;
	struct gk20a_cs_snapshot *data = gr->cs_data;
	int err;

	gk20a_dbg_fn("");

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_CYCLESTATS_SNAPSHOT;
	msg.handle = vgpu_get_handle(g);
	p = &msg.params.cyclestats_snapshot;
	p->handle = ch->virt_ctx;
	p->subcmd = NVGPU_IOCTL_CHANNEL_CYCLE_STATS_SNAPSHOT_CMD_FLUSH;
	p->buf_info = (uintptr_t)data->hw_get - (uintptr_t)data->hw_snapshot;

	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));

	err = (err || msg.ret) ? -1 : 0;

	*pending = p->buf_info;
	*hw_overflow = p->hw_overflow;

	return err;
}

static int vgpu_css_attach(struct channel_gk20a *ch,
		struct gk20a_cs_snapshot_client *cs_client)
{
	struct gk20a *g = ch->g;
	struct tegra_vgpu_cmd_msg msg = {};
	struct tegra_vgpu_channel_cyclestats_snapshot_params *p =
				&msg.params.cyclestats_snapshot;
	int err;

	gk20a_dbg_fn("");

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_CYCLESTATS_SNAPSHOT;
	msg.handle = vgpu_get_handle(g);
	p->handle = ch->virt_ctx;
	p->subcmd = NVGPU_IOCTL_CHANNEL_CYCLE_STATS_SNAPSHOT_CMD_ATTACH;
	p->perfmon_count = cs_client->perfmon_count;

	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	err = err ? err : msg.ret;
	if (err)
		nvgpu_err(g, "failed");
	else
		cs_client->perfmon_start = p->perfmon_start;

	return err;
}

int vgpu_css_detach(struct channel_gk20a *ch,
		struct gk20a_cs_snapshot_client *cs_client)
{
	struct gk20a *g = ch->g;
	struct tegra_vgpu_cmd_msg msg = {};
	struct tegra_vgpu_channel_cyclestats_snapshot_params *p =
				&msg.params.cyclestats_snapshot;
	int err;

	gk20a_dbg_fn("");

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_CYCLESTATS_SNAPSHOT;
	msg.handle = vgpu_get_handle(g);
	p->handle = ch->virt_ctx;
	p->subcmd = NVGPU_IOCTL_CHANNEL_CYCLE_STATS_SNAPSHOT_CMD_DETACH;
	p->perfmon_start = cs_client->perfmon_start;
	p->perfmon_count = cs_client->perfmon_count;

	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	err = err ? err : msg.ret;
	if (err)
		nvgpu_err(g, "failed");

	return err;
}

int vgpu_css_enable_snapshot_buffer(struct channel_gk20a *ch,
				struct gk20a_cs_snapshot_client *cs_client)
{
	int ret;

	ret = vgpu_css_attach(ch, cs_client);
	if (ret)
		return ret;

	ret = vgpu_css_init_snapshot_buffer(&ch->g->gr);
	return ret;
}
#endif /* CONFIG_GK20A_CYCLE_STATS */
