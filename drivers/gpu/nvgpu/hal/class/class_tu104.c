/*
 * Copyright (c) 2019 NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/class.h>
#include <nvgpu/barrier.h>

#include "class_gv11b.h"
#include "class_tu104.h"

bool tu104_class_is_valid(u32 class_num)
{
	nvgpu_speculation_barrier();
	switch (class_num) {
	case TURING_CHANNEL_GPFIFO_A:
	case TURING_A:
	case TURING_COMPUTE_A:
	case TURING_DMA_COPY_A:
		return true;
	default:
		break;
	}

	return gv11b_class_is_valid(class_num);
};

bool tu104_class_is_valid_gfx(u32 class_num)
{
	nvgpu_speculation_barrier();
	switch (class_num) {
	case TURING_A:
		return true;
	default:
		break;
	}

	return gv11b_class_is_valid_gfx(class_num);
}

bool tu104_class_is_valid_compute(u32 class_num)
{
	nvgpu_speculation_barrier();
	switch (class_num) {
	case TURING_COMPUTE_A:
		return true;
	default:
		break;
	}

	return gv11b_class_is_valid_compute(class_num);
}
