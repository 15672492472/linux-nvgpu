/*
 * GK20A PMU (aka. gPMU outside gk20a context)
 *
 * Copyright (c) 2011-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/nvgpu_common.h>
#include <nvgpu/pmu/queue.h>
#include <nvgpu/pmu/cmd.h>
#include <nvgpu/timers.h>
#include <nvgpu/kmem.h>
#include <nvgpu/dma.h>
#include <nvgpu/log.h>
#include <nvgpu/bug.h>
#include <nvgpu/firmware.h>
#include <nvgpu/falcon.h>
#include <nvgpu/mm.h>
#include <nvgpu/io.h>
#include <nvgpu/clk_arb.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/utils.h>
#include <nvgpu/unit.h>

#include "gk20a/gr_gk20a.h"
#include "pmu_gk20a.h"

#include <nvgpu/hw/gk20a/hw_mc_gk20a.h>
#include <nvgpu/hw/gk20a/hw_pwr_gk20a.h>
#include <nvgpu/hw/gk20a/hw_top_gk20a.h>

bool nvgpu_find_hex_in_string(char *strings, struct gk20a *g, u32 *hex_pos)
{
	u32 i = 0, j = (u32)strlen(strings);

	for (; i < j; i++) {
		if (strings[i] == '%') {
			if (strings[i + 1U] == 'x' || strings[i + 1U] == 'X') {
				*hex_pos = i;
				return true;
			}
		}
	}
	*hex_pos = U32_MAX;
	return false;
}

u32 gk20a_pmu_get_irqdest(struct gk20a *g)
{
	u32 intr_dest;

	/* dest 0=falcon, 1=host; level 0=irq0, 1=irq1 */
	intr_dest = pwr_falcon_irqdest_host_gptmr_f(0)    |
		pwr_falcon_irqdest_host_wdtmr_f(1)    |
		pwr_falcon_irqdest_host_mthd_f(0)     |
		pwr_falcon_irqdest_host_ctxsw_f(0)    |
		pwr_falcon_irqdest_host_halt_f(1)     |
		pwr_falcon_irqdest_host_exterr_f(0)   |
		pwr_falcon_irqdest_host_swgen0_f(1)   |
		pwr_falcon_irqdest_host_swgen1_f(0)   |
		pwr_falcon_irqdest_host_ext_f(0xff)   |
		pwr_falcon_irqdest_target_gptmr_f(1)  |
		pwr_falcon_irqdest_target_wdtmr_f(0)  |
		pwr_falcon_irqdest_target_mthd_f(0)   |
		pwr_falcon_irqdest_target_ctxsw_f(0)  |
		pwr_falcon_irqdest_target_halt_f(0)   |
		pwr_falcon_irqdest_target_exterr_f(0) |
		pwr_falcon_irqdest_target_swgen0_f(0) |
		pwr_falcon_irqdest_target_swgen1_f(0) |
		pwr_falcon_irqdest_target_ext_f(0xff);

	return intr_dest;
}

void gk20a_pmu_enable_irq(struct nvgpu_pmu *pmu, bool enable)
{
	struct gk20a *g = gk20a_from_pmu(pmu);
	u32 intr_mask;
	u32 intr_dest;

	nvgpu_log_fn(g, " ");

	g->ops.mc.intr_unit_config(g, MC_INTR_UNIT_DISABLE, true,
			mc_intr_mask_0_pmu_enabled_f());
	g->ops.mc.intr_unit_config(g, MC_INTR_UNIT_DISABLE, false,
			mc_intr_mask_1_pmu_enabled_f());

	nvgpu_falcon_set_irq(&pmu->flcn, false, 0x0, 0x0);

	if (enable) {
		intr_dest = g->ops.pmu.get_irqdest(g);
		/* 0=disable, 1=enable */
		intr_mask = pwr_falcon_irqmset_gptmr_f(1)  |
			pwr_falcon_irqmset_wdtmr_f(1)  |
			pwr_falcon_irqmset_mthd_f(0)   |
			pwr_falcon_irqmset_ctxsw_f(0)  |
			pwr_falcon_irqmset_halt_f(1)   |
			pwr_falcon_irqmset_exterr_f(1) |
			pwr_falcon_irqmset_swgen0_f(1) |
			pwr_falcon_irqmset_swgen1_f(1);

		nvgpu_falcon_set_irq(&pmu->flcn, true, intr_mask, intr_dest);

		g->ops.mc.intr_unit_config(g, MC_INTR_UNIT_ENABLE, true,
				mc_intr_mask_0_pmu_enabled_f());
	}

	nvgpu_log_fn(g, "done");
}



int pmu_bootstrap(struct nvgpu_pmu *pmu)
{
	struct gk20a *g = gk20a_from_pmu(pmu);
	struct mm_gk20a *mm = &g->mm;
	struct pmu_ucode_desc *desc =
		(struct pmu_ucode_desc *)(void *)pmu->fw_image->data;
	u32 addr_code, addr_data, addr_load;
	u32 i, blocks, addr_args;
	int err;
	u32 inst_block_ptr;

	nvgpu_log_fn(g, " ");

	gk20a_writel(g, pwr_falcon_itfen_r(),
		gk20a_readl(g, pwr_falcon_itfen_r()) |
		pwr_falcon_itfen_ctxen_enable_f());
	inst_block_ptr = nvgpu_inst_block_ptr(g, &mm->pmu.inst_block);
	gk20a_writel(g, pwr_pmu_new_instblk_r(),
		pwr_pmu_new_instblk_ptr_f(inst_block_ptr) |
		pwr_pmu_new_instblk_valid_f(1) |
		pwr_pmu_new_instblk_target_sys_coh_f());

	/* TBD: load all other surfaces */
	g->ops.pmu_ver.set_pmu_cmdline_args_trace_size(
		pmu, GK20A_PMU_TRACE_BUFSIZE);
	g->ops.pmu_ver.set_pmu_cmdline_args_trace_dma_base(pmu);
	g->ops.pmu_ver.set_pmu_cmdline_args_trace_dma_idx(
		pmu, GK20A_PMU_DMAIDX_VIRT);

	g->ops.pmu_ver.set_pmu_cmdline_args_cpu_freq(pmu,
		g->ops.clk.get_rate(g, CTRL_CLK_DOMAIN_PWRCLK));

	addr_args = (pwr_falcon_hwcfg_dmem_size_v(
		gk20a_readl(g, pwr_falcon_hwcfg_r()))
			<< GK20A_PMU_DMEM_BLKSIZE2) -
		g->ops.pmu_ver.get_pmu_cmdline_args_size(pmu);

	nvgpu_falcon_copy_to_dmem(&pmu->flcn, addr_args,
			(u8 *)(g->ops.pmu_ver.get_pmu_cmdline_args_ptr(pmu)),
			g->ops.pmu_ver.get_pmu_cmdline_args_size(pmu), 0);

	gk20a_writel(g, pwr_falcon_dmemc_r(0),
		pwr_falcon_dmemc_offs_f(0) |
		pwr_falcon_dmemc_blk_f(0)  |
		pwr_falcon_dmemc_aincw_f(1));

	addr_code = u64_lo32((pmu->ucode.gpu_va +
			desc->app_start_offset +
			desc->app_resident_code_offset) >> 8) ;
	addr_data = u64_lo32((pmu->ucode.gpu_va +
			desc->app_start_offset +
			desc->app_resident_data_offset) >> 8);
	addr_load = u64_lo32((pmu->ucode.gpu_va +
			desc->bootloader_start_offset) >> 8);

	gk20a_writel(g, pwr_falcon_dmemd_r(0), GK20A_PMU_DMAIDX_UCODE);
	gk20a_writel(g, pwr_falcon_dmemd_r(0), addr_code);
	gk20a_writel(g, pwr_falcon_dmemd_r(0), desc->app_size);
	gk20a_writel(g, pwr_falcon_dmemd_r(0), desc->app_resident_code_size);
	gk20a_writel(g, pwr_falcon_dmemd_r(0), desc->app_imem_entry);
	gk20a_writel(g, pwr_falcon_dmemd_r(0), addr_data);
	gk20a_writel(g, pwr_falcon_dmemd_r(0), desc->app_resident_data_size);
	gk20a_writel(g, pwr_falcon_dmemd_r(0), addr_code);
	gk20a_writel(g, pwr_falcon_dmemd_r(0), 0x1);
	gk20a_writel(g, pwr_falcon_dmemd_r(0), addr_args);

	g->ops.pmu.write_dmatrfbase(g,
			addr_load - (desc->bootloader_imem_offset >> U32(8)));

	blocks = ((desc->bootloader_size + 0xFFU) & ~0xFFU) >> 8;

	for (i = 0; i < blocks; i++) {
		gk20a_writel(g, pwr_falcon_dmatrfmoffs_r(),
			desc->bootloader_imem_offset + (i << 8));
		gk20a_writel(g, pwr_falcon_dmatrffboffs_r(),
			desc->bootloader_imem_offset + (i << 8));
		gk20a_writel(g, pwr_falcon_dmatrfcmd_r(),
			pwr_falcon_dmatrfcmd_imem_f(1)  |
			pwr_falcon_dmatrfcmd_write_f(0) |
			pwr_falcon_dmatrfcmd_size_f(6)  |
			pwr_falcon_dmatrfcmd_ctxdma_f(GK20A_PMU_DMAIDX_UCODE));
	}

	err = nvgpu_falcon_bootstrap(&g->pmu.flcn,
				     desc->bootloader_entry_point);

	gk20a_writel(g, pwr_falcon_os_r(), desc->app_version);

	return err;
}

void gk20a_pmu_pg_idle_counter_config(struct gk20a *g, u32 pg_engine_id)
{
	gk20a_writel(g, pwr_pmu_pg_idlefilth_r(pg_engine_id),
		PMU_PG_IDLE_THRESHOLD);
	gk20a_writel(g, pwr_pmu_pg_ppuidlefilth_r(pg_engine_id),
		PMU_PG_POST_POWERUP_IDLE_THRESHOLD);
}

int gk20a_pmu_queue_head(struct gk20a *g, u32 queue_id, u32 queue_index,
			u32 *head, bool set)
{
	u32 queue_head_size = 0;

	if (g->ops.pmu.pmu_get_queue_head_size != NULL) {
		queue_head_size = g->ops.pmu.pmu_get_queue_head_size();
	}

	BUG_ON((head == NULL) || (queue_head_size == 0U));

	if (PMU_IS_COMMAND_QUEUE(queue_id)) {

		if (queue_index >= queue_head_size) {
			return -EINVAL;
		}

		if (!set) {
			*head = pwr_pmu_queue_head_address_v(
				gk20a_readl(g,
				g->ops.pmu.pmu_get_queue_head(queue_index)));
		} else {
			gk20a_writel(g,
				g->ops.pmu.pmu_get_queue_head(queue_index),
				pwr_pmu_queue_head_address_f(*head));
		}
	} else {
		if (!set) {
			*head = pwr_pmu_msgq_head_val_v(
				gk20a_readl(g, pwr_pmu_msgq_head_r()));
		} else {
			gk20a_writel(g,
				pwr_pmu_msgq_head_r(),
				pwr_pmu_msgq_head_val_f(*head));
		}
	}

	return 0;
}

int gk20a_pmu_queue_tail(struct gk20a *g, u32 queue_id, u32 queue_index,
			u32 *tail, bool set)
{
	u32 queue_tail_size = 0;

	if (g->ops.pmu.pmu_get_queue_tail_size != NULL) {
		queue_tail_size = g->ops.pmu.pmu_get_queue_tail_size();
	}

	BUG_ON((tail == NULL) || (queue_tail_size == 0U));

	if (PMU_IS_COMMAND_QUEUE(queue_id)) {

		if (queue_index >= queue_tail_size) {
			return -EINVAL;
		}

		if (!set) {
			*tail = pwr_pmu_queue_tail_address_v(gk20a_readl(g,
					g->ops.pmu.pmu_get_queue_tail(queue_index)));
		} else {
			gk20a_writel(g,
				g->ops.pmu.pmu_get_queue_tail(queue_index),
				pwr_pmu_queue_tail_address_f(*tail));
		}

	} else {
		if (!set) {
			*tail = pwr_pmu_msgq_tail_val_v(
				gk20a_readl(g, pwr_pmu_msgq_tail_r()));
		} else {
			gk20a_writel(g,
				pwr_pmu_msgq_tail_r(),
				pwr_pmu_msgq_tail_val_f(*tail));
		}
	}

	return 0;
}

void gk20a_pmu_msgq_tail(struct nvgpu_pmu *pmu, u32 *tail, bool set)
{
	struct gk20a *g = gk20a_from_pmu(pmu);
	u32 queue_tail_size = 0;

	if (g->ops.pmu.pmu_get_queue_tail_size != NULL) {
		queue_tail_size = g->ops.pmu.pmu_get_queue_tail_size();
	}

	BUG_ON((tail == NULL) || (queue_tail_size == 0U));

	if (!set) {
		*tail = pwr_pmu_msgq_tail_val_v(
			gk20a_readl(g, pwr_pmu_msgq_tail_r()));
	} else {
		gk20a_writel(g,
			pwr_pmu_msgq_tail_r(),
			pwr_pmu_msgq_tail_val_f(*tail));
	}
}

void gk20a_write_dmatrfbase(struct gk20a *g, u32 addr)
{
	gk20a_writel(g, pwr_falcon_dmatrfbase_r(), addr);
}

bool gk20a_pmu_is_engine_in_reset(struct gk20a *g)
{
	bool status = false;

	status = g->ops.mc.is_enabled(g, NVGPU_UNIT_PWR);

	return status;
}

int gk20a_pmu_engine_reset(struct gk20a *g, bool do_reset)
{
	u32 reset_mask = g->ops.mc.reset_mask(g, NVGPU_UNIT_PWR);

	if (do_reset) {
		g->ops.mc.enable(g, reset_mask);
	} else {
		g->ops.mc.disable(g, reset_mask);
	}

	return 0;
}

bool gk20a_is_pmu_supported(struct gk20a *g)
{
	return true;
}

int nvgpu_pmu_handle_therm_event(struct nvgpu_pmu *pmu,
			struct nv_pmu_therm_msg *msg)
{
	struct gk20a *g = gk20a_from_pmu(pmu);

	nvgpu_log_fn(g, " ");

	switch (msg->msg_type) {
	case NV_PMU_THERM_MSG_ID_EVENT_HW_SLOWDOWN_NOTIFICATION:
		if (msg->hw_slct_msg.mask == BIT(NV_PMU_THERM_EVENT_THERMAL_1)) {
			nvgpu_clk_arb_send_thermal_alarm(pmu->g);
		} else {
			nvgpu_pmu_dbg(g, "Unwanted/Unregistered thermal event received %d",
				msg->hw_slct_msg.mask);
		}
		break;
	default:
		nvgpu_pmu_dbg(g, "unknown therm event received %d",
			      msg->msg_type);
		break;
	}

	return 0;
}

bool gk20a_pmu_is_interrupted(struct nvgpu_pmu *pmu)
{
	struct gk20a *g = gk20a_from_pmu(pmu);
	u32 servicedpmuint;

	servicedpmuint = pwr_falcon_irqstat_halt_true_f() |
			pwr_falcon_irqstat_exterr_true_f() |
			pwr_falcon_irqstat_swgen0_true_f();

	if ((gk20a_readl(g, pwr_falcon_irqstat_r()) & servicedpmuint) != 0U) {
		return true;
	}

	return false;
}

void gk20a_pmu_isr(struct gk20a *g)
{
	struct nvgpu_pmu *pmu = &g->pmu;
	u32 intr, mask;
	bool recheck = false;

	nvgpu_log_fn(g, " ");

	nvgpu_mutex_acquire(&pmu->isr_mutex);
	if (!pmu->isr_enabled) {
		nvgpu_mutex_release(&pmu->isr_mutex);
		return;
	}

	mask = gk20a_readl(g, pwr_falcon_irqmask_r()) &
		gk20a_readl(g, pwr_falcon_irqdest_r());

	intr = gk20a_readl(g, pwr_falcon_irqstat_r());

	nvgpu_pmu_dbg(g, "received falcon interrupt: 0x%08x", intr);

	intr = gk20a_readl(g, pwr_falcon_irqstat_r()) & mask;
	if ((intr == 0U) || (pmu->pmu_state == PMU_STATE_OFF)) {
		gk20a_writel(g, pwr_falcon_irqsclr_r(), intr);
		nvgpu_mutex_release(&pmu->isr_mutex);
		return;
	}

	if ((intr & pwr_falcon_irqstat_halt_true_f()) != 0U) {
		nvgpu_err(g, "pmu halt intr not implemented");
		nvgpu_pmu_dump_falcon_stats(pmu);
		if (gk20a_readl(g, pwr_pmu_mailbox_r
				(PMU_MODE_MISMATCH_STATUS_MAILBOX_R)) ==
				PMU_MODE_MISMATCH_STATUS_VAL) {
			if (g->ops.pmu.dump_secure_fuses != NULL) {
				g->ops.pmu.dump_secure_fuses(g);
			}
		}
	}
	if ((intr & pwr_falcon_irqstat_exterr_true_f()) != 0U) {
		nvgpu_err(g,
			"pmu exterr intr not implemented. Clearing interrupt.");
		nvgpu_pmu_dump_falcon_stats(pmu);

		gk20a_writel(g, pwr_falcon_exterrstat_r(),
			gk20a_readl(g, pwr_falcon_exterrstat_r()) &
				~pwr_falcon_exterrstat_valid_m());
	}

	if (g->ops.pmu.handle_ext_irq != NULL) {
		g->ops.pmu.handle_ext_irq(g, intr);
	}

	if ((intr & pwr_falcon_irqstat_swgen0_true_f()) != 0U) {
		nvgpu_pmu_process_message(pmu);
		recheck = true;
	}

	gk20a_writel(g, pwr_falcon_irqsclr_r(), intr);

	if (recheck) {
		if (!nvgpu_pmu_queue_is_empty(&pmu->queues,
					      PMU_MESSAGE_QUEUE)) {
			gk20a_writel(g, pwr_falcon_irqsset_r(),
				pwr_falcon_irqsset_swgen0_set_f());
		}
	}

	nvgpu_mutex_release(&pmu->isr_mutex);
}

void gk20a_pmu_init_perfmon_counter(struct gk20a *g)
{
	u32 data;

	/* use counter #3 for GR && CE2 busy cycles */
	gk20a_writel(g, pwr_pmu_idle_mask_r(3),
		pwr_pmu_idle_mask_gr_enabled_f() |
		pwr_pmu_idle_mask_ce_2_enabled_f());

	/* disable idle filtering for counters 3 and 6 */
	data = gk20a_readl(g, pwr_pmu_idle_ctrl_r(3));
	data = set_field(data, pwr_pmu_idle_ctrl_value_m() |
			pwr_pmu_idle_ctrl_filter_m(),
			pwr_pmu_idle_ctrl_value_busy_f() |
			pwr_pmu_idle_ctrl_filter_disabled_f());
	gk20a_writel(g, pwr_pmu_idle_ctrl_r(3), data);

	/* use counter #6 for total cycles */
	data = gk20a_readl(g, pwr_pmu_idle_ctrl_r(6));
	data = set_field(data, pwr_pmu_idle_ctrl_value_m() |
			pwr_pmu_idle_ctrl_filter_m(),
			pwr_pmu_idle_ctrl_value_always_f() |
			pwr_pmu_idle_ctrl_filter_disabled_f());
	gk20a_writel(g, pwr_pmu_idle_ctrl_r(6), data);

	/*
	 * We don't want to disturb counters #3 and #6, which are used by
	 * perfmon, so we add wiring also to counters #1 and #2 for
	 * exposing raw counter readings.
	 */
	gk20a_writel(g, pwr_pmu_idle_mask_r(1),
		pwr_pmu_idle_mask_gr_enabled_f() |
		pwr_pmu_idle_mask_ce_2_enabled_f());

	data = gk20a_readl(g, pwr_pmu_idle_ctrl_r(1));
	data = set_field(data, pwr_pmu_idle_ctrl_value_m() |
			pwr_pmu_idle_ctrl_filter_m(),
			pwr_pmu_idle_ctrl_value_busy_f() |
			pwr_pmu_idle_ctrl_filter_disabled_f());
	gk20a_writel(g, pwr_pmu_idle_ctrl_r(1), data);

	data = gk20a_readl(g, pwr_pmu_idle_ctrl_r(2));
	data = set_field(data, pwr_pmu_idle_ctrl_value_m() |
			pwr_pmu_idle_ctrl_filter_m(),
			pwr_pmu_idle_ctrl_value_always_f() |
			pwr_pmu_idle_ctrl_filter_disabled_f());
	gk20a_writel(g, pwr_pmu_idle_ctrl_r(2), data);

	/*
	 * use counters 4 and 0 for perfmon to log busy cycles and total cycles
	 * counter #0 overflow sets pmu idle intr status bit
	 */
	gk20a_writel(g, pwr_pmu_idle_intr_r(),
		     pwr_pmu_idle_intr_en_f(0));

	gk20a_writel(g, pwr_pmu_idle_threshold_r(0),
		     pwr_pmu_idle_threshold_value_f(0x7FFFFFFF));

	data = gk20a_readl(g, pwr_pmu_idle_ctrl_r(0));
	data = set_field(data, pwr_pmu_idle_ctrl_value_m() |
			pwr_pmu_idle_ctrl_filter_m(),
			pwr_pmu_idle_ctrl_value_always_f() |
			pwr_pmu_idle_ctrl_filter_disabled_f());
	gk20a_writel(g, pwr_pmu_idle_ctrl_r(0), data);

	gk20a_writel(g, pwr_pmu_idle_mask_r(4),
		pwr_pmu_idle_mask_gr_enabled_f() |
		pwr_pmu_idle_mask_ce_2_enabled_f());

	data = gk20a_readl(g, pwr_pmu_idle_ctrl_r(4));
	data = set_field(data, pwr_pmu_idle_ctrl_value_m() |
			pwr_pmu_idle_ctrl_filter_m(),
			pwr_pmu_idle_ctrl_value_busy_f() |
			pwr_pmu_idle_ctrl_filter_disabled_f());
	gk20a_writel(g, pwr_pmu_idle_ctrl_r(4), data);

	gk20a_writel(g, pwr_pmu_idle_count_r(0), pwr_pmu_idle_count_reset_f(1));
	gk20a_writel(g, pwr_pmu_idle_count_r(4), pwr_pmu_idle_count_reset_f(1));
	gk20a_writel(g, pwr_pmu_idle_intr_status_r(),
		     pwr_pmu_idle_intr_status_intr_f(1));
}

u32 gk20a_pmu_read_idle_counter(struct gk20a *g, u32 counter_id)
{
	return pwr_pmu_idle_count_value_v(
		gk20a_readl(g, pwr_pmu_idle_count_r(counter_id)));
}

void gk20a_pmu_reset_idle_counter(struct gk20a *g, u32 counter_id)
{
	gk20a_writel(g, pwr_pmu_idle_count_r(counter_id),
		pwr_pmu_idle_count_reset_f(1));
}

u32 gk20a_pmu_read_idle_intr_status(struct gk20a *g)
{
	return pwr_pmu_idle_intr_status_intr_v(
		gk20a_readl(g, pwr_pmu_idle_intr_status_r()));
}

void gk20a_pmu_clear_idle_intr_status(struct gk20a *g)
{
	gk20a_writel(g, pwr_pmu_idle_intr_status_r(),
		     pwr_pmu_idle_intr_status_intr_f(1));
}

u32 gk20a_pmu_falcon_base_addr(void)
{
	return pwr_falcon_irqsset_r();
}

int gk20a_pmu_bar0_error_status(struct gk20a *g, u32 *bar0_status,
	u32 *etype)
{
	u32 val = 0;
	u32 err_status = 0;

	val = gk20a_readl(g, pwr_pmu_bar0_error_status_r());
	*bar0_status = val;
	if (val == 0U) {
		return 0;
	}
	if ((val & pwr_pmu_bar0_error_status_timeout_host_m()) != 0U) {
		*etype = ((val & pwr_pmu_bar0_error_status_err_cmd_m()) != 0U)
			? PMU_BAR0_HOST_WRITE_TOUT : PMU_BAR0_HOST_READ_TOUT;
	} else if ((val & pwr_pmu_bar0_error_status_timeout_fecs_m()) != 0U) {
		*etype = ((val & pwr_pmu_bar0_error_status_err_cmd_m()) != 0U)
			? PMU_BAR0_FECS_WRITE_TOUT : PMU_BAR0_FECS_READ_TOUT;
	} else if ((val & pwr_pmu_bar0_error_status_cmd_hwerr_m()) != 0U) {
		*etype = ((val & pwr_pmu_bar0_error_status_err_cmd_m()) != 0U)
			? PMU_BAR0_CMD_WRITE_HWERR : PMU_BAR0_CMD_READ_HWERR;
	} else if ((val & pwr_pmu_bar0_error_status_fecserr_m()) != 0U) {
		*etype = ((val & pwr_pmu_bar0_error_status_err_cmd_m()) != 0U)
			? PMU_BAR0_WRITE_FECSERR : PMU_BAR0_READ_FECSERR;
		err_status = gk20a_readl(g, pwr_pmu_bar0_fecs_error_r());
		/*
		 * BAR0_FECS_ERROR would only record the first error code if
		 * multiple FECS error happen. Once BAR0_FECS_ERROR is cleared,
		 * BAR0_FECS_ERROR can record the error code from FECS again.
		 * Writing status regiter to clear the FECS Hardware state.
		 */
		gk20a_writel(g, pwr_pmu_bar0_fecs_error_r(), err_status);
	} else if ((val & pwr_pmu_bar0_error_status_hosterr_m()) != 0U) {
		*etype = ((val & pwr_pmu_bar0_error_status_err_cmd_m()) != 0U)
			? PMU_BAR0_WRITE_HOSTERR : PMU_BAR0_READ_HOSTERR;
		/*
		 * BAR0_HOST_ERROR would only record the first error code if
		 * multiple HOST error happen. Once BAR0_HOST_ERROR is cleared,
		 * BAR0_HOST_ERROR can record the error code from HOST again.
		 * Writing status regiter to clear the FECS Hardware state.
		 *
		 * Defining clear ops for host err as gk20a does not have
		 * status register for this.
		 */
		if (g->ops.pmu.pmu_clear_bar0_host_err_status != NULL) {
			g->ops.pmu.pmu_clear_bar0_host_err_status(g);
		}
	} else {
		nvgpu_err(g, "PMU bar0 status type is not found");
	}

	/* Writing Bar0 status regiter to clear the Hardware state */
	gk20a_writel(g, pwr_pmu_bar0_error_status_r(), val);
	return (-EIO);
}
