/*
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef NVGPU_STRING_H
#define NVGPU_STRING_H

#include <nvgpu/types.h>

#ifdef __KERNEL__
#include <linux/string.h>
#endif

/**
 * nvgpu_memcpy - Copy memory buffer
 *
 * @destb - Buffer into which data is to be copied.
 * @srcb - Buffer from which data is to be copied.
 * @n - Number of bytes to copy from src buffer to dest buffer.
 *
 * Copy memory from source buffer to destination buffer.
 */
void nvgpu_memcpy(u8 *destb, const u8 *srcb, size_t n);

/**
 * nvgpu_memcmp - Compare memory buffers
 *
 * @b1 - First buffer to use in memory comparison.
 * @b2 - Second buffer to use in memory comparison.
 * @n - Number of bytes to compare between buffer1 and buffer2.
 *
 * Compare the first n bytes of two memory buffers.  If the contents of the
 * two buffers match then zero is returned.  If the contents of b1 are less
 * than b2 then a value less than zero is returned.  If the contents of b1
 * are greater than b2 then a value greater than zero is returned.
 */
int nvgpu_memcmp(const u8 *b1, const u8 *b2, size_t n);

#endif /* NVGPU_STRING_H */
