/*
 * Copyright (c) 2017-2018, NVIDIA CORPORATION.  All rights reserved.
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
#include <nvgpu/dma.h>
#include <nvgpu/gmmu.h>
#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/list.h>
#include <nvgpu/log2.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/enabled.h>

#define pd_dbg(g, fmt, args...) nvgpu_log(g, gpu_dbg_pd_cache, fmt, ##args)

/**
 * DOC: PD cache
 *
 * To save memory when using sub-page sized PD levels in Pascal and beyond a way
 * of packing PD tables together is necessary. If a PD table only requires 1024
 * bytes, then it is possible to have 4 of these PDs in one page. This is even
 * more pronounced for 256 byte PD tables.
 *
 * This also matters for page directories on any chip when using a 64K page
 * granule. Having 4K PDs packed into a 64K page saves a bunch of memory. Even
 * more so for the 256B PDs on Pascal+.
 *
 * The pd cache is basially just a slab allocator. Each instance of the nvgpu
 * driver makes one of these structs:
 *
 *   struct nvgpu_pd_cache {
 *      struct nvgpu_list_node		 full[NVGPU_PD_CACHE_COUNT];
 *      struct nvgpu_list_node		 partial[NVGPU_PD_CACHE_COUNT];
 *
 *      struct nvgpu_rbtree_node	*mem_tree;
 *   };
 *
 * There are two sets of lists, the full and the partial. The full lists contain
 * pages of memory for which all the memory in that page is in use. The partial
 * lists contain partially full pages of memory which can be used for more PD
 * allocations. There a couple of assumptions here:
 *
 *   1. PDs greater than or equal to the page size bypass the pd cache.
 *   2. PDs are always power of 2 and greater than %NVGPU_PD_CACHE_MIN bytes.
 *
 * There are NVGPU_PD_CACHE_COUNT full lists and the same number of partial
 * lists. For a 4Kb page NVGPU_PD_CACHE_COUNT is 4. This is enough space for
 * 256, 512, 1024, and 2048 byte PDs.
 *
 * nvgpu_pd_alloc() will allocate a PD for the GMMU. It will check if the PD
 * size is page size or larger and choose the correct allocation scheme - either
 * from the PD cache or directly. Similarly nvgpu_pd_free() will free a PD
 * allocated by nvgpu_pd_alloc().
 */

/*
 * Minimum size of a cache. The number of different caches in the nvgpu_pd_cache
 * structure is of course depending on this. The MIN_SHIFT define is the right
 * number of bits to shift to determine which list to use in the array of lists.
 */
#define NVGPU_PD_CACHE_MIN		256U
#define NVGPU_PD_CACHE_MIN_SHIFT	9U
#if PAGE_SIZE == 4096
#define NVGPU_PD_CACHE_COUNT		4U
#elif PAGE_SIZE == 65536
#define NVGPU_PD_CACHE_COUNT		8U
#else
#error "Unsupported page size."
#endif

struct nvgpu_pd_mem_entry {
	struct nvgpu_mem		mem;

	/*
	 * Size of the page directories (not the mem). alloc_map is a bitmap
	 * showing which PDs have been allocated. The size of mem will always
	 * be one page. pd_size will always be a power of 2.
	 */
	u32				pd_size;
	DECLARE_BITMAP(alloc_map, PAGE_SIZE / NVGPU_PD_CACHE_MIN);
	u32				allocs;

	struct nvgpu_list_node		list_entry;
	struct nvgpu_rbtree_node	tree_entry;
};

static inline struct nvgpu_pd_mem_entry *
nvgpu_pd_mem_entry_from_list_entry(struct nvgpu_list_node *node)
{
	return (struct nvgpu_pd_mem_entry *)
		((uintptr_t)node -
		 offsetof(struct nvgpu_pd_mem_entry, list_entry));
};

static inline struct nvgpu_pd_mem_entry *
nvgpu_pd_mem_entry_from_tree_entry(struct nvgpu_rbtree_node *node)
{
	return (struct nvgpu_pd_mem_entry *)
		((uintptr_t)node -
		 offsetof(struct nvgpu_pd_mem_entry, tree_entry));
};

/*
 * A cache for allocating PD memory from. This enables smaller PDs to be packed
 * into single pages.
 *
 * This is fairly complex so see the documentation in pd_cache.c for a full
 * description of how this is organized.
 */
struct nvgpu_pd_cache {
	/*
	 * Array of lists of full nvgpu_pd_mem_entries and partially full (or
	 * empty) nvgpu_pd_mem_entries.
	 */
	struct nvgpu_list_node		 full[NVGPU_PD_CACHE_COUNT];
	struct nvgpu_list_node		 partial[NVGPU_PD_CACHE_COUNT];

	/*
	 * Tree of all allocated struct nvgpu_mem's for fast look up.
	 */
	struct nvgpu_rbtree_node	*mem_tree;

	/*
	 * All access to the cache much be locked. This protects the lists and
	 * the rb tree.
	 */
	struct nvgpu_mutex		 lock;
};

static u32 nvgpu_pd_cache_nr(u32 bytes)
{
	unsigned long tmp = ilog2((unsigned long)bytes >>
			((unsigned long)NVGPU_PD_CACHE_MIN_SHIFT - 1UL));

	nvgpu_assert(tmp <= U32_MAX);
	return (u32)tmp;
}

static u32 nvgpu_pd_cache_get_nr_entries(struct nvgpu_pd_mem_entry *pentry)
{
	return PAGE_SIZE / pentry->pd_size;
}

/*
 * Return the _physical_ address of a page directory.
 */
u64 nvgpu_pd_gpu_addr(struct gk20a *g, struct nvgpu_gmmu_pd *pd)
{
	u64 page_addr;

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_NVLINK)) {
		page_addr = nvgpu_mem_get_phys_addr(g, pd->mem);
	} else {
		page_addr = nvgpu_mem_get_addr(g, pd->mem);
	}

	return page_addr + pd->mem_offs;
}

u32 nvgpu_pd_offset_from_index(const struct gk20a_mmu_level *l, u32 pd_idx)
{
	return (pd_idx * l->entry_size) / U32(sizeof(u32));
}

void nvgpu_pd_write(struct gk20a *g, struct nvgpu_gmmu_pd *pd,
		    size_t w, u32 data)
{
	nvgpu_mem_wr32(g, pd->mem,
		       (u32)((pd->mem_offs / sizeof(u32)) + w), data);
}

int nvgpu_pd_cache_init(struct gk20a *g)
{
	struct nvgpu_pd_cache *cache;
	u32 i;
	int err = 0;


	/*
	 * This gets called from finalize_poweron() so we need to make sure we
	 * don't reinit the pd_cache over and over.
	 */
	if (g->mm.pd_cache != NULL) {
		return 0;
	}

	cache = nvgpu_kzalloc(g, sizeof(*cache));
	if (cache == NULL) {
		nvgpu_err(g, "Failed to alloc pd_cache!");
		return -ENOMEM;
	}

	for (i = 0U; i < NVGPU_PD_CACHE_COUNT; i++) {
		nvgpu_init_list_node(&cache->full[i]);
		nvgpu_init_list_node(&cache->partial[i]);
	}

	cache->mem_tree = NULL;

	err = nvgpu_mutex_init(&cache->lock);
	if (err != 0) {
		nvgpu_err(g, "Error in cache.lock initialization");
		nvgpu_kfree(g, cache);
		return err;
	}

	g->mm.pd_cache = cache;

	pd_dbg(g, "PD cache initialized!");

	return 0;
}

void nvgpu_pd_cache_fini(struct gk20a *g)
{
	u32 i;
	struct nvgpu_pd_cache *cache = g->mm.pd_cache;

	if (cache == NULL) {
		return;
	}

	for (i = 0U; i < NVGPU_PD_CACHE_COUNT; i++) {
		(void) WARN_ON(!nvgpu_list_empty(&cache->full[i]));
		(void) WARN_ON(!nvgpu_list_empty(&cache->partial[i]));
	}

	nvgpu_kfree(g, g->mm.pd_cache);
	g->mm.pd_cache = NULL;
}

/*
 * This is the simple pass-through for greater than page or page sized PDs.
 *
 * Note: this does not need the cache lock since it does not modify any of the
 * PD cache data structures.
 */
static int nvgpu_pd_cache_alloc_direct(struct gk20a *g,
				       struct nvgpu_gmmu_pd *pd, u32 bytes)
{
	int err;
	unsigned long flags = 0;

	pd_dbg(g, "PD-Alloc [D] %u bytes", bytes);

	pd->mem = nvgpu_kzalloc(g, sizeof(*pd->mem));
	if (pd->mem == NULL) {
		nvgpu_err(g, "OOM allocating nvgpu_mem struct!");
		return -ENOMEM;
	}

	/*
	 * If bytes == PAGE_SIZE then it's impossible to get a discontiguous DMA
	 * allocation. Some DMA implementations may, despite this fact, still
	 * use the contiguous pool for page sized allocations. As such only
	 * request explicitly contiguous allocs if the page directory is larger
	 * than the page size. Also, of course, this is all only revelant for
	 * GPUs not using an IOMMU. If there is an IOMMU DMA allocs are always
	 * going to be virtually contiguous and we don't have to force the
	 * underlying allocations to be physically contiguous as well.
	 */
	if (!nvgpu_iommuable(g) && bytes > PAGE_SIZE) {
		flags = NVGPU_DMA_FORCE_CONTIGUOUS;
	}

	err = nvgpu_dma_alloc_flags(g, flags, bytes, pd->mem);
	if (err != 0) {
		nvgpu_err(g, "OOM allocating page directory!");
		nvgpu_kfree(g, pd->mem);
		return -ENOMEM;
	}

	pd->cached = false;
	pd->mem_offs = 0;

	return 0;
}

/*
 * Make a new nvgpu_pd_cache_entry and allocate a PD from it. Update the passed
 * pd to reflect this allocation.
 */
static int nvgpu_pd_cache_alloc_new(struct gk20a *g,
				    struct nvgpu_pd_cache *cache,
				    struct nvgpu_gmmu_pd *pd,
				    u32 bytes)
{
	struct nvgpu_pd_mem_entry *pentry;

	pd_dbg(g, "PD-Alloc [C]   New: offs=0");

	pentry = nvgpu_kzalloc(g, sizeof(*pentry));
	if (pentry == NULL) {
		nvgpu_err(g, "OOM allocating pentry!");
		return -ENOMEM;
	}

	if (nvgpu_dma_alloc(g, PAGE_SIZE, &pentry->mem) != 0) {
		nvgpu_kfree(g, pentry);
		nvgpu_err(g, "Unable to DMA alloc!");
		return -ENOMEM;
	}

	pentry->pd_size = bytes;
	nvgpu_list_add(&pentry->list_entry,
		       &cache->partial[nvgpu_pd_cache_nr(bytes)]);

	/*
	 * This allocates the very first PD table in the set of tables in this
	 * nvgpu_pd_mem_entry.
	 */
	set_bit(0, pentry->alloc_map);
	pentry->allocs = 1;

	/*
	 * Now update the nvgpu_gmmu_pd to reflect this allocation.
	 */
	pd->mem = &pentry->mem;
	pd->mem_offs = 0;
	pd->cached = true;

	pentry->tree_entry.key_start = (u64)(uintptr_t)&pentry->mem;
	nvgpu_rbtree_insert(&pentry->tree_entry, &cache->mem_tree);

	return 0;
}

static int nvgpu_pd_cache_alloc_from_partial(struct gk20a *g,
					     struct nvgpu_pd_cache *cache,
					     struct nvgpu_pd_mem_entry *pentry,
					     struct nvgpu_gmmu_pd *pd)
{
	unsigned long bit_offs;
	u32 mem_offs;
	u32 nr_bits = nvgpu_pd_cache_get_nr_entries(pentry);

	/*
	 * Find and allocate an open PD.
	 */
	bit_offs = find_first_zero_bit(pentry->alloc_map, nr_bits);
	nvgpu_assert(bit_offs <= U32_MAX);
	mem_offs = (u32)bit_offs * pentry->pd_size;

	pd_dbg(g, "PD-Alloc [C]   Partial: offs=%lu nr_bits=%d src=0x%p",
	       bit_offs, nr_bits, pentry);

	/* Bit map full. Somethings wrong. */
	nvgpu_assert(bit_offs < nr_bits);

	set_bit((int)bit_offs, pentry->alloc_map);
	pentry->allocs += 1U;

	/*
	 * First update the pd.
	 */
	pd->mem = &pentry->mem;
	pd->mem_offs = mem_offs;
	pd->cached = true;

	/*
	 * Now make sure the pentry is in the correct list (full vs partial).
	 */
	if (pentry->allocs >= nr_bits) {
		pd_dbg(g, "Adding pentry to full list!");
		nvgpu_list_del(&pentry->list_entry);
		nvgpu_list_add(&pentry->list_entry,
			&cache->full[nvgpu_pd_cache_nr(pentry->pd_size)]);
	}

	return 0;
}

/*
 * Get a partially full nvgpu_pd_mem_entry. Returns NULL if there is no partial
 * nvgpu_pd_mem_entry's.
 */
static struct nvgpu_pd_mem_entry *nvgpu_pd_cache_get_partial(
	struct nvgpu_pd_cache *cache, u32 bytes)
{
	struct nvgpu_list_node *list =
		&cache->partial[nvgpu_pd_cache_nr(bytes)];

	if (nvgpu_list_empty(list)) {
		return NULL;
	}

	return nvgpu_list_first_entry(list,
				      nvgpu_pd_mem_entry,
				      list_entry);
}

/*
 * Allocate memory from an nvgpu_mem for the page directory.
 */
static int nvgpu_pd_cache_alloc(struct gk20a *g, struct nvgpu_pd_cache *cache,
				struct nvgpu_gmmu_pd *pd, u32 bytes)
{
	struct nvgpu_pd_mem_entry *pentry;
	int err;

	pd_dbg(g, "PD-Alloc [C] %u bytes", bytes);

	if ((bytes & (bytes - 1U)) != 0U ||
	     bytes < NVGPU_PD_CACHE_MIN) {
		pd_dbg(g, "PD-Alloc [C]   Invalid (bytes=%u)!", bytes);
		return -EINVAL;
	}

	nvgpu_assert(bytes < PAGE_SIZE);

	pentry = nvgpu_pd_cache_get_partial(cache, bytes);
	if (pentry == NULL) {
		err = nvgpu_pd_cache_alloc_new(g, cache, pd, bytes);
	} else {
		err = nvgpu_pd_cache_alloc_from_partial(g, cache, pentry, pd);
	}

	if (err != 0) {
		nvgpu_err(g, "PD-Alloc [C] Failed!");
	}

	return err;
}

/*
 * Allocate the DMA memory for a page directory. This handles the necessary PD
 * cache logistics. Since on Parker and later GPUs some of the page  directories
 * are smaller than a page packing these PDs together saves a lot of memory.
 */
int nvgpu_pd_alloc(struct vm_gk20a *vm, struct nvgpu_gmmu_pd *pd, u32 bytes)
{
	struct gk20a *g = gk20a_from_vm(vm);
	int err;

	/*
	 * Simple case: PD is bigger than a page so just do a regular DMA
	 * alloc.
	 */
	if (bytes >= PAGE_SIZE) {
		err = nvgpu_pd_cache_alloc_direct(g, pd, bytes);
		if (err != 0) {
			return err;
		}
		pd->pd_size = bytes;

		return 0;
	}

	if (WARN_ON(g->mm.pd_cache == NULL)) {
		return -ENOMEM;
	}

	nvgpu_mutex_acquire(&g->mm.pd_cache->lock);
	err = nvgpu_pd_cache_alloc(g, g->mm.pd_cache, pd, bytes);
	pd->pd_size = bytes;
	nvgpu_mutex_release(&g->mm.pd_cache->lock);

	return err;
}

static void nvgpu_pd_cache_free_direct(struct gk20a *g,
				       struct nvgpu_gmmu_pd *pd)
{
	pd_dbg(g, "PD-Free  [D] 0x%p", pd->mem);

	if (pd->mem == NULL) {
		return;
	}

	nvgpu_dma_free(g, pd->mem);
	nvgpu_kfree(g, pd->mem);
	pd->mem = NULL;
}

static void nvgpu_pd_cache_free_mem_entry(struct gk20a *g,
					  struct nvgpu_pd_cache *cache,
					  struct nvgpu_pd_mem_entry *pentry)
{
	nvgpu_dma_free(g, &pentry->mem);
	nvgpu_list_del(&pentry->list_entry);
	nvgpu_rbtree_unlink(&pentry->tree_entry, &cache->mem_tree);
	nvgpu_kfree(g, pentry);
}

static void nvgpu_pd_cache_do_free(struct gk20a *g,
				   struct nvgpu_pd_cache *cache,
				   struct nvgpu_pd_mem_entry *pentry,
				   struct nvgpu_gmmu_pd *pd)
{
	u32 bit = pd->mem_offs / pentry->pd_size;

	/* Mark entry as free. */
	clear_bit((int)bit, pentry->alloc_map);
	pentry->allocs -= 1U;

	if (pentry->allocs > 0U) {
		/*
		 * Partially full still. If it was already on the partial list
		 * this just re-adds it.
		 */
		nvgpu_list_del(&pentry->list_entry);
		nvgpu_list_add(&pentry->list_entry,
			&cache->partial[nvgpu_pd_cache_nr(pentry->pd_size)]);
	} else {
		/* Empty now so free it. */
		nvgpu_pd_cache_free_mem_entry(g, cache, pentry);
	}

	pd->mem = NULL;
}

static struct nvgpu_pd_mem_entry *nvgpu_pd_cache_look_up(
	struct gk20a *g,
	struct nvgpu_pd_cache *cache,
	struct nvgpu_gmmu_pd *pd)
{
	struct nvgpu_rbtree_node *node = NULL;

	nvgpu_rbtree_search((u64)(uintptr_t)pd->mem, &node,
			    cache->mem_tree);
	if (node == NULL) {
		return NULL;
	}

	return nvgpu_pd_mem_entry_from_tree_entry(node);
}

static void nvgpu_pd_cache_free(struct gk20a *g, struct nvgpu_pd_cache *cache,
				struct nvgpu_gmmu_pd *pd)
{
	struct nvgpu_pd_mem_entry *pentry;

	pd_dbg(g, "PD-Free  [C] 0x%p", pd->mem);

	pentry = nvgpu_pd_cache_look_up(g, cache, pd);
	if (pentry == NULL) {
		(void) WARN(true, "Attempting to free non-existent pd");
		return;
	}

	nvgpu_pd_cache_do_free(g, cache, pentry, pd);
}

void nvgpu_pd_free(struct vm_gk20a *vm, struct nvgpu_gmmu_pd *pd)
{
	struct gk20a *g = gk20a_from_vm(vm);

	/*
	 * Simple case: just DMA free.
	 */
	if (!pd->cached) {
		return nvgpu_pd_cache_free_direct(g, pd);
	}

	nvgpu_mutex_acquire(&g->mm.pd_cache->lock);
	nvgpu_pd_cache_free(g, g->mm.pd_cache, pd);
	nvgpu_mutex_release(&g->mm.pd_cache->lock);
}
