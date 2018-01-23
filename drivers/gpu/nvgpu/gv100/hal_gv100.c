/*
 * GV100 Tegra HAL interface
 *
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

#include <linux/types.h>
#include <linux/printk.h>

#include <linux/types.h>
#include <linux/tegra_gpu_t19x.h>

#include "gk20a/gk20a.h"
#include "gk20a/fifo_gk20a.h"
#include "gk20a/fecs_trace_gk20a.h"
#include "gk20a/css_gr_gk20a.h"
#include "gk20a/mc_gk20a.h"
#include "gk20a/dbg_gpu_gk20a.h"
#include "gk20a/bus_gk20a.h"
#include "gk20a/pramin_gk20a.h"
#include "gk20a/flcn_gk20a.h"
#include "gk20a/regops_gk20a.h"
#include "gk20a/fb_gk20a.h"
#include "gk20a/mm_gk20a.h"
#include "gk20a/pmu_gk20a.h"
#include "gk20a/gr_gk20a.h"

#include "gm20b/ltc_gm20b.h"
#include "gm20b/gr_gm20b.h"
#include "gm20b/fifo_gm20b.h"
#include "gm20b/fb_gm20b.h"
#include "gm20b/mm_gm20b.h"
#include "gm20b/pmu_gm20b.h"
#include "gm20b/acr_gm20b.h"

#include "gp10b/fb_gp10b.h"
#include "gp10b/gr_gp10b.h"

#include "gp106/clk_gp106.h"
#include "gp106/clk_arb_gp106.h"
#include "gp106/pmu_gp106.h"
#include "gp106/acr_gp106.h"
#include "gp106/sec2_gp106.h"
#include "gp106/bios_gp106.h"
#include "gv100/bios_gv100.h"
#include "gp106/therm_gp106.h"
#include "gp106/xve_gp106.h"
#include "gp106/clk_gp106.h"
#include "gp106/flcn_gp106.h"
#include "gp10b/ltc_gp10b.h"
#include "gp10b/therm_gp10b.h"
#include "gp10b/mc_gp10b.h"
#include "gp10b/ce_gp10b.h"
#include "gp10b/priv_ring_gp10b.h"
#include "gp10b/fifo_gp10b.h"
#include "gp10b/fecs_trace_gp10b.h"
#include "gp10b/mm_gp10b.h"
#include "gp10b/pmu_gp10b.h"

#include "gv11b/css_gr_gv11b.h"
#include "gv11b/dbg_gpu_gv11b.h"
#include "gv11b/hal_gv11b.h"
#include "gv100/gr_gv100.h"
#include "gv11b/gr_gv11b.h"
#include "gv11b/mc_gv11b.h"
#include "gv11b/ltc_gv11b.h"
#include "gv11b/gv11b.h"
#include "gv11b/ce_gv11b.h"
#include "gv100/gr_ctx_gv100.h"
#include "gv11b/mm_gv11b.h"
#include "gv11b/pmu_gv11b.h"
#include "gv11b/fb_gv11b.h"
#include "gv100/mm_gv100.h"
#include "gv11b/pmu_gv11b.h"
#include "gv100/fb_gv100.h"
#include "gv100/fifo_gv100.h"
#include "gv11b/fifo_gv11b.h"
#include "gv11b/regops_gv11b.h"

#include "gv11b/gv11b_gating_reglist.h"
#include "gv100/regops_gv100.h"
#include "gv11b/subctx_gv11b.h"

#include "gv100.h"
#include "hal_gv100.h"
#include "gv100/fb_gv100.h"
#include "gv100/mm_gv100.h"

#include <nvgpu/bus.h>
#include <nvgpu/debug.h>
#include <nvgpu/enabled.h>
#include <nvgpu/ctxsw_trace.h>

#include <nvgpu/hw/gv100/hw_proj_gv100.h>
#include <nvgpu/hw/gv100/hw_fifo_gv100.h>
#include <nvgpu/hw/gv100/hw_ram_gv100.h>
#include <nvgpu/hw/gv100/hw_top_gv100.h>
#include <nvgpu/hw/gv100/hw_pram_gv100.h>
#include <nvgpu/hw/gv100/hw_pwr_gv100.h>

static int gv100_get_litter_value(struct gk20a *g, int value)
{
	int ret = EINVAL;
	switch (value) {
	case GPU_LIT_NUM_GPCS:
		ret = proj_scal_litter_num_gpcs_v();
		break;
	case GPU_LIT_NUM_PES_PER_GPC:
		ret = proj_scal_litter_num_pes_per_gpc_v();
		break;
	case GPU_LIT_NUM_ZCULL_BANKS:
		ret = proj_scal_litter_num_zcull_banks_v();
		break;
	case GPU_LIT_NUM_TPC_PER_GPC:
		ret = proj_scal_litter_num_tpc_per_gpc_v();
		break;
	case GPU_LIT_NUM_SM_PER_TPC:
		ret = proj_scal_litter_num_sm_per_tpc_v();
		break;
	case GPU_LIT_NUM_FBPS:
		ret = proj_scal_litter_num_fbps_v();
		break;
	case GPU_LIT_GPC_BASE:
		ret = proj_gpc_base_v();
		break;
	case GPU_LIT_GPC_STRIDE:
		ret = proj_gpc_stride_v();
		break;
	case GPU_LIT_GPC_SHARED_BASE:
		ret = proj_gpc_shared_base_v();
		break;
	case GPU_LIT_TPC_IN_GPC_BASE:
		ret = proj_tpc_in_gpc_base_v();
		break;
	case GPU_LIT_TPC_IN_GPC_STRIDE:
		ret = proj_tpc_in_gpc_stride_v();
		break;
	case GPU_LIT_TPC_IN_GPC_SHARED_BASE:
		ret = proj_tpc_in_gpc_shared_base_v();
		break;
	case GPU_LIT_PPC_IN_GPC_BASE:
		ret = proj_ppc_in_gpc_base_v();
		break;
	case GPU_LIT_PPC_IN_GPC_STRIDE:
		ret = proj_ppc_in_gpc_stride_v();
		break;
	case GPU_LIT_PPC_IN_GPC_SHARED_BASE:
		ret = proj_ppc_in_gpc_shared_base_v();
		break;
	case GPU_LIT_ROP_BASE:
		ret = proj_rop_base_v();
		break;
	case GPU_LIT_ROP_STRIDE:
		ret = proj_rop_stride_v();
		break;
	case GPU_LIT_ROP_SHARED_BASE:
		ret = proj_rop_shared_base_v();
		break;
	case GPU_LIT_HOST_NUM_ENGINES:
		ret = proj_host_num_engines_v();
		break;
	case GPU_LIT_HOST_NUM_PBDMA:
		ret = proj_host_num_pbdma_v();
		break;
	case GPU_LIT_LTC_STRIDE:
		ret = proj_ltc_stride_v();
		break;
	case GPU_LIT_LTS_STRIDE:
		ret = proj_lts_stride_v();
		break;
	case GPU_LIT_NUM_FBPAS:
		ret = proj_scal_litter_num_fbpas_v();
		break;
	case GPU_LIT_FBPA_SHARED_BASE:
		ret = proj_fbpa_shared_base_v();
		break;
	case GPU_LIT_FBPA_BASE:
		ret = proj_fbpa_base_v();
		break;
	case GPU_LIT_FBPA_STRIDE:
		ret = proj_fbpa_stride_v();
		break;
	case GPU_LIT_SM_PRI_STRIDE:
		ret = proj_sm_stride_v();
		break;
	case GPU_LIT_SMPC_PRI_BASE:
		ret = proj_smpc_base_v();
		break;
	case GPU_LIT_SMPC_PRI_SHARED_BASE:
		ret = proj_smpc_shared_base_v();
		break;
	case GPU_LIT_SMPC_PRI_UNIQUE_BASE:
		ret = proj_smpc_unique_base_v();
		break;
	case GPU_LIT_SMPC_PRI_STRIDE:
		ret = proj_smpc_stride_v();
		break;
	case GPU_LIT_TWOD_CLASS:
		ret = FERMI_TWOD_A;
		break;
	case GPU_LIT_THREED_CLASS:
		ret = VOLTA_A;
		break;
	case GPU_LIT_COMPUTE_CLASS:
		ret = VOLTA_COMPUTE_A;
		break;
	case GPU_LIT_GPFIFO_CLASS:
		ret = VOLTA_CHANNEL_GPFIFO_A;
		break;
	case GPU_LIT_I2M_CLASS:
		ret = KEPLER_INLINE_TO_MEMORY_B;
		break;
	case GPU_LIT_DMA_COPY_CLASS:
		ret = VOLTA_DMA_COPY_A;
		break;
	default:
		break;
	}

	return ret;
}

int gv100_init_gpu_characteristics(struct gk20a *g)
{
	int err;

	err = gk20a_init_gpu_characteristics(g);
	if (err)
		return err;

	__nvgpu_set_enabled(g, NVGPU_SUPPORT_TSG_SUBCONTEXTS, true);

	return 0;
}



static const struct gpu_ops gv100_ops = {
	.bios = {
		.init = gp106_bios_init,
		.preos_wait_for_halt = gv100_bios_preos_wait_for_halt,
		.preos_reload_check = gv100_bios_preos_reload_check,
	},
	.ltc = {
		.determine_L2_size_bytes = gp10b_determine_L2_size_bytes,
		.set_zbc_s_entry = gv11b_ltc_set_zbc_stencil_entry,
		.set_zbc_color_entry = gm20b_ltc_set_zbc_color_entry,
		.set_zbc_depth_entry = gm20b_ltc_set_zbc_depth_entry,
		.init_cbc = NULL,
		.init_fs_state = gv11b_ltc_init_fs_state,
		.init_comptags = gp10b_ltc_init_comptags,
		.cbc_ctrl = gm20b_ltc_cbc_ctrl,
		.isr = gv11b_ltc_isr,
		.cbc_fix_config = NULL,
		.flush = gm20b_flush_ltc,
		.set_enabled = gp10b_ltc_set_enabled,
	},
	.ce2 = {
		.isr_stall = gv11b_ce_isr,
		.isr_nonstall = gp10b_ce_nonstall_isr,
		.get_num_pce = gv11b_ce_get_num_pce,
	},
	.gr = {
		.get_patch_slots = gr_gv100_get_patch_slots,
		.init_gpc_mmu = gr_gv11b_init_gpc_mmu,
		.bundle_cb_defaults = gr_gv100_bundle_cb_defaults,
		.cb_size_default = gr_gv100_cb_size_default,
		.calc_global_ctx_buffer_size =
			gr_gv11b_calc_global_ctx_buffer_size,
		.commit_global_attrib_cb = gr_gv11b_commit_global_attrib_cb,
		.commit_global_bundle_cb = gr_gp10b_commit_global_bundle_cb,
		.commit_global_cb_manager = gr_gp10b_commit_global_cb_manager,
		.commit_global_pagepool = gr_gp10b_commit_global_pagepool,
		.handle_sw_method = gr_gv11b_handle_sw_method,
		.set_alpha_circular_buffer_size =
			gr_gv11b_set_alpha_circular_buffer_size,
		.set_circular_buffer_size = gr_gv11b_set_circular_buffer_size,
		.enable_hww_exceptions = gr_gv11b_enable_hww_exceptions,
		.is_valid_class = gr_gv11b_is_valid_class,
		.is_valid_gfx_class = gr_gv11b_is_valid_gfx_class,
		.is_valid_compute_class = gr_gv11b_is_valid_compute_class,
		.get_sm_dsm_perf_regs = gv11b_gr_get_sm_dsm_perf_regs,
		.get_sm_dsm_perf_ctrl_regs = gv11b_gr_get_sm_dsm_perf_ctrl_regs,
		.init_fs_state = gr_gv11b_init_fs_state,
		.set_hww_esr_report_mask = gv11b_gr_set_hww_esr_report_mask,
		.falcon_load_ucode = gr_gm20b_load_ctxsw_ucode_segments,
		.load_ctxsw_ucode = gr_gm20b_load_ctxsw_ucode,
		.set_gpc_tpc_mask = gr_gv100_set_gpc_tpc_mask,
		.get_gpc_tpc_mask = gr_gm20b_get_gpc_tpc_mask,
		.alloc_obj_ctx = gk20a_alloc_obj_ctx,
		.bind_ctxsw_zcull = gr_gk20a_bind_ctxsw_zcull,
		.get_zcull_info = gr_gk20a_get_zcull_info,
		.is_tpc_addr = gr_gm20b_is_tpc_addr,
		.get_tpc_num = gr_gm20b_get_tpc_num,
		.detect_sm_arch = gr_gv11b_detect_sm_arch,
		.add_zbc_color = gr_gp10b_add_zbc_color,
		.add_zbc_depth = gr_gp10b_add_zbc_depth,
		.get_gpcs_swdx_dss_zbc_c_format_reg =
			gr_gv11b_get_gpcs_swdx_dss_zbc_c_format_reg,
		.get_gpcs_swdx_dss_zbc_z_format_reg =
			gr_gv11b_get_gpcs_swdx_dss_zbc_z_format_reg,
		.zbc_set_table = gk20a_gr_zbc_set_table,
		.zbc_query_table = gr_gk20a_query_zbc,
		.pmu_save_zbc = gk20a_pmu_save_zbc,
		.add_zbc = gr_gk20a_add_zbc,
		.pagepool_default_size = gr_gv11b_pagepool_default_size,
		.init_ctx_state = gr_gp10b_init_ctx_state,
		.alloc_gr_ctx = gr_gp10b_alloc_gr_ctx,
		.free_gr_ctx = gr_gk20a_free_gr_ctx,
		.update_ctxsw_preemption_mode =
			gr_gp10b_update_ctxsw_preemption_mode,
		.dump_gr_regs = gr_gv11b_dump_gr_status_regs,
		.update_pc_sampling = gr_gm20b_update_pc_sampling,
		.get_fbp_en_mask = gr_gm20b_get_fbp_en_mask,
		.get_max_ltc_per_fbp = gr_gm20b_get_max_ltc_per_fbp,
		.get_max_lts_per_ltc = gr_gm20b_get_max_lts_per_ltc,
		.get_rop_l2_en_mask = gr_gm20b_rop_l2_en_mask,
		.get_max_fbps_count = gr_gm20b_get_max_fbps_count,
		.init_sm_dsm_reg_info = gv11b_gr_init_sm_dsm_reg_info,
		.wait_empty = gr_gv11b_wait_empty,
		.init_cyclestats = gr_gm20b_init_cyclestats,
		.set_sm_debug_mode = gv11b_gr_set_sm_debug_mode,
		.enable_cde_in_fecs = gr_gm20b_enable_cde_in_fecs,
		.bpt_reg_info = gv11b_gr_bpt_reg_info,
		.get_access_map = gr_gv11b_get_access_map,
		.handle_fecs_error = gr_gv11b_handle_fecs_error,
		.handle_sm_exception = gr_gk20a_handle_sm_exception,
		.handle_tex_exception = gr_gv11b_handle_tex_exception,
		.enable_gpc_exceptions = gr_gv11b_enable_gpc_exceptions,
		.enable_exceptions = gr_gv11b_enable_exceptions,
		.get_lrf_tex_ltc_dram_override = get_ecc_override_val,
		.update_smpc_ctxsw_mode = gr_gk20a_update_smpc_ctxsw_mode,
		.update_hwpm_ctxsw_mode = gr_gk20a_update_hwpm_ctxsw_mode,
		.record_sm_error_state = gv11b_gr_record_sm_error_state,
		.update_sm_error_state = gv11b_gr_update_sm_error_state,
		.clear_sm_error_state = gm20b_gr_clear_sm_error_state,
		.suspend_contexts = gr_gp10b_suspend_contexts,
		.resume_contexts = gr_gk20a_resume_contexts,
		.get_preemption_mode_flags = gr_gp10b_get_preemption_mode_flags,
		.init_sm_id_table = gr_gv100_init_sm_id_table,
		.load_smid_config = gr_gv11b_load_smid_config,
		.program_sm_id_numbering = gr_gv11b_program_sm_id_numbering,
		.is_ltcs_ltss_addr = gr_gm20b_is_ltcs_ltss_addr,
		.is_ltcn_ltss_addr = gr_gm20b_is_ltcn_ltss_addr,
		.split_lts_broadcast_addr = gr_gm20b_split_lts_broadcast_addr,
		.split_ltc_broadcast_addr = gr_gm20b_split_ltc_broadcast_addr,
		.setup_rop_mapping = gr_gv11b_setup_rop_mapping,
		.program_zcull_mapping = gr_gv11b_program_zcull_mapping,
		.commit_global_timeslice = gr_gv11b_commit_global_timeslice,
		.commit_inst = gr_gv11b_commit_inst,
		.write_zcull_ptr = gr_gv11b_write_zcull_ptr,
		.write_pm_ptr = gr_gv11b_write_pm_ptr,
		.init_elcg_mode = gr_gv11b_init_elcg_mode,
		.load_tpc_mask = gr_gv11b_load_tpc_mask,
		.inval_icache = gr_gk20a_inval_icache,
		.trigger_suspend = gv11b_gr_sm_trigger_suspend,
		.wait_for_pause = gr_gk20a_wait_for_pause,
		.resume_from_pause = gv11b_gr_resume_from_pause,
		.clear_sm_errors = gr_gk20a_clear_sm_errors,
		.tpc_enabled_exceptions = gr_gk20a_tpc_enabled_exceptions,
		.get_esr_sm_sel = gv11b_gr_get_esr_sm_sel,
		.sm_debugger_attached = gv11b_gr_sm_debugger_attached,
		.suspend_single_sm = gv11b_gr_suspend_single_sm,
		.suspend_all_sms = gv11b_gr_suspend_all_sms,
		.resume_single_sm = gv11b_gr_resume_single_sm,
		.resume_all_sms = gv11b_gr_resume_all_sms,
		.get_sm_hww_warp_esr = gv11b_gr_get_sm_hww_warp_esr,
		.get_sm_hww_global_esr = gv11b_gr_get_sm_hww_global_esr,
		.get_sm_no_lock_down_hww_global_esr_mask =
			gv11b_gr_get_sm_no_lock_down_hww_global_esr_mask,
		.lock_down_sm = gv11b_gr_lock_down_sm,
		.wait_for_sm_lock_down = gv11b_gr_wait_for_sm_lock_down,
		.clear_sm_hww = gv11b_gr_clear_sm_hww,
		.init_ovr_sm_dsm_perf =  gv11b_gr_init_ovr_sm_dsm_perf,
		.get_ovr_perf_regs = gv11b_gr_get_ovr_perf_regs,
		.disable_rd_coalesce = gm20a_gr_disable_rd_coalesce,
		.set_boosted_ctx = gr_gp10b_set_boosted_ctx,
		.set_preemption_mode = gr_gp10b_set_preemption_mode,
		.set_czf_bypass = NULL,
		.pre_process_sm_exception = gr_gv11b_pre_process_sm_exception,
		.set_preemption_buffer_va = gr_gv11b_set_preemption_buffer_va,
		.init_preemption_state = NULL,
		.update_boosted_ctx = gr_gp10b_update_boosted_ctx,
		.set_bes_crop_debug3 = gr_gp10b_set_bes_crop_debug3,
		.set_bes_crop_debug4 = gr_gp10b_set_bes_crop_debug4,
		.create_gr_sysfs = gr_gv11b_create_sysfs,
		.set_ctxsw_preemption_mode = gr_gp10b_set_ctxsw_preemption_mode,
		.is_etpc_addr = gv11b_gr_pri_is_etpc_addr,
		.egpc_etpc_priv_addr_table = gv11b_gr_egpc_etpc_priv_addr_table,
		.handle_tpc_mpc_exception = gr_gv11b_handle_tpc_mpc_exception,
		.zbc_s_query_table = gr_gv11b_zbc_s_query_table,
		.load_zbc_s_default_tbl = gr_gv11b_load_stencil_default_tbl,
		.handle_gpc_gpcmmu_exception =
			gr_gv11b_handle_gpc_gpcmmu_exception,
		.add_zbc_type_s = gr_gv11b_add_zbc_type_s,
		.get_egpc_base = gv11b_gr_get_egpc_base,
		.get_egpc_etpc_num = gv11b_gr_get_egpc_etpc_num,
		.handle_gpc_gpccs_exception =
			gr_gv11b_handle_gpc_gpccs_exception,
		.load_zbc_s_tbl = gr_gv11b_load_stencil_tbl,
		.access_smpc_reg = gv11b_gr_access_smpc_reg,
		.is_egpc_addr = gv11b_gr_pri_is_egpc_addr,
		.add_zbc_s = gr_gv11b_add_zbc_stencil,
		.handle_gcc_exception = gr_gv11b_handle_gcc_exception,
		.init_sw_veid_bundle = gr_gv11b_init_sw_veid_bundle,
		.handle_tpc_sm_ecc_exception =
			gr_gv11b_handle_tpc_sm_ecc_exception,
		.decode_egpc_addr = gv11b_gr_decode_egpc_addr,
	},
	.fb = {
		.reset = gv100_fb_reset,
		.init_hw = gk20a_fb_init_hw,
		.init_fs_state = NULL,
		.set_mmu_page_size = gm20b_fb_set_mmu_page_size,
		.set_use_full_comp_tag_line =
			gm20b_fb_set_use_full_comp_tag_line,
		.compression_page_size = gp10b_fb_compression_page_size,
		.compressible_page_size = gp10b_fb_compressible_page_size,
		.compression_align_mask = gm20b_fb_compression_align_mask,
		.vpr_info_fetch = gm20b_fb_vpr_info_fetch,
		.dump_vpr_wpr_info = gm20b_fb_dump_vpr_wpr_info,
		.read_wpr_info = gm20b_fb_read_wpr_info,
		.is_debug_mode_enabled = gm20b_fb_debug_mode_enabled,
		.set_debug_mode = gm20b_fb_set_debug_mode,
		.tlb_invalidate = gk20a_fb_tlb_invalidate,
		.hub_isr = gv11b_fb_hub_isr,
		.mem_unlock = gv100_fb_memory_unlock,
	},
	.fifo = {
		.get_preempt_timeout = gv100_fifo_get_preempt_timeout,
		.init_fifo_setup_hw = gv11b_init_fifo_setup_hw,
		.bind_channel = channel_gm20b_bind,
		.unbind_channel = channel_gv11b_unbind,
		.disable_channel = gk20a_fifo_disable_channel,
		.enable_channel = gk20a_fifo_enable_channel,
		.alloc_inst = gk20a_fifo_alloc_inst,
		.free_inst = gk20a_fifo_free_inst,
		.setup_ramfc = channel_gv11b_setup_ramfc,
		.default_timeslice_us = gk20a_fifo_default_timeslice_us,
		.setup_userd = gk20a_fifo_setup_userd,
		.userd_gp_get = gv11b_userd_gp_get,
		.userd_gp_put = gv11b_userd_gp_put,
		.userd_pb_get = gv11b_userd_pb_get,
		.pbdma_acquire_val = gk20a_fifo_pbdma_acquire_val,
		.preempt_channel = gv11b_fifo_preempt_channel,
		.preempt_tsg = gv11b_fifo_preempt_tsg,
		.enable_tsg = gv11b_fifo_enable_tsg,
		.disable_tsg = gk20a_disable_tsg,
		.tsg_verify_channel_status = gk20a_fifo_tsg_unbind_channel_verify_status,
		.tsg_verify_status_ctx_reload = gm20b_fifo_tsg_verify_status_ctx_reload,
		.tsg_verify_status_faulted = gv11b_fifo_tsg_verify_status_faulted,
		.update_runlist = gk20a_fifo_update_runlist,
		.trigger_mmu_fault = NULL,
		.get_mmu_fault_info = NULL,
		.wait_engine_idle = gk20a_fifo_wait_engine_idle,
		.get_num_fifos = gv100_fifo_get_num_fifos,
		.get_pbdma_signature = gp10b_fifo_get_pbdma_signature,
		.set_runlist_interleave = gk20a_fifo_set_runlist_interleave,
		.tsg_set_timeslice = gk20a_fifo_tsg_set_timeslice,
		.force_reset_ch = gk20a_fifo_force_reset_ch,
		.engine_enum_from_type = gp10b_fifo_engine_enum_from_type,
		.device_info_data_parse = gp10b_device_info_data_parse,
		.eng_runlist_base_size = fifo_eng_runlist_base__size_1_v,
		.init_engine_info = gk20a_fifo_init_engine_info,
		.runlist_entry_size = ram_rl_entry_size_v,
		.get_tsg_runlist_entry = gv11b_get_tsg_runlist_entry,
		.get_ch_runlist_entry = gv11b_get_ch_runlist_entry,
		.is_fault_engine_subid_gpc = gv11b_is_fault_engine_subid_gpc,
		.dump_pbdma_status = gk20a_dump_pbdma_status,
		.dump_eng_status = gv11b_dump_eng_status,
		.dump_channel_status_ramfc = gv11b_dump_channel_status_ramfc,
		.intr_0_error_mask = gv11b_fifo_intr_0_error_mask,
		.is_preempt_pending = gv11b_fifo_is_preempt_pending,
		.init_pbdma_intr_descs = gv11b_fifo_init_pbdma_intr_descs,
		.reset_enable_hw = gk20a_init_fifo_reset_enable_hw,
		.teardown_ch_tsg = gv11b_fifo_teardown_ch_tsg,
		.handle_sched_error = gv11b_fifo_handle_sched_error,
		.handle_pbdma_intr_0 = gv11b_fifo_handle_pbdma_intr_0,
		.handle_pbdma_intr_1 = gv11b_fifo_handle_pbdma_intr_1,
		.init_eng_method_buffers = gv11b_fifo_init_eng_method_buffers,
		.deinit_eng_method_buffers =
			gv11b_fifo_deinit_eng_method_buffers,
		.tsg_bind_channel = gk20a_tsg_bind_channel,
		.tsg_unbind_channel = gk20a_tsg_unbind_channel,
#ifdef CONFIG_TEGRA_GK20A_NVHOST
		.alloc_syncpt_buf = gv11b_fifo_alloc_syncpt_buf,
		.free_syncpt_buf = gv11b_fifo_free_syncpt_buf,
		.add_syncpt_wait_cmd = gv11b_fifo_add_syncpt_wait_cmd,
		.get_syncpt_wait_cmd_size = gv11b_fifo_get_syncpt_wait_cmd_size,
		.add_syncpt_incr_cmd = gv11b_fifo_add_syncpt_incr_cmd,
		.get_syncpt_incr_cmd_size = gv11b_fifo_get_syncpt_incr_cmd_size,
#endif
		.resetup_ramfc = NULL,
		.device_info_fault_id = top_device_info_data_fault_id_enum_v,
		.free_channel_ctx_header = gv11b_free_subctx_header,
		.preempt_ch_tsg = gv11b_fifo_preempt_ch_tsg,
		.handle_ctxsw_timeout = gv11b_fifo_handle_ctxsw_timeout,
		.apply_ctxsw_timeout_intr = gv100_apply_ctxsw_timeout_intr,
	},
	.gr_ctx = {
		.get_netlist_name = gr_gv100_get_netlist_name,
		.is_fw_defined = gr_gv100_is_firmware_defined,
	},
#ifdef CONFIG_GK20A_CTXSW_TRACE
	.fecs_trace = {
		.alloc_user_buffer = NULL,
		.free_user_buffer = NULL,
		.mmap_user_buffer = NULL,
		.init = NULL,
		.deinit = NULL,
		.enable = NULL,
		.disable = NULL,
		.is_enabled = NULL,
		.reset = NULL,
		.flush = NULL,
		.poll = NULL,
		.bind_channel = NULL,
		.unbind_channel = NULL,
		.max_entries = NULL,
	},
#endif /* CONFIG_GK20A_CTXSW_TRACE */
	.mm = {
		.support_sparse = gm20b_mm_support_sparse,
		.gmmu_map = gk20a_locked_gmmu_map,
		.gmmu_unmap = gk20a_locked_gmmu_unmap,
		.vm_bind_channel = gk20a_vm_bind_channel,
		.fb_flush = gk20a_mm_fb_flush,
		.l2_invalidate = gk20a_mm_l2_invalidate,
		.l2_flush = gv11b_mm_l2_flush,
		.cbc_clean = gk20a_mm_cbc_clean,
		.set_big_page_size = gm20b_mm_set_big_page_size,
		.get_big_page_sizes = gm20b_mm_get_big_page_sizes,
		.get_default_big_page_size = gp10b_mm_get_default_big_page_size,
		.gpu_phys_addr = gv11b_gpu_phys_addr,
		.get_mmu_levels = gp10b_mm_get_mmu_levels,
		.get_vidmem_size = gv100_mm_get_vidmem_size,
		.init_pdb = gp10b_mm_init_pdb,
		.init_mm_setup_hw = gv11b_init_mm_setup_hw,
		.is_bar1_supported = gv11b_mm_is_bar1_supported,
		.alloc_inst_block = gk20a_alloc_inst_block,
		.init_inst_block = gv11b_init_inst_block,
		.mmu_fault_pending = gv11b_mm_mmu_fault_pending,
		.get_kind_invalid = gm20b_get_kind_invalid,
		.get_kind_pitch = gm20b_get_kind_pitch,
		.init_bar2_vm = gp10b_init_bar2_vm,
		.init_bar2_mm_hw_setup = gv11b_init_bar2_mm_hw_setup,
		.remove_bar2_vm = gv11b_mm_remove_bar2_vm,
		.fault_info_mem_destroy = gv11b_mm_fault_info_mem_destroy,
		.get_flush_retries = gv100_mm_get_flush_retries,
	},
	.pramin = {
		.enter = gk20a_pramin_enter,
		.exit = gk20a_pramin_exit,
		.data032_r = pram_data032_r,
	},
	.pmu = {
		.init_wpr_region = gm20b_pmu_init_acr,
		.load_lsfalcon_ucode = gp106_load_falcon_ucode,
		.is_lazy_bootstrap = gp106_is_lazy_bootstrap,
		.is_priv_load = gp106_is_priv_load,
		.prepare_ucode = gp106_prepare_ucode_blob,
		.pmu_setup_hw_and_bootstrap = gp106_bootstrap_hs_flcn,
		.get_wpr = gp106_wpr_info,
		.alloc_blob_space = gp106_alloc_blob_space,
		.pmu_populate_loader_cfg = gp106_pmu_populate_loader_cfg,
		.flcn_populate_bl_dmem_desc = gp106_flcn_populate_bl_dmem_desc,
		.falcon_wait_for_halt = sec2_wait_for_halt,
		.falcon_clear_halt_interrupt_status =
			sec2_clear_halt_interrupt_status,
		.init_falcon_setup_hw = init_sec2_setup_hw1,
		.pmu_queue_tail = gk20a_pmu_queue_tail,
		.pmu_get_queue_head = pwr_pmu_queue_head_r,
		.pmu_mutex_release = gk20a_pmu_mutex_release,
		.is_pmu_supported = gp106_is_pmu_supported,
		.pmu_pg_supported_engines_list = gp106_pmu_pg_engines_list,
		.pmu_elpg_statistics = gp106_pmu_elpg_statistics,
		.pmu_init_perfmon = nvgpu_pmu_init_perfmon,
		.pmu_perfmon_start_sampling = nvgpu_pmu_perfmon_start_sampling,
		.pmu_perfmon_stop_sampling = nvgpu_pmu_perfmon_stop_sampling,
		.pmu_mutex_acquire = gk20a_pmu_mutex_acquire,
		.pmu_is_lpwr_feature_supported =
			gp106_pmu_is_lpwr_feature_supported,
		.pmu_msgq_tail = gk20a_pmu_msgq_tail,
		.pmu_pg_engines_feature_list = gp106_pmu_pg_feature_list,
		.pmu_get_queue_head_size = pwr_pmu_queue_head__size_1_v,
		.pmu_queue_head = gk20a_pmu_queue_head,
		.pmu_pg_param_post_init = nvgpu_lpwr_post_init,
		.pmu_get_queue_tail_size = pwr_pmu_queue_tail__size_1_v,
		.pmu_pg_init_param = gp106_pg_param_init,
		.reset_engine = gp106_pmu_engine_reset,
		.write_dmatrfbase = gp10b_write_dmatrfbase,
		.pmu_mutex_size = pwr_pmu_mutex__size_1_v,
		.is_engine_in_reset = gp106_pmu_is_engine_in_reset,
		.pmu_get_queue_tail = pwr_pmu_queue_tail_r,
		.get_irqdest = gk20a_pmu_get_irqdest,
	},
	.clk = {
		.init_clk_support = gp106_init_clk_support,
		.get_crystal_clk_hz = gp106_crystal_clk_hz,
		.measure_freq = gp106_clk_measure_freq,
		.suspend_clk_support = gp106_suspend_clk_support,
	},
	.clk_arb = {
		.get_arbiter_clk_domains = gp106_get_arbiter_clk_domains,
		.get_arbiter_clk_range = gp106_get_arbiter_clk_range,
		.get_arbiter_clk_default = gp106_get_arbiter_clk_default,
		.get_current_pstate = nvgpu_clk_arb_get_current_pstate,
	},
	.regops = {
		.get_global_whitelist_ranges =
			gv100_get_global_whitelist_ranges,
		.get_global_whitelist_ranges_count =
			gv100_get_global_whitelist_ranges_count,
		.get_context_whitelist_ranges =
			gv100_get_context_whitelist_ranges,
		.get_context_whitelist_ranges_count =
			gv100_get_context_whitelist_ranges_count,
		.get_runcontrol_whitelist = gv100_get_runcontrol_whitelist,
		.get_runcontrol_whitelist_count =
			gv100_get_runcontrol_whitelist_count,
		.get_runcontrol_whitelist_ranges =
			gv100_get_runcontrol_whitelist_ranges,
		.get_runcontrol_whitelist_ranges_count =
			gv100_get_runcontrol_whitelist_ranges_count,
		.get_qctl_whitelist = gv100_get_qctl_whitelist,
		.get_qctl_whitelist_count = gv100_get_qctl_whitelist_count,
		.get_qctl_whitelist_ranges = gv100_get_qctl_whitelist_ranges,
		.get_qctl_whitelist_ranges_count =
			gv100_get_qctl_whitelist_ranges_count,
		.apply_smpc_war = gv100_apply_smpc_war,
	},
	.mc = {
		.intr_enable = mc_gv11b_intr_enable,
		.intr_unit_config = mc_gp10b_intr_unit_config,
		.isr_stall = mc_gp10b_isr_stall,
		.intr_stall = mc_gp10b_intr_stall,
		.intr_stall_pause = mc_gp10b_intr_stall_pause,
		.intr_stall_resume = mc_gp10b_intr_stall_resume,
		.intr_nonstall = mc_gp10b_intr_nonstall,
		.intr_nonstall_pause = mc_gp10b_intr_nonstall_pause,
		.intr_nonstall_resume = mc_gp10b_intr_nonstall_resume,
		.enable = gk20a_mc_enable,
		.disable = gk20a_mc_disable,
		.reset = gk20a_mc_reset,
		.boot_0 = gk20a_mc_boot_0,
		.is_intr1_pending = mc_gp10b_is_intr1_pending,
		.is_intr_hub_pending = gv11b_mc_is_intr_hub_pending,
	},
	.debug = {
		.show_dump = gk20a_debug_show_dump,
	},
	.dbg_session_ops = {
		.exec_reg_ops = exec_regops_gk20a,
		.dbg_set_powergate = dbg_set_powergate,
		.check_and_set_global_reservation =
			nvgpu_check_and_set_global_reservation,
		.check_and_set_context_reservation =
			nvgpu_check_and_set_context_reservation,
		.release_profiler_reservation =
			nvgpu_release_profiler_reservation,
		.perfbuffer_enable = gv11b_perfbuf_enable_locked,
		.perfbuffer_disable = gv11b_perfbuf_disable_locked,
	},
	.bus = {
		.init_hw = gk20a_bus_init_hw,
		.isr = gk20a_bus_isr,
		.read_ptimer = gk20a_read_ptimer,
		.get_timestamps_zipper = nvgpu_get_timestamps_zipper,
		.bar1_bind = NULL,
	},
#if defined(CONFIG_GK20A_CYCLE_STATS)
	.css = {
		.enable_snapshot = gv11b_css_hw_enable_snapshot,
		.disable_snapshot = gv11b_css_hw_disable_snapshot,
		.check_data_available = gv11b_css_hw_check_data_available,
		.set_handled_snapshots = css_hw_set_handled_snapshots,
		.allocate_perfmon_ids = css_gr_allocate_perfmon_ids,
		.release_perfmon_ids = css_gr_release_perfmon_ids,
	},
#endif
	.xve = {
		.get_speed        = xve_get_speed_gp106,
		.xve_readl        = xve_xve_readl_gp106,
		.xve_writel       = xve_xve_writel_gp106,
		.disable_aspm     = xve_disable_aspm_gp106,
		.reset_gpu        = xve_reset_gpu_gp106,
#if defined(CONFIG_PCI_MSI)
		.rearm_msi        = xve_rearm_msi_gp106,
#endif
		.enable_shadow_rom = xve_enable_shadow_rom_gp106,
		.disable_shadow_rom = xve_disable_shadow_rom_gp106,
	},
	.falcon = {
		.falcon_hal_sw_init = gp106_falcon_hal_sw_init,
	},
	.priv_ring = {
		.isr = gp10b_priv_ring_isr,
	},
	.chip_init_gpu_characteristics = gv100_init_gpu_characteristics,
	.get_litter_value = gv100_get_litter_value,
};

int gv100_init_hal(struct gk20a *g)
{
	struct gpu_ops *gops = &g->ops;

	gops->bios = gv100_ops.bios;
	gops->ltc = gv100_ops.ltc;
	gops->ce2 = gv100_ops.ce2;
	gops->gr = gv100_ops.gr;
	gops->fb = gv100_ops.fb;
	gops->clock_gating = gv100_ops.clock_gating;
	gops->fifo = gv100_ops.fifo;
	gops->gr_ctx = gv100_ops.gr_ctx;
	gops->mm = gv100_ops.mm;
#ifdef CONFIG_GK20A_CTXSW_TRACE
	gops->fecs_trace = gv100_ops.fecs_trace;
#endif
	gops->pramin = gv100_ops.pramin;
	gops->therm = gv100_ops.therm;
	gops->pmu = gv100_ops.pmu;
	gops->regops = gv100_ops.regops;
	gops->mc = gv100_ops.mc;
	gops->debug = gv100_ops.debug;
	gops->dbg_session_ops = gv100_ops.dbg_session_ops;
	gops->bus = gv100_ops.bus;
#if defined(CONFIG_GK20A_CYCLE_STATS)
	gops->css = gv100_ops.css;
#endif
	gops->xve = gv100_ops.xve;
	gops->falcon = gv100_ops.falcon;
	gops->priv_ring = gv100_ops.priv_ring;

	/* clocks */
	gops->clk.init_clk_support = gv100_ops.clk.init_clk_support;
	gops->clk.get_crystal_clk_hz = gv100_ops.clk.get_crystal_clk_hz;
	gops->clk.measure_freq = gv100_ops.clk.measure_freq;
	gops->clk.suspend_clk_support = gv100_ops.clk.suspend_clk_support;

	/* Lone functions */
	gops->chip_init_gpu_characteristics =
		gv100_ops.chip_init_gpu_characteristics;
	gops->get_litter_value = gv100_ops.get_litter_value;

	__nvgpu_set_enabled(g, NVGPU_GR_USE_DMA_FOR_FW_BOOTSTRAP, true);
	__nvgpu_set_enabled(g, NVGPU_SEC_PRIVSECURITY, true);
	__nvgpu_set_enabled(g, NVGPU_SEC_SECUREGPCCS, true);
	__nvgpu_set_enabled(g, NVGPU_PMU_FECS_BOOTSTRAP_DONE, false);
	/* for now */
	__nvgpu_set_enabled(g, NVGPU_PMU_PSTATE, false);

	g->pmu_lsf_pmu_wpr_init_done = 0;
	g->bootstrap_owner = LSF_FALCON_ID_SEC2;

	g->name = "gv10x";

	return 0;
}
