/*
 * GK20A L2
 *
 * Copyright (c) 2011-2017, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/dma.h>

#include "gk20a.h"
#include "gr_gk20a.h"

int gk20a_ltc_alloc_phys_cbc(struct gk20a *g, size_t compbit_backing_size)
{
	struct gr_gk20a *gr = &g->gr;

	return nvgpu_dma_alloc_flags_sys(g, NVGPU_DMA_FORCE_CONTIGUOUS,
				    compbit_backing_size,
				    &gr->compbit_store.mem);
}

int gk20a_ltc_alloc_virt_cbc(struct gk20a *g, size_t compbit_backing_size)
{
	struct gr_gk20a *gr = &g->gr;

	return nvgpu_dma_alloc_flags_sys(g, NVGPU_DMA_NO_KERNEL_MAPPING,
				    compbit_backing_size,
				    &gr->compbit_store.mem);
}
