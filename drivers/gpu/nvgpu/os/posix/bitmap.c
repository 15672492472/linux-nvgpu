/*
 * Copyright (c) 2018-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <nvgpu/posix/utils.h>
#include <nvgpu/posix/bitops.h>
#include <nvgpu/posix/atomic.h>

#define BIT_MASK(nr)	(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)	((nr) / BITS_PER_LONG)

unsigned long nvgpu_posix_ffs(unsigned long word)
{
	int ret = 0;

	if ((word & (unsigned long) LONG_MAX) != 0UL) {
		ret = __builtin_ffsl(
			nvgpu_safe_cast_u64_to_s64(
				(word & (unsigned long) LONG_MAX)));
	} else {
		if (word > (unsigned long) LONG_MAX) {
			ret = (int) 64;
		}
	}

	return nvgpu_safe_cast_s32_to_u64(ret);
}

unsigned long nvgpu_posix_fls(unsigned long word)
{
	unsigned long ret;

	if (word == 0UL) {
		/* __builtin_clzl() below is undefined for 0, so we have
		 * to handle that as a special case.
		 */
		ret = 0UL;
	} else {
		ret = (sizeof(unsigned long) * 8UL) -
			(nvgpu_safe_cast_s32_to_u64(__builtin_clzl(word)));
	}

	return ret;
}

static unsigned long nvgpu_posix_find_next_bit(const unsigned long *addr,
				     unsigned long n,
				     unsigned long start,
				     bool invert)
{
	unsigned long idx, idx_max;
	unsigned long w;
	unsigned long start_mask;

	/*
	 * We make a mask we can XOR into the word so that we can invert the
	 * word without requiring a branch. I.e instead of doing:
	 *
	 *   w = invert ? ~addr[idx] : addr[idx]
	 *
	 * We can do:
	 *
	 *   w = addr[idx] ^= invert_mask
	 *
	 * This saves us a branch every iteration through the loop. Now we can
	 * always just look for 1s.
	 */
	unsigned long invert_mask = invert ? ~0UL : 0UL;

	if (start >= n) {
		return n;
	}

	start_mask = ~0UL << (start & (BITS_PER_LONG - 1UL));

	idx = start / BITS_PER_LONG;
	w = (addr[idx] ^ invert_mask) & start_mask;

	start = round_up(start, BITS_PER_LONG);

	idx_max = (n - 1UL) / BITS_PER_LONG;

	/*
	 * Find the first non-zero word taking into account start and
	 * invert.
	 */
	while (w == 0UL) {
		idx = nvgpu_safe_add_u64(idx, 1UL);
		if (idx > idx_max) {
			return n;
		}

		start = nvgpu_safe_add_u64(start, BITS_PER_LONG);

		w = addr[idx] ^ invert_mask;
	}

	return min(n, (nvgpu_safe_add_u64(((ffs(w)) - 1UL),
				(nvgpu_safe_mult_u64(idx, BITS_PER_LONG)))));
}

unsigned long find_first_bit(const unsigned long *addr, unsigned long size)
{
	return nvgpu_posix_find_next_bit(addr, size, 0, false);
}

unsigned long find_first_zero_bit(const unsigned long *addr, unsigned long size)
{
	return nvgpu_posix_find_next_bit(addr, size, 0, true);
}

unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset)
{
	return nvgpu_posix_find_next_bit(addr, size, offset, false);
}

static unsigned long find_next_zero_bit(const unsigned long *addr,
					unsigned long size,
					unsigned long offset)
{
	return nvgpu_posix_find_next_bit(addr, size, offset, true);
}

void nvgpu_bitmap_set(unsigned long *map, unsigned int start, unsigned int len)
{
	unsigned int end = start + len;

	/*
	 * Super slow naive implementation. But speed isn't what matters here.
	 */
	while (start < end) {
		nvgpu_set_bit(start++, map);
	}
}

void nvgpu_bitmap_clear(unsigned long *map,
				unsigned int start, unsigned int len)
{
	unsigned int end = start + len;

	while (start < end) {
		nvgpu_clear_bit(start++, map);
	}
}

/*
 * This is essentially a find-first-fit allocator: this searches a bitmap for
 * the first space that is large enough to satisfy the requested size of bits.
 * That means that this is not a vary smart allocator. But it is fast relative
 * to an allocator that goes looking for an optimal location.
 */
unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset)
{
	unsigned long offs;

	while ((nvgpu_safe_add_u64(start, (unsigned long)nr)) <= size) {
		start = find_next_zero_bit(map, size, start);

		start = nvgpu_safe_sub_u64(
			ALIGN_MASK(nvgpu_safe_add_u64(start, align_offset),
				align_mask),
			align_offset);

		/*
		 * Not enough space left to satisfy the requested area.
		 */
		if ((nvgpu_safe_add_u64(start, (unsigned long)nr)) > size) {
			return size;
		}

		offs = find_next_bit(map, size, start);

		if ((offs - start) >= nr) {
			return start;
		}

		start = offs + 1UL;
	}

	return size;
}

unsigned long bitmap_find_next_zero_area(unsigned long *map,
					 unsigned long size,
					 unsigned long start,
					 unsigned int nr,
					 unsigned long align_mask)
{
	return bitmap_find_next_zero_area_off(map, size, start, nr,
					      align_mask, 0);
}

bool nvgpu_test_bit(unsigned int nr, const volatile unsigned long *addr)
{
	return (1UL & (addr[BIT_WORD(nr)] >>
			(nr & (BITS_PER_LONG-1UL)))) != 0UL;
}

bool nvgpu_test_and_set_bit(unsigned int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	volatile unsigned long *p = addr + BIT_WORD(nr);

	return (atomic_fetch_or(p, mask) & mask) != 0ULL;
}

bool nvgpu_test_and_clear_bit(unsigned int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	volatile unsigned long *p = addr + BIT_WORD(nr);

	return (atomic_fetch_and(p, ~mask) & mask) != 0ULL;
}

void nvgpu_set_bit(unsigned int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	volatile unsigned long *p = addr + BIT_WORD(nr);

	(void)atomic_fetch_or(p, mask);
}

void nvgpu_clear_bit(unsigned int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	volatile unsigned long *p = addr + BIT_WORD(nr);

	(void)atomic_fetch_and(p, ~mask);
}
