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

#include <nvgpu/types.h>
#include <nvgpu/soc.h>
#include <nvgpu/io.h>
#include <nvgpu/utils.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/netlist.h>
#include <nvgpu/gr/global_ctx.h>
#include <nvgpu/gr/ctx.h>
#include <nvgpu/gr/config.h>
#include <nvgpu/gr/gr.h>

#include "gk20a/gr_gk20a.h"
#include "gk20a/gr_pri_gk20a.h"

#include "gp10b/gr_gp10b.h"

#include "gv11b/gr_gv11b.h"

#include "tu104/gr_tu104.h"

#include <nvgpu/hw/tu104/hw_gr_tu104.h>

bool gr_tu104_is_valid_class(struct gk20a *g, u32 class_num)
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

	return gr_gv11b_is_valid_class(g, class_num);
};

bool gr_tu104_is_valid_gfx_class(struct gk20a *g, u32 class_num)
{
	nvgpu_speculation_barrier();
	switch (class_num) {
	case TURING_A:
		return true;
	default:
		break;
	}

	return gr_gv11b_is_valid_gfx_class(g, class_num);
}

bool gr_tu104_is_valid_compute_class(struct gk20a *g, u32 class_num)
{
	nvgpu_speculation_barrier();
	switch (class_num) {
	case TURING_COMPUTE_A:
		return true;
	default:
		break;
	}

	return gr_gv11b_is_valid_compute_class(g, class_num);
}

int gr_tu104_init_sw_bundle64(struct gk20a *g)
{
	u32 i;
	u32 last_bundle_data_lo = 0;
	u32 last_bundle_data_hi = 0;
	int err = 0;
	struct netlist_av64_list *sw_bundle64_init =
			&g->netlist_vars->sw_bundle64_init;

	for (i = 0U; i < sw_bundle64_init->count; i++) {
		if (i == 0U ||
		   (last_bundle_data_lo != sw_bundle64_init->l[i].value_lo) ||
		   (last_bundle_data_hi != sw_bundle64_init->l[i].value_hi)) {
			nvgpu_writel(g, gr_pipe_bundle_data_r(),
				sw_bundle64_init->l[i].value_lo);
			nvgpu_writel(g, gr_pipe_bundle_data_hi_r(),
				sw_bundle64_init->l[i].value_hi);

			last_bundle_data_lo = sw_bundle64_init->l[i].value_lo;
			last_bundle_data_hi = sw_bundle64_init->l[i].value_hi;
		}

		nvgpu_writel(g, gr_pipe_bundle_address_r(),
			sw_bundle64_init->l[i].addr);

		if (gr_pipe_bundle_address_value_v(sw_bundle64_init->l[i].addr)
				== GR_GO_IDLE_BUNDLE) {
			err = g->ops.gr.init.wait_idle(g);
		} else if (nvgpu_platform_is_silicon(g)) {
			err = g->ops.gr.init.wait_fe_idle(g);
		}
		if (err != 0) {
			break;
		}
	}

	return err;
}

int gr_tu104_init_gfxp_rtv_cb(struct gk20a *g,
		  struct nvgpu_gr_ctx *gr_ctx, struct vm_gk20a *vm)
{
	u32 rtv_cb_size;

	nvgpu_log_fn(g, " ");

	rtv_cb_size =
		(gr_scc_rm_rtv_cb_size_div_256b_default_f() +
		gr_scc_rm_rtv_cb_size_div_256b_db_adder_f() +
		gr_scc_rm_rtv_cb_size_div_256b_gfxp_adder_f()) *
		gr_scc_rm_rtv_cb_size_div_256b_byte_granularity_v();

	nvgpu_gr_ctx_set_size(g->gr.gr_ctx_desc,
		NVGPU_GR_CTX_GFXP_RTVCB_CTXSW, rtv_cb_size);

	return 0;
}

void gr_tu104_bundle_cb_defaults(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;

	gr->bundle_cb_default_size =
		gr_scc_bundle_cb_size_div_256b__prod_v();
	gr->min_gpm_fifo_depth =
		gr_pd_ab_dist_cfg2_state_limit_min_gpm_fifo_depths_v();
	gr->bundle_cb_token_limit =
		gr_pd_ab_dist_cfg2_token_limit_init_v();
}

void gr_tu104_cb_size_default(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;

	if (gr->attrib_cb_default_size == 0U) {
		gr->attrib_cb_default_size =
			gr_gpc0_ppc0_cbm_beta_cb_size_v_default_v();
	}
	gr->alpha_cb_default_size =
		gr_gpc0_ppc0_cbm_alpha_cb_size_v_default_v();
	gr->attrib_cb_gfxp_default_size =
		gr_gpc0_ppc0_cbm_beta_cb_size_v_gfxp_v();
	gr->attrib_cb_gfxp_size =
		gr_gpc0_ppc0_cbm_beta_cb_size_v_gfxp_v();
}

void gr_tu104_enable_gpc_exceptions(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;
	u32 tpc_mask;

	gk20a_writel(g, gr_gpcs_tpcs_tpccs_tpc_exception_en_r(),
			gr_gpcs_tpcs_tpccs_tpc_exception_en_sm_enabled_f());

	tpc_mask =
		gr_gpcs_gpccs_gpc_exception_en_tpc_f(
			BIT32(nvgpu_gr_config_get_max_tpc_per_gpc_count(gr->config)) - 1U);

	gk20a_writel(g, gr_gpcs_gpccs_gpc_exception_en_r(),
		(tpc_mask | gr_gpcs_gpccs_gpc_exception_en_gcc_f(1U) |
			    gr_gpcs_gpccs_gpc_exception_en_gpccs_f(1U) |
			    gr_gpcs_gpccs_gpc_exception_en_gpcmmu_f(1U)));
}

int gr_tu104_get_offset_in_gpccs_segment(struct gk20a *g,
					enum ctxsw_addr_type addr_type,
					u32 num_tpcs,
					u32 num_ppcs,
					u32 reg_list_ppc_count,
					u32 *__offset_in_segment)
{
	u32 offset_in_segment = 0;
	u32 num_pes_per_gpc = nvgpu_get_litter_value(g,
				GPU_LIT_NUM_PES_PER_GPC);

	if (addr_type == CTXSW_ADDR_TYPE_TPC) {
		/*
		 * reg = g->netlist_vars->ctxsw_regs.tpc.l;
		 * offset_in_segment = 0;
		 */
	} else if (addr_type == CTXSW_ADDR_TYPE_PPC) {
		/*
		 * The ucode stores TPC data before PPC data.
		 * Advance offset past TPC data to PPC data.
		 */
		offset_in_segment =
			((g->netlist_vars->ctxsw_regs.tpc.count *
				num_tpcs) << 2);
	} else if (addr_type == CTXSW_ADDR_TYPE_GPC) {
		/*
		 * The ucode stores TPC/PPC data before GPC data.
		 * Advance offset past TPC/PPC data to GPC data.
		 *
		 * Note 1 PES_PER_GPC case
		 */
		if (num_pes_per_gpc > 1U) {
			offset_in_segment =
				(((g->netlist_vars->ctxsw_regs.tpc.count *
					num_tpcs) << 2) +
				((reg_list_ppc_count * num_ppcs) << 2));
		} else {
			offset_in_segment =
				((g->netlist_vars->ctxsw_regs.tpc.count *
					num_tpcs) << 2);
		}
	} else if ((addr_type == CTXSW_ADDR_TYPE_EGPC) ||
			(addr_type == CTXSW_ADDR_TYPE_ETPC)) {
		if (num_pes_per_gpc > 1U) {
			offset_in_segment =
				((g->netlist_vars->ctxsw_regs.tpc.count *
					num_tpcs) << 2) +
				((reg_list_ppc_count * num_ppcs) << 2) +
				(g->netlist_vars->ctxsw_regs.gpc.count << 2);
		} else {
			offset_in_segment =
				((g->netlist_vars->ctxsw_regs.tpc.count *
					num_tpcs) << 2) +
				(g->netlist_vars->ctxsw_regs.gpc.count << 2);
		}

		/* aligned to next 256 byte */
		offset_in_segment = ALIGN(offset_in_segment, 256);

		nvgpu_log(g, gpu_dbg_info | gpu_dbg_gpu_dbg,
			"egpc etpc offset_in_segment 0x%#08x",
			offset_in_segment);
	} else {
		nvgpu_log_fn(g, "Unknown address type.");
		return -EINVAL;
	}

	*__offset_in_segment = offset_in_segment;
	return 0;
}

static void gr_tu104_set_sm_disp_ctrl(struct gk20a *g, u32 data)
{
	u32 reg_val;

	nvgpu_log_fn(g, " ");

	reg_val = nvgpu_readl(g, gr_gpcs_tpcs_sm_disp_ctrl_r());

	if ((data & NVC5C0_SET_SM_DISP_CTRL_COMPUTE_SHADER_QUAD_MASK)
		       == NVC5C0_SET_SM_DISP_CTRL_COMPUTE_SHADER_QUAD_DISABLE) {
		reg_val = set_field(reg_val,
			gr_gpcs_tpcs_sm_disp_ctrl_compute_shader_quad_m(),
			gr_gpcs_tpcs_sm_disp_ctrl_compute_shader_quad_disable_f()
			);
	} else if ((data & NVC5C0_SET_SM_DISP_CTRL_COMPUTE_SHADER_QUAD_MASK)
			== NVC5C0_SET_SM_DISP_CTRL_COMPUTE_SHADER_QUAD_ENABLE) {
		reg_val = set_field(reg_val,
			gr_gpcs_tpcs_sm_disp_ctrl_compute_shader_quad_m(),
			gr_gpcs_tpcs_sm_disp_ctrl_compute_shader_quad_enable_f()
			);
	}

	nvgpu_writel(g, gr_gpcs_tpcs_sm_disp_ctrl_r(), reg_val);
}

int gr_tu104_handle_sw_method(struct gk20a *g, u32 addr,
			      u32 class_num, u32 offset, u32 data)
{
	nvgpu_log_fn(g, " ");

	if (class_num == TURING_COMPUTE_A) {
		switch (offset << 2) {
		case NVC5C0_SET_SHADER_EXCEPTIONS:
			gv11b_gr_set_shader_exceptions(g, data);
			break;
		case NVC5C0_SET_SKEDCHECK:
			gr_gv11b_set_skedcheck(g, data);
			break;
		case NVC5C0_SET_SM_DISP_CTRL:
			gr_tu104_set_sm_disp_ctrl(g, data);
			break;
		case NVC5C0_SET_SHADER_CUT_COLLECTOR:
			gr_gv11b_set_shader_cut_collector(g, data);
			break;
		default:
			goto fail;
		}
	}

	if (class_num == TURING_A) {
		switch (offset << 2) {
		case NVC597_SET_SHADER_EXCEPTIONS:
			gv11b_gr_set_shader_exceptions(g, data);
			break;
		case NVC597_SET_CIRCULAR_BUFFER_SIZE:
			g->ops.gr.set_circular_buffer_size(g, data);
			break;
		case NVC597_SET_ALPHA_CIRCULAR_BUFFER_SIZE:
			g->ops.gr.set_alpha_circular_buffer_size(g, data);
			break;
		case NVC597_SET_GO_IDLE_TIMEOUT:
			gr_gv11b_set_go_idle_timeout(g, data);
			break;
		case NVC097_SET_COALESCE_BUFFER_SIZE:
			gr_gv11b_set_coalesce_buffer_size(g, data);
			break;
		case NVC597_SET_TEX_IN_DBG:
			gr_gv11b_set_tex_in_dbg(g, data);
			break;
		case NVC597_SET_SKEDCHECK:
			gr_gv11b_set_skedcheck(g, data);
			break;
		case NVC597_SET_BES_CROP_DEBUG3:
			g->ops.gr.set_bes_crop_debug3(g, data);
			break;
		case NVC597_SET_BES_CROP_DEBUG4:
			g->ops.gr.set_bes_crop_debug4(g, data);
			break;
		case NVC597_SET_SM_DISP_CTRL:
			gr_tu104_set_sm_disp_ctrl(g, data);
			break;
		case NVC597_SET_SHADER_CUT_COLLECTOR:
			gr_gv11b_set_shader_cut_collector(g, data);
			break;
		default:
			goto fail;
		}
	}
	return 0;

fail:
	return -EINVAL;
}

void gr_tu104_init_sm_dsm_reg_info(void)
{
	return;
}

void gr_tu104_get_sm_dsm_perf_ctrl_regs(struct gk20a *g,
				        u32 *num_sm_dsm_perf_ctrl_regs,
				        u32 **sm_dsm_perf_ctrl_regs,
				        u32 *ctrl_register_stride)
{
	*num_sm_dsm_perf_ctrl_regs = 0;
	*sm_dsm_perf_ctrl_regs = NULL;
	*ctrl_register_stride = 0;
}

void gr_tu104_log_mme_exception(struct gk20a *g)
{
	u32 mme_hww_esr = nvgpu_readl(g, gr_mme_hww_esr_r());
	u32 mme_hww_info = nvgpu_readl(g, gr_mme_hww_esr_info_r());

	if ((mme_hww_esr &
	     gr_mme_hww_esr_missing_macro_data_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: MISSING_MACRO_DATA");
	}

	if ((mme_hww_esr &
	     gr_mme_hww_esr_illegal_mme_method_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: ILLEGAL_MME_METHOD");
	}

	if ((mme_hww_esr &
	     gr_mme_hww_esr_dma_dram_access_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: DMA_DRAM_ACCESS_OUT_OF_BOUNDS");
	}

	if ((mme_hww_esr &
	     gr_mme_hww_esr_dma_illegal_fifo_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: DMA_ILLEGAL_FIFO_CONFIG");
	}

	if ((mme_hww_esr &
	     gr_mme_hww_esr_dma_read_overflow_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: DMA_READ_FIFOED_OVERFLOW");
	}

	if ((mme_hww_esr &
	     gr_mme_hww_esr_dma_fifo_resized_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: DMA_FIFO_RESIZED_WHEN_NONIDLE");
	}

	if ((mme_hww_esr & gr_mme_hww_esr_illegal_opcode_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: ILLEGAL_OPCODE");
	}

	if ((mme_hww_esr & gr_mme_hww_esr_branch_in_delay_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: BRANCH_IN_DELAY_SHOT");
	}

	if ((mme_hww_esr & gr_mme_hww_esr_inst_ram_acess_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: INSTR_RAM_ACCESS_OUT_OF_BOUNDS");
	}

	if ((mme_hww_esr & gr_mme_hww_esr_data_ram_access_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: DATA_RAM_ACCESS_OUT_OF_BOUNDS");
	}

	if ((mme_hww_esr & gr_mme_hww_esr_dma_read_pb_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: DMA_READ_FIFOED_FROM_PB");
	}

	if (gr_mme_hww_esr_info_pc_valid_v(mme_hww_info) == 0x1U) {
		nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
			 "GR MME EXCEPTION: INFO2 0x%x, INFO3 0x%x, INFO4 0x%x",
			 nvgpu_readl(g, gr_mme_hww_esr_info2_r()),
			 nvgpu_readl(g, gr_mme_hww_esr_info3_r()),
			 nvgpu_readl(g, gr_mme_hww_esr_info4_r()));
	}
}