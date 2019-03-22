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
#include <nvgpu/gr/ctx.h>

#include "gr_init_tu104.h"

#include <nvgpu/hw/tu104/hw_gr_tu104.h>

u32 tu104_gr_init_get_rtv_cb_size(struct gk20a *g)
{
	return (gr_scc_rm_rtv_cb_size_div_256b_default_f() +
			gr_scc_rm_rtv_cb_size_div_256b_db_adder_f()) *
		gr_scc_bundle_cb_size_div_256b_byte_granularity_v();
}

static void tu104_gr_init_patch_rtv_cb(struct gk20a *g,
	struct nvgpu_gr_ctx *gr_ctx,
	u64 addr, u32 size, u32 gfxpAddSize, bool patch)
{
	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_scc_rm_rtv_cb_base_r(),
		gr_scc_rm_rtv_cb_base_addr_39_8_f(addr), patch);
	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_scc_rm_rtv_cb_size_r(),
		gr_scc_rm_rtv_cb_size_div_256b_f(size), patch);
	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_gpcs_gcc_rm_rtv_cb_base_r(),
		gr_gpcs_gcc_rm_rtv_cb_base_addr_39_8_f(addr), patch);
	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_scc_rm_gfxp_reserve_r(),
		gr_scc_rm_gfxp_reserve_rtv_cb_size_div_256b_f(gfxpAddSize),
		patch);
}

void tu104_gr_init_commit_rtv_cb(struct gk20a *g, u64 addr,
	struct nvgpu_gr_ctx *gr_ctx, bool patch)
{
	u32 size;

	addr = addr >> U64(gr_scc_rm_rtv_cb_base_addr_39_8_align_bits_f());
	size = (gr_scc_rm_rtv_cb_size_div_256b_default_f() +
			gr_scc_rm_rtv_cb_size_div_256b_db_adder_f());

	tu104_gr_init_patch_rtv_cb(g, gr_ctx, addr, size, 0, patch);
}

void tu104_gr_init_commit_gfxp_rtv_cb(struct gk20a *g,
	struct nvgpu_gr_ctx *gr_ctx, bool patch)
{
	u64 addr;
	u64 addr_lo;
	u64 addr_hi;
	u32 rtv_cb_size;
	u32 gfxp_addr_size;

	nvgpu_log_fn(g, " ");

	rtv_cb_size =
		(gr_scc_rm_rtv_cb_size_div_256b_default_f() +
		gr_scc_rm_rtv_cb_size_div_256b_db_adder_f() +
		gr_scc_rm_rtv_cb_size_div_256b_gfxp_adder_f());
	gfxp_addr_size = gr_scc_rm_rtv_cb_size_div_256b_gfxp_adder_f();

	/* GFXP RTV circular buffer */
	addr_lo = (u64)(u64_lo32(gr_ctx->gfxp_rtvcb_ctxsw_buffer.gpu_va) >>
	       gr_scc_rm_rtv_cb_base_addr_39_8_align_bits_f());
	addr_hi = (u64)(u64_hi32(gr_ctx->gfxp_rtvcb_ctxsw_buffer.gpu_va));
	addr = addr_lo |
	       (addr_hi <<
	       (32U - gr_scc_rm_rtv_cb_base_addr_39_8_align_bits_f()));

	tu104_gr_init_patch_rtv_cb(g, gr_ctx, addr,
		rtv_cb_size, gfxp_addr_size, patch);
}

u32 tu104_gr_init_get_bundle_cb_default_size(struct gk20a *g)
{
	return gr_scc_bundle_cb_size_div_256b__prod_v();
}

u32 tu104_gr_init_get_min_gpm_fifo_depth(struct gk20a *g)
{
	return gr_pd_ab_dist_cfg2_state_limit_min_gpm_fifo_depths_v();
}

u32 tu104_gr_init_get_bundle_cb_token_limit(struct gk20a *g)
{
	return gr_pd_ab_dist_cfg2_token_limit_init_v();
}

u32 tu104_gr_init_get_attrib_cb_default_size(struct gk20a *g)
{
	return gr_gpc0_ppc0_cbm_beta_cb_size_v_default_v();
}

u32 tu104_gr_init_get_alpha_cb_default_size(struct gk20a *g)
{
	return gr_gpc0_ppc0_cbm_alpha_cb_size_v_default_v();
}

u32 tu104_gr_init_get_attrib_cb_gfxp_default_size(struct gk20a *g)
{
	return gr_gpc0_ppc0_cbm_beta_cb_size_v_gfxp_v();
}

u32 tu104_gr_init_get_attrib_cb_gfxp_size(struct gk20a *g)
{
	return gr_gpc0_ppc0_cbm_beta_cb_size_v_gfxp_v();
}

