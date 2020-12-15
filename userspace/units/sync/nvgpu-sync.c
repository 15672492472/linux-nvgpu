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
#include <unit/unit.h>
#include <unit/io.h>
#include <nvgpu/types.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/hal_init.h>
#include <nvgpu/dma.h>
#include <nvgpu/posix/io.h>
#include <os/posix/os_posix.h>
#include <nvgpu/posix/posix-fault-injection.h>
#include <nvgpu/posix/posix-nvhost.h>
#include <nvgpu/channel.h>
#include <nvgpu/channel_sync.h>
#include <nvgpu/channel_sync_syncpt.h>

#include "../fifo/nvgpu-fifo.h"
#include "../fifo/nvgpu-fifo-gv11b.h"
#include "nvgpu-sync.h"

#define NV_PMC_BOOT_0_ARCHITECTURE_GV110        (0x00000015 << \
						NVGPU_GPU_ARCHITECTURE_SHIFT)
#define NV_PMC_BOOT_0_IMPLEMENTATION_B          0xB

#define assert(cond)	unit_assert(cond, goto done)

static struct nvgpu_channel *ch;

static int init_syncpt_mem(struct unit_module *m, struct gk20a *g)
{
	u64 nr_pages;
	int err;
	if (!nvgpu_mem_is_valid(&g->syncpt_mem)) {
		nr_pages = U64(DIV_ROUND_UP(g->syncpt_unit_size,
					    PAGE_SIZE));
		err = nvgpu_mem_create_from_phys(g, &g->syncpt_mem,
				g->syncpt_unit_base, nr_pages);
		if (err != 0) {
			nvgpu_err(g, "Failed to create syncpt mem");
			return err;
		}
	}

	return 0;
}

static int de_init_syncpt_mem(struct unit_module *m, struct gk20a *g)
{
	if (nvgpu_mem_is_valid(&g->syncpt_mem))
		nvgpu_dma_free(g, &g->syncpt_mem);

	return 0;
}

static int init_channel_vm(struct unit_module *m, struct nvgpu_channel *ch)
{
	u64 low_hole, aperture_size;
	struct gk20a *g = ch->g;
	struct nvgpu_os_posix *p = nvgpu_os_posix_from_gk20a(g);
	struct mm_gk20a *mm = &g->mm;

	p->mm_is_iommuable = true;
	/*
	 * Initialize one VM space for system memory to be used throughout this
	 * unit module.
	 * Values below are similar to those used in nvgpu_init_system_vm()
	 */
	low_hole = SZ_4K * 16UL;
	aperture_size = GK20A_PMU_VA_SIZE;

	mm->pmu.aperture_size = GK20A_PMU_VA_SIZE;
	mm->channel.user_size = NV_MM_DEFAULT_USER_SIZE -
					NV_MM_DEFAULT_KERNEL_SIZE;
	mm->channel.kernel_size = NV_MM_DEFAULT_KERNEL_SIZE;

	mm->pmu.vm = nvgpu_vm_init(g,
				   g->ops.mm.gmmu.get_default_big_page_size(),
				   low_hole,
				   aperture_size - low_hole,
				   aperture_size,
				   true,
				   false,
				   false,
				   "system");
	if (mm->pmu.vm == NULL) {
		unit_return_fail(m, "nvgpu_vm_init failed\n");
	}

	ch->vm = mm->pmu.vm;

	return UNIT_SUCCESS;
}

int test_sync_init(struct unit_module *m, struct gk20a *g, void *args)
{
	int ret = 0;

	test_fifo_setup_gv11b_reg_space(m, g);

	nvgpu_set_enabled(g, NVGPU_HAS_SYNCPOINTS, true);

	/*
	 * HAL init parameters for gv11b
	 */
	g->params.gpu_arch = NV_PMC_BOOT_0_ARCHITECTURE_GV110;
	g->params.gpu_impl = NV_PMC_BOOT_0_IMPLEMENTATION_B;

	/*
	 * HAL init required for getting
	 * the sync ops initialized.
	 */
	ret = nvgpu_init_hal(g);
	if (ret != 0) {
		return -ENODEV;
	}

	/*
	 * Init g->nvhost_dev containing sync metadata
	 */
	ret = nvgpu_get_nvhost_dev(g);
	if (ret != 0) {
		unit_return_fail(m, "nvgpu_sync_early_init failed\n");
	}

	/*
	 * Alloc memory for g->syncpt_mem
	 */
	ret = init_syncpt_mem(m, g);
	if (ret != 0) {
		nvgpu_free_nvhost_dev(g);
		unit_return_fail(m, "sync mem allocation failure");
	}

	/*
	 * Alloc memory for channel
	 */
	ch = nvgpu_kzalloc(g, sizeof(struct nvgpu_channel));
	if (ch == NULL) {
		de_init_syncpt_mem(m, g);
		nvgpu_free_nvhost_dev(g);
		unit_return_fail(m, "sync channel creation failure");
	}

	ch->g = g;

	/*
	 * Alloc and Init a VM for the channel
	 */
	ret = init_channel_vm(m, ch);
	if (ret != 0) {
		nvgpu_kfree(g, ch);
		de_init_syncpt_mem(m, g);
		nvgpu_free_nvhost_dev(g);
		unit_return_fail(m, "sync channel vm init failure");
	}

	return UNIT_SUCCESS;
}

#define F_SYNC_DESTROY_SET_SAFE                  0
#define F_SYNC_DESTROY_LAST                      1

static const char *f_sync_destroy_syncpt[] = {
	"sync_destroy_set_safe",
	"sync_destroy",
};

int test_sync_create_destroy_sync(struct unit_module *m, struct gk20a *g, void *args)
{
	struct nvgpu_channel_sync *sync = NULL;
	u32 branches;
	bool set_safe_state = true;

	u32 syncpt_value = 0U;
	int ret = UNIT_FAIL;

	for (branches = 0U; branches <= F_SYNC_DESTROY_LAST; branches++) {

		sync = nvgpu_channel_sync_create(ch, true);
		if (sync == NULL) {
			unit_return_fail(m, "unexpected failure in creating sync points");
		}

		syncpt_value = g->nvhost_dev->syncpt_value;

		unit_info(m, "Syncpt ID: %u, Syncpt Value: %u\n",
			g->nvhost_dev->syncpt_id, syncpt_value);

		assert((g->nvhost_dev->syncpt_id > 0U) &&
			(g->nvhost_dev->syncpt_id <= NUM_HW_PTS));

		assert(syncpt_value < (UINT_MAX - SYNCPT_SAFE_STATE_INCR));

		if (branches == F_SYNC_DESTROY_SET_SAFE) {
			set_safe_state = false;
		}

		unit_info(m, "%s branch: %s\n", __func__, f_sync_destroy_syncpt[branches]);

		nvgpu_channel_sync_destroy(sync, set_safe_state);

		sync = NULL;
	}

	ret = UNIT_SUCCESS;

done:
	if (sync != NULL)
		nvgpu_channel_sync_destroy(sync, set_safe_state);

	if (nvgpu_mem_is_valid(&g->syncpt_mem) &&
			ch->vm->syncpt_ro_map_gpu_va != 0ULL) {
		nvgpu_gmmu_unmap(ch->vm, &g->syncpt_mem,
				ch->vm->syncpt_ro_map_gpu_va);
		ch->vm->syncpt_ro_map_gpu_va = 0ULL;
	}

	return ret;
}

int test_sync_set_safe_state(struct unit_module *m, struct gk20a *g, void *args)
{
	struct nvgpu_channel_sync *sync = NULL;

	u32 syncpt_value, syncpt_id;
	u32 syncpt_safe_state_val;

	int ret = UNIT_FAIL;

	sync = nvgpu_channel_sync_create(ch, true);
	if (sync == NULL) {
		unit_return_fail(m, "unexpected failure in creating sync points");
	}

	syncpt_id = g->nvhost_dev->syncpt_id;
	syncpt_value = g->nvhost_dev->syncpt_value;

	unit_info(m, "Syncpt ID: %u, Syncpt Value: %u\n",
		syncpt_id, syncpt_value);

	assert((syncpt_id > 0U) && (syncpt_id <= NUM_HW_PTS));

	assert(syncpt_value < (UINT_MAX - SYNCPT_SAFE_STATE_INCR));

	nvgpu_channel_sync_set_safe_state(sync);

	syncpt_safe_state_val = g->nvhost_dev->syncpt_value;

	if ((syncpt_safe_state_val - syncpt_value) != SYNCPT_SAFE_STATE_INCR) {
		unit_return_fail(m, "unexpected increment value for safe state");
	}

	nvgpu_channel_sync_destroy(sync, false);

	sync = NULL;

	ret = UNIT_SUCCESS;

done:
	if (sync != NULL)
		nvgpu_channel_sync_destroy(sync, false);

	return ret;
}

int test_sync_usermanaged_syncpt_apis(struct unit_module *m, struct gk20a *g, void *args)
{
	struct nvgpu_channel_sync *user_sync = NULL;
	struct nvgpu_channel_sync_syncpt *user_sync_syncpt = NULL;

	u32 syncpt_id = 0U;
	u64 syncpt_buf_addr = 0ULL;

	int ret = UNIT_FAIL;

	user_sync = nvgpu_channel_sync_create(ch, true);
	if (user_sync == NULL) {
		unit_return_fail(m, "unexpected failure in creating user sync points");
	}

	user_sync_syncpt = nvgpu_channel_sync_to_syncpt(user_sync);
	if (user_sync_syncpt == NULL) {
		unit_return_fail(m, "unexpected failure in creating user_sync_syncpt");
	}

	syncpt_id = nvgpu_channel_sync_get_syncpt_id(user_sync_syncpt);
	assert((syncpt_id > 0U) && (syncpt_id <= NUM_HW_PTS));

	syncpt_buf_addr = nvgpu_channel_sync_get_syncpt_address(user_sync_syncpt);
	assert(syncpt_buf_addr > 0ULL);

	unit_info(m, "Syncpt ID: %u, Syncpt Shim GPU VA: %llu\n",
		syncpt_id, syncpt_buf_addr);

	nvgpu_channel_sync_destroy(user_sync, false);

	user_sync = NULL;

	ret = UNIT_SUCCESS;

done:
	if (user_sync != NULL)
		nvgpu_channel_sync_destroy(user_sync, false);

	return ret;
}

#define F_SYNC_SYNCPT_ALLOC_FAILED		0
#define F_SYNC_USER_MANAGED			1
#define F_SYNC_NVHOST_CLIENT_MANAGED_FAIL	2
#define F_SYNC_RO_MAP_GPU_VA_MAP_FAIL		3
#define F_SYNC_MEM_CREATE_PHYS_FAIL		4
#define F_SYNC_BUF_MAP_FAIL			5
#define F_SYNC_FAIL_LAST			6

static const char *f_syncpt_open[] = {
	"syncpt_alloc_failed",
	"syncpt_user_managed_false",
	"syncpt_get_client_managed_fail",
	"syncpt_ro_map_gpu_va_fail",
	"syncpt_create_phys_mem_fail",
	"syncpt_buf_map_fail",
};

static void clear_test_params(struct gk20a *g, bool *user_managed,
		bool *fault_injection_enabled, u32 branch,
		struct nvgpu_posix_fault_inj *kmem_fi)
{
	if (!(*user_managed)) {
		*user_managed = true;
	}

	if (ch->vm->guest_managed) {
		ch->vm->guest_managed = false;
	}

	if (*fault_injection_enabled) {
		nvgpu_posix_enable_fault_injection(kmem_fi, false, 0);
		*fault_injection_enabled = false;
	}

	if (branch == F_SYNC_NVHOST_CLIENT_MANAGED_FAIL) {
		g->nvhost_dev->syncpt_id = 1U;
	}

	if (ch->vm->syncpt_ro_map_gpu_va) {
		ch->vm->syncpt_ro_map_gpu_va = 0ULL;
	}
}

int test_sync_create_fail(struct unit_module *m, struct gk20a *g, void *args)
{
	struct nvgpu_channel_sync *sync = NULL;
	struct nvgpu_posix_fault_inj *kmem_fi;
	u32 branches;
	bool user_managed = true;
	bool fault_injection_enabled = false;
	int ret = UNIT_FAIL;

	kmem_fi = nvgpu_kmem_get_fault_injection();

	ch->vm->syncpt_ro_map_gpu_va = 0U;

	for (branches = 0U; branches < F_SYNC_FAIL_LAST; branches++) {

		u32 syncpt_id, syncpt_value;

		/*
		 * This is normally not cleared when a syncpt's last ref
		 * is removed. Hence, explicitely zero it after every failure
		 */
		g->nvhost_dev->syncpt_id = 0U;

		if (branches == F_SYNC_SYNCPT_ALLOC_FAILED) {
			/* fail first kzalloc call */
			nvgpu_posix_enable_fault_injection(kmem_fi, true, 0);
			fault_injection_enabled = true;
		} else if (branches == F_SYNC_USER_MANAGED) {
			user_managed = false;
		} else if (branches == F_SYNC_NVHOST_CLIENT_MANAGED_FAIL) {
			g->nvhost_dev->syncpt_id = 20U; /* arbitary id */
		} else if (branches == F_SYNC_RO_MAP_GPU_VA_MAP_FAIL) {
			/* fail Read-Only nvgpu_gmmu_map of g->syncpt_mem */
			ch->vm->guest_managed = true;
		} else if (branches == F_SYNC_MEM_CREATE_PHYS_FAIL) {
			/*
			 * bypass map of g->syncpt_mem and fail at
			 * nvgpu_mem_create_from_phys after first kzalloc.
			 */
			ch->vm->syncpt_ro_map_gpu_va = 0x1000ULL;
			nvgpu_posix_enable_fault_injection(kmem_fi, true, 1);
			fault_injection_enabled = true;
		} else if (branches == F_SYNC_BUF_MAP_FAIL) {
			/*
			 * bypass map of g->syncpt_mem and fail at
			 * nvgpu_gmmu_map after first kzalloc and then two
			 * consequtive calls to kmalloc
			 */
			ch->vm->syncpt_ro_map_gpu_va = 1ULL;
			nvgpu_posix_enable_fault_injection(kmem_fi, true, 3);
			fault_injection_enabled = true;
		} else {
			continue;
		}

		unit_info(m, "%s branch: %s\n", __func__, f_syncpt_open[branches]);

		sync = nvgpu_channel_sync_create(ch, user_managed);
		if (sync != NULL) {
			nvgpu_channel_sync_destroy(sync, true);
			unit_return_fail(m, "expected failure in creating sync points");
		}

		syncpt_id = g->nvhost_dev->syncpt_id;
		syncpt_value = g->nvhost_dev->syncpt_value;

		assert(syncpt_id == 0U);
		assert(syncpt_value == 0U);

		clear_test_params(g, &user_managed, &fault_injection_enabled,
			branches, kmem_fi);

	}

	ret = UNIT_SUCCESS;

done:
	clear_test_params(g, &user_managed, &fault_injection_enabled,
		0, kmem_fi);

	return ret;
}

int test_sync_deinit(struct unit_module *m, struct gk20a *g, void *args)
{

	nvgpu_vm_put(g->mm.pmu.vm);

	if (ch != NULL) {
		nvgpu_kfree(g, ch);
	}

	de_init_syncpt_mem(m, g);

	if (g->nvhost_dev == NULL) {
		unit_return_fail(m ,"no valid nvhost device exists\n");
	}

	nvgpu_free_nvhost_dev(g);

	test_fifo_cleanup_gv11b_reg_space(m, g);

	return UNIT_SUCCESS;
}

struct unit_module_test nvgpu_sync_tests[] = {
	UNIT_TEST(sync_init, test_sync_init, NULL, 0),
	UNIT_TEST(sync_create_destroy, test_sync_create_destroy_sync, NULL, 0),
	UNIT_TEST(sync_set_safe_state, test_sync_set_safe_state, NULL, 0),
	UNIT_TEST(sync_user_managed_apis, test_sync_usermanaged_syncpt_apis, NULL, 0),
	UNIT_TEST(sync_fail, test_sync_create_fail, NULL, 0),
	UNIT_TEST(sync_deinit, test_sync_deinit, NULL, 0),
};

UNIT_MODULE(nvgpu-sync, nvgpu_sync_tests, UNIT_PRIO_NVGPU_TEST);
