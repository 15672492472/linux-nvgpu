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

#ifndef NVGPU_KMEM_H
#define NVGPU_KMEM_H

/**
 * @file
 *
 * Abstract interface for interacting with general kernel memory.
 */

#include <nvgpu/types.h>
#include <nvgpu/utils.h>

struct gk20a;

/*
 * When there's other implementations make sure they are included instead of
 * Linux when not compiling on Linux!
 */
#ifdef __KERNEL__
#include <nvgpu/linux/kmem.h>
#else
#include <nvgpu/posix/kmem.h>
#endif

/**
 * @file
 *
 * Kmem cache support
 * ------------------
 *
 * In Linux there is support for the notion of a kmem_cache. It gives better
 * memory usage characteristics for lots of allocations of the same size. Think
 * structs that get allocated over and over. Normal kmalloc() type routines
 * typically round to the next power-of-2 since that's easy.
 *
 * But if we know the size ahead of time the packing for the allocations can be
 * much better. This is the benefit of a slab allocator. This type hides the
 * underlying kmem_cache (or absense thereof).
 */
struct nvgpu_kmem_cache;

#ifdef CONFIG_NVGPU_TRACK_MEM_USAGE
/*
 * Uncomment this if you want to enable stack traces in the memory profiling.
 * Since this is a fairly high overhead operation and is only necessary for
 * debugging actual bugs it's left here for developers to enable.
 */
/* #define __NVGPU_SAVE_KALLOC_STACK_TRACES */

/*
 * Defined per-OS.
 */
struct nvgpu_mem_alloc_tracker;
#endif


/**
 * nvgpu_kmem_cache_create - create an nvgpu kernel memory cache.
 *
 * @param g	The GPU driver struct using this cache.
 * @param size	Size of the object allocated by the cache.
 *
 * This cache can be used to allocate objects of size #size. Common usage would
 * be for a struct that gets allocated a lot. In that case #size should be
 * sizeof(struct my_struct).
 *
 * A given implementation of this need not do anything special. The allocation
 * routines can simply be passed on to nvgpu_kzalloc() if desired so packing
 * and alignment of the structs cannot be assumed.
 */
struct nvgpu_kmem_cache *nvgpu_kmem_cache_create(struct gk20a *g, size_t size);

/**
 * nvgpu_kmem_cache_destroy - destroy a cache created by
 *                            nvgpu_kmem_cache_create().
 *
 * @param cache	The cache to destroy.
 */
void nvgpu_kmem_cache_destroy(struct nvgpu_kmem_cache *cache);

/**
 * nvgpu_kmem_cache_alloc - Allocate an object from the cache
 *
 * @param cache	The cache to alloc from.
 */
void *nvgpu_kmem_cache_alloc(struct nvgpu_kmem_cache *cache);

/**
 * nvgpu_kmem_cache_free - Free an object back to a cache
 *
 * @param cache	The cache to return the object to.
 * @param ptr	Pointer to the object to free.
 */
void nvgpu_kmem_cache_free(struct nvgpu_kmem_cache *cache, void *ptr);

/**
 * nvgpu_kmalloc - Allocate from the kernel's allocator.
 *
 * @param g	Current GPU.
 * @param size	Size of the allocation.
 *
 * Allocate a chunk of system memory from the kernel. Allocations larger than 1
 * page may fail even when there may appear to be enough memory.
 *
 * This function may sleep so cannot be used in IRQs.
 */
#define nvgpu_kmalloc(g, size)	nvgpu_kmalloc_impl(g, size, NVGPU_GET_IP)

/**
 * nvgpu_kzalloc - Allocate from the kernel's allocator.
 *
 * @param g	Current GPU.
 * @param size	Size of the allocation.
 *
 * Identical to nvgpu_kalloc() except the memory will be zeroed before being
 * returned.
 */
#define nvgpu_kzalloc(g, size)	nvgpu_kzalloc_impl(g, size, NVGPU_GET_IP)

/**
 * nvgpu_kcalloc - Allocate from the kernel's allocator.
 *
 * @param g	Current GPU.
 * @param n	Number of objects.
 * @param size	Size of each object.
 *
 * Identical to nvgpu_kalloc() except the size of the memory chunk returned is
 * #n * #size.
 */
#define nvgpu_kcalloc(g, n, size)	\
	nvgpu_kcalloc_impl(g, n, size, NVGPU_GET_IP)

/**
 * nvgpu_vmalloc - Allocate memory and return a map to it.
 *
 * @param g	Current GPU.
 * @param size	Size of the allocation.
 *
 * Allocate some memory and return a pointer to a virtual memory mapping of
 * that memory in the kernel's virtual address space. The underlying physical
 * memory is not guaranteed to be contiguous (and indeed likely isn't). This
 * allows for much larger allocations to be done without worrying about as much
 * about physical memory fragmentation.
 *
 * This function may sleep.
 */
#define nvgpu_vmalloc(g, size)	nvgpu_vmalloc_impl(g, size, NVGPU_GET_IP)

/**
 * nvgpu_vzalloc - Allocate memory and return a map to it.
 *
 * @param g	Current GPU.
 * @param size	Size of the allocation.
 *
 * Identical to nvgpu_vmalloc() except this will return zero'ed memory.
 */
#define nvgpu_vzalloc(g, size)	nvgpu_vzalloc_impl(g, size, NVGPU_GET_IP)

/**
 * nvgpu_kfree - Frees an alloc from nvgpu_kmalloc, nvgpu_kzalloc,
 *               nvgpu_kcalloc.
 *
 * @param g	Current GPU.
 * @param addr	Address of object to free.
 */
#define nvgpu_kfree(g, addr)	nvgpu_kfree_impl(g, addr)

/**
 * nvgpu_vfree - Frees an alloc from nvgpu_vmalloc, nvgpu_vzalloc.
 *
 * @param g	Current GPU.
 * @param addr	Address of object to free.
 */
#define nvgpu_vfree(g, addr)	nvgpu_vfree_impl(g, addr)

#define kmem_dbg(g, fmt, args...)		\
	nvgpu_log(g, gpu_dbg_kmem, fmt, ##args)

/**
 * nvgpu_kmem_init - Initialize the kmem tracking stuff.
 *
 * @param g	The driver to init.
 *
 * Returns non-zero on failure.
 */
int nvgpu_kmem_init(struct gk20a *g);

/**
 * nvgpu_kmem_fini - Finalize the kmem tracking code
 *
 * @param g	The GPU.
 * @param flags	Flags that control operation of this finalization.
 *
 * Cleanup resources used by nvgpu_kmem. Available flags for cleanup are:
 *
 *   - NVGPU_KMEM_FINI_DO_NOTHING
 *   - NVGPU_KMEM_FINI_FORCE_CLEANUP
 *   - NVGPU_KMEM_FINI_DUMP_ALLOCS
 *   - NVGPU_KMEM_FINI_WARN
 *   - NVGPU_KMEM_FINI_BUG
 *
 * NVGPU_KMEM_FINI_DO_NOTHING will be overridden by anything else specified.
 * Put another way don't just add NVGPU_KMEM_FINI_DO_NOTHING and expect that
 * to suppress other flags from doing anything.
 */
void nvgpu_kmem_fini(struct gk20a *g, int flags);

/*
 * These will simply be ignored if CONFIG_NVGPU_TRACK_MEM_USAGE is not defined.
 */
#define NVGPU_KMEM_FINI_DO_NOTHING		0
#define NVGPU_KMEM_FINI_FORCE_CLEANUP		(1 << 0)
#define NVGPU_KMEM_FINI_DUMP_ALLOCS		(1 << 1)
#define NVGPU_KMEM_FINI_WARN			(1 << 2)
#define NVGPU_KMEM_FINI_BUG			(1 << 3)

/*
 * Implemented by the OS interface.
 */
void *nvgpu_big_alloc_impl(struct gk20a *g, size_t size, bool clear);

/**
 * nvgpu_big_malloc - Pick virtual or physical alloc based on @size
 *
 * @param g	The GPU.
 * @param size	Size of the allocation.
 *
 * On some platforms (i.e Linux) it is possible to allocate memory directly
 * mapped into the kernel's address space (kmalloc) or allocate discontiguous
 * pages which are then mapped into a special kernel address range. Each type
 * of allocation has pros and cons. kmalloc() for instance lets you allocate
 * small buffers more space efficiently but vmalloc() allows you to successfully
 * allocate much larger buffers without worrying about fragmentation as much
 * (but will allocate in multiples of page size).
 *
 * This function aims to provide the right allocation for when buffers are of
 * variable size. In some cases the code doesn't know ahead of time if the
 * buffer is going to be big or small so this does the check for you and
 * provides the right type of memory allocation.
 *
 * Returns a pointer to a virtual address range that the kernel can access or
 * %NULL on failure.
 */
static inline void *nvgpu_big_malloc(struct gk20a *g, size_t size)
{
	return nvgpu_big_alloc_impl(g, size, false);
}

/**
 * nvgpu_big_malloc - Pick virtual or physical alloc based on @size
 *
 * @param g	The GPU.
 * @param size	Size of the allocation.
 *
 * Zeroed memory version of nvgpu_big_malloc().
 */
static inline void *nvgpu_big_zalloc(struct gk20a *g, size_t size)
{
	return nvgpu_big_alloc_impl(g, size, true);
}

/**
 * nvgpu_big_free - Free and alloc from nvgpu_big_zalloc() or
 *                  nvgpu_big_malloc().
 * @param g	The GPU.
 * @param p	A pointer allocated by nvgpu_big_zalloc() or nvgpu_big_malloc().
 */
void nvgpu_big_free(struct gk20a *g, void *p);

#endif /* NVGPU_KMEM_H */
