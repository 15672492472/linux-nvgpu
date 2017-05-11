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

#include <linux/dma-attrs.h>
#include <linux/dma-mapping.h>

#include <nvgpu/dma.h>
#include <nvgpu/lock.h>
#include <nvgpu/bug.h>
#include <nvgpu/gmmu.h>

#include <nvgpu/linux/dma.h>

#include "gk20a/gk20a.h"

#if defined(CONFIG_GK20A_VIDMEM)
static u64 __nvgpu_dma_alloc(struct nvgpu_allocator *allocator, dma_addr_t at,
				size_t size)
{
	u64 addr = 0;

	if (at)
		addr = nvgpu_alloc_fixed(allocator, at, size, 0);
	else
		addr = nvgpu_alloc(allocator, size);

	return addr;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
static void nvgpu_dma_flags_to_attrs(unsigned long *attrs,
		unsigned long flags)
#define ATTR_ARG(x) *x
#else
static void nvgpu_dma_flags_to_attrs(struct dma_attrs *attrs,
		unsigned long flags)
#define ATTR_ARG(x) x
#endif
{
	if (flags & NVGPU_DMA_NO_KERNEL_MAPPING)
		dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, ATTR_ARG(attrs));
	if (flags & NVGPU_DMA_FORCE_CONTIGUOUS)
		dma_set_attr(DMA_ATTR_FORCE_CONTIGUOUS, ATTR_ARG(attrs));
	if (flags & NVGPU_DMA_READ_ONLY)
		dma_set_attr(DMA_ATTR_READ_ONLY, ATTR_ARG(attrs));
#undef ATTR_ARG
}

int nvgpu_dma_alloc(struct gk20a *g, size_t size, struct nvgpu_mem *mem)
{
	return nvgpu_dma_alloc_flags(g, 0, size, mem);
}

int nvgpu_dma_alloc_flags(struct gk20a *g, unsigned long flags, size_t size,
		struct nvgpu_mem *mem)
{
	if (g->mm.vidmem_is_vidmem) {
		/*
		 * Force the no-kernel-mapping flag on because we don't support
		 * the lack of it for vidmem - the user should not care when
		 * using nvgpu_gmmu_alloc_map and it's vidmem, or if there's a
		 * difference, the user should use the flag explicitly anyway.
		 */
		int err = nvgpu_dma_alloc_flags_vid(g,
				flags | NVGPU_DMA_NO_KERNEL_MAPPING,
				size, mem);

		if (!err)
			return 0;
		/*
		 * Fall back to sysmem (which may then also fail) in case
		 * vidmem is exhausted.
		 */
	}

	return nvgpu_dma_alloc_flags_sys(g, flags, size, mem);
}

int nvgpu_dma_alloc_sys(struct gk20a *g, size_t size, struct nvgpu_mem *mem)
{
	return nvgpu_dma_alloc_flags_sys(g, 0, size, mem);
}

int nvgpu_dma_alloc_flags_sys(struct gk20a *g, unsigned long flags,
		size_t size, struct nvgpu_mem *mem)
{
	struct device *d = dev_from_gk20a(g);
	int err;
	dma_addr_t iova;

	gk20a_dbg_fn("");

	if (flags) {
		DEFINE_DMA_ATTRS(dma_attrs);

		nvgpu_dma_flags_to_attrs(&dma_attrs, flags);

		if (flags & NVGPU_DMA_NO_KERNEL_MAPPING) {
			mem->priv.pages = dma_alloc_attrs(d,
					size, &iova, GFP_KERNEL,
					__DMA_ATTR(dma_attrs));
			if (!mem->priv.pages)
				return -ENOMEM;
		} else {
			mem->cpu_va = dma_alloc_attrs(d,
					size, &iova, GFP_KERNEL,
					__DMA_ATTR(dma_attrs));
			if (!mem->cpu_va)
				return -ENOMEM;
		}
	} else {
		mem->cpu_va = dma_alloc_coherent(d, size, &iova, GFP_KERNEL);
		if (!mem->cpu_va)
			return -ENOMEM;
	}

	if (flags & NVGPU_DMA_NO_KERNEL_MAPPING)
		err = nvgpu_get_sgtable_from_pages(g, &mem->priv.sgt,
						   mem->priv.pages,
						   iova, size);
	else {
		err = nvgpu_get_sgtable(g, &mem->priv.sgt, mem->cpu_va,
					iova, size);
		memset(mem->cpu_va, 0, size);
	}
	if (err)
		goto fail_free;

	mem->size = size;
	mem->aperture = APERTURE_SYSMEM;
	mem->priv.flags = flags;

	gk20a_dbg_fn("done");

	return 0;

fail_free:
	dma_free_coherent(d, size, mem->cpu_va, iova);
	mem->cpu_va = NULL;
	mem->priv.sgt = NULL;
	return err;
}

int nvgpu_dma_alloc_vid(struct gk20a *g, size_t size, struct nvgpu_mem *mem)
{
	return nvgpu_dma_alloc_flags_vid(g,
			NVGPU_DMA_NO_KERNEL_MAPPING, size, mem);
}

int nvgpu_dma_alloc_flags_vid(struct gk20a *g, unsigned long flags,
		size_t size, struct nvgpu_mem *mem)
{
	return nvgpu_dma_alloc_flags_vid_at(g, flags, size, mem, 0);
}

int nvgpu_dma_alloc_flags_vid_at(struct gk20a *g, unsigned long flags,
		size_t size, struct nvgpu_mem *mem, dma_addr_t at)
{
#if defined(CONFIG_GK20A_VIDMEM)
	u64 addr;
	int err;
	struct nvgpu_allocator *vidmem_alloc = g->mm.vidmem.cleared ?
		&g->mm.vidmem.allocator :
		&g->mm.vidmem.bootstrap_allocator;
	int before_pending;

	gk20a_dbg_fn("");

	if (!nvgpu_alloc_initialized(&g->mm.vidmem.allocator))
		return -ENOSYS;

	/*
	 * Our own allocator doesn't have any flags yet, and we can't
	 * kernel-map these, so require explicit flags.
	 */
	WARN_ON(flags != NVGPU_DMA_NO_KERNEL_MAPPING);

	nvgpu_mutex_acquire(&g->mm.vidmem.clear_list_mutex);
	before_pending = atomic64_read(&g->mm.vidmem.bytes_pending);
	addr = __nvgpu_dma_alloc(vidmem_alloc, at, size);
	nvgpu_mutex_release(&g->mm.vidmem.clear_list_mutex);
	if (!addr) {
		/*
		 * If memory is known to be freed soon, let the user know that
		 * it may be available after a while.
		 */
		if (before_pending)
			return -EAGAIN;
		else
			return -ENOMEM;
	}

	if (at)
		mem->mem_flags |= NVGPU_MEM_FLAG_FIXED;

	mem->priv.sgt = nvgpu_kzalloc(g, sizeof(struct sg_table));
	if (!mem->priv.sgt) {
		err = -ENOMEM;
		goto fail_physfree;
	}

	err = sg_alloc_table(mem->priv.sgt, 1, GFP_KERNEL);
	if (err)
		goto fail_kfree;

	set_vidmem_page_alloc(mem->priv.sgt->sgl, addr);
	sg_set_page(mem->priv.sgt->sgl, NULL, size, 0);

	mem->size = size;
	mem->aperture = APERTURE_VIDMEM;
	mem->allocator = vidmem_alloc;
	mem->priv.flags = flags;

	nvgpu_init_list_node(&mem->clear_list_entry);

	gk20a_dbg_fn("done at 0x%llx size %zu", addr, size);

	return 0;

fail_kfree:
	nvgpu_kfree(g, mem->priv.sgt);
fail_physfree:
	nvgpu_free(&g->mm.vidmem.allocator, addr);
	return err;
#else
	return -ENOSYS;
#endif
}

int nvgpu_dma_alloc_map(struct vm_gk20a *vm, size_t size,
		struct nvgpu_mem *mem)
{
	return nvgpu_dma_alloc_map_flags(vm, 0, size, mem);
}

int nvgpu_dma_alloc_map_flags(struct vm_gk20a *vm, unsigned long flags,
		size_t size, struct nvgpu_mem *mem)
{
	if (vm->mm->vidmem_is_vidmem) {
		/*
		 * Force the no-kernel-mapping flag on because we don't support
		 * the lack of it for vidmem - the user should not care when
		 * using nvgpu_dma_alloc_map and it's vidmem, or if there's a
		 * difference, the user should use the flag explicitly anyway.
		 */
		int err = nvgpu_dma_alloc_map_flags_vid(vm,
				flags | NVGPU_DMA_NO_KERNEL_MAPPING,
				size, mem);

		if (!err)
			return 0;
		/*
		 * Fall back to sysmem (which may then also fail) in case
		 * vidmem is exhausted.
		 */
	}

	return nvgpu_dma_alloc_map_flags_sys(vm, flags, size, mem);
}

int nvgpu_dma_alloc_map_sys(struct vm_gk20a *vm, size_t size,
		struct nvgpu_mem *mem)
{
	return nvgpu_dma_alloc_map_flags_sys(vm, 0, size, mem);
}

int nvgpu_dma_alloc_map_flags_sys(struct vm_gk20a *vm, unsigned long flags,
		size_t size, struct nvgpu_mem *mem)
{
	int err = nvgpu_dma_alloc_flags_sys(vm->mm->g, flags, size, mem);

	if (err)
		return err;

	mem->gpu_va = nvgpu_gmmu_map(vm, mem, size, 0,
				     gk20a_mem_flag_none, false,
				     mem->aperture);
	if (!mem->gpu_va) {
		err = -ENOMEM;
		goto fail_free;
	}

	return 0;

fail_free:
	nvgpu_dma_free(vm->mm->g, mem);
	return err;
}

int nvgpu_dma_alloc_map_vid(struct vm_gk20a *vm, size_t size,
		struct nvgpu_mem *mem)
{
	return nvgpu_dma_alloc_map_flags_vid(vm,
			NVGPU_DMA_NO_KERNEL_MAPPING, size, mem);
}

int nvgpu_dma_alloc_map_flags_vid(struct vm_gk20a *vm, unsigned long flags,
		size_t size, struct nvgpu_mem *mem)
{
	int err = nvgpu_dma_alloc_flags_vid(vm->mm->g, flags, size, mem);

	if (err)
		return err;

	mem->gpu_va = nvgpu_gmmu_map(vm, mem, size, 0,
				     gk20a_mem_flag_none, false,
				     mem->aperture);
	if (!mem->gpu_va) {
		err = -ENOMEM;
		goto fail_free;
	}

	return 0;

fail_free:
	nvgpu_dma_free(vm->mm->g, mem);
	return err;
}

static void nvgpu_dma_free_sys(struct gk20a *g, struct nvgpu_mem *mem)
{
	struct device *d = dev_from_gk20a(g);

	if (!(mem->mem_flags & NVGPU_MEM_FLAG_SHADOW_COPY) &&
	    !(mem->mem_flags & __NVGPU_MEM_FLAG_NO_DMA) &&
	    (mem->cpu_va || mem->priv.pages)) {
		if (mem->priv.flags) {
			DEFINE_DMA_ATTRS(dma_attrs);

			nvgpu_dma_flags_to_attrs(&dma_attrs, mem->priv.flags);

			if (mem->priv.flags & NVGPU_DMA_NO_KERNEL_MAPPING) {
				dma_free_attrs(d, mem->size, mem->priv.pages,
					sg_dma_address(mem->priv.sgt->sgl),
					__DMA_ATTR(dma_attrs));
			} else {
				dma_free_attrs(d, mem->size, mem->cpu_va,
					sg_dma_address(mem->priv.sgt->sgl),
					__DMA_ATTR(dma_attrs));
			}
		} else {
			dma_free_coherent(d, mem->size, mem->cpu_va,
					sg_dma_address(mem->priv.sgt->sgl));
		}
		mem->cpu_va = NULL;
		mem->priv.pages = NULL;
	}

	/*
	 * When this flag is set we expect that pages is still populated but not
	 * by the DMA API.
	 */
	if (mem->mem_flags & __NVGPU_MEM_FLAG_NO_DMA)
		nvgpu_kfree(g, mem->priv.pages);

	if (mem->priv.sgt)
		nvgpu_free_sgtable(g, &mem->priv.sgt);

	mem->size = 0;
	mem->aperture = APERTURE_INVALID;
}

static void nvgpu_dma_free_vid(struct gk20a *g, struct nvgpu_mem *mem)
{
#if defined(CONFIG_GK20A_VIDMEM)
	bool was_empty;

	/* Sanity check - only this supported when allocating. */
	WARN_ON(mem->priv.flags != NVGPU_DMA_NO_KERNEL_MAPPING);

	if (mem->mem_flags & NVGPU_MEM_FLAG_USER_MEM) {
		nvgpu_mutex_acquire(&g->mm.vidmem.clear_list_mutex);
		was_empty = nvgpu_list_empty(&g->mm.vidmem.clear_list_head);
		nvgpu_list_add_tail(&mem->clear_list_entry,
			      &g->mm.vidmem.clear_list_head);
		atomic64_add(mem->size, &g->mm.vidmem.bytes_pending);
		nvgpu_mutex_release(&g->mm.vidmem.clear_list_mutex);

		if (was_empty) {
			cancel_work_sync(&g->mm.vidmem.clear_mem_worker);
			schedule_work(&g->mm.vidmem.clear_mem_worker);
		}
	} else {
		nvgpu_memset(g, mem, 0, 0, mem->size);
		nvgpu_free(mem->allocator,
			   (u64)get_vidmem_page_alloc(mem->priv.sgt->sgl));
		nvgpu_free_sgtable(g, &mem->priv.sgt);

		mem->size = 0;
		mem->aperture = APERTURE_INVALID;
	}
#endif
}

void nvgpu_dma_free(struct gk20a *g, struct nvgpu_mem *mem)
{
	switch (mem->aperture) {
	case APERTURE_SYSMEM:
		return nvgpu_dma_free_sys(g, mem);
	case APERTURE_VIDMEM:
		return nvgpu_dma_free_vid(g, mem);
	default:
		break; /* like free() on "null" memory */
	}
}

void nvgpu_dma_unmap_free(struct vm_gk20a *vm, struct nvgpu_mem *mem)
{
	if (mem->gpu_va)
		nvgpu_gmmu_unmap(vm, mem, mem->gpu_va);
	mem->gpu_va = 0;

	nvgpu_dma_free(vm->mm->g, mem);
}

int nvgpu_get_sgtable(struct gk20a *g, struct sg_table **sgt,
		      void *cpuva, u64 iova, size_t size)
{
	int err = 0;
	struct sg_table *tbl;

	tbl = nvgpu_kzalloc(g, sizeof(struct sg_table));
	if (!tbl) {
		err = -ENOMEM;
		goto fail;
	}

	err = dma_get_sgtable(dev_from_gk20a(g), tbl, cpuva, iova, size);
	if (err)
		goto fail;

	sg_dma_address(tbl->sgl) = iova;
	*sgt = tbl;

	return 0;

fail:
	if (tbl)
		nvgpu_kfree(g, tbl);

	return err;
}

int nvgpu_get_sgtable_from_pages(struct gk20a *g, struct sg_table **sgt,
				 struct page **pages, u64 iova, size_t size)
{
	int err = 0;
	struct sg_table *tbl;

	tbl = nvgpu_kzalloc(g, sizeof(struct sg_table));
	if (!tbl) {
		err = -ENOMEM;
		goto fail;
	}

	err = sg_alloc_table_from_pages(tbl, pages,
					DIV_ROUND_UP(size, PAGE_SIZE),
					0, size, GFP_KERNEL);
	if (err)
		goto fail;

	sg_dma_address(tbl->sgl) = iova;
	*sgt = tbl;

	return 0;

fail:
	if (tbl)
		nvgpu_kfree(g, tbl);

	return err;
}

void nvgpu_free_sgtable(struct gk20a *g, struct sg_table **sgt)
{
	sg_free_table(*sgt);
	nvgpu_kfree(g, *sgt);
	*sgt = NULL;
}
