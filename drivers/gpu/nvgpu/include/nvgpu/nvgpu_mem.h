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

#ifndef __NVGPU_NVGPU_MEM_H__
#define __NVGPU_NVGPU_MEM_H__

#include <nvgpu/types.h>
#include <nvgpu/list.h>

#include <nvgpu/linux/nvgpu_mem.h>

struct page;
struct sg_table;

struct gk20a;
struct nvgpu_allocator;

/*
 * Real location of a buffer - nvgpu_aperture_mask() will deduce what will be
 * told to the gpu about the aperture, but this flag designates where the
 * memory actually was allocated from.
 */
enum nvgpu_aperture {
	APERTURE_INVALID, /* unallocated or N/A */
	APERTURE_SYSMEM,
	APERTURE_VIDMEM
};

struct nvgpu_mem {
	/*
	 * Populated for all nvgpu_mem structs - vidmem or system.
	 */
	enum nvgpu_aperture			 aperture;
	size_t					 size;
	u64					 gpu_va;
	bool					 skip_wmb;

	/*
	 * Set when a nvgpu_mem struct is not a "real" nvgpu_mem struct. Instead
	 * the struct is just a copy of another nvgpu_mem struct.
	 */
#define NVGPU_MEM_FLAG_SHADOW_COPY		 (1 << 0)
	unsigned long				 mem_flags;

	/*
	 * Only populated for a sysmem allocation.
	 */
	void					*cpu_va;

	/*
	 * Fields only populated for vidmem allocations.
	 */
	bool					 fixed;
	bool					 user_mem;
	struct nvgpu_allocator			*allocator;
	struct nvgpu_list_node			 clear_list_entry;

	/*
	 * This is defined by the system specific header. It can be empty if
	 * there's no system specific stuff for a given system.
	 */
	struct nvgpu_mem_priv			 priv;
};

static inline struct nvgpu_mem *
nvgpu_mem_from_clear_list_entry(struct nvgpu_list_node *node)
{
	return (struct nvgpu_mem *)
		((uintptr_t)node - offsetof(struct nvgpu_mem,
					    clear_list_entry));
};

static inline const char *nvgpu_aperture_str(enum nvgpu_aperture aperture)
{
	switch (aperture) {
		case APERTURE_INVALID: return "invalid";
		case APERTURE_SYSMEM:  return "sysmem";
		case APERTURE_VIDMEM:  return "vidmem";
	};
	return "UNKNOWN";
}

/**
 * nvgpu_mem_create_from_mem - Create a new nvgpu_mem struct from an old one.
 *
 * @g          - The GPU.
 * @dest       - Destination nvgpu_mem to hold resulting memory description.
 * @src        - Source memory. Must be valid.
 * @start_page - Starting page to use.
 * @nr_pages   - Number of pages to place in the new nvgpu_mem.
 *
 * Create a new nvgpu_mem struct describing a subsection of the @src nvgpu_mem.
 * This will create an nvpgu_mem object starting at @start_page and is @nr_pages
 * long. This currently only works on SYSMEM nvgpu_mems. If this is called on a
 * VIDMEM nvgpu_mem then this will return an error.
 *
 * There is a _major_ caveat to this API: if the source buffer is freed before
 * the copy is freed then the copy will become invalid. This is a result from
 * how typical DMA APIs work: we can't call free on the buffer multiple times.
 * Nor can we call free on parts of a buffer. Thus the only way to ensure that
 * the entire buffer is actually freed is to call free once on the source
 * buffer. Since these nvgpu_mem structs are not ref-counted in anyway it is up
 * to the caller of this API to _ensure_ that the resulting nvgpu_mem buffer
 * from this API is freed before the source buffer. Otherwise there can and will
 * be memory corruption.
 *
 * The resulting nvgpu_mem should be released with the nvgpu_dma_free() or the
 * nvgpu_dma_unmap_free() function depending on whether or not the resulting
 * nvgpu_mem has been mapped.
 *
 * This will return 0 on success. An error is returned if the resulting
 * nvgpu_mem would not make sense or if a new scatter gather table cannot be
 * created.
 */
int nvgpu_mem_create_from_mem(struct gk20a *g,
			      struct nvgpu_mem *dest, struct nvgpu_mem *src,
			      int start_page, int nr_pages);

/*
 * Buffer accessors - wrap between begin() and end() if there is no permanent
 * kernel mapping for this buffer.
 */

int nvgpu_mem_begin(struct gk20a *g, struct nvgpu_mem *mem);
/* nop for null mem, like with free() or vunmap() */
void nvgpu_mem_end(struct gk20a *g, struct nvgpu_mem *mem);

/* word-indexed offset */
u32 nvgpu_mem_rd32(struct gk20a *g, struct nvgpu_mem *mem, u32 w);
/* byte offset (32b-aligned) */
u32 nvgpu_mem_rd(struct gk20a *g, struct nvgpu_mem *mem, u32 offset);
/* memcpy to cpu, offset and size in bytes (32b-aligned) */
void nvgpu_mem_rd_n(struct gk20a *g, struct nvgpu_mem *mem, u32 offset,
		void *dest, u32 size);

/* word-indexed offset */
void nvgpu_mem_wr32(struct gk20a *g, struct nvgpu_mem *mem, u32 w, u32 data);
/* byte offset (32b-aligned) */
void nvgpu_mem_wr(struct gk20a *g, struct nvgpu_mem *mem, u32 offset, u32 data);
/* memcpy from cpu, offset and size in bytes (32b-aligned) */
void nvgpu_mem_wr_n(struct gk20a *g, struct nvgpu_mem *mem, u32 offset,
		void *src, u32 size);
/* size and offset in bytes (32b-aligned), filled with the constant byte c */
void nvgpu_memset(struct gk20a *g, struct nvgpu_mem *mem, u32 offset,
		u32 c, u32 size);

u32 __nvgpu_aperture_mask(struct gk20a *g, enum nvgpu_aperture aperture,
		u32 sysmem_mask, u32 vidmem_mask);
u32 nvgpu_aperture_mask(struct gk20a *g, struct nvgpu_mem *mem,
		u32 sysmem_mask, u32 vidmem_mask);

#endif
