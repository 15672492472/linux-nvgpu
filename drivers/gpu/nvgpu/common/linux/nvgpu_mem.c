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

#include <nvgpu/dma.h>
#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/page_allocator.h>
#include <nvgpu/log.h>
#include <nvgpu/bug.h>

#include <nvgpu/linux/dma.h>

#include "gk20a/gk20a.h"
#include "gk20a/mm_gk20a.h"

u32 __nvgpu_aperture_mask(struct gk20a *g, enum nvgpu_aperture aperture,
		u32 sysmem_mask, u32 vidmem_mask)
{
	switch (aperture) {
	case APERTURE_SYSMEM:
		/* sysmem for dgpus; some igpus consider system memory vidmem */
		return g->mm.vidmem_is_vidmem ? sysmem_mask : vidmem_mask;
	case APERTURE_VIDMEM:
		/* for dgpus only */
		return vidmem_mask;
	case APERTURE_INVALID:
		WARN_ON("Bad aperture");
	}
	return 0;
}

u32 nvgpu_aperture_mask(struct gk20a *g, struct nvgpu_mem *mem,
		u32 sysmem_mask, u32 vidmem_mask)
{
	return __nvgpu_aperture_mask(g, mem->aperture,
			sysmem_mask, vidmem_mask);
}

int nvgpu_mem_begin(struct gk20a *g, struct nvgpu_mem *mem)
{
	void *cpu_va;

	if (mem->aperture != APERTURE_SYSMEM || g->mm.force_pramin)
		return 0;

	/*
	 * A CPU mapping is implicitly made for all SYSMEM DMA allocations that
	 * don't have NVGPU_DMA_NO_KERNEL_MAPPING. Thus we don't need to make
	 * another CPU mapping.
	 */
	if (!(mem->priv.flags & NVGPU_DMA_NO_KERNEL_MAPPING))
		return 0;

	if (WARN_ON(mem->cpu_va)) {
		nvgpu_warn(g, "nested");
		return -EBUSY;
	}

	cpu_va = vmap(mem->priv.pages,
			PAGE_ALIGN(mem->size) >> PAGE_SHIFT,
			0, pgprot_writecombine(PAGE_KERNEL));

	if (WARN_ON(!cpu_va))
		return -ENOMEM;

	mem->cpu_va = cpu_va;
	return 0;
}

void nvgpu_mem_end(struct gk20a *g, struct nvgpu_mem *mem)
{
	if (mem->aperture != APERTURE_SYSMEM || g->mm.force_pramin)
		return;

	/*
	 * Similar to nvgpu_mem_begin() we don't need to unmap the CPU mapping
	 * already made by the DMA API.
	 */
	if (!(mem->priv.flags & NVGPU_DMA_NO_KERNEL_MAPPING))
		return;

	vunmap(mem->cpu_va);
	mem->cpu_va = NULL;
}

u32 nvgpu_mem_rd32(struct gk20a *g, struct nvgpu_mem *mem, u32 w)
{
	u32 data = 0;

	if (mem->aperture == APERTURE_SYSMEM && !g->mm.force_pramin) {
		u32 *ptr = mem->cpu_va;

		WARN_ON(!ptr);
		data = ptr[w];
#ifdef CONFIG_TEGRA_SIMULATION_PLATFORM
		gk20a_dbg(gpu_dbg_mem, " %p = 0x%x", ptr + w, data);
#endif
	} else if (mem->aperture == APERTURE_VIDMEM || g->mm.force_pramin) {
		u32 value;
		u32 *p = &value;

		nvgpu_pramin_access_batched(g, mem, w * sizeof(u32),
				sizeof(u32), pramin_access_batch_rd_n, &p);

		data = value;

	} else {
		WARN_ON("Accessing unallocated nvgpu_mem");
	}

	return data;
}

u32 nvgpu_mem_rd(struct gk20a *g, struct nvgpu_mem *mem, u32 offset)
{
	WARN_ON(offset & 3);
	return nvgpu_mem_rd32(g, mem, offset / sizeof(u32));
}

void nvgpu_mem_rd_n(struct gk20a *g, struct nvgpu_mem *mem,
		u32 offset, void *dest, u32 size)
{
	WARN_ON(offset & 3);
	WARN_ON(size & 3);

	if (mem->aperture == APERTURE_SYSMEM && !g->mm.force_pramin) {
		u8 *src = (u8 *)mem->cpu_va + offset;

		WARN_ON(!mem->cpu_va);
		memcpy(dest, src, size);
#ifdef CONFIG_TEGRA_SIMULATION_PLATFORM
		if (size)
			gk20a_dbg(gpu_dbg_mem, " %p = 0x%x ... [%d bytes]",
					src, *dest, size);
#endif
	} else if (mem->aperture == APERTURE_VIDMEM || g->mm.force_pramin) {
		u32 *dest_u32 = dest;

		nvgpu_pramin_access_batched(g, mem, offset, size,
				pramin_access_batch_rd_n, &dest_u32);
	} else {
		WARN_ON("Accessing unallocated nvgpu_mem");
	}
}

void nvgpu_mem_wr32(struct gk20a *g, struct nvgpu_mem *mem, u32 w, u32 data)
{
	if (mem->aperture == APERTURE_SYSMEM && !g->mm.force_pramin) {
		u32 *ptr = mem->cpu_va;

		WARN_ON(!ptr);
#ifdef CONFIG_TEGRA_SIMULATION_PLATFORM
		gk20a_dbg(gpu_dbg_mem, " %p = 0x%x", ptr + w, data);
#endif
		ptr[w] = data;
	} else if (mem->aperture == APERTURE_VIDMEM || g->mm.force_pramin) {
		u32 value = data;
		u32 *p = &value;

		nvgpu_pramin_access_batched(g, mem, w * sizeof(u32),
				sizeof(u32), pramin_access_batch_wr_n, &p);
		if (!mem->skip_wmb)
			wmb();
	} else {
		WARN_ON("Accessing unallocated nvgpu_mem");
	}
}

void nvgpu_mem_wr(struct gk20a *g, struct nvgpu_mem *mem, u32 offset, u32 data)
{
	WARN_ON(offset & 3);
	nvgpu_mem_wr32(g, mem, offset / sizeof(u32), data);
}

void nvgpu_mem_wr_n(struct gk20a *g, struct nvgpu_mem *mem, u32 offset,
		void *src, u32 size)
{
	WARN_ON(offset & 3);
	WARN_ON(size & 3);

	if (mem->aperture == APERTURE_SYSMEM && !g->mm.force_pramin) {
		u8 *dest = (u8 *)mem->cpu_va + offset;

		WARN_ON(!mem->cpu_va);
#ifdef CONFIG_TEGRA_SIMULATION_PLATFORM
		if (size)
			gk20a_dbg(gpu_dbg_mem, " %p = 0x%x ... [%d bytes]",
					dest, *src, size);
#endif
		memcpy(dest, src, size);
	} else if (mem->aperture == APERTURE_VIDMEM || g->mm.force_pramin) {
		u32 *src_u32 = src;

		nvgpu_pramin_access_batched(g, mem, offset, size,
				pramin_access_batch_wr_n, &src_u32);
		if (!mem->skip_wmb)
			wmb();
	} else {
		WARN_ON("Accessing unallocated nvgpu_mem");
	}
}

void nvgpu_memset(struct gk20a *g, struct nvgpu_mem *mem, u32 offset,
		u32 c, u32 size)
{
	WARN_ON(offset & 3);
	WARN_ON(size & 3);
	WARN_ON(c & ~0xff);

	c &= 0xff;

	if (mem->aperture == APERTURE_SYSMEM && !g->mm.force_pramin) {
		u8 *dest = (u8 *)mem->cpu_va + offset;

		WARN_ON(!mem->cpu_va);
#ifdef CONFIG_TEGRA_SIMULATION_PLATFORM
		if (size)
			gk20a_dbg(gpu_dbg_mem, " %p = 0x%x [times %d]",
				dest, c, size);
#endif
		memset(dest, c, size);
	} else if (mem->aperture == APERTURE_VIDMEM || g->mm.force_pramin) {
		u32 repeat_value = c | (c << 8) | (c << 16) | (c << 24);
		u32 *p = &repeat_value;

		nvgpu_pramin_access_batched(g, mem, offset, size,
				pramin_access_batch_set, &p);
		if (!mem->skip_wmb)
			wmb();
	} else {
		WARN_ON("Accessing unallocated nvgpu_mem");
	}
}

/*
 * Be careful how you use this! You are responsible for correctly freeing this
 * memory.
 */
int nvgpu_mem_create_from_mem(struct gk20a *g,
			      struct nvgpu_mem *dest, struct nvgpu_mem *src,
			      int start_page, int nr_pages)
{
	int ret;
	u64 start = start_page * PAGE_SIZE;
	u64 size = nr_pages * PAGE_SIZE;
	dma_addr_t new_iova;

	if (src->aperture != APERTURE_SYSMEM)
		return -EINVAL;

	/* Some silly things a caller might do... */
	if (size > src->size)
		return -EINVAL;
	if ((start + size) > src->size)
		return -EINVAL;

	dest->mem_flags = src->mem_flags | NVGPU_MEM_FLAG_SHADOW_COPY;
	dest->aperture  = src->aperture;
	dest->skip_wmb  = src->skip_wmb;
	dest->size      = size;

	/*
	 * Re-use the CPU mapping only if the mapping was made by the DMA API.
	 */
	if (!(src->priv.flags & NVGPU_DMA_NO_KERNEL_MAPPING))
		dest->cpu_va = src->cpu_va + (PAGE_SIZE * start_page);

	dest->priv.pages = src->priv.pages + start_page;
	dest->priv.flags = src->priv.flags;

	new_iova = sg_dma_address(src->priv.sgt->sgl) ?
		sg_dma_address(src->priv.sgt->sgl) + start : 0;

	/*
	 * Make a new SG table that is based only on the subset of pages that
	 * is passed to us. This table gets freed by the dma free routines.
	 */
	if (src->priv.flags & NVGPU_DMA_NO_KERNEL_MAPPING)
		ret = nvgpu_get_sgtable_from_pages(g, &dest->priv.sgt,
						   src->priv.pages + start_page,
						   new_iova, size);
	else
		ret = nvgpu_get_sgtable(g, &dest->priv.sgt, dest->cpu_va,
					new_iova, size);

	return ret;
}

int __nvgpu_mem_create_from_pages(struct gk20a *g, struct nvgpu_mem *dest,
				  struct page **pages, int nr_pages)
{
	struct sg_table *sgt;
	struct page **our_pages =
		nvgpu_kmalloc(g, sizeof(struct page *) * nr_pages);

	if (!our_pages)
		return -ENOMEM;

	memcpy(our_pages, pages, sizeof(struct page *) * nr_pages);

	if (nvgpu_get_sgtable_from_pages(g, &sgt, pages, 0,
					 nr_pages * PAGE_SIZE)) {
		nvgpu_kfree(g, our_pages);
		return -ENOMEM;
	}

	/*
	 * If we are making an SGT from physical pages we can be reasonably
	 * certain that this should bypass the SMMU - thus we set the DMA (aka
	 * IOVA) address to 0. This tells the GMMU mapping code to not make a
	 * mapping directed to the SMMU.
	 */
	sg_dma_address(sgt->sgl) = 0;

	dest->mem_flags  = __NVGPU_MEM_FLAG_NO_DMA;
	dest->aperture   = APERTURE_SYSMEM;
	dest->skip_wmb   = 0;
	dest->size       = PAGE_SIZE * nr_pages;

	dest->priv.flags = 0;
	dest->priv.pages = our_pages;
	dest->priv.sgt   = sgt;

	return 0;
}
