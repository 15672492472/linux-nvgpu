/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <unit/io.h>
#include <unit/unit.h>

#include <nvgpu/channel.h>
#include <nvgpu/tsg.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/pbdma_status.h>
#include <nvgpu/engines.h>
#include <nvgpu/runlist.h>
#include <nvgpu/fuse.h>
#include <nvgpu/dma.h>
#include <nvgpu/io.h>
#include <nvgpu/posix/io.h>

#include "hal/fifo/fifo_intr_gk20a.h"
#include <nvgpu/hw/gk20a/hw_fifo_gk20a.h>

#include "../../nvgpu-fifo-common.h"
#include "nvgpu-fifo-intr-gk20a.h"

#ifdef FIFO_GK20A_INTR_UNIT_DEBUG
#undef unit_verbose
#define unit_verbose	unit_info
#else
#define unit_verbose(unit, msg, ...) \
	do { \
		if (0) { \
			unit_info(unit, msg, ##__VA_ARGS__); \
		} \
	} while (0)
#endif

#define assert(cond)	unit_assert(cond, goto done)

struct unit_ctx {
	u32 count;
	bool fail;
	bool recover;
};

static struct unit_ctx u;

int test_gk20a_fifo_intr_1_enable(struct unit_module *m,
		struct gk20a *g, void *args)
{
	int ret = UNIT_FAIL;

	nvgpu_writel(g, fifo_intr_en_1_r(), 0);
	gk20a_fifo_intr_1_enable(g, true);
	assert((nvgpu_readl(g, fifo_intr_en_1_r()) &
		fifo_intr_0_channel_intr_pending_f()) != 0);

	gk20a_fifo_intr_1_enable(g, false);
	assert((nvgpu_readl(g, fifo_intr_en_1_r()) &
		fifo_intr_0_channel_intr_pending_f()) == 0);

	assert(ret == UNIT_FAIL);
	ret = UNIT_SUCCESS;
done:
	return ret;
}

int test_gk20a_fifo_intr_1_isr(struct unit_module *m,
		struct gk20a *g, void *args)
{
	int ret = UNIT_FAIL;

	/* no channel intr pending */
	nvgpu_writel(g, fifo_intr_0_r(), ~fifo_intr_0_channel_intr_pending_f());
	gk20a_fifo_intr_1_isr(g);
	assert(nvgpu_readl(g, fifo_intr_0_r()) == 0);

	/* channel intr pending */
	nvgpu_writel(g, fifo_intr_0_r(), U32_MAX);
	gk20a_fifo_intr_1_isr(g);
	assert(nvgpu_readl(g, fifo_intr_0_r()) == fifo_intr_0_channel_intr_pending_f());

	ret = UNIT_SUCCESS;
done:
	return ret;
}

static void stub_gr_falcon_dump_stats(struct gk20a *g)
{
	nvgpu_writel(g, fifo_intr_chsw_error_r(), 0);
	u.count++;
}

int test_gk20a_fifo_intr_handle_chsw_error(struct unit_module *m,
		struct gk20a *g, void *args)
{
	int ret = UNIT_FAIL;
	struct gpu_ops gops = g->ops;

	g->ops.gr.falcon.dump_stats = stub_gr_falcon_dump_stats;

	u.count = 0;
	nvgpu_writel(g, fifo_intr_chsw_error_r(), 0xcafe);
	gk20a_fifo_intr_handle_chsw_error(g);
	assert(u.count > 0);
	assert(nvgpu_readl(g, fifo_intr_chsw_error_r()) == 0xcafe);

	ret = UNIT_SUCCESS;
done:
	g->ops = gops;
	return ret;
}


static void writel_access_reg_fn(struct gk20a *g,
	struct nvgpu_reg_access *access)
{
	u.fail = (access->addr != fifo_intr_runlist_r()) ||
		 (access->value != 0xcafe);
}

static void readl_access_reg_fn(struct gk20a *g,
		struct nvgpu_reg_access *access)
{
	if (access->addr == fifo_intr_runlist_r()) {
		access->value = 0xcafe;
	} else {
		u.fail = true;
	}
}

int test_gk20a_fifo_intr_handle_runlist_event(struct unit_module *m,
		struct gk20a *g, void *args)
{
	int ret = UNIT_FAIL;
	struct nvgpu_posix_io_callbacks *old_io;
	struct nvgpu_posix_io_callbacks new_io = {
		.readl = readl_access_reg_fn,
		.writel = writel_access_reg_fn
	};

	u.fail = false;
	old_io = nvgpu_posix_register_io(g, &new_io);
	gk20a_fifo_intr_handle_runlist_event(g);
	assert(!u.fail);

	ret = UNIT_SUCCESS;
done:
	(void) nvgpu_posix_register_io(g, old_io);
	return ret;
}

static bool stub_pbdma_handle_intr(struct gk20a *g, u32 pbdma_id,
	u32 *error_notifier, struct nvgpu_pbdma_status_info *pbdma_status)
{
	if (nvgpu_readl(g, fifo_intr_pbdma_id_r()) != BIT(pbdma_id)) {
		u.fail = true;
	}

	pbdma_status->chsw_status = NVGPU_PBDMA_CHSW_STATUS_INVALID;
	u.count++;

	return u.recover;
}

int test_gk20a_fifo_pbdma_isr(struct unit_module *m,
		struct gk20a *g, void *args)
{
	int ret = UNIT_FAIL;
	u32 pending;
	int i;
	u32 pbdma_id;
	u32 num_pbdma = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_PBDMA);
	struct gpu_ops gops = g->ops;

	assert(num_pbdma > 0);

	g->ops.pbdma.handle_intr = stub_pbdma_handle_intr;

	u.fail = false;
	for (i = 0; i < 2; i++) {
		u.recover = (i > 0);
		for (pbdma_id = 0; pbdma_id < num_pbdma; pbdma_id++) {
			nvgpu_writel(g, fifo_intr_pbdma_id_r(), BIT(pbdma_id));
			u.count = 0;
			pending = gk20a_fifo_pbdma_isr(g);
			assert(pending == fifo_intr_0_pbdma_intr_pending_f());
			assert(!u.fail);
			assert(u.count == 1);
		}
	}
	ret = UNIT_SUCCESS;

done:
	g->ops = gops;
	return ret;
}