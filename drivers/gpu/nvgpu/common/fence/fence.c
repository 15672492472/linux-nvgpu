/*
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

#include <nvgpu/kmem.h>
#include <nvgpu/soc.h>
#include <nvgpu/nvhost.h>
#include <nvgpu/barrier.h>
#include <nvgpu/os_fence.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/semaphore.h>
#include <nvgpu/fence.h>
#include <nvgpu/channel_sync_syncpt.h>

static struct nvgpu_fence_type *nvgpu_fence_from_ref(struct nvgpu_ref *ref)
{
	return (struct nvgpu_fence_type *)((uintptr_t)ref -
				offsetof(struct nvgpu_fence_type, ref));
}

static void nvgpu_fence_free(struct nvgpu_ref *ref)
{
	struct nvgpu_fence_type *f = nvgpu_fence_from_ref(ref);
	struct gk20a *g = f->g;

	if (nvgpu_os_fence_is_initialized(&f->os_fence)) {
		f->os_fence.ops->drop_ref(&f->os_fence);
	}

	if (f->semaphore != NULL) {
		nvgpu_semaphore_put(f->semaphore);
	}

	if (f->allocator != NULL) {
		if (nvgpu_alloc_initialized(f->allocator)) {
			nvgpu_free(f->allocator, (u64)(uintptr_t)f);
		}
	} else {
		nvgpu_kfree(g, f);
	}
}

void nvgpu_fence_put(struct nvgpu_fence_type *f)
{
	if (f != NULL) {
		nvgpu_ref_put(&f->ref, nvgpu_fence_free);
	}
}

struct nvgpu_fence_type *nvgpu_fence_get(struct nvgpu_fence_type *f)
{
	if (f != NULL) {
		nvgpu_ref_get(&f->ref);
	}
	return f;
}

static bool nvgpu_fence_is_valid(struct nvgpu_fence_type *f)
{
	bool valid = f->valid;

	nvgpu_smp_rmb();
	return valid;
}

int nvgpu_fence_install_fd(struct nvgpu_fence_type *f, int fd)
{
	if ((f == NULL) || !nvgpu_fence_is_valid(f) ||
			!nvgpu_os_fence_is_initialized(&f->os_fence)) {
		return -EINVAL;
	}

	f->os_fence.ops->install_fence(&f->os_fence, fd);

	return 0;
}

int nvgpu_fence_wait(struct gk20a *g, struct nvgpu_fence_type *f,
							u32 timeout)
{
	if ((f != NULL) && nvgpu_fence_is_valid(f)) {
		if (!nvgpu_platform_is_silicon(g)) {
			timeout = U32_MAX;
		}
		return f->ops->wait(f, timeout);
	}
	return 0;
}

bool nvgpu_fence_is_expired(struct nvgpu_fence_type *f)
{
	if ((f != NULL) && nvgpu_fence_is_valid(f) && (f->ops != NULL)) {
		return f->ops->is_expired(f);
	} else {
		return true;
	}
}

int nvgpu_fence_pool_alloc(struct channel_gk20a *ch, unsigned int count)
{
	int err;
	size_t size;
	struct nvgpu_fence_type *fence_pool = NULL;

	size = sizeof(struct nvgpu_fence_type);
	if (count <= UINT_MAX / size) {
		size = count * size;
		fence_pool = nvgpu_vzalloc(ch->g, size);
	}

	if (fence_pool == NULL) {
		return -ENOMEM;
	}

	err = nvgpu_lockless_allocator_init(ch->g, &ch->fence_allocator,
				"fence_pool", (size_t)fence_pool, size,
				sizeof(struct nvgpu_fence_type), 0);
	if (err != 0) {
		goto fail;
	}

	return 0;

fail:
	nvgpu_vfree(ch->g, fence_pool);
	return err;
}

void nvgpu_fence_pool_free(struct channel_gk20a *ch)
{
	if (nvgpu_alloc_initialized(&ch->fence_allocator)) {
		struct nvgpu_fence_type *fence_pool;
			fence_pool = (struct nvgpu_fence_type *)(uintptr_t)
				nvgpu_alloc_base(&ch->fence_allocator);
		nvgpu_alloc_destroy(&ch->fence_allocator);
		nvgpu_vfree(ch->g, fence_pool);
	}
}

struct nvgpu_fence_type *nvgpu_fence_alloc(struct channel_gk20a *ch)
{
	struct nvgpu_fence_type *fence = NULL;

	if (channel_gk20a_is_prealloc_enabled(ch)) {
		if (nvgpu_alloc_initialized(&ch->fence_allocator)) {
			fence = (struct nvgpu_fence_type *)(uintptr_t)
				nvgpu_alloc(&ch->fence_allocator,
					sizeof(struct nvgpu_fence_type));

			/* clear the node and reset the allocator pointer */
			if (fence != NULL) {
				(void) memset(fence, 0, sizeof(*fence));
				fence->allocator = &ch->fence_allocator;
			}
		}
	} else {
		fence = nvgpu_kzalloc(ch->g, sizeof(struct nvgpu_fence_type));
	}

	if (fence != NULL) {
		nvgpu_ref_init(&fence->ref);
		fence->g = ch->g;
	}

	return fence;
}

void nvgpu_fence_init(struct nvgpu_fence_type *f,
		const struct nvgpu_fence_ops *ops,
		struct nvgpu_os_fence os_fence)
{
	if (f == NULL) {
		return;
	}
	f->ops = ops;
	f->syncpt_id = NVGPU_INVALID_SYNCPT_ID;
	f->semaphore = NULL;
	f->os_fence = os_fence;
}

/* Fences that are backed by GPU semaphores: */

static int nvgpu_semaphore_fence_wait(struct nvgpu_fence_type *f, u32 timeout)
{
	if (!nvgpu_semaphore_is_acquired(f->semaphore)) {
		return 0;
	}

	return NVGPU_COND_WAIT_INTERRUPTIBLE(
		f->semaphore_wq,
		!nvgpu_semaphore_is_acquired(f->semaphore),
		timeout);
}

static bool nvgpu_semaphore_fence_is_expired(struct nvgpu_fence_type *f)
{
	return !nvgpu_semaphore_is_acquired(f->semaphore);
}

static const struct nvgpu_fence_ops nvgpu_semaphore_fence_ops = {
	.wait = &nvgpu_semaphore_fence_wait,
	.is_expired = &nvgpu_semaphore_fence_is_expired,
};

/* This function takes ownership of the semaphore as well as the os_fence */
int nvgpu_fence_from_semaphore(
		struct nvgpu_fence_type *fence_out,
		struct nvgpu_semaphore *semaphore,
		struct nvgpu_cond *semaphore_wq,
		struct nvgpu_os_fence os_fence)
{
	struct nvgpu_fence_type *f = fence_out;

	nvgpu_fence_init(f, &nvgpu_semaphore_fence_ops, os_fence);
	if (f == NULL) {
		return -EINVAL;
	}


	f->semaphore = semaphore;
	f->semaphore_wq = semaphore_wq;

	/* commit previous writes before setting the valid flag */
	nvgpu_smp_wmb();
	f->valid = true;

	return 0;
}

#ifdef CONFIG_TEGRA_GK20A_NVHOST
/* Fences that are backed by host1x syncpoints: */

static int nvgpu_fence_syncpt_wait(struct nvgpu_fence_type *f, u32 timeout)
{
	return nvgpu_nvhost_syncpt_wait_timeout_ext(
			f->nvhost_dev, f->syncpt_id, f->syncpt_value,
			timeout, NULL, NULL);
}

static bool nvgpu_fence_syncpt_is_expired(struct nvgpu_fence_type *f)
{

	/*
	 * In cases we don't register a notifier, we can't expect the
	 * syncpt value to be updated. For this case, we force a read
	 * of the value from HW, and then check for expiration.
	 */
	if (!nvgpu_nvhost_syncpt_is_expired_ext(f->nvhost_dev, f->syncpt_id,
				f->syncpt_value)) {
		u32 val;

		if (!nvgpu_nvhost_syncpt_read_ext_check(f->nvhost_dev,
				f->syncpt_id, &val)) {
			return nvgpu_nvhost_syncpt_is_expired_ext(
					f->nvhost_dev,
					f->syncpt_id, f->syncpt_value);
		}
	}

	return true;
}

static const struct nvgpu_fence_ops nvgpu_fence_syncpt_ops = {
	.wait = &nvgpu_fence_syncpt_wait,
	.is_expired = &nvgpu_fence_syncpt_is_expired,
};

/* This function takes the ownership of the os_fence */
int nvgpu_fence_from_syncpt(
		struct nvgpu_fence_type *fence_out,
		struct nvgpu_nvhost_dev *nvhost_dev,
		u32 id, u32 value, struct nvgpu_os_fence os_fence)
{
	struct nvgpu_fence_type *f = fence_out;

	nvgpu_fence_init(f, &nvgpu_fence_syncpt_ops, os_fence);
	if (!f) {
		return -EINVAL;
	}

	f->nvhost_dev = nvhost_dev;
	f->syncpt_id = id;
	f->syncpt_value = value;

	/* commit previous writes before setting the valid flag */
	nvgpu_smp_wmb();
	f->valid = true;

	return 0;
}
#else
int nvgpu_fence_from_syncpt(
		struct nvgpu_fence_type *fence_out,
		struct nvgpu_nvhost_dev *nvhost_dev,
		u32 id, u32 value, struct nvgpu_os_fence os_fence)
{
	return -EINVAL;
}
#endif
