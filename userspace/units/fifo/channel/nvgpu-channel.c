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
#include <nvgpu/channel_sync.h>
#include <nvgpu/dma.h>
#include <nvgpu/engines.h>
#include <nvgpu/tsg.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/runlist.h>

#include <nvgpu/posix/posix-fault-injection.h>

#include "../nvgpu-fifo.h"

#define MAX_STUB	2

struct stub_ctx {
	u32 chid;
	u32 tsgid;
};

struct stub_ctx stub[MAX_STUB];

struct channel_unit_ctx {
	u32 branches;
	struct stub_ctx stub[MAX_STUB];
};

static struct channel_unit_ctx unit_ctx;

static void subtest_setup(u32 branches)
{
	u32 i;

	unit_ctx.branches = branches;
	memset(stub, 0, sizeof(stub));
	for (i = 0; i < MAX_STUB; i++) {
		stub[i].chid = NVGPU_INVALID_CHANNEL_ID;
	}
}

#define subtest_pruned	test_fifo_subtest_pruned
#define branches_str	test_fifo_flags_str

#define assert(cond)	unit_assert(cond, goto done)

#define F_CHANNEL_SETUP_SW_VZALLOC_FAIL				BIT(0)
#define F_CHANNEL_SETUP_SW_LAST					BIT(1)

/* TODO: nvgpu_cond_init failure, not testable yet */
#define F_CHANNEL_SETUP_SW_INIT_SUPPORT_FAIL_COND_INIT

static const char *f_channel_setup_sw[] = {
	"vzalloc_fail",
};

static u32 stub_channel_count(struct gk20a *g)
{
	return 32;
}

static int test_channel_setup_sw(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct gpu_ops gops = g->ops;
	struct nvgpu_fifo *f = &g->fifo;
	struct nvgpu_posix_fault_inj *kmem_fi;
	u32 branches;
	int ret = UNIT_FAIL;
	int err;
	u32 fail = F_CHANNEL_SETUP_SW_VZALLOC_FAIL;

	u32 prune = fail;

	kmem_fi = nvgpu_kmem_get_fault_injection();

	g->ops.channel.count = stub_channel_count;

	for (branches = 0U; branches < F_CHANNEL_SETUP_SW_LAST; branches++) {

		if (subtest_pruned(branches, prune)) {
			unit_verbose(m, "%s branches=%s (pruned)\n",
				__func__,
				branches_str(branches, f_channel_setup_sw));
			continue;
		}
		subtest_setup(branches);

		nvgpu_posix_enable_fault_injection(kmem_fi,
			branches & F_CHANNEL_SETUP_SW_VZALLOC_FAIL ?
			true : false, 0);

		unit_verbose(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_setup_sw));

		err = nvgpu_channel_setup_sw(g);

		if (branches & fail) {
			assert(err != 0);
			assert(f->channel == NULL);
		} else {
			assert(err == 0);
			nvgpu_channel_cleanup_sw(g);
		}
	}

	ret = UNIT_SUCCESS;
done:
	nvgpu_posix_enable_fault_injection(kmem_fi, false, 0);
	if (ret != UNIT_SUCCESS) {
		unit_err(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_setup_sw));
	}
	g->ops = gops;
	return ret;
}

#define F_CHANNEL_OPEN_ENGINE_NOT_VALID		BIT(0)
#define F_CHANNEL_OPEN_PRIVILEGED		BIT(1)
#define F_CHANNEL_OPEN_ALLOC_CH_FAIL		BIT(2)
#define F_CHANNEL_OPEN_ALLOC_CH_WARN0		BIT(3)
#define F_CHANNEL_OPEN_ALLOC_CH_WARN1		BIT(4)
#define F_CHANNEL_OPEN_ALLOC_CH_AGGRESSIVE	BIT(5)
#define F_CHANNEL_OPEN_BUG_ON			BIT(6)
#define F_CHANNEL_OPEN_ALLOC_INST_FAIL		BIT(7)
#define F_CHANNEL_OPEN_OS			BIT(8)
#define F_CHANNEL_OPEN_LAST			BIT(9)


/* TODO: cover nvgpu_cond_init failures */
#define F_CHANNEL_OPEN_COND0_INIT_FAIL
#define F_CHANNEL_OPEN_COND1_INIT_FAIL

static const char *f_channel_open[] = {
	"engine_not_valid",
	"privileged",
	"alloc_ch_fail",
	"alloc_ch_warn0",
	"alloc_ch_warn1",
	"aggressive_destroy",
	"bug_on",
	"alloc_inst_fail",
	"cond0_init_fail",
	"cond1_init_fail",
	"hal",
};

static int stub_channel_alloc_inst_ENOMEM(struct gk20a *g,
		struct nvgpu_channel *ch)
{
	return -ENOMEM;
}

static int test_channel_open(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct nvgpu_fifo *f = &g->fifo;
	struct nvgpu_fifo fifo = g->fifo;
	struct gpu_ops gops = g->ops;
	struct nvgpu_channel *ch, *next_ch;
	struct nvgpu_posix_fault_inj *kmem_fi;
	u32 branches;
	int ret = UNIT_FAIL;
	u32 fail =
		F_CHANNEL_OPEN_ALLOC_CH_FAIL |
		F_CHANNEL_OPEN_BUG_ON |
		F_CHANNEL_OPEN_ALLOC_INST_FAIL;
	u32 prune = fail |
		F_CHANNEL_OPEN_ALLOC_CH_WARN0 |
		F_CHANNEL_OPEN_ALLOC_CH_WARN1;
	u32 runlist_id;
	bool privileged;
	int err;
	void (*os_channel_open)(struct nvgpu_channel *ch) =
		g->os_channel.open;

	kmem_fi = nvgpu_kmem_get_fault_injection();

	for (branches = 0U; branches < F_CHANNEL_OPEN_LAST; branches++) {

		if (subtest_pruned(branches, prune)) {
			unit_verbose(m, "%s branches=%s (pruned)\n", __func__,
				branches_str(branches, f_channel_open));
			continue;
		}
		subtest_setup(branches);
		unit_verbose(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_open));

		nvgpu_posix_enable_fault_injection(kmem_fi, false, 0);

		next_ch =
			nvgpu_list_empty(&f->free_chs) ? NULL :
			nvgpu_list_first_entry(&f->free_chs,
				nvgpu_channel, free_chs);
		assert(next_ch != NULL);

		runlist_id =
			branches & F_CHANNEL_OPEN_ENGINE_NOT_VALID ?
			NVGPU_INVALID_RUNLIST_ID : NVGPU_ENGINE_GR;

		privileged =
			branches & F_CHANNEL_OPEN_PRIVILEGED ?
			true : false;

		if (branches & F_CHANNEL_OPEN_ALLOC_CH_FAIL) {
			nvgpu_init_list_node(&f->free_chs);
		}

		if (branches & F_CHANNEL_OPEN_ALLOC_CH_WARN0) {
			nvgpu_atomic_inc(&next_ch->ref_count);
		}

		if (branches & F_CHANNEL_OPEN_ALLOC_CH_WARN1) {
			next_ch->referenceable = false;
		}

		if (branches & F_CHANNEL_OPEN_ALLOC_CH_AGGRESSIVE) {
			g->aggressive_sync_destroy_thresh += 1U;
			f->used_channels += 2U;
		}

		g->ops.channel.alloc_inst =
			branches & F_CHANNEL_OPEN_ALLOC_INST_FAIL ?
			stub_channel_alloc_inst_ENOMEM :
			gops.channel.alloc_inst;

		if (branches & F_CHANNEL_OPEN_BUG_ON) {
			next_ch->g = (void *)1;
		}

		err = EXPECT_BUG(
			ch = gk20a_open_new_channel(g, runlist_id,
				privileged, getpid(), getpid());
		);

		if (branches & F_CHANNEL_OPEN_BUG_ON) {
			next_ch->g = NULL;
			assert(err != 0);
		} else {
			assert(err == 0);
		};

		if (branches & F_CHANNEL_OPEN_ALLOC_CH_WARN1) {
			next_ch->referenceable = true;
		}

		if (branches & F_CHANNEL_OPEN_ALLOC_CH_AGGRESSIVE) {
			g->aggressive_sync_destroy_thresh -= 1U;
			f->used_channels -= 2U;
			assert(g->aggressive_sync_destroy);
			g->aggressive_sync_destroy = false;
		}

		if (branches & fail) {
			if (branches & F_CHANNEL_OPEN_ALLOC_CH_FAIL) {
				f->free_chs = fifo.free_chs;
			}

			if (branches & F_CHANNEL_OPEN_ALLOC_CH_WARN0) {
				nvgpu_atomic_dec(&ch->ref_count);
			}
			assert(ch == NULL);
		} else {
			assert(ch != NULL);
			assert(ch->g == g);
			assert(nvgpu_list_empty(&ch->free_chs));

			nvgpu_channel_close(ch);
			ch = NULL;
		}
	}
	ret = UNIT_SUCCESS;

done:
	if (ret != UNIT_SUCCESS) {
		unit_err(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_open));
	}
	nvgpu_posix_enable_fault_injection(kmem_fi, false, 0);
	if (ch != NULL) {
		nvgpu_channel_close(ch);
	}
	g->ops = gops;
	g->os_channel.open = os_channel_open;
	return ret;
}

#define F_CHANNEL_CLOSE_ALREADY_FREED		BIT(0)
#define F_CHANNEL_CLOSE_FORCE			BIT(1)
#define F_CHANNEL_CLOSE_DYING			BIT(2)
#define F_CHANNEL_CLOSE_TSG_BOUND		BIT(3)
#define F_CHANNEL_CLOSE_TSG_UNBIND_FAIL		BIT(4)
#define F_CHANNEL_CLOSE_OS_CLOSE		BIT(5)
#define F_CHANNEL_CLOSE_NON_REFERENCEABLE	BIT(6)
#define F_CHANNEL_CLOSE_AS_BOUND		BIT(7)
#define F_CHANNEL_CLOSE_FREE_SUBCTX		BIT(8)
#define F_CHANNEL_CLOSE_USER_SYNC		BIT(9)
#define F_CHANNEL_CLOSE_LAST			BIT(10)

/* nvgpu_tsg_unbind_channel always return 0 */

static const char *f_channel_close[] = {
	"already_freed",
	"force",
	"dying",
	"tsg_bound",
	"tsg_unbind_fail",
	"os_close",
	"non_referenceable",
	"as_bound",
	"free_subctx",
	"user_sync",
};

static void stub_os_channel_close(struct nvgpu_channel *ch, bool force)
{
	stub[0].chid = ch->chid;
}

static void stub_gr_intr_flush_channel_tlb(struct gk20a *g)
{
}

static bool channel_close_pruned(u32 branches, u32 final)
{
	u32 branches_init = branches;

	if (subtest_pruned(branches, final)) {
		return true;
	}

	/* TODO: nvgpu_tsg_unbind_channel always returns 0 */
	branches &= ~F_CHANNEL_CLOSE_TSG_UNBIND_FAIL;


	if ((branches & F_CHANNEL_CLOSE_AS_BOUND) == 0) {
		branches &= ~F_CHANNEL_CLOSE_FREE_SUBCTX;
		branches &= ~F_CHANNEL_CLOSE_USER_SYNC;
	}

	if (branches < branches_init) {
		return true;
	}

	return false;
}

static int test_channel_close(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct gpu_ops gops = g->ops;
	struct nvgpu_channel *ch;
	struct nvgpu_tsg *tsg;
	u32 branches = 0U;
	int ret = UNIT_FAIL;
	u32 fail = F_CHANNEL_CLOSE_ALREADY_FREED |
		   F_CHANNEL_CLOSE_NON_REFERENCEABLE;
	u32 prune = fail;
	u32 runlist_id = NVGPU_INVALID_RUNLIST_ID;
	void (*os_channel_close)(struct nvgpu_channel *ch, bool force) =
		g->os_channel.close;
	bool privileged = false;
	bool force;
	int err = 0;
	struct mm_gk20a mm;
	struct vm_gk20a vm;

	tsg = nvgpu_tsg_open(g, getpid());
	assert(tsg != NULL);

	g->ops.gr.intr.flush_channel_tlb = stub_gr_intr_flush_channel_tlb;

	for (branches = 0U; branches < F_CHANNEL_CLOSE_LAST; branches++) {

		if (channel_close_pruned(branches, prune)) {
			unit_verbose(m, "%s branches=%s (pruned)\n", __func__,
				branches_str(branches, f_channel_close));
			continue;
		}
		subtest_setup(branches);

		unit_verbose(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_close));

		ch = gk20a_open_new_channel(g, runlist_id,
				privileged, getpid(), getpid());
		assert(ch != NULL);

		ch->usermode_submit_enabled = true;

		force = branches & F_CHANNEL_CLOSE_FORCE ? true : false;

		nvgpu_set_enabled(g, NVGPU_DRIVER_IS_DYING,
			branches & F_CHANNEL_CLOSE_DYING ?  true : false);

		g->os_channel.close = branches & F_CHANNEL_CLOSE_OS_CLOSE ?
			stub_os_channel_close : NULL;

		if (branches & F_CHANNEL_CLOSE_TSG_BOUND) {
			err = nvgpu_tsg_bind_channel(tsg, ch);
			assert(err == 0);
		}

		ch->referenceable =
			branches & F_CHANNEL_CLOSE_NON_REFERENCEABLE ?
			false : true;

		if (branches & F_CHANNEL_CLOSE_AS_BOUND) {
			memset(&mm, 0, sizeof(mm));
			memset(&vm, 0, sizeof(vm));
			mm.g = g;
			vm.mm = &mm;
			ch->vm = &vm;
			nvgpu_ref_init(&vm.ref);
			nvgpu_ref_get(&vm.ref);
		} else {
			ch->vm = NULL;
		}

		g->ops.gr.setup.free_subctx =
			branches & F_CHANNEL_CLOSE_FREE_SUBCTX ?
			gops.gr.setup.free_subctx : NULL;

		if (branches & F_CHANNEL_CLOSE_USER_SYNC) {
			ch->user_sync = nvgpu_channel_sync_create(ch, true);
			assert(err == 0);
		}

		if (branches & F_CHANNEL_CLOSE_ALREADY_FREED) {
			nvgpu_channel_close(ch);
		}

		if (force) {
			err = EXPECT_BUG(nvgpu_channel_kill(ch));
		} else {
			err = EXPECT_BUG(nvgpu_channel_close(ch));
		}

		if (branches & F_CHANNEL_CLOSE_ALREADY_FREED) {
			assert(err != 0);
			assert(ch->g == NULL);
			continue;
		}

		if (branches & fail) {
			assert(ch->g != NULL);
			assert(nvgpu_list_empty(&ch->free_chs));

			if (branches & F_CHANNEL_CLOSE_ALREADY_FREED) {
				continue;
			}
			ch->referenceable = true;
			nvgpu_channel_kill(ch);
			continue;
		}

		if (branches & F_CHANNEL_CLOSE_DYING) {
			/* when driver is dying, tsg unbind is skipped */
			nvgpu_init_list_node(&tsg->ch_list);
			nvgpu_ref_put(&tsg->refcount, nvgpu_tsg_release);
		} else {
			assert(!nvgpu_list_empty(&ch->free_chs));
			assert(nvgpu_list_empty(&tsg->ch_list));
		}

		if (branches & F_CHANNEL_CLOSE_OS_CLOSE) {
			assert(stub[0].chid == ch->chid);
		}

		if (!(branches & F_CHANNEL_CLOSE_AS_BOUND)) {
			goto unbind;
		}

		if (branches & F_CHANNEL_CLOSE_FREE_SUBCTX) {
			assert(ch->subctx == NULL);
		}

		if (ch->subctx != NULL) {
			if (g->ops.gr.setup.free_subctx != NULL) {
				g->ops.gr.setup.free_subctx(ch);
			}
			ch->subctx = NULL;
		}

		assert(ch->usermode_submit_enabled == false);

		/* we took an extra reference to avoid nvgpu_vm_remove_ref */
		assert(nvgpu_ref_put_return(&vm.ref, NULL));

		assert(ch->user_sync == NULL);

unbind:
		/*
		 * branches not taken in safety build:
		 * - ch->sync != NULL
		 * - allow railgate for deterministic channel
		 * - unlink all debug sessions
		 * - free pre-allocated resouretes
		 * - channel refcount tracking
		 */
		assert(ch->g == NULL);
		assert(!ch->referenceable);
		assert(!nvgpu_list_empty(&ch->free_chs));

		ch = NULL;
	}
	ret = UNIT_SUCCESS;

done:
	if (ret != UNIT_SUCCESS) {
		unit_err(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_close));
	}
	nvgpu_set_enabled(g, NVGPU_DRIVER_IS_DYING, false);
	if (ch != NULL) {
		nvgpu_channel_close(ch);
	}
	if (tsg != NULL) {
		nvgpu_ref_put(&tsg->refcount, nvgpu_tsg_release);
	}
	g->ops = gops;
	g->os_channel.close = os_channel_close;
	return ret;
}

#define F_CHANNEL_SETUP_BIND_NO_AS				BIT(0)
#define F_CHANNEL_SETUP_BIND_HAS_GPFIFO_MEM			BIT(1)
#define F_CHANNEL_SETUP_BIND_USERMODE_ENABLED			BIT(2)
#define F_CHANNEL_SETUP_BIND_USERMODE_ALLOC_BUF_NULL		BIT(3)
#define F_CHANNEL_SETUP_BIND_USERMODE_ALLOC_BUF_FAIL		BIT(4)
#define F_CHANNEL_SETUP_BIND_USERMODE_SETUP_RAMFC_FAIL		BIT(5)
#define F_CHANNEL_SETUP_BIND_USERMODE_UPDATE_RL_FAIL		BIT(6)
#define F_CHANNEL_SETUP_BIND_LAST				BIT(7)

static const char *f_channel_setup_bind[] = {
	"no_as",
	"has_gpfifo_mem",
	"usermode_enabled",
	"alloc_buf_null",
	"alloc_buf_fail",
	"setup_ramfc_fail",
	"update_rl_fail",
};

static int stub_os_channel_alloc_usermode_buffers(struct nvgpu_channel *ch,
		struct nvgpu_setup_bind_args *args)
{
	int err;
	struct gk20a *g = ch->g;

	err = nvgpu_dma_alloc(g, PAGE_SIZE, &ch->usermode_userd);
	if (err != 0) {
		return err;
	}

	err = nvgpu_dma_alloc(g, PAGE_SIZE, &ch->usermode_gpfifo);
	if (err != 0) {
		return err;
	}

	stub[0].chid = ch->chid;
	return err;
}

static int stub_os_channel_alloc_usermode_buffers_ENOMEM(
		struct nvgpu_channel *ch, struct nvgpu_setup_bind_args *args)
{
	return -ENOMEM;
}

static int stub_runlist_update_for_channel(struct gk20a *g, u32 runlist_id,
		struct nvgpu_channel *ch, bool add, bool wait_for_finish)
{
	stub[1].chid = ch->chid;
	return 0;
}

static int stub_runlist_update_for_channel_ETIMEDOUT(struct gk20a *g,
		u32 runlist_id, struct nvgpu_channel *ch, bool add,
		bool wait_for_finish)
{
	return -ETIMEDOUT;
}

static int stub_ramfc_setup_EINVAL(struct nvgpu_channel *ch, u64 gpfifo_base,
		u32 gpfifo_entries, u64 pbdma_acquire_timeout, u32 flags)
{
	return -EINVAL;
}

static int stub_mm_l2_flush(struct gk20a *g, bool invalidate)
{
	return 0;
}

static int test_channel_setup_bind(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct gpu_ops gops = g->ops;
	struct nvgpu_channel *ch = NULL;
	struct nvgpu_tsg *tsg = NULL;
	u32 branches = 0U;
	int ret = UNIT_FAIL;
	u32 fail =
		F_CHANNEL_SETUP_BIND_NO_AS |
		F_CHANNEL_SETUP_BIND_HAS_GPFIFO_MEM |
		F_CHANNEL_SETUP_BIND_USERMODE_ENABLED |
		F_CHANNEL_SETUP_BIND_USERMODE_ALLOC_BUF_NULL |
		F_CHANNEL_SETUP_BIND_USERMODE_ALLOC_BUF_FAIL |
		F_CHANNEL_SETUP_BIND_USERMODE_SETUP_RAMFC_FAIL |
		F_CHANNEL_SETUP_BIND_USERMODE_UPDATE_RL_FAIL;
	u32 prune = fail;
	u32 runlist_id = NVGPU_INVALID_RUNLIST_ID;
	bool privileged = false;
	int err;
	struct nvgpu_mem pdb_mem;
	struct mm_gk20a mm;
	struct vm_gk20a vm;
	int (*alloc_usermode_buffers)(struct nvgpu_channel *c,
		struct nvgpu_setup_bind_args *args) =
			g->os_channel.alloc_usermode_buffers;
	struct nvgpu_setup_bind_args bind_args;

	tsg = nvgpu_tsg_open(g, getpid());
	assert(tsg != NULL);

	ch = gk20a_open_new_channel(g, runlist_id,
			privileged, getpid(), getpid());
	assert(ch != NULL);

	g->ops.gr.intr.flush_channel_tlb = stub_gr_intr_flush_channel_tlb;
	g->ops.mm.cache.l2_flush = stub_mm_l2_flush;	/* bug 2621189 */

	memset(&mm, 0, sizeof(mm));
	memset(&vm, 0, sizeof(vm));
	mm.g = g;
	vm.mm = &mm;
	ch->vm = &vm;
	err = nvgpu_dma_alloc(g, PAGE_SIZE, &pdb_mem);
	assert(err == 0);
	vm.pdb.mem = &pdb_mem;

	memset(&bind_args, 0, sizeof(bind_args));
	bind_args.flags = NVGPU_SETUP_BIND_FLAGS_USERMODE_SUPPORT;
	bind_args.num_gpfifo_entries = 32;

	for (branches = 0U; branches < F_CHANNEL_SETUP_BIND_LAST; branches++) {

		if (subtest_pruned(branches, prune)) {
			unit_verbose(m, "%s branches=%s (pruned)\n", __func__,
				branches_str(branches,
					f_channel_setup_bind));
			continue;
		}
		subtest_setup(branches);

		unit_verbose(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_setup_bind));

		ch->vm = branches & F_CHANNEL_SETUP_BIND_NO_AS ?
			NULL : &vm;

		if (branches & F_CHANNEL_SETUP_BIND_HAS_GPFIFO_MEM) {
			err = nvgpu_dma_alloc(g, PAGE_SIZE, &ch->gpfifo.mem);
			assert(err == 0);
		}

		if (branches & F_CHANNEL_SETUP_BIND_USERMODE_ENABLED) {
			ch->usermode_submit_enabled = true;
		}

		g->os_channel.alloc_usermode_buffers = branches &
			F_CHANNEL_SETUP_BIND_USERMODE_ALLOC_BUF_NULL ?
				NULL : stub_os_channel_alloc_usermode_buffers;

		if (branches & F_CHANNEL_SETUP_BIND_USERMODE_ALLOC_BUF_FAIL) {
			g->os_channel.alloc_usermode_buffers =
				stub_os_channel_alloc_usermode_buffers_ENOMEM;
		}

		g->ops.runlist.update_for_channel = branches &
			F_CHANNEL_SETUP_BIND_USERMODE_UPDATE_RL_FAIL ?
				stub_runlist_update_for_channel_ETIMEDOUT :
				stub_runlist_update_for_channel;

		g->ops.ramfc.setup = branches &
			F_CHANNEL_SETUP_BIND_USERMODE_SETUP_RAMFC_FAIL ?
				stub_ramfc_setup_EINVAL : gops.ramfc.setup;

		err = nvgpu_channel_setup_bind(ch, &bind_args);

		if (branches & fail) {
			assert(err != 0);
			assert(!nvgpu_mem_is_valid(&ch->usermode_userd));
			assert(!nvgpu_mem_is_valid(&ch->usermode_gpfifo));
			nvgpu_dma_free(g, &ch->gpfifo.mem);
			ch->usermode_submit_enabled = false;
			assert(nvgpu_atomic_read(&ch->bound) == false);
		} else {
			assert(err == 0);
			assert(stub[0].chid == ch->chid);
			assert(ch->usermode_submit_enabled == true);
			assert(ch->userd_iova != 0U);
			assert(stub[1].chid == ch->chid);
			assert(nvgpu_atomic_read(&ch->bound) == true);
			nvgpu_dma_free(g, &ch->usermode_userd);
			nvgpu_dma_free(g, &ch->usermode_gpfifo);
			ch->userd_iova = 0U;
			nvgpu_atomic_set(&ch->bound, false);
		}
	}
	ret = UNIT_SUCCESS;

done:
	if (ret != UNIT_SUCCESS) {
		unit_err(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_setup_bind));
	}
	nvgpu_set_enabled(g, NVGPU_DRIVER_IS_DYING, false);
	if (ch != NULL) {
		nvgpu_channel_close(ch);
	}
	nvgpu_dma_free(g, &pdb_mem);
	g->os_channel.alloc_usermode_buffers = alloc_usermode_buffers;
	g->ops = gops;
	return ret;
}

#define F_CHANNEL_ALLOC_INST_ENOMEM				BIT(0)
#define F_CHANNEL_ALLOC_INST_LAST				BIT(1)

static const char *f_channel_alloc_inst[] = {
	"nomem",
};

static int test_channel_alloc_inst(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct nvgpu_channel *ch = NULL;
	u32 branches = 0U;
	u32 fail = F_CHANNEL_ALLOC_INST_ENOMEM;
	u32 prune = fail;
	int ret = UNIT_FAIL;
	u32 runlist_id = NVGPU_INVALID_RUNLIST_ID;
	bool privileged = false;
	struct nvgpu_posix_fault_inj *dma_fi;
	int err;

	dma_fi = nvgpu_dma_alloc_get_fault_injection();

	ch = gk20a_open_new_channel(g, runlist_id,
			privileged, getpid(), getpid());
	assert(ch != NULL);

	for (branches = 0U; branches < F_CHANNEL_ALLOC_INST_LAST; branches++) {

		if (subtest_pruned(branches, prune)) {
			unit_verbose(m, "%s branches=%s (pruned)\n", __func__,
				branches_str(branches,
					f_channel_alloc_inst));
			continue;
		}
		subtest_setup(branches);
		unit_verbose(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_alloc_inst));

		nvgpu_posix_enable_fault_injection(dma_fi,
			branches & F_CHANNEL_ALLOC_INST_ENOMEM ?
				true : false, 0);

		err = nvgpu_channel_alloc_inst(g, ch);

		if (branches & fail) {
			assert(err != 0);
			assert(ch->inst_block.aperture ==
					APERTURE_INVALID);
		} else {
			assert(err == 0);
			assert(ch->inst_block.aperture !=
					APERTURE_INVALID);
		}

		nvgpu_channel_free_inst(g, ch);
		assert(ch->inst_block.aperture == APERTURE_INVALID);
	}
	ret = UNIT_SUCCESS;

done:
	if (ret != UNIT_SUCCESS) {
		unit_err(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_alloc_inst));
	}
	if (ch != NULL) {
		nvgpu_channel_close(ch);
	}
	nvgpu_posix_enable_fault_injection(dma_fi, false, 0);
	return ret;
}

/*
 * channel non-referenceable case is covered when no match is found
 * since we looked up all possible channels.
 */
#define F_CHANNEL_FROM_INST_NO_INIT			BIT(0)
#define F_CHANNEL_FROM_INST_NO_CHANNEL			BIT(1)
#define F_CHANNEL_FROM_INST_MATCH_A			BIT(2)
#define F_CHANNEL_FROM_INST_MATCH_B			BIT(3)
#define F_CHANNEL_FROM_INST_LAST			BIT(4)

static const char *f_channel_from_inst[] = {
	"no_init",
	"no_channel",
	"match_a",
	"match_b",
};

static int test_channel_from_inst(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct nvgpu_channel *ch = NULL;
	struct nvgpu_channel *chA = NULL;
	struct nvgpu_channel *chB = NULL;
	struct nvgpu_fifo *f = &g->fifo;
	struct nvgpu_fifo fifo = g->fifo;
	u32 branches = 0U;
	u32 found =
		F_CHANNEL_FROM_INST_MATCH_A |
		F_CHANNEL_FROM_INST_MATCH_B;
	u32 prune =  found |
		F_CHANNEL_FROM_INST_NO_INIT |
		F_CHANNEL_FROM_INST_NO_CHANNEL;
	int ret = UNIT_FAIL;
	u32 runlist_id = NVGPU_INVALID_RUNLIST_ID;
	u64 inst_ptr;
	bool privileged = false;

	chA = gk20a_open_new_channel(g, runlist_id,
			privileged, getpid(), getpid());
	assert(chA != NULL);

	chB = gk20a_open_new_channel(g, runlist_id,
			privileged, getpid(), getpid());
	assert(chB != NULL);

	assert(f->num_channels > 0U);

	for (branches = 0U; branches < F_CHANNEL_FROM_INST_LAST; branches++) {

		if (subtest_pruned(branches, prune)) {
			unit_verbose(m, "%s branches=%s (pruned)\n", __func__,
				branches_str(branches,
					f_channel_from_inst));
			continue;
		}
		subtest_setup(branches);
		unit_verbose(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_from_inst));

		if (branches & F_CHANNEL_FROM_INST_NO_INIT) {
			f->channel = NULL;
		}

		if (branches & F_CHANNEL_FROM_INST_NO_CHANNEL) {
			f->num_channels = 0U;
		}

		inst_ptr = (u64)-1;

		if (branches & F_CHANNEL_FROM_INST_MATCH_A) {
			inst_ptr = nvgpu_inst_block_addr(g, &chA->inst_block);
		}

		if (branches & F_CHANNEL_FROM_INST_MATCH_B) {
			inst_ptr = nvgpu_inst_block_addr(g, &chB->inst_block);
		}

		ch = nvgpu_channel_refch_from_inst_ptr(g, inst_ptr);

		if (branches & found) {
			if (branches & F_CHANNEL_FROM_INST_MATCH_A) {
				assert(ch == chA);
			}
			if (branches & F_CHANNEL_FROM_INST_MATCH_B) {
				assert(ch == chB);
			}
			assert(nvgpu_atomic_read(&ch->ref_count) == 2);
			nvgpu_channel_put(ch);
		} else {
			f->channel = fifo.channel;
			f->num_channels = fifo.num_channels;
			assert(ch == NULL);
		}
	}
	ret = UNIT_SUCCESS;

done:
	if (ret != UNIT_SUCCESS) {
		unit_err(m, "%s branches=%s\n", __func__,
			branches_str(branches, f_channel_from_inst));
	}
	if (chA != NULL) {
		nvgpu_channel_close(chA);
	}
	if (chB != NULL) {
		nvgpu_channel_close(chB);
	}
	return ret;
}

static void stub_tsg_enable(struct nvgpu_tsg *tsg)
{
	stub[0].tsgid = tsg->tsgid;
}

static void stub_tsg_disable(struct nvgpu_tsg *tsg)
{
	stub[1].tsgid = tsg->tsgid;
}

static int test_channel_enable_disable_tsg(struct unit_module *m,
		struct gk20a *g, void *args)
{
	struct gpu_ops gops = g->ops;
	struct nvgpu_channel *ch = NULL;
	struct nvgpu_tsg *tsg = NULL;
	u32 branches = 0U;
	u32 runlist_id = NVGPU_INVALID_RUNLIST_ID;
	bool privileged = false;
	int err;
	int ret = UNIT_FAIL;

	tsg = nvgpu_tsg_open(g, getpid());
	assert(tsg != NULL);

	ch = gk20a_open_new_channel(g, runlist_id,
			privileged, getpid(), getpid());
	assert(ch != NULL);

	err = nvgpu_tsg_bind_channel(tsg, ch);
	assert(err == 0);

	g->ops.tsg.enable = stub_tsg_enable;
	g->ops.tsg.disable = stub_tsg_disable;

	subtest_setup(branches);

	err = nvgpu_channel_enable_tsg(g, ch);
	assert(stub[0].tsgid = tsg->tsgid);

	err = nvgpu_channel_disable_tsg(g, ch);
	assert(stub[1].tsgid = tsg->tsgid);

	subtest_setup(branches);

	err = nvgpu_tsg_unbind_channel(tsg, ch);
	assert(err == 0);

	err = nvgpu_channel_enable_tsg(g, ch);
	assert(err != 0);

	err = nvgpu_channel_disable_tsg(g, ch);
	assert(err != 0);

	ret = UNIT_SUCCESS;

done:
	if (ch != NULL) {
		nvgpu_channel_close(ch);
	}
	if (tsg != NULL) {
		nvgpu_ref_put(&tsg->refcount, nvgpu_tsg_release);
	}
	g->ops = gops;
	return ret;
}

struct unit_module_test nvgpu_channel_tests[] = {
	UNIT_TEST(setup_sw, test_channel_setup_sw, &unit_ctx, 0),
	UNIT_TEST(init_support, test_fifo_init_support, &unit_ctx, 0),
	UNIT_TEST(open, test_channel_open, &unit_ctx, 0),
	UNIT_TEST(close, test_channel_close, &unit_ctx, 0),
	UNIT_TEST(setup_bind, test_channel_setup_bind, &unit_ctx, 0),
	UNIT_TEST(alloc_inst, test_channel_alloc_inst, &unit_ctx, 0),
	UNIT_TEST(from_inst, test_channel_from_inst, &unit_ctx, 0),
	UNIT_TEST(enable_disable_tsg,
			test_channel_enable_disable_tsg, &unit_ctx, 0),
	UNIT_TEST(remove_support, test_fifo_remove_support, &unit_ctx, 0),
};

UNIT_MODULE(nvgpu_channel, nvgpu_channel_tests, UNIT_PRIO_NVGPU_TEST);
