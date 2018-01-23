/*
 * Copyright (c) 2017-2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gk20a/gk20a.h>

#include "common/linux/vgpu/vgpu.h"
#include "gv11b/fifo_gv11b.h"
#include <nvgpu/nvhost.h>

#include <linux/tegra_vgpu.h>

#ifdef CONFIG_TEGRA_GK20A_NVHOST
int vgpu_gv11b_fifo_alloc_syncpt_buf(struct channel_gk20a *c,
				u32 syncpt_id, struct nvgpu_mem *syncpt_buf)
{
	int err;
	struct gk20a *g = c->g;
	struct vm_gk20a *vm = c->vm;
	struct tegra_vgpu_cmd_msg msg = {};
	struct tegra_vgpu_map_syncpt_params *p = &msg.params.map_syncpt;

	/*
	 * Add ro map for complete sync point shim range in vm.
	 * All channels sharing same vm will share same ro mapping.
	 * Create rw map for current channel sync point.
	 */
	if (!vm->syncpt_ro_map_gpu_va) {
		vm->syncpt_ro_map_gpu_va = __nvgpu_vm_alloc_va(vm,
				g->syncpt_unit_size,
				gmmu_page_size_kernel);
		if (!vm->syncpt_ro_map_gpu_va) {
			nvgpu_err(g, "allocating read-only va space failed");
			return -ENOMEM;
		}

		msg.cmd = TEGRA_VGPU_CMD_MAP_SYNCPT;
		msg.handle = vgpu_get_handle(g);
		p->as_handle = c->vm->handle;
		p->gpu_va = vm->syncpt_ro_map_gpu_va;
		p->len = g->syncpt_unit_size;
		p->offset = 0;
		p->prot = TEGRA_VGPU_MAP_PROT_READ_ONLY;
		err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
		err = err ? err : msg.ret;
		if (err) {
			nvgpu_err(g,
				"mapping read-only va space failed err %d",
				err);
			__nvgpu_vm_free_va(c->vm, vm->syncpt_ro_map_gpu_va,
					gmmu_page_size_kernel);
			vm->syncpt_ro_map_gpu_va = 0;
			return err;
		}
	}

	syncpt_buf->gpu_va = __nvgpu_vm_alloc_va(c->vm, g->syncpt_size,
			gmmu_page_size_kernel);
	if (!syncpt_buf->gpu_va) {
		nvgpu_err(g, "allocating syncpt va space failed");
		return -ENOMEM;
	}

	msg.cmd = TEGRA_VGPU_CMD_MAP_SYNCPT;
	msg.handle = vgpu_get_handle(g);
	p->as_handle = c->vm->handle;
	p->gpu_va = syncpt_buf->gpu_va;
	p->len = g->syncpt_size;
	p->offset =
		nvgpu_nvhost_syncpt_unit_interface_get_byte_offset(syncpt_id);
	p->prot = TEGRA_VGPU_MAP_PROT_NONE;
	err = vgpu_comm_sendrecv(&msg, sizeof(msg), sizeof(msg));
	err = err ? err : msg.ret;
	if (err) {
		nvgpu_err(g, "mapping syncpt va space failed err %d", err);
		__nvgpu_vm_free_va(c->vm, syncpt_buf->gpu_va,
				gmmu_page_size_kernel);
		return err;
	}

	return 0;
}
#endif /* CONFIG_TEGRA_GK20A_NVHOST */

int vgpu_gv11b_init_fifo_setup_hw(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	struct vgpu_priv_data *priv = vgpu_get_priv_data(g);

	f->max_subctx_count = priv->constants.max_subctx_count;

	return 0;
}
