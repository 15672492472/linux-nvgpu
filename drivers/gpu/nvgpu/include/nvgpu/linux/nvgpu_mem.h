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

#ifndef __NVGPU_LINUX_NVGPU_MEM_H__
#define __NVGPU_LINUX_NVGPU_MEM_H__

struct page;
struct sg_table;

struct gk20a;
struct nvgpu_mem;

struct nvgpu_mem_priv {
	struct page **pages;
	struct sg_table *sgt;
	unsigned long flags;
};

/**
 * __nvgpu_mem_create_from_pages - Create an nvgpu_mem from physical pages.
 *
 * @g        - The GPU.
 * @dest     - nvgpu_mem to initialize.
 * @pages    - A list of page pointers.
 * @nr_pages - The number of pages in @pages.
 *
 * Create a new nvgpu_mem struct from a pre-existing list of physical pages. The
 * pages need not be contiguous (the underlying scatter gather list will help
 * with that). However, note, this API will explicitly make it so that the GMMU
 * mapping code bypasses SMMU access for the passed pages. This allows one to
 * make mem_descs that describe MMIO regions or other non-DRAM things.
 *
 * This only works for SYSMEM (or other things like SYSMEM - basically just not
 * VIDMEM). Also, this API is only available for Linux as it heavily depends on
 * the notion of struct %page.
 *
 * The resulting nvgpu_mem should be released with the nvgpu_dma_free() or the
 * nvgpu_dma_unmap_free() function depending on whether or not the resulting
 * nvgpu_mem has been mapped. The underlying pages themselves must be cleaned up
 * by the caller of this API.
 *
 * Returns 0 on success, or a relevant error otherwise.
 */
int __nvgpu_mem_create_from_pages(struct gk20a *g, struct nvgpu_mem *dest,
				  struct page **pages, int nr_pages);

#endif
