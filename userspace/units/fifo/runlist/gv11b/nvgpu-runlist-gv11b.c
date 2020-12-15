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
#include <nvgpu/engines.h>
#include <nvgpu/runlist.h>
#include <nvgpu/fuse.h>
#include <nvgpu/dma.h>
#include <nvgpu/gr/ctx.h>

#include "common/gr/ctx_priv.h"
#include <nvgpu/posix/posix-fault-injection.h>

#include "hal/fifo/runlist_ram_gv11b.h"

#include <nvgpu/hw/gv11b/hw_ram_gv11b.h>


#include "../../nvgpu-fifo-common.h"
#include "nvgpu-runlist-gv11b.h"

#define RUNLIST_GV11B_UNIT_DEBUG
#ifdef RUNLIST_GV11B_UNIT_DEBUG
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

int test_gv11b_runlist_entry_size(struct unit_module *m,
		struct gk20a *g, void *args)
{
	int ret = UNIT_FAIL;

	assert(gv11b_runlist_entry_size(g) == ram_rl_entry_size_v());
	ret = UNIT_SUCCESS;
done:
	return ret;
}

#define RL_MAX_TIMESLICE_TIMEOUT ram_rl_entry_tsg_timeslice_timeout_v(U32_MAX)
#define RL_MAX_TIMESLICE_SCALE ram_rl_entry_tsg_timeslice_scale_v(U32_MAX)

int test_gv11b_runlist_get_tsg_entry(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct nvgpu_tsg *tsg = NULL;
	int ret = UNIT_FAIL;
	u32 timeslice;
	u32 runlist[4];

	tsg = nvgpu_tsg_open(g, getpid());
	assert(tsg != NULL);

	/* no scaling */
	timeslice = RL_MAX_TIMESLICE_TIMEOUT / 2;
	gv11b_runlist_get_tsg_entry(tsg, runlist, timeslice);
	assert(ram_rl_entry_tsg_timeslice_timeout_v(runlist[0]) == timeslice);
	assert(ram_rl_entry_tsg_timeslice_scale_v(runlist[0]) == 0U);
	assert(runlist[1] == ram_rl_entry_tsg_length_f(tsg->num_active_channels));
	assert(runlist[2] == ram_rl_entry_tsg_tsgid_f(tsg->tsgid));

	/* scaling */
	timeslice = RL_MAX_TIMESLICE_TIMEOUT + 1;
	gv11b_runlist_get_tsg_entry(tsg, runlist, timeslice);
	assert(ram_rl_entry_tsg_timeslice_timeout_v(runlist[0]) == (timeslice >> 1U));
	assert(ram_rl_entry_tsg_timeslice_scale_v(runlist[0]) == 1U);

	/* oversize */
	timeslice = U32_MAX;
	gv11b_runlist_get_tsg_entry(tsg, runlist, timeslice);
	assert(ram_rl_entry_tsg_timeslice_timeout_v(runlist[0]) == RL_MAX_TIMESLICE_TIMEOUT);
	assert(ram_rl_entry_tsg_timeslice_scale_v(runlist[0]) == RL_MAX_TIMESLICE_SCALE);

	ret = UNIT_SUCCESS;

done:
	if (tsg != NULL) {
		nvgpu_ref_put(&tsg->refcount, nvgpu_tsg_release);
	}
	return ret;
}

int test_gv11b_runlist_get_ch_entry(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct nvgpu_channel *ch = NULL;
	int ret = UNIT_FAIL;
	u32 runlist[4];
	struct nvgpu_mem mem;

	ch = nvgpu_channel_open_new(g, NVGPU_INVALID_RUNLIST_ID,
			false, getpid(), getpid());
	assert(ch);

	ch->userd_mem = &mem;
	mem.aperture = APERTURE_SYSMEM;
	ch->userd_iova = 0x1000beef;

	gv11b_runlist_get_ch_entry(ch, runlist);
	assert(runlist[1] == u64_hi32(ch->userd_iova));
	assert(ram_rl_entry_chid_f(runlist[2]) == ch->chid);
	assert(runlist[3] == u64_hi32(nvgpu_inst_block_addr(g, &ch->inst_block)));

	ch->userd_mem = NULL;

	ret = UNIT_SUCCESS;

done:
	if (ch != NULL) {
		nvgpu_channel_close(ch);
	}
	return ret;
}

struct unit_module_test nvgpu_runlist_gv11b_tests[] = {
	UNIT_TEST(init_support, test_fifo_init_support, NULL, 0),
	UNIT_TEST(entry_size, test_gv11b_runlist_entry_size, NULL, 0),
	UNIT_TEST(get_tsg_entry, test_gv11b_runlist_get_tsg_entry, NULL, 0),
	UNIT_TEST(get_ch_entry, test_gv11b_runlist_get_ch_entry, NULL, 0),
	UNIT_TEST(remove_support, test_fifo_remove_support, NULL, 0),
};

UNIT_MODULE(nvgpu_runlist_gv11b, nvgpu_runlist_gv11b_tests, UNIT_PRIO_NVGPU_TEST);
