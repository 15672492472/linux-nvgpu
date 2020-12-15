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

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <unit/io.h>
#include <unit/unit.h>

#include <nvgpu/types.h>
#include <nvgpu/bitops.h>
#include <nvgpu/pbdma.h>
#include <nvgpu/pbdma_status.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/fifo.h>
#include <nvgpu/engines.h>

#include <nvgpu/posix/posix-fault-injection.h>

#include "hal/init/hal_gv11b.h"

#include "../nvgpu-fifo-common.h"
#include "../nvgpu-fifo-gv11b.h"
#include "nvgpu-pbdma.h"

#ifdef PBDMA_UNIT_DEBUG
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

#define pruned		test_fifo_subtest_pruned
#define branches_str	test_fifo_flags_str

struct unit_ctx {
	u32 branches;
};

static struct unit_ctx unit_ctx;

#define F_PBDMA_SETUP_SW_KZALLOC_FAIL		BIT(0)
#define F_PBDMA_SETUP_SW_DEVICE_FATAL_0		BIT(1)
#define F_PBDMA_SETUP_SW_CHANNEL_FATAL_0	BIT(2)
#define F_PBDMA_SETUP_SW_RESTARTABLE_0		BIT(3)
#define F_PBDMA_SETUP_SW_LAST			BIT(4)

static u32 stub_pbdma_device_fatal_0_intr_descs(void) {
	return F_PBDMA_SETUP_SW_DEVICE_FATAL_0;
}

static u32 stub_pbdma_channel_fatal_0_intr_descs(void) {
	return F_PBDMA_SETUP_SW_CHANNEL_FATAL_0;
}

static u32 stub_pbdma_restartable_0_intr_descs(void) {
	return F_PBDMA_SETUP_SW_RESTARTABLE_0;
}

int test_pbdma_setup_sw(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct nvgpu_fifo *f = &g->fifo;
	struct gpu_ops gops = g->ops;
	struct nvgpu_posix_fault_inj *kmem_fi;
	u32 branches = 0U;
	int ret = UNIT_FAIL;
	u32 fail = F_PBDMA_SETUP_SW_KZALLOC_FAIL;
	static const char *labels[] = {
		"kzalloc_fail",
		"device_fatal_0",
		"channel_fatal_0",
		"restartable_0",
	};
	u32 prune = fail;
	int err;

	kmem_fi = nvgpu_kmem_get_fault_injection();

	err = test_fifo_setup_gv11b_reg_space(m, g);
	assert(err == 0);

	gv11b_init_hal(g);

	for (branches = 0U; branches < F_PBDMA_SETUP_SW_LAST; branches++) {

		if (pruned(branches, prune)) {
			unit_verbose(m, "%s branches=%s (pruned)\n", __func__,
				branches_str(branches, labels));
			continue;
		}
		unit_verbose(m, "%s branches=%s\n", __func__,
			branches_str(branches, labels));

		nvgpu_posix_enable_fault_injection(kmem_fi,
			branches & F_PBDMA_SETUP_SW_KZALLOC_FAIL ?
				true : false, 0);

		f->intr.pbdma.device_fatal_0 = 0;
		f->intr.pbdma.channel_fatal_0 = 0;
		f->intr.pbdma.restartable_0 = 0;

		g->ops.pbdma.device_fatal_0_intr_descs =
			branches & F_PBDMA_SETUP_SW_DEVICE_FATAL_0 ?
			stub_pbdma_device_fatal_0_intr_descs : NULL;

		g->ops.pbdma.channel_fatal_0_intr_descs =
			branches & F_PBDMA_SETUP_SW_CHANNEL_FATAL_0 ?
			stub_pbdma_channel_fatal_0_intr_descs : NULL;

		g->ops.pbdma.restartable_0_intr_descs =
			branches & F_PBDMA_SETUP_SW_RESTARTABLE_0 ?
			stub_pbdma_restartable_0_intr_descs : NULL;

		err = nvgpu_pbdma_setup_sw(g);

		if (branches & fail) {
			assert(err != 0);
			assert(f->pbdma_map == NULL);
		} else {
			assert(err == 0);
			assert(f->pbdma_map != NULL);
			assert(f->intr.pbdma.device_fatal_0 ==
				(branches & F_PBDMA_SETUP_SW_DEVICE_FATAL_0));
			assert(f->intr.pbdma.channel_fatal_0 ==
				(branches & F_PBDMA_SETUP_SW_CHANNEL_FATAL_0));
			assert(f->intr.pbdma.restartable_0 ==
				(branches & F_PBDMA_SETUP_SW_RESTARTABLE_0));
			nvgpu_pbdma_cleanup_sw(g);
			assert(f->pbdma_map == NULL);
		}
	}
	ret = UNIT_SUCCESS;

done:
	if (ret != UNIT_SUCCESS) {
		unit_err(m, "%s branches=%s\n", __func__,
			branches_str(branches, labels));
	}
	g->ops = gops;
	nvgpu_posix_enable_fault_injection(kmem_fi, false, 0);
	return ret;
}

int test_pbdma_find_for_runlist(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct nvgpu_fifo *f = &g->fifo;
	struct nvgpu_fifo fifo = g->fifo;
	u32 runlist_id;
	bool active;
	bool found;
	u32 pbdma_id;
	int ret = UNIT_FAIL;

	for (runlist_id = 0; runlist_id < f->max_runlists; runlist_id++) {

		active = nvgpu_engine_is_valid_runlist_id(g, runlist_id);

		pbdma_id = U32_MAX;
		found = nvgpu_pbdma_find_for_runlist(g, runlist_id, &pbdma_id);

		if (active) {
			assert(found);
			assert(pbdma_id != U32_MAX);
			assert((f->pbdma_map[pbdma_id] & BIT(runlist_id)) != 0);
		} else {
			assert(!found);
			assert(pbdma_id == U32_MAX);
		}
	}

	f->num_pbdma = 0;
	assert(!nvgpu_pbdma_find_for_runlist(g, 0, &pbdma_id));

	ret = UNIT_SUCCESS;

done:
	g->fifo = fifo;

	return ret;
}

int test_pbdma_status(struct unit_module *m,
		struct gk20a *g, void *args)
{
	int ret = UNIT_FAIL;
	struct nvgpu_pbdma_status_info pbdma_status;

	memset(&pbdma_status, 0, sizeof(pbdma_status));
	for (pbdma_status.chsw_status = NVGPU_PBDMA_CHSW_STATUS_INVALID;
		pbdma_status.chsw_status <= NVGPU_PBDMA_CHSW_STATUS_SWITCH;
		pbdma_status.chsw_status++)
	{
		assert(nvgpu_pbdma_status_is_chsw_switch(&pbdma_status) ==
			(pbdma_status.chsw_status == NVGPU_PBDMA_CHSW_STATUS_SWITCH));
		assert(nvgpu_pbdma_status_is_chsw_load(&pbdma_status) ==
			(pbdma_status.chsw_status == NVGPU_PBDMA_CHSW_STATUS_LOAD));
		assert(nvgpu_pbdma_status_is_chsw_save(&pbdma_status) ==
			(pbdma_status.chsw_status == NVGPU_PBDMA_CHSW_STATUS_SAVE));
		assert(nvgpu_pbdma_status_is_chsw_valid(&pbdma_status) ==
			(pbdma_status.chsw_status == NVGPU_PBDMA_CHSW_STATUS_VALID));
	}

	pbdma_status.id_type = PBDMA_STATUS_ID_TYPE_CHID;
	assert(nvgpu_pbdma_status_is_id_type_tsg(&pbdma_status) == false);
	pbdma_status.id_type = PBDMA_STATUS_ID_TYPE_TSGID;
	assert(nvgpu_pbdma_status_is_id_type_tsg(&pbdma_status) == true);
	pbdma_status.id_type = PBDMA_STATUS_ID_TYPE_INVALID;
	assert(nvgpu_pbdma_status_is_id_type_tsg(&pbdma_status) == false);

	pbdma_status.next_id_type = PBDMA_STATUS_ID_TYPE_CHID;
	assert(nvgpu_pbdma_status_is_next_id_type_tsg(&pbdma_status) == false);
	pbdma_status.next_id_type = PBDMA_STATUS_ID_TYPE_TSGID;
	assert(nvgpu_pbdma_status_is_next_id_type_tsg(&pbdma_status) == true);
	pbdma_status.next_id_type = PBDMA_STATUS_ID_TYPE_INVALID;
	assert(nvgpu_pbdma_status_is_next_id_type_tsg(&pbdma_status) == false);

	ret = UNIT_SUCCESS;

done:
	return ret;
}

struct unit_module_test nvgpu_pbdma_tests[] = {
	UNIT_TEST(setup_sw, test_pbdma_setup_sw, &unit_ctx, 0),
	UNIT_TEST(init_support, test_fifo_init_support, &unit_ctx, 0),
	UNIT_TEST(pbdma_find_for_runlist, test_pbdma_find_for_runlist, &unit_ctx, 0),
	UNIT_TEST(pbdma_status, test_pbdma_status, &unit_ctx, 0),
	UNIT_TEST(remove_support, test_fifo_remove_support, &unit_ctx, 0),
};

UNIT_MODULE(nvgpu_pbdma, nvgpu_pbdma_tests, UNIT_PRIO_NVGPU_TEST);
