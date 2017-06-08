/*
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __NVGPU_NVHOST_H__
#define __NVGPU_NVHOST_H__

#ifdef CONFIG_TEGRA_GK20A_NVHOST

struct nvgpu_nvhost_dev;
struct gk20a;
struct sync_pt;
struct sync_fence;
struct timespec;

int nvgpu_get_nvhost_dev(struct gk20a *g);
void nvgpu_free_nvhost_dev(struct gk20a *g);

int nvgpu_nvhost_module_busy_ext(struct nvgpu_nvhost_dev *nvhost_dev);
void nvgpu_nvhost_module_idle_ext(struct nvgpu_nvhost_dev *nvhost_dev);

void nvgpu_nvhost_debug_dump_device(struct nvgpu_nvhost_dev *nvhost_dev);

int nvgpu_nvhost_syncpt_is_expired_ext(struct nvgpu_nvhost_dev *nvhost_dev,
	u32 id, u32 thresh);
int nvgpu_nvhost_syncpt_wait_timeout_ext(struct nvgpu_nvhost_dev *nvhost_dev,
	u32 id, u32 thresh, u32 timeout, u32 *value, struct timespec *ts);

u32 nvgpu_nvhost_syncpt_incr_max_ext(struct nvgpu_nvhost_dev *nvhost_dev,
	u32 id, u32 incrs);
void nvgpu_nvhost_syncpt_set_min_eq_max_ext(struct nvgpu_nvhost_dev *nvhost_dev,
	u32 id);
int nvgpu_nvhost_syncpt_read_ext_check(struct nvgpu_nvhost_dev *nvhost_dev,
	u32 id, u32 *val);

int nvgpu_nvhost_intr_register_notifier(struct nvgpu_nvhost_dev *nvhost_dev,
	u32 id, u32 thresh, void (*callback)(void *, int), void *private_data);

const char *nvgpu_nvhost_syncpt_get_name(struct nvgpu_nvhost_dev *nvhost_dev,
	int id);
bool nvgpu_nvhost_syncpt_is_valid_pt_ext(struct nvgpu_nvhost_dev *nvhost_dev,
	u32 id);
void nvgpu_nvhost_syncpt_put_ref_ext(struct nvgpu_nvhost_dev *nvhost_dev,
	u32 id);
u32 nvgpu_nvhost_get_syncpt_host_managed(struct nvgpu_nvhost_dev *nvhost_dev,
	u32 param,
	const char *syncpt_name);

int nvgpu_nvhost_create_symlink(struct gk20a *g);
void nvgpu_nvhost_remove_symlink(struct gk20a *g);

#ifdef CONFIG_SYNC
u32 nvgpu_nvhost_sync_pt_id(struct sync_pt *pt);
u32 nvgpu_nvhost_sync_pt_thresh(struct sync_pt *pt);
int nvgpu_nvhost_sync_num_pts(struct sync_fence *fence);

struct sync_fence *nvgpu_nvhost_sync_fdget(int fd);
struct sync_fence *nvgpu_nvhost_sync_create_fence(
	struct nvgpu_nvhost_dev *nvhost_dev,
	u32 id, u32 thresh, u32 num_pts, const char *name);
#endif /* CONFIG_SYNC */
#endif /* CONFIG_TEGRA_GK20A_NVHOST */
#endif /* __NVGPU_NVHOST_H__ */
