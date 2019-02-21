/*
 * Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/bug.h>
#include <nvgpu/log.h>
#include <nvgpu/list.h>
#include <nvgpu/dma.h>
#include <nvgpu/gmmu.h>
#include <nvgpu/pd_cache.h>
#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/nvgpu_sgt.h>
#include <nvgpu/enabled.h>
#include <nvgpu/page_allocator.h>
#include <nvgpu/barrier.h>
#include <nvgpu/vidmem.h>
#include <nvgpu/sizes.h>
#include <nvgpu/types.h>
#include <nvgpu/gk20a.h>

#include "gk20a/mm_gk20a.h"

#define __gmmu_dbg(g, attrs, fmt, args...)				\
	do {								\
		if ((attrs)->debug) {					\
			nvgpu_info(g, fmt, ##args);			\
		} else {						\
			nvgpu_log(g, gpu_dbg_map, fmt, ##args);		\
		}							\
	} while (false)

#define __gmmu_dbg_v(g, attrs, fmt, args...)				\
	do {								\
		if ((attrs)->debug) {					\
			nvgpu_info(g, fmt, ##args);			\
		} else {						\
			nvgpu_log(g, gpu_dbg_map_v, fmt, ##args);	\
		}							\
	} while (false)

static int pd_allocate(struct vm_gk20a *vm,
		       struct nvgpu_gmmu_pd *pd,
		       const struct gk20a_mmu_level *l,
		       struct nvgpu_gmmu_attrs *attrs);
static u32 pd_size(const struct gk20a_mmu_level *l,
		   struct nvgpu_gmmu_attrs *attrs);
/*
 * Core GMMU map function for the kernel to use. If @addr is 0 then the GPU
 * VA will be allocated for you. If addr is non-zero then the buffer will be
 * mapped at @addr.
 */
static u64 __nvgpu_gmmu_map(struct vm_gk20a *vm,
			    struct nvgpu_mem *mem,
			    u64 addr,
			    u64 size,
			    u32 flags,
			    enum gk20a_mem_rw_flag rw_flag,
			    bool priv,
			    enum nvgpu_aperture aperture)
{
	struct gk20a *g = gk20a_from_vm(vm);
	u64 vaddr;

	struct nvgpu_sgt *sgt = nvgpu_sgt_create_from_mem(g, mem);

	if (sgt == NULL) {
		return 0;
	}

	/*
	 * Later on, when we free this nvgpu_mem's GPU mapping, we are going to
	 * potentially have to free the GPU VA space. If the address passed in
	 * is non-zero then this API is not expected to manage the VA space and
	 * therefor we should not try and free it. But otherwise, if we do
	 * manage the VA alloc, we obviously must free it.
	 */
	if (addr != 0U) {
		mem->free_gpu_va = false;
	} else {
		mem->free_gpu_va = true;
	}

	nvgpu_mutex_acquire(&vm->update_gmmu_lock);
	vaddr = g->ops.mm.gmmu_map(vm, addr,
				   sgt,    /* sg list */
				   0,      /* sg offset */
				   size,
				   GMMU_PAGE_SIZE_KERNEL,
				   0,      /* kind */
				   0,      /* ctag_offset */
				   flags, rw_flag,
				   false,  /* clear_ctags */
				   false,  /* sparse */
				   priv,   /* priv */
				   NULL,   /* mapping_batch handle */
				   aperture);
	nvgpu_mutex_release(&vm->update_gmmu_lock);

	nvgpu_sgt_free(g, sgt);

	if (vaddr == 0ULL) {
		nvgpu_err(g, "failed to map buffer!");
		return 0;
	}

	return vaddr;
}

/*
 * Map a nvgpu_mem into the GMMU. This is for kernel space to use.
 */
u64 nvgpu_gmmu_map(struct vm_gk20a *vm,
		   struct nvgpu_mem *mem,
		   u64 size,
		   u32 flags,
		   enum gk20a_mem_rw_flag rw_flag,
		   bool priv,
		   enum nvgpu_aperture aperture)
{
	return __nvgpu_gmmu_map(vm, mem, 0, size, flags, rw_flag, priv,
			aperture);
}

/*
 * Like nvgpu_gmmu_map() except this can work on a fixed address.
 */
u64 nvgpu_gmmu_map_fixed(struct vm_gk20a *vm,
			 struct nvgpu_mem *mem,
			 u64 addr,
			 u64 size,
			 u32 flags,
			 enum gk20a_mem_rw_flag rw_flag,
			 bool priv,
			 enum nvgpu_aperture aperture)
{
	return __nvgpu_gmmu_map(vm, mem, addr, size, flags, rw_flag, priv,
			aperture);
}

void nvgpu_gmmu_unmap(struct vm_gk20a *vm, struct nvgpu_mem *mem, u64 gpu_va)
{
	struct gk20a *g = gk20a_from_vm(vm);

	nvgpu_mutex_acquire(&vm->update_gmmu_lock);
	g->ops.mm.gmmu_unmap(vm,
			     gpu_va,
			     mem->size,
			     GMMU_PAGE_SIZE_KERNEL,
			     mem->free_gpu_va,
			     gk20a_mem_flag_none,
			     false,
			     NULL);

	nvgpu_mutex_release(&vm->update_gmmu_lock);
}

int nvgpu_gmmu_init_page_table(struct vm_gk20a *vm)
{
	u32 pdb_size;
	int err;

	/*
	 * Need this just for page size. Everything else can be ignored. Also
	 * note that we can just use pgsz 0 (i.e small pages) since the number
	 * of bits present in the top level PDE are the same for small/large
	 * page VMs.
	 */
	struct nvgpu_gmmu_attrs attrs = {
		.pgsz = 0,
	};

	/*
	 * PDB size here must be at least 4096 bytes so that its address is 4K
	 * aligned. Although lower PDE tables can be aligned at 256B boundaries
	 * the PDB must be 4K aligned.
	 *
	 * Currently PAGE_SIZE is used, even when 64K, to work around an issue
	 * with the PDB TLB invalidate code not being pd_cache aware yet.
	 */
	pdb_size = ALIGN(pd_size(&vm->mmu_levels[0], &attrs), PAGE_SIZE);

	err = nvgpu_pd_alloc(vm, &vm->pdb, pdb_size);
	if (WARN_ON(err != 0)) {
		return err;
	}

	/*
	 * One nvgpu_mb() is done after all mapping operations. Don't need
	 * individual barriers for each PD write.
	 */
	vm->pdb.mem->skip_wmb = true;

	return 0;
}

/*
 * Return the aligned length based on the page size in attrs.
 */
static u64 nvgpu_align_map_length(struct vm_gk20a *vm, u64 length,
				  struct nvgpu_gmmu_attrs *attrs)
{
	u64 page_size = vm->gmmu_page_sizes[attrs->pgsz];

	return ALIGN(length, page_size);
}

static u32 pd_entries(const struct gk20a_mmu_level *l,
		      struct nvgpu_gmmu_attrs *attrs)
{
	/*
	 * Number of entries in a PD is easy to compute from the number of bits
	 * used to index the page directory. That is simply 2 raised to the
	 * number of bits.
	 */
	return BIT32(l->hi_bit[attrs->pgsz] - l->lo_bit[attrs->pgsz] + 1);
}

/*
 * Computes the size of a PD table (in bytes).
 */
static u32 pd_size(const struct gk20a_mmu_level *l,
		   struct nvgpu_gmmu_attrs *attrs)
{
	return pd_entries(l, attrs) * l->entry_size;
}

/*
 * Allocate a physically contiguous region big enough for a gmmu page table
 * of the specified level and page size. The whole range is zeroed so that any
 * accesses will fault until proper values are programmed.
 */
static int pd_allocate(struct vm_gk20a *vm,
		       struct nvgpu_gmmu_pd *pd,
		       const struct gk20a_mmu_level *l,
		       struct nvgpu_gmmu_attrs *attrs)
{
	int err;

	/*
	 * Same basic logic as in pd_allocate_children() except we (re)allocate
	 * the underlying DMA memory here.
	 */
	if (pd->mem != NULL && pd->pd_size >= pd_size(l, attrs)) {
		return 0;
	}

	if (pd->mem != NULL) {
		nvgpu_pd_free(vm, pd);
		pd->mem = NULL;
	}

	err = nvgpu_pd_alloc(vm, pd, pd_size(l, attrs));
	if (err != 0) {
		nvgpu_info(vm->mm->g, "error allocating page directory!");
		return err;
	}

	/*
	 * One nvgpu_mb() is done after all mapping operations. Don't need
	 * individual barriers for each PD write.
	 */
	pd->mem->skip_wmb = true;

	return 0;
}

/*
 * Compute what page directory index at the passed level the passed virtual
 * address corresponds to. @attrs is necessary for determining the page size
 * which is used to pick the right bit offsets for the GMMU level.
 */
static u32 pd_index(const struct gk20a_mmu_level *l, u64 virt,
		    struct nvgpu_gmmu_attrs *attrs)
{
	u64 pd_mask = (1ULL << ((u64)l->hi_bit[attrs->pgsz] + 1U)) - 1ULL;
	u32 pd_shift = (u64)l->lo_bit[attrs->pgsz];

	/*
	 * For convenience we don't bother computing the lower bound of the
	 * mask; it's easier to just shift it off.
	 */
	return (virt & pd_mask) >> pd_shift;
}

static int pd_allocate_children(struct vm_gk20a *vm,
				const struct gk20a_mmu_level *l,
				struct nvgpu_gmmu_pd *pd,
				struct nvgpu_gmmu_attrs *attrs)
{
	struct gk20a *g = gk20a_from_vm(vm);

	/*
	 * Check that we have already allocated enough pd_entries for this
	 * page directory. There's 4 possible cases:
	 *
	 *   1. This pd is new and therefor has no entries.
	 *   2. This pd does not have enough entries.
	 *   3. This pd has exactly the right number of entries.
	 *   4. This pd has more than enough entries.
	 *
	 * (3) and (4) are easy: just return. Case (1) is also straight forward:
	 * just allocate enough space for the number of pd_entries.
	 *
	 * Case (2) is rare but can happen. It occurs when we have a PD that has
	 * already been allocated for some VA range with a page size of 64K. If
	 * later on we free that VA range and then remap that VA range with a
	 * 4K page size map then we now need more pd space. As such we need to
	 * reallocate this pd entry array.
	 *
	 * Critically case (2) should _only_ ever happen when the PD is not in
	 * use. Obviously blowing away a bunch of previous PDEs would be
	 * catastrophic. But the buddy allocator logic prevents mixing page
	 * sizes within a single last level PD range. Therefor we should only
	 * ever see this once the entire PD range has been freed - otherwise
	 * there would be mixing (which, remember, is prevented by the buddy
	 * allocator).
	 */
	if (pd->num_entries >= (int)pd_entries(l, attrs)) {
		return 0;
	}

	if (pd->entries != NULL) {
		nvgpu_vfree(g, pd->entries);
	}

	pd->num_entries = pd_entries(l, attrs);
	pd->entries = nvgpu_vzalloc(g, sizeof(struct nvgpu_gmmu_pd) *
				(unsigned long)pd->num_entries);
	if (pd->entries == NULL) {
		pd->num_entries = 0;
		return -ENOMEM;
	}

	return 0;
}

/*
 * This function programs the GMMU based on two ranges: a physical range and a
 * GPU virtual range. The virtual is mapped to the physical. Physical in this
 * case can mean either a real physical sysmem address or a IO virtual address
 * (for instance when a system has an IOMMU running).
 *
 * The rest of the parameters are for describing the actual mapping itself.
 *
 * This function recursively calls itself for handling PDEs. At the final level
 * a PTE handler is called. The phys and virt ranges are adjusted for each
 * recursion so that each invocation of this function need only worry about the
 * range it is passed.
 *
 * phys_addr will always point to a contiguous range - the discontiguous nature
 * of DMA buffers is taken care of at the layer above this.
 */
static int __set_pd_level(struct vm_gk20a *vm,
			  struct nvgpu_gmmu_pd *pd,
			  int lvl,
			  u64 phys_addr,
			  u64 virt_addr, u64 length,
			  struct nvgpu_gmmu_attrs *attrs)
{
	int err = 0;
	u64 pde_range;
	struct gk20a *g = gk20a_from_vm(vm);
	struct nvgpu_gmmu_pd *next_pd = NULL;
	const struct gk20a_mmu_level *l      = &vm->mmu_levels[lvl];
	const struct gk20a_mmu_level *next_l = &vm->mmu_levels[lvl + 1];

	/*
	 * 5 levels for Pascal+. For pre-pascal we only have 2. This puts
	 * offsets into the page table debugging code which makes it easier to
	 * see what level prints are from.
	 */
	static const char *__lvl_debug[] = {
		"",          /* L=0 */
		"  ",        /* L=1 */
		"    ",      /* L=2 */
		"      ",    /* L=3 */
		"        ",  /* L=4 */
	};

	pde_range = 1ULL << (u64)l->lo_bit[attrs->pgsz];

	__gmmu_dbg_v(g, attrs,
		     "L=%d   %sGPU virt %#-12llx +%#-9llx -> phys %#-12llx",
		     lvl,
		     __lvl_debug[lvl],
		     virt_addr,
		     length,
		     phys_addr);

	/*
	 * Iterate across the mapping in chunks the size of this level's PDE.
	 * For each of those chunks program our level's PDE and then, if there's
	 * a next level, program the next level's PDEs/PTEs.
	 */
	while (length != 0ULL) {
		u32 pd_idx = pd_index(l, virt_addr, attrs);
		u64 chunk_size;
		u64 target_addr;

		/*
		 * Truncate the pde_range when the virtual address does not
		 * start at a PDE boundary.
		 */
		chunk_size = min(length,
				 pde_range - (virt_addr & (pde_range - 1U)));

		/*
		 * If the next level has an update_entry function then we know
		 * that _this_ level points to PDEs (not PTEs). Thus we need to
		 * have a bunch of children PDs.
		 */
		if (next_l->update_entry != NULL) {
			if (pd_allocate_children(vm, l, pd, attrs) != 0) {
				return -ENOMEM;
			}

			/*
			 * Get the next PD so that we know what to put in this
			 * current PD. If the next level is actually PTEs then
			 * we don't need this - we will just use the real
			 * physical target.
			 */
			next_pd = &pd->entries[pd_idx];

			/*
			 * Allocate the backing memory for next_pd.
			 */
			if (pd_allocate(vm, next_pd, next_l, attrs) != 0) {
				return -ENOMEM;
			}
		}

		/*
		 * This is the address we want to program into the actual PDE/
		 * PTE. When the next level is PDEs we need the target address
		 * to be the table of PDEs. When the next level is PTEs the
		 * target addr is the real physical address we are aiming for.
		 */
		target_addr = (next_pd != NULL) ?
			nvgpu_pd_gpu_addr(g, next_pd) :
			phys_addr;

		l->update_entry(vm, l,
				pd, pd_idx,
				virt_addr,
				target_addr,
				attrs);

		if (next_l->update_entry != NULL) {
			err = __set_pd_level(vm, next_pd,
					     lvl + 1,
					     phys_addr,
					     virt_addr,
					     chunk_size,
					     attrs);

			if (err != 0) {
				return err;
			}
		}

		virt_addr += chunk_size;

		/*
		 * Only add to phys_addr if it's non-zero. A zero value implies
		 * we are unmapping as as a result we don't want to place
		 * non-zero phys addresses in the PTEs. A non-zero phys-addr
		 * would also confuse the lower level PTE programming code.
		 */
		if (phys_addr != 0ULL) {
			phys_addr += chunk_size;
		}
		length -= chunk_size;
	}

	__gmmu_dbg_v(g, attrs, "L=%d   %s%s", lvl, __lvl_debug[lvl], "ret!");

	return 0;
}

static int __nvgpu_gmmu_do_update_page_table(struct vm_gk20a *vm,
					     struct nvgpu_sgt *sgt,
					     u64 space_to_skip,
					     u64 virt_addr,
					     u64 length,
					     struct nvgpu_gmmu_attrs *attrs)
{
	struct gk20a *g = gk20a_from_vm(vm);
	struct nvgpu_sgl *sgl;
	int err = 0;

	if (sgt == NULL) {
		/*
		 * This is considered an unmap. Just pass in 0 as the physical
		 * address for the entire GPU range.
		 */
		err = __set_pd_level(vm, &vm->pdb,
				     0,
				     0,
				     virt_addr, length,
				     attrs);
		return err;
	}

	/*
	 * At this point we have a scatter-gather list pointing to some number
	 * of discontiguous chunks of memory. We must iterate over that list and
	 * generate a GMMU map call for each chunk. There are several
	 * possibilities:
	 *
	 *  1. IOMMU enabled, IOMMU addressing (typical iGPU)
	 *  2. IOMMU enabled, IOMMU bypass     (NVLINK bypasses SMMU)
	 *  3. IOMMU disabled                  (less common but still supported)
	 *  4. VIDMEM
	 *
	 * For (1) we can assume that there's really only one actual SG chunk
	 * since the IOMMU gives us a single contiguous address range. However,
	 * for (2), (3) and (4) we have to actually go through each SG entry and
	 * map each chunk individually.
	 */
	if (nvgpu_aperture_is_sysmem(attrs->aperture) &&
	    nvgpu_iommuable(g) &&
	    nvgpu_sgt_iommuable(g, sgt)) {
		u64 io_addr = nvgpu_sgt_get_gpu_addr(g, sgt, sgt->sgl, attrs);

		io_addr += space_to_skip;

		err = __set_pd_level(vm, &vm->pdb,
				     0,
				     io_addr,
				     virt_addr,
				     length,
				     attrs);

		return err;
	}

	/*
	 * Handle cases (2), (3), and (4): do the no-IOMMU mapping. In this case
	 * we really are mapping physical pages directly.
	 */
	nvgpu_sgt_for_each_sgl(sgl, sgt) {
		/*
		 * ipa_addr == phys_addr for non virtualized OSes.
		 */
		u64 phys_addr;
		u64 ipa_addr;
		/*
		 * For non virtualized OSes SGL entries are contiguous in
		 * physical memory (sgl_length == phys_length). For virtualized
		 * OSes SGL entries are mapped to intermediate physical memory
		 * which may subsequently point to discontiguous physical
		 * memory. Therefore phys_length may not be equal to sgl_length.
		 */
		u64 phys_length;
		u64 sgl_length;

		/*
		 * Cut out sgl ents for space_to_skip.
		 */
		if (space_to_skip != 0ULL &&
		    space_to_skip >= nvgpu_sgt_get_length(sgt, sgl)) {
			space_to_skip -= nvgpu_sgt_get_length(sgt, sgl);
			continue;
		}

		/*
		 * IPA and PA have 1:1 mapping for non virtualized OSes.
		 */
		ipa_addr = nvgpu_sgt_get_ipa(g, sgt, sgl);

		/*
		 * For non-virtualized OSes SGL entries are contiguous and hence
		 * sgl_length == phys_length. For virtualized OSes the
		 * phys_length will be updated by nvgpu_sgt_ipa_to_pa.
		 */
		sgl_length = nvgpu_sgt_get_length(sgt, sgl);
		phys_length = sgl_length;

		while (sgl_length > 0ULL && length > 0ULL) {
			/*
			 * Holds the size of the portion of SGL that is backed
			 * with physically contiguous memory.
			 */
			u64 sgl_contiguous_length;
			/*
			 * Number of bytes of the SGL entry that is actually
			 * mapped after accounting for space_to_skip.
			 */
			u64 mapped_sgl_length;

			/*
			 * For virtualized OSes translate IPA to PA. Retrieve
			 * the size of the underlying physical memory chunk to
			 * which SGL has been mapped.
			 */
			phys_addr = nvgpu_sgt_ipa_to_pa(g, sgt, sgl, ipa_addr,
					&phys_length);
			phys_addr = g->ops.mm.gpu_phys_addr(g, attrs, phys_addr)
				+ space_to_skip;

			/*
			 * For virtualized OSes when phys_length is less than
			 * sgl_length check if space_to_skip exceeds phys_length
			 * if so skip this memory chunk
			 */
			if (space_to_skip >= phys_length) {
				space_to_skip -= phys_length;
				ipa_addr += phys_length;
				sgl_length -= phys_length;
				continue;
			}

			sgl_contiguous_length = min(phys_length, sgl_length);
			mapped_sgl_length = min(length, sgl_contiguous_length -
					space_to_skip);

			err = __set_pd_level(vm, &vm->pdb,
						 0,
						 phys_addr,
						 virt_addr,
						 mapped_sgl_length,
						 attrs);
			if (err != 0) {
				return err;
			}

			/*
			 * Update the map pointer and the remaining length.
			 */
			virt_addr  += mapped_sgl_length;
			length     -= mapped_sgl_length;
			sgl_length -= mapped_sgl_length + space_to_skip;
			ipa_addr   += mapped_sgl_length + space_to_skip;

			/*
			 * Space has been skipped so zero this for future
			 * chunks.
			 */
			space_to_skip = 0;
		}

		if (length == 0ULL) {
			break;
		}
	}

	return err;
}

/*
 * This is the true top level GMMU mapping logic. This breaks down the incoming
 * scatter gather table and does actual programming of GPU virtual address to
 * physical* address.
 *
 * The update of each level of the page tables is farmed out to chip specific
 * implementations. But the logic around that is generic to all chips. Every
 * chip has some number of PDE levels and then a PTE level.
 *
 * Each chunk of the incoming SGL is sent to the chip specific implementation
 * of page table update.
 *
 * [*] Note: the "physical" address may actually be an IO virtual address in the
 *     case of SMMU usage.
 */
static int __nvgpu_gmmu_update_page_table(struct vm_gk20a *vm,
					  struct nvgpu_sgt *sgt,
					  u64 space_to_skip,
					  u64 virt_addr,
					  u64 length,
					  struct nvgpu_gmmu_attrs *attrs)
{
	struct gk20a *g = gk20a_from_vm(vm);
	u32 page_size;
	int err;

	/* note: here we need to map kernel to small, since the
	 * low-level mmu code assumes 0 is small and 1 is big pages */
	if (attrs->pgsz == GMMU_PAGE_SIZE_KERNEL) {
		attrs->pgsz = GMMU_PAGE_SIZE_SMALL;
	}

	page_size = vm->gmmu_page_sizes[attrs->pgsz];

	if ((space_to_skip & (U64(page_size) - U64(1))) != 0ULL) {
		return -EINVAL;
	}

	/*
	 * Update length to be aligned to the passed page size.
	 */
	length = nvgpu_align_map_length(vm, length, attrs);

	__gmmu_dbg(g, attrs,
		   "vm=%s "
		   "%-5s GPU virt %#-12llx +%#-9llx    phys %#-12llx "
		   "phys offset: %#-4llx;  pgsz: %3dkb perm=%-2s | "
		   "kind=%#02x APT=%-6s %c%c%c%c",
		   vm->name,
		   (sgt != NULL) ? "MAP" : "UNMAP",
		   virt_addr,
		   length,
		   (sgt != NULL) ? nvgpu_sgt_get_phys(g, sgt, sgt->sgl) : 0ULL,
		   space_to_skip,
		   page_size >> 10,
		   nvgpu_gmmu_perm_str(attrs->rw_flag),
		   attrs->kind_v,
		   nvgpu_aperture_str(g, attrs->aperture),
		   attrs->cacheable ? 'C' : '-',
		   attrs->sparse    ? 'S' : '-',
		   attrs->priv      ? 'P' : '-',
		   attrs->valid     ? 'V' : '-');

	err = __nvgpu_gmmu_do_update_page_table(vm,
						sgt,
						space_to_skip,
						virt_addr,
						length,
						attrs);

	nvgpu_mb();

	__gmmu_dbg(g, attrs, "%-5s Done!",
				(sgt != NULL) ? "MAP" : "UNMAP");

	return err;
}

/**
 * gk20a_locked_gmmu_map - Map a buffer into the GMMU
 *
 * This is for non-vGPU chips. It's part of the HAL at the moment but really
 * should not be. Chip specific stuff is handled at the PTE/PDE programming
 * layer. The rest of the logic is essentially generic for all chips.
 *
 * To call this function you must have locked the VM lock: vm->update_gmmu_lock.
 * However, note: this function is not called directly. It's used through the
 * mm.gmmu_lock() HAL. So before calling the mm.gmmu_lock() HAL make sure you
 * have the update_gmmu_lock aquired.
 */
u64 gk20a_locked_gmmu_map(struct vm_gk20a *vm,
			  u64 vaddr,
			  struct nvgpu_sgt *sgt,
			  u64 buffer_offset,
			  u64 size,
			  u32 pgsz_idx,
			  u8 kind_v,
			  u32 ctag_offset,
			  u32 flags,
			  enum gk20a_mem_rw_flag rw_flag,
			  bool clear_ctags,
			  bool sparse,
			  bool priv,
			  struct vm_gk20a_mapping_batch *batch,
			  enum nvgpu_aperture aperture)
{
	struct gk20a *g = gk20a_from_vm(vm);
	int err = 0;
	bool allocated = false;
	int ctag_granularity = g->ops.fb.compression_page_size(g);
	struct nvgpu_gmmu_attrs attrs = {
		.pgsz      = pgsz_idx,
		.kind_v    = kind_v,
		.ctag      = (u64)ctag_offset * (u64)ctag_granularity,
		.cacheable = flags & NVGPU_VM_MAP_CACHEABLE,
		.rw_flag   = rw_flag,
		.sparse    = sparse,
		.priv      = priv,
		.valid     = (flags & NVGPU_VM_MAP_UNMAPPED_PTE) == 0U,
		.aperture  = aperture
	};

	/*
	 * We need to add the buffer_offset within compression_page_size so that
	 * the programmed ctagline gets increased at compression_page_size
	 * boundaries.
	 */
	if (attrs.ctag != 0ULL) {
		attrs.ctag += buffer_offset & (U64(ctag_granularity) - U64(1));
	}

	attrs.l3_alloc = (bool)(flags & NVGPU_VM_MAP_L3_ALLOC);

	/*
	 * Only allocate a new GPU VA range if we haven't already been passed a
	 * GPU VA range. This facilitates fixed mappings.
	 */
	if (vaddr == 0ULL) {
		vaddr = nvgpu_vm_alloc_va(vm, size, pgsz_idx);
		if (vaddr == 0ULL) {
			nvgpu_err(g, "failed to allocate va space");
			err = -ENOMEM;
			goto fail_alloc;
		}
		allocated = true;
	}

	err = __nvgpu_gmmu_update_page_table(vm, sgt, buffer_offset,
					     vaddr, size, &attrs);
	if (err != 0) {
		nvgpu_err(g, "failed to update ptes on map");
		goto fail_validate;
	}

	if (batch == NULL) {
		g->ops.fb.tlb_invalidate(g, vm->pdb.mem);
	} else {
		batch->need_tlb_invalidate = true;
	}

	return vaddr;

fail_validate:
	if (allocated) {
		nvgpu_vm_free_va(vm, vaddr, pgsz_idx);
	}
fail_alloc:
	nvgpu_err(g, "%s: failed with err=%d", __func__, err);
	return 0;
}

void gk20a_locked_gmmu_unmap(struct vm_gk20a *vm,
			     u64 vaddr,
			     u64 size,
			     u32 pgsz_idx,
			     bool va_allocated,
			     enum gk20a_mem_rw_flag rw_flag,
			     bool sparse,
			     struct vm_gk20a_mapping_batch *batch)
{
	int err = 0;
	struct gk20a *g = gk20a_from_vm(vm);
	struct nvgpu_gmmu_attrs attrs = {
		.pgsz      = pgsz_idx,
		.kind_v    = 0,
		.ctag      = 0,
		.cacheable = 0,
		.rw_flag   = rw_flag,
		.sparse    = sparse,
		.priv      = 0,
		.valid     = 0,
		.aperture  = APERTURE_INVALID,
	};

	if (va_allocated) {
		nvgpu_vm_free_va(vm, vaddr, pgsz_idx);
	}

	/* unmap here needs to know the page size we assigned at mapping */
	err = __nvgpu_gmmu_update_page_table(vm, NULL, 0,
					     vaddr, size, &attrs);
	if (err != 0) {
		nvgpu_err(g, "failed to update gmmu ptes on unmap");
	}

	if (batch == NULL) {
		if (gk20a_mm_l2_flush(g, true) != 0) {
			nvgpu_err(g, "gk20a_mm_l2_flush[1] failed");
		}
		g->ops.fb.tlb_invalidate(g, vm->pdb.mem);
	} else {
		if (!batch->gpu_l2_flushed) {
			if (gk20a_mm_l2_flush(g, true) != 0) {
				nvgpu_err(g, "gk20a_mm_l2_flush[2] failed");
			}
			batch->gpu_l2_flushed = true;
		}
		batch->need_tlb_invalidate = true;
	}
}

u32 __nvgpu_pte_words(struct gk20a *g)
{
	const struct gk20a_mmu_level *l = g->ops.mm.get_mmu_levels(g, SZ_64K);
	const struct gk20a_mmu_level *next_l;

	/*
	 * Iterate to the bottom GMMU level - the PTE level. The levels array
	 * is always NULL terminated (by the update_entry function).
	 */
	do {
		next_l = l + 1;
		if (next_l->update_entry == NULL) {
			break;
		}

		l++;
	} while (true);

	return (u32)(l->entry_size / sizeof(u32));
}

/*
 * Recursively walk the pages tables to find the PTE.
 */
static int __nvgpu_locate_pte(struct gk20a *g, struct vm_gk20a *vm,
			      struct nvgpu_gmmu_pd *pd,
			      u64 vaddr, int lvl,
			      struct nvgpu_gmmu_attrs *attrs,
			      u32 *data,
			      struct nvgpu_gmmu_pd **pd_out, u32 *pd_idx_out,
			      u32 *pd_offs_out)
{
	const struct gk20a_mmu_level *l      = &vm->mmu_levels[lvl];
	const struct gk20a_mmu_level *next_l = &vm->mmu_levels[lvl + 1];
	u32 pd_idx = pd_index(l, vaddr, attrs);
	u32 pte_base;
	u32 pte_size;
	u32 i;

	/*
	 * If this isn't the final level (i.e there's a valid next level)
	 * then find the next level PD and recurse.
	 */
	if (next_l->update_entry != NULL) {
		struct nvgpu_gmmu_pd *pd_next = pd->entries + pd_idx;

		/* Invalid entry! */
		if (pd_next->mem == NULL) {
			return -EINVAL;
		}

		attrs->pgsz = l->get_pgsz(g, l, pd, pd_idx);

		if (attrs->pgsz >= GMMU_NR_PAGE_SIZES) {
			return -EINVAL;
		}

		return __nvgpu_locate_pte(g, vm, pd_next,
					  vaddr, lvl + 1, attrs,
					  data, pd_out, pd_idx_out,
					  pd_offs_out);
	}

	if (pd->mem == NULL) {
		return -EINVAL;
	}

	/*
	 * Take into account the real offset into the nvgpu_mem since the PD
	 * may be located at an offset other than 0 (due to PD packing).
	 */
	pte_base = (u32)(pd->mem_offs / sizeof(u32)) +
		nvgpu_pd_offset_from_index(l, pd_idx);
	pte_size = (u32)(l->entry_size / sizeof(u32));

	if (data != NULL) {
		for (i = 0; i < pte_size; i++) {
			data[i] = nvgpu_mem_rd32(g, pd->mem, pte_base + i);
		}
	}

	if (pd_out != NULL) {
		*pd_out = pd;
	}

	if (pd_idx_out != NULL) {
		*pd_idx_out = pd_idx;
	}

	if (pd_offs_out != NULL) {
		*pd_offs_out = nvgpu_pd_offset_from_index(l, pd_idx);
	}

	return 0;
}

int __nvgpu_get_pte(struct gk20a *g, struct vm_gk20a *vm, u64 vaddr, u32 *pte)
{
	struct nvgpu_gmmu_attrs attrs = {
		.pgsz = 0,
	};

	return __nvgpu_locate_pte(g, vm, &vm->pdb,
				  vaddr, 0, &attrs,
				  pte, NULL, NULL, NULL);
}

int __nvgpu_set_pte(struct gk20a *g, struct vm_gk20a *vm, u64 vaddr, u32 *pte)
{
	struct nvgpu_gmmu_pd *pd;
	u32 pd_idx, pd_offs, pte_size, i;
	int err;
	struct nvgpu_gmmu_attrs attrs = {
		.pgsz = 0,
	};
	struct nvgpu_gmmu_attrs *attrs_ptr = &attrs;

	err = __nvgpu_locate_pte(g, vm, &vm->pdb,
				 vaddr, 0, &attrs,
				 NULL, &pd, &pd_idx, &pd_offs);
	if (err != 0) {
		return err;
	}

	pte_size = __nvgpu_pte_words(g);

	for (i = 0; i < pte_size; i++) {
		nvgpu_pd_write(g, pd, (size_t)pd_offs + (size_t)i, pte[i]);
		pte_dbg(g, attrs_ptr,
			"PTE: idx=%-4u (%d) 0x%08x", pd_idx, i, pte[i]);
	}

	/*
	 * Ensures the pd_write()s are done. The pd_write() does not do this
	 * since generally there's lots of pd_write()s called one after another.
	 * There probably also needs to be a TLB invalidate as well but we leave
	 * that to the caller of this function.
	 */
	nvgpu_wmb();

	return 0;
}
