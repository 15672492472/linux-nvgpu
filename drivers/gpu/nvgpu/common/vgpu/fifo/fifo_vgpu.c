/*
 * Virtualized GPU Fifo
 *
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

#include <trace/events/gk20a.h>

#include <nvgpu/kmem.h>
#include <nvgpu/dma.h>
#include <nvgpu/atomic.h>
#include <nvgpu/bug.h>
#include <nvgpu/barrier.h>
#include <nvgpu/io.h>
#include <nvgpu/error_notifier.h>
#include <nvgpu/vgpu/vgpu_ivc.h>
#include <nvgpu/vgpu/vgpu.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/runlist.h>
#include <nvgpu/string.h>
#include <nvgpu/vm_area.h>

#include "fifo_vgpu.h"

#include <nvgpu/hw/gk20a/hw_ram_gk20a.h>

void vgpu_channel_bind(struct channel_gk20a *ch)
{
	struct tegra_vgpu_cmd_msg msg;
	struct tegra_vgpu_channel_config_params *p =
			&msg.params.channel_config;
	int err;
	struct gk20a *g = ch->g;

	nvgpu_log_info(g, "bind channel %d", ch->chid);

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_BIND;
	msg.handle = vgpu_get_handle(ch->g);
	p->handle = ch->virt_ctx;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	WARN_ON(err || msg.ret);

	nvgpu_smp_wmb();
	nvgpu_atomic_set(&ch->bound, true);
}

void vgpu_channel_unbind(struct channel_gk20a *ch)
{
	struct gk20a *g = ch->g;

	nvgpu_log_fn(g, " ");

	if (nvgpu_atomic_cmpxchg(&ch->bound, true, false)) {
		struct tegra_vgpu_cmd_msg msg;
		struct tegra_vgpu_channel_config_params *p =
				&msg.params.channel_config;
		int err;

		msg.cmd = TEGRA_VGPU_CMD_CHANNEL_UNBIND;
		msg.handle = vgpu_get_handle(ch->g);
		p->handle = ch->virt_ctx;
		err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
		WARN_ON(err || msg.ret);
	}

}

int vgpu_channel_alloc_inst(struct gk20a *g, struct channel_gk20a *ch)
{
	struct tegra_vgpu_cmd_msg msg;
	struct tegra_vgpu_channel_hwctx_params *p = &msg.params.channel_hwctx;
	int err;

	nvgpu_log_fn(g, " ");

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_ALLOC_HWCTX;
	msg.handle = vgpu_get_handle(g);
	p->id = ch->chid;
	p->pid = (u64)ch->pid;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	if (err || msg.ret) {
		nvgpu_err(g, "fail");
		return -ENOMEM;
	}

	ch->virt_ctx = p->handle;
	nvgpu_log_fn(g, "done");
	return 0;
}

void vgpu_channel_free_inst(struct gk20a *g, struct channel_gk20a *ch)
{
	struct tegra_vgpu_cmd_msg msg;
	struct tegra_vgpu_channel_hwctx_params *p = &msg.params.channel_hwctx;
	int err;

	nvgpu_log_fn(g, " ");

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_FREE_HWCTX;
	msg.handle = vgpu_get_handle(g);
	p->handle = ch->virt_ctx;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	WARN_ON(err || msg.ret);
}

void vgpu_channel_enable(struct channel_gk20a *ch)
{
	struct tegra_vgpu_cmd_msg msg;
	struct tegra_vgpu_channel_config_params *p =
			&msg.params.channel_config;
	int err;
	struct gk20a *g = ch->g;

	nvgpu_log_fn(g, " ");

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_ENABLE;
	msg.handle = vgpu_get_handle(ch->g);
	p->handle = ch->virt_ctx;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	WARN_ON(err || msg.ret);
}

void vgpu_channel_disable(struct channel_gk20a *ch)
{
	struct tegra_vgpu_cmd_msg msg;
	struct tegra_vgpu_channel_config_params *p =
			&msg.params.channel_config;
	int err;
	struct gk20a *g = ch->g;

	nvgpu_log_fn(g, " ");

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_DISABLE;
	msg.handle = vgpu_get_handle(ch->g);
	p->handle = ch->virt_ctx;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	WARN_ON(err || msg.ret);
}

int vgpu_channel_setup_ramfc(struct channel_gk20a *ch, u64 gpfifo_base,
				u32 gpfifo_entries,
				unsigned long acquire_timeout, u32 flags)
{
	struct tegra_vgpu_cmd_msg msg;
	struct tegra_vgpu_ramfc_params *p = &msg.params.ramfc;
	int err;
	struct gk20a *g = ch->g;

	nvgpu_log_fn(g, " ");

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_SETUP_RAMFC;
	msg.handle = vgpu_get_handle(ch->g);
	p->handle = ch->virt_ctx;
	p->gpfifo_va = gpfifo_base;
	p->num_entries = gpfifo_entries;
	p->userd_addr = ch->userd_iova;
	p->iova = 0;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));

	return (err || msg.ret) ? -ENOMEM : 0;
}

int vgpu_fifo_init_engine_info(struct fifo_gk20a *f)
{
	struct vgpu_priv_data *priv = vgpu_get_priv_data(f->g);
	struct tegra_vgpu_engines_info *engines = &priv->constants.engines_info;
	u32 i;
	struct gk20a *g = f->g;

	nvgpu_log_fn(g, " ");

	if (engines->num_engines > TEGRA_VGPU_MAX_ENGINES) {
		nvgpu_err(f->g, "num_engines %d larger than max %d",
			engines->num_engines, TEGRA_VGPU_MAX_ENGINES);
		return -EINVAL;
	}

	f->num_engines = engines->num_engines;
	for (i = 0; i < f->num_engines; i++) {
		struct fifo_engine_info_gk20a *info =
				&f->engine_info[engines->info[i].engine_id];

		if (engines->info[i].engine_id >= f->max_engines) {
			nvgpu_err(f->g, "engine id %d larger than max %d",
				engines->info[i].engine_id,
				f->max_engines);
			return -EINVAL;
		}

		info->intr_mask = engines->info[i].intr_mask;
		info->reset_mask = engines->info[i].reset_mask;
		info->runlist_id = engines->info[i].runlist_id;
		info->pbdma_id = engines->info[i].pbdma_id;
		info->inst_id = engines->info[i].inst_id;
		info->pri_base = engines->info[i].pri_base;
		info->engine_enum = engines->info[i].engine_enum;
		info->fault_id = engines->info[i].fault_id;
		f->active_engines_list[i] = engines->info[i].engine_id;
	}

	nvgpu_log_fn(g, "done");

	return 0;
}

static int vgpu_fifo_setup_sw(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	struct vgpu_priv_data *priv = vgpu_get_priv_data(g);
	unsigned int chid;
	int err = 0;

	nvgpu_log_fn(g, " ");

	if (f->sw_ready) {
		nvgpu_log_fn(g, "skip init");
		return 0;
	}

	f->g = g;
	f->num_channels = g->ops.channel.count(g);
	f->runlist_entry_size = g->ops.runlist.entry_size(g);
	f->num_runlist_entries = g->ops.runlist.length_max(g);
	f->max_engines = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_ENGINES);
	f->userd_entry_size = g->ops.fifo.userd_entry_size(g);

	err = gk20a_fifo_init_userd_slabs(g);
	if (err != 0) {
		nvgpu_err(g, "userd slab init failed, err=%d", err);
		return err;
	}

	f->channel = nvgpu_vzalloc(g, f->num_channels * sizeof(*f->channel));
	f->tsg = nvgpu_vzalloc(g, f->num_channels * sizeof(*f->tsg));
	f->engine_info = nvgpu_kzalloc(g, f->max_engines *
				       sizeof(*f->engine_info));
	f->active_engines_list = nvgpu_kzalloc(g, f->max_engines * sizeof(u32));

	if (!(f->channel && f->tsg && f->engine_info &&
		f->active_engines_list)) {
		err = -ENOMEM;
		goto clean_up;
	}
	(void) memset(f->active_engines_list, 0xff, (f->max_engines *
		sizeof(u32)));

	g->ops.fifo.init_engine_info(f);

	err = nvgpu_runlist_setup_sw(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init runlist");
		goto clean_up;
	}

	nvgpu_init_list_node(&f->free_chs);
	nvgpu_mutex_init(&f->free_chs_mutex);

	for (chid = 0; chid < f->num_channels; chid++) {
		gk20a_init_channel_support(g, chid);
		gk20a_init_tsg_support(g, chid);
	}
	nvgpu_mutex_init(&f->tsg_inuse_mutex);

	err = nvgpu_channel_worker_init(g);
	if (err) {
		goto clean_up;
	}

	f->deferred_reset_pending = false;
	nvgpu_mutex_init(&f->deferred_reset_mutex);

	f->channel_base = priv->constants.channel_base;

	f->sw_ready = true;

	nvgpu_log_fn(g, "done");
	return 0;

clean_up:
	nvgpu_log_fn(g, "fail");
	/* FIXME: unmap from bar1 */
	gk20a_fifo_free_userd_slabs(g);

	nvgpu_vfree(g, f->channel);
	f->channel = NULL;
	nvgpu_vfree(g, f->tsg);
	f->tsg = NULL;
	nvgpu_kfree(g, f->engine_info);
	f->engine_info = NULL;
	nvgpu_kfree(g, f->active_engines_list);
	f->active_engines_list = NULL;

	return err;
}

int vgpu_init_fifo_setup_hw(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 v, v1 = 0x33, v2 = 0x55;
	struct nvgpu_mem *mem = &f->userd_slabs[0];
	u32 bar1_vaddr;
	volatile u32 *cpu_vaddr;
	int err;

	nvgpu_log_fn(g, " ");

	/* allocate and map first userd slab for bar1 test. */
	err = nvgpu_dma_alloc_sys(g, PAGE_SIZE, mem);
	if (err != 0) {
		nvgpu_err(g, "userd allocation failed, err=%d", err);
		return err;
	}
	mem->gpu_va = g->ops.mm.bar1_map_userd(g, mem, 0);
	f->userd_gpu_va = mem->gpu_va;

	/* test write, read through bar1 @ userd region before
	 * turning on the snooping */

	cpu_vaddr = mem->cpu_va;
	bar1_vaddr = mem->gpu_va;

	nvgpu_log_info(g, "test bar1 @ vaddr 0x%x",
		   bar1_vaddr);

	v = gk20a_bar1_readl(g, bar1_vaddr);

	*cpu_vaddr = v1;
	nvgpu_mb();

	if (v1 != gk20a_bar1_readl(g, bar1_vaddr)) {
		nvgpu_err(g, "bar1 broken @ gk20a!");
		return -EINVAL;
	}

	gk20a_bar1_writel(g, bar1_vaddr, v2);

	if (v2 != gk20a_bar1_readl(g, bar1_vaddr)) {
		nvgpu_err(g, "bar1 broken @ gk20a!");
		return -EINVAL;
	}

	/* is it visible to the cpu? */
	if (*cpu_vaddr != v2) {
		nvgpu_err(g, "cpu didn't see bar1 write @ %p!",
			cpu_vaddr);
	}

	/* put it back */
	gk20a_bar1_writel(g, bar1_vaddr, v);

	nvgpu_log_fn(g, "done");

	return 0;
}

int vgpu_init_fifo_support(struct gk20a *g)
{
	u32 err;

	nvgpu_log_fn(g, " ");

	err = vgpu_fifo_setup_sw(g);
	if (err) {
		return err;
	}

	if (g->ops.fifo.init_fifo_setup_hw) {
		err = g->ops.fifo.init_fifo_setup_hw(g);
	}
	return err;
}

int vgpu_fifo_preempt_channel(struct gk20a *g, struct channel_gk20a *ch)
{
	struct tegra_vgpu_cmd_msg msg;
	struct tegra_vgpu_channel_config_params *p =
			&msg.params.channel_config;
	int err;

	nvgpu_log_fn(g, " ");

	if (!nvgpu_atomic_read(&ch->bound)) {
		return 0;
	}

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_PREEMPT;
	msg.handle = vgpu_get_handle(g);
	p->handle = ch->virt_ctx;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));

	if (err || msg.ret) {
		nvgpu_err(g,
			"preempt channel %d failed", ch->chid);
		err = -ENOMEM;
	}

	return err;
}

int vgpu_fifo_preempt_tsg(struct gk20a *g, struct tsg_gk20a *tsg)
{
	struct tegra_vgpu_cmd_msg msg;
	struct tegra_vgpu_tsg_preempt_params *p =
			&msg.params.tsg_preempt;
	int err;

	nvgpu_log_fn(g, " ");

	msg.cmd = TEGRA_VGPU_CMD_TSG_PREEMPT;
	msg.handle = vgpu_get_handle(g);
	p->tsg_id = tsg->tsgid;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	err = err ? err : msg.ret;

	if (err) {
		nvgpu_err(g,
			"preempt tsg %u failed", tsg->tsgid);
	}

	return err;
}

int vgpu_fifo_force_reset_ch(struct channel_gk20a *ch,
					u32 err_code, bool verbose)
{
	struct tsg_gk20a *tsg = NULL;
	struct channel_gk20a *ch_tsg = NULL;
	struct gk20a *g = ch->g;
	struct tegra_vgpu_cmd_msg msg = {0};
	struct tegra_vgpu_channel_config_params *p =
			&msg.params.channel_config;
	int err;

	nvgpu_log_fn(g, " ");

	tsg = tsg_gk20a_from_ch(ch);
	if (tsg != NULL) {
		nvgpu_rwsem_down_read(&tsg->ch_list_lock);

		nvgpu_list_for_each_entry(ch_tsg, &tsg->ch_list,
				channel_gk20a, ch_entry) {
			if (gk20a_channel_get(ch_tsg)) {
				g->ops.fifo.set_error_notifier(ch_tsg,
								err_code);
				gk20a_channel_set_unserviceable(ch_tsg);
				gk20a_channel_put(ch_tsg);
			}
		}

		nvgpu_rwsem_up_read(&tsg->ch_list_lock);
	} else {
		nvgpu_err(g, "chid: %d is not bound to tsg", ch->chid);
	}

	msg.cmd = TEGRA_VGPU_CMD_CHANNEL_FORCE_RESET;
	msg.handle = vgpu_get_handle(ch->g);
	p->handle = ch->virt_ctx;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	WARN_ON(err || msg.ret);
	if (!err) {
		gk20a_channel_abort(ch, false);
	}
	return err ? err : msg.ret;
}

static void vgpu_fifo_set_ctx_mmu_error_ch(struct gk20a *g,
		struct channel_gk20a *ch)
{
	/*
	 * If error code is already set, this mmu fault
	 * was triggered as part of recovery from other
	 * error condition.
	 * Don't overwrite error flag.
	 */
	nvgpu_set_error_notifier_if_empty(ch,
		NVGPU_ERR_NOTIFIER_FIFO_ERROR_MMU_ERR_FLT);

	/* mark channel as faulted */
	gk20a_channel_set_unserviceable(ch);

	/* unblock pending waits */
	nvgpu_cond_broadcast_interruptible(&ch->semaphore_wq);
	nvgpu_cond_broadcast_interruptible(&ch->notifier_wq);
}

static void vgpu_fifo_set_ctx_mmu_error_ch_tsg(struct gk20a *g,
		struct channel_gk20a *ch)
{
	struct tsg_gk20a *tsg = NULL;
	struct channel_gk20a *ch_tsg = NULL;

	tsg = tsg_gk20a_from_ch(ch);
	if (tsg != NULL) {
		nvgpu_rwsem_down_read(&tsg->ch_list_lock);

		nvgpu_list_for_each_entry(ch_tsg, &tsg->ch_list,
				channel_gk20a, ch_entry) {
			if (gk20a_channel_get(ch_tsg)) {
				vgpu_fifo_set_ctx_mmu_error_ch(g, ch_tsg);
				gk20a_channel_put(ch_tsg);
			}
		}

		nvgpu_rwsem_up_read(&tsg->ch_list_lock);
	} else {
		nvgpu_err(g, "chid: %d is not bound to tsg", ch->chid);
	}
}

int vgpu_fifo_isr(struct gk20a *g, struct tegra_vgpu_fifo_intr_info *info)
{
	struct channel_gk20a *ch = gk20a_channel_from_id(g, info->chid);

	nvgpu_log_fn(g, " ");
	if (!ch) {
		return 0;
	}

	nvgpu_err(g, "fifo intr (%d) on ch %u",
		info->type, info->chid);

	trace_gk20a_channel_reset(ch->chid, ch->tsgid);

	switch (info->type) {
	case TEGRA_VGPU_FIFO_INTR_PBDMA:
		g->ops.fifo.set_error_notifier(ch,
						NVGPU_ERR_NOTIFIER_PBDMA_ERROR);
		break;
	case TEGRA_VGPU_FIFO_INTR_CTXSW_TIMEOUT:
		g->ops.fifo.set_error_notifier(ch,
				NVGPU_ERR_NOTIFIER_FIFO_ERROR_IDLE_TIMEOUT);
		break;
	case TEGRA_VGPU_FIFO_INTR_MMU_FAULT:
		vgpu_fifo_set_ctx_mmu_error_ch_tsg(g, ch);
		gk20a_channel_abort(ch, false);
		break;
	default:
		WARN_ON(1);
		break;
	}

	gk20a_channel_put(ch);
	return 0;
}

u32 vgpu_fifo_default_timeslice_us(struct gk20a *g)
{
	struct vgpu_priv_data *priv = vgpu_get_priv_data(g);

	return priv->constants.default_timeslice_us;
}

u32 vgpu_channel_count(struct gk20a *g)
{
	struct vgpu_priv_data *priv = vgpu_get_priv_data(g);

	return priv->constants.num_channels;
}
