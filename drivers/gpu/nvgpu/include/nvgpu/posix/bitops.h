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

#ifndef NVGPU_POSIX_BITOPS_H
#define NVGPU_POSIX_BITOPS_H

#include <nvgpu/types.h>

/*
 * Assume an 8 bit byte, of course.
 */
#define BITS_PER_BYTE	8UL
#define BITS_PER_LONG 	((unsigned long)__SIZEOF_LONG__ * BITS_PER_BYTE)
#define BITS_TO_LONGS(bits)			\
	(nvgpu_safe_add_u64(bits, BITS_PER_LONG - 1UL) / BITS_PER_LONG)

/*
 * Deprecated; use the explicit BITxx() macros instead.
 */
#define BIT(i)		BIT64(i)

#define GENMASK(hi, lo) \
	(((~0UL) - (1UL << (lo)) + 1UL) &	\
		(~0UL >> (BITS_PER_LONG - 1UL - (unsigned long)(hi))))

/*
 * Can't use BITS_TO_LONGS to declare arrays where we can't use BUG(), so if the
 * range is invalid, use -1 for the size which will generate a compiler error.
 */
#define DECLARE_BITMAP(bmap, bits)					\
	unsigned long bmap[(((LONG_MAX - (bits)) < (BITS_PER_LONG - 1UL)) ? \
		-1 :							\
		(long int)(((bits) + BITS_PER_LONG - 1UL) /		\
						BITS_PER_LONG))]

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size));		\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1U))

unsigned long nvgpu_posix_ffs(unsigned long word);
unsigned long nvgpu_posix_fls(unsigned long word);

#define nvgpu_ffs(word)	nvgpu_posix_ffs(word)
#define nvgpu_fls(word)	nvgpu_posix_fls(word)

#define ffz(word)	(nvgpu_ffs(~(word)) - 1UL)

unsigned long find_first_bit(const unsigned long *addr, unsigned long size);
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset);
unsigned long find_first_zero_bit(const unsigned long *addr,
				  unsigned long size);

bool nvgpu_test_bit(unsigned int nr, const volatile unsigned long *addr);
bool nvgpu_test_and_set_bit(unsigned int nr, volatile unsigned long *addr);
bool nvgpu_test_and_clear_bit(unsigned int nr, volatile unsigned long *addr);

/*
 * These two are atomic.
 */
void nvgpu_set_bit(unsigned int nr, volatile unsigned long *addr);
void nvgpu_clear_bit(unsigned int nr, volatile unsigned long *addr);

void nvgpu_bitmap_set(unsigned long *map, unsigned int start, unsigned int len);
void nvgpu_bitmap_clear(unsigned long *map, unsigned int start, unsigned int len);
unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset);
unsigned long bitmap_find_next_zero_area(unsigned long *map,
					 unsigned long size,
					 unsigned long start,
					 unsigned int nr,
					 unsigned long align_mask);

#endif /* NVGPU_POSIX_BITOPS_H */
