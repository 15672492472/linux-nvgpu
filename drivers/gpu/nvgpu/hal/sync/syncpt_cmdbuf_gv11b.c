/*
 * GV11B syncpt cmdbuf
 *
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
#include <nvgpu/mm.h>
#include <nvgpu/vm.h>
#include <nvgpu/gmmu.h>
#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/dma.h>
#include <nvgpu/lock.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/nvhost.h>

#include "syncpt_cmdbuf_gv11b.h"

static int set_syncpt_ro_map_gpu_va_locked(struct vm_gk20a *vm)
{
	struct gk20a *g = gk20a_from_vm(vm);

	if (vm->syncpt_ro_map_gpu_va) {
		return 0;
	}

	vm->syncpt_ro_map_gpu_va = nvgpu_gmmu_map(vm,
			&g->syncpt_mem, g->syncpt_unit_size,
			0, gk20a_mem_flag_read_only,
			false, APERTURE_SYSMEM);

	if (!vm->syncpt_ro_map_gpu_va) {
		nvgpu_err(g, "failed to ro map syncpt buffer");
		return -ENOMEM;
	}

	return 0;
}

int gv11b_syncpt_alloc_buf(struct nvgpu_channel *c,
		u32 syncpt_id, struct nvgpu_mem *syncpt_buf)
{
	u64 nr_pages;
	int err = 0;
	struct gk20a *g = c->g;

	/*
	 * Add ro map for complete sync point shim range in vm
	 * All channels sharing same vm will share same ro mapping.
	 * Create rw map for current channel sync point
	 */
	nvgpu_mutex_acquire(&c->vm->syncpt_ro_map_lock);
	err = set_syncpt_ro_map_gpu_va_locked(c->vm);
	nvgpu_mutex_release(&c->vm->syncpt_ro_map_lock);
	if (err != 0) {
		return err;
	}

	nr_pages = DIV_ROUND_UP(g->syncpt_size, PAGE_SIZE);
	nvgpu_mem_create_from_phys(g, syncpt_buf,
		(g->syncpt_unit_base +
		nvgpu_nvhost_syncpt_unit_interface_get_byte_offset(syncpt_id)),
		nr_pages);
	syncpt_buf->gpu_va = nvgpu_gmmu_map(c->vm, syncpt_buf,
			g->syncpt_size, 0, gk20a_mem_flag_none,
			false, APERTURE_SYSMEM);

	if (!syncpt_buf->gpu_va) {
		nvgpu_err(g, "failed to map syncpt buffer");
		nvgpu_dma_free(g, syncpt_buf);
		err = -ENOMEM;
	}
	return err;
}

void gv11b_syncpt_free_buf(struct nvgpu_channel *c,
		struct nvgpu_mem *syncpt_buf)
{
	nvgpu_gmmu_unmap(c->vm, syncpt_buf, syncpt_buf->gpu_va);
	nvgpu_dma_free(c->g, syncpt_buf);
}

int gv11b_syncpt_get_sync_ro_map(struct vm_gk20a *vm,
		u64 *base_gpuva, u32 *sync_size)
{
	struct gk20a *g = gk20a_from_vm(vm);
	int err;

	nvgpu_mutex_acquire(&vm->syncpt_ro_map_lock);
	err = set_syncpt_ro_map_gpu_va_locked(vm);
	nvgpu_mutex_release(&vm->syncpt_ro_map_lock);
	if (err != 0) {
		return err;
	}

	*base_gpuva = vm->syncpt_ro_map_gpu_va;
	*sync_size = g->syncpt_size;

	return 0;
}

void gv11b_syncpt_add_wait_cmd(struct gk20a *g,
		struct priv_cmd_entry *cmd, u32 off,
		u32 id, u32 thresh, u64 gpu_va_base)
{
	u64 gpu_va = gpu_va_base +
		nvgpu_nvhost_syncpt_unit_interface_get_byte_offset(id);

	nvgpu_log_fn(g, " ");

	off = cmd->off + off;

	/* sema_addr_lo */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010017);
	nvgpu_mem_wr32(g, cmd->mem, off++, gpu_va & 0xffffffff);

	/* sema_addr_hi */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010018);
	nvgpu_mem_wr32(g, cmd->mem, off++, (gpu_va >> 32) & 0xff);

	/* payload_lo */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010019);
	nvgpu_mem_wr32(g, cmd->mem, off++, thresh);

	/* payload_hi : ignored */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001a);
	nvgpu_mem_wr32(g, cmd->mem, off++, 0);

	/* sema_execute : acq_strict_geq | switch_en | 32bit */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001b);
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2 | (1 << 12));
}

u32 gv11b_syncpt_get_wait_cmd_size(void)
{
	return 10U;
}

u32 gv11b_syncpt_get_incr_per_release(void)
{
	return 1U;
}

void gv11b_syncpt_add_incr_cmd(struct gk20a *g,
		bool wfi_cmd, struct priv_cmd_entry *cmd,
		u32 id, u64 gpu_va)
{
	u32 off = cmd->off;

	nvgpu_log_fn(g, " ");

	/* sema_addr_lo */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010017);
	nvgpu_mem_wr32(g, cmd->mem, off++, gpu_va & 0xffffffff);

	/* sema_addr_hi */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010018);
	nvgpu_mem_wr32(g, cmd->mem, off++, (gpu_va >> 32) & 0xff);

	/* payload_lo */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010019);
	nvgpu_mem_wr32(g, cmd->mem, off++, 0);

	/* payload_hi : ignored */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001a);
	nvgpu_mem_wr32(g, cmd->mem, off++, 0);

	/* sema_execute : release | wfi | 32bit */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001b);
	nvgpu_mem_wr32(g, cmd->mem, off++,
		0x1 | ((wfi_cmd ? 0x1 : 0x0) << 20));
}

u32 gv11b_syncpt_get_incr_cmd_size(bool wfi_cmd)
{
	return 10U;
}
