/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/gk20a.h>
#include <nvgpu/gr/subctx.h>
#include <nvgpu/gr/ctx.h>
#include <nvgpu/gmmu.h>

struct nvgpu_gr_subctx *nvgpu_gr_subctx_alloc(struct gk20a *g,
	struct vm_gk20a *vm)
{
	struct nvgpu_gr_subctx *subctx;
	int err = 0;

	nvgpu_log_fn(g, " ");

	subctx = nvgpu_kzalloc(g, sizeof(*subctx));
	if (subctx == NULL) {
		return NULL;
	}

	err = nvgpu_dma_alloc_sys(g,
			g->ops.gr.ctxsw_prog.hw_get_fecs_header_size(),
			&subctx->ctx_header);
	if (err != 0) {
		nvgpu_err(g, "failed to allocate sub ctx header");
		goto err_free_subctx;
	}

	subctx->ctx_header.gpu_va = nvgpu_gmmu_map(vm,
				&subctx->ctx_header,
				subctx->ctx_header.size,
				0, /* not GPU-cacheable */
				gk20a_mem_flag_none, true,
				subctx->ctx_header.aperture);
	if (subctx->ctx_header.gpu_va == 0ULL) {
		nvgpu_err(g, "failed to map ctx header");
		goto err_free_ctx_header;
	}

	return subctx;

err_free_ctx_header:
	nvgpu_dma_free(g, &subctx->ctx_header);
err_free_subctx:
	nvgpu_kfree(g, subctx);
	return NULL;
}

void nvgpu_gr_subctx_free(struct gk20a *g,
	struct nvgpu_gr_subctx *subctx,
	struct vm_gk20a *vm)
{
	nvgpu_log_fn(g, " ");

	nvgpu_gmmu_unmap(vm, &subctx->ctx_header,
		subctx->ctx_header.gpu_va);
	nvgpu_dma_free(g, &subctx->ctx_header);
	nvgpu_kfree(g, subctx);
}

void nvgpu_gr_subctx_load_ctx_header(struct gk20a *g,
	struct nvgpu_gr_subctx *subctx,
	struct nvgpu_gr_ctx *gr_ctx, u64 gpu_va)
{
	struct nvgpu_mem *ctxheader = &subctx->ctx_header;
	int err = 0;

	err = g->ops.mm.l2_flush(g, true);
	if (err != 0) {
		nvgpu_err(g, "l2_flush failed");
	}

	/* set priv access map */
	g->ops.gr.ctxsw_prog.set_priv_access_map_addr(g, ctxheader,
		nvgpu_gr_ctx_get_global_ctx_va(gr_ctx,
			NVGPU_GR_CTX_PRIV_ACCESS_MAP_VA));

	g->ops.gr.ctxsw_prog.set_patch_addr(g, ctxheader,
		gr_ctx->patch_ctx.mem.gpu_va);

	g->ops.gr.ctxsw_prog.set_pm_ptr(g, ctxheader,
		gr_ctx->pm_ctx.mem.gpu_va);
	g->ops.gr.ctxsw_prog.set_zcull_ptr(g, ctxheader,
		gr_ctx->zcull_ctx.gpu_va);

	g->ops.gr.ctxsw_prog.set_context_buffer_ptr(g, ctxheader, gpu_va);

	g->ops.gr.ctxsw_prog.set_type_per_veid_header(g, ctxheader);
}
