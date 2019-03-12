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

#ifdef CONFIG_TEGRA_NVLINK

#include <nvgpu/io.h>
#include <nvgpu/gk20a.h>
#include "intr_and_err_handling_gv100.h"

#include <nvgpu/hw/gv100/hw_nvlipt_gv100.h>
#include <nvgpu/hw/gv100/hw_ioctrl_gv100.h>
#include <nvgpu/hw/gv100/hw_nvl_gv100.h>
#include <nvgpu/hw/gv100/hw_ioctrlmif_gv100.h>
#include <nvgpu/hw/gv100/hw_nvtlc_gv100.h>

/*
 * The manuals are missing some useful defines
 * we add them for now
 */
#define IPT_INTR_CONTROL_LINK(i) (nvlipt_intr_control_link0_r() + (i)*4U)
#define IPT_ERR_UC_STATUS_LINK(i) (nvlipt_err_uc_status_link0_r() + (i)*36U)
#define IPT_ERR_UC_MASK_LINK(i) (nvlipt_err_uc_mask_link0_r() + (i)*36U)
#define IPT_ERR_UC_SEVERITY_LINK(i) (nvlipt_err_uc_severity_link0_r() + (i)*36U)
#define IPT_ERR_UC_FIRST_LINK(i) (nvlipt_err_uc_first_link0_r() + (i)*36U)
#define IPT_ERR_UC_ADVISORY_LINK(i) (nvlipt_err_uc_advisory_link0_r() + (i)*36U)
#define IPT_ERR_C_STATUS_LINK(i) (nvlipt_err_c_status_link0_r() + (i)*36U)
#define IPT_ERR_C_MASK_LINK(i) (nvlipt_err_c_mask_link0_r() + (i)*36U)
#define IPT_ERR_C_FIRST_LINK(i) (nvlipt_err_c_first_link0_r() + (i)*36U)
#define IPT_ERR_CONTROL_LINK(i) (nvlipt_err_control_link0_r() + (i)*4U)

#define IPT_ERR_UC_ACTIVE_BITS	(nvlipt_err_uc_status_link0_dlprotocol_f(1) | \
				 nvlipt_err_uc_status_link0_datapoisoned_f(1) | \
				 nvlipt_err_uc_status_link0_flowcontrol_f(1) | \
				 nvlipt_err_uc_status_link0_responsetimeout_f(1) | \
				 nvlipt_err_uc_status_link0_targeterror_f(1) | \
				 nvlipt_err_uc_status_link0_unexpectedresponse_f(1) | \
				 nvlipt_err_uc_status_link0_receiveroverflow_f(1) | \
				 nvlipt_err_uc_status_link0_malformedpacket_f(1) | \
				 nvlipt_err_uc_status_link0_stompedpacketreceived_f(1) | \
				 nvlipt_err_uc_status_link0_unsupportedrequest_f(1) | \
				 nvlipt_err_uc_status_link0_ucinternal_f(1))
/*
 * Init TLC per link interrupts
 */
static void gv100_nvlink_tlc_intr_enable(struct gk20a *g, u32 link_id,
								bool enable)
{
	u32 reg_rx0 = 0, reg_rx1 = 0, reg_tx = 0;

	if (enable) {
		/* Set PROD values */
		reg_rx0 = 0x0FFFFFF;
		reg_rx1 = 0x03FFFFF;
		reg_tx =  0x1FFFFFF;
	}

	TLC_REG_WR32(g, link_id, nvtlc_rx_err_report_en_0_r(), reg_rx0);
	TLC_REG_WR32(g, link_id, nvtlc_rx_err_report_en_1_r(), reg_rx1);
	TLC_REG_WR32(g, link_id, nvtlc_tx_err_report_en_0_r(), reg_tx);
}

/*
 * helper function to get TLC intr status in common structure
 */
static void gv100_nvlink_tlc_get_intr(struct gk20a *g, u32 link_id)
{
	g->nvlink.tlc_rx_err_status_0[link_id] =
		TLC_REG_RD32(g, link_id, nvtlc_rx_err_status_0_r());
	g->nvlink.tlc_rx_err_status_1[link_id] =
		TLC_REG_RD32(g, link_id, nvtlc_rx_err_status_1_r());
	g->nvlink.tlc_tx_err_status_0[link_id] =
		TLC_REG_RD32(g, link_id, nvtlc_tx_err_status_0_r());
}

/*
 * Interrupt routine handler for TLC
 */
static void gv100_nvlink_tlc_isr(struct gk20a *g, u32 link_id)
{

	if (g->nvlink.tlc_rx_err_status_0[link_id] != 0U) {
		/* All TLC RX 0 errors are fatal. Notify and disable */
		nvgpu_err(g, "Fatal TLC RX 0 interrupt on link %d mask: %x",
			link_id, g->nvlink.tlc_rx_err_status_0[link_id]);
		TLC_REG_WR32(g, link_id, nvtlc_rx_err_first_0_r(),
				g->nvlink.tlc_rx_err_status_0[link_id]);
		TLC_REG_WR32(g, link_id, nvtlc_rx_err_status_0_r(),
				g->nvlink.tlc_rx_err_status_0[link_id]);
	}
	if (g->nvlink.tlc_rx_err_status_1[link_id] != 0U) {
		/* All TLC RX 1 errors are fatal. Notify and disable */
		nvgpu_err(g, "Fatal TLC RX 1 interrupt on link %d mask: %x",
			link_id, g->nvlink.tlc_rx_err_status_1[link_id]);
		TLC_REG_WR32(g, link_id, nvtlc_rx_err_first_1_r(),
				g->nvlink.tlc_rx_err_status_1[link_id]);
		TLC_REG_WR32(g, link_id, nvtlc_rx_err_status_1_r(),
				g->nvlink.tlc_rx_err_status_1[link_id]);
	}
	if (g->nvlink.tlc_tx_err_status_0[link_id] != 0U) {
		/* All TLC TX 0 errors are fatal. Notify and disable */
		nvgpu_err(g, "Fatal TLC TX 0 interrupt on link %d mask: %x",
			link_id, g->nvlink.tlc_tx_err_status_0[link_id]);
		TLC_REG_WR32(g, link_id, nvtlc_tx_err_first_0_r(),
				g->nvlink.tlc_tx_err_status_0[link_id]);
		TLC_REG_WR32(g, link_id, nvtlc_tx_err_status_0_r(),
				g->nvlink.tlc_tx_err_status_0[link_id]);
	}
}

/*
 * DLPL interrupt enable helper
 */
void gv100_nvlink_dlpl_intr_enable(struct gk20a *g, u32 link_id, bool enable)
{
	u32 reg = 0;

	/* Always disable nonstall tree */
	DLPL_REG_WR32(g, link_id, nvl_intr_nonstall_en_r(), 0);

	if (!enable)
	{
		DLPL_REG_WR32(g, link_id, nvl_intr_stall_en_r(), 0);
		return;
	}

	/* Clear interrupt register to get rid of stale state (W1C) */
	DLPL_REG_WR32(g, link_id, nvl_intr_r(), 0xffffffffU);
	DLPL_REG_WR32(g, link_id, nvl_intr_sw2_r(), 0xffffffffU);

	reg = nvl_intr_stall_en_tx_recovery_long_enable_f()          |
		nvl_intr_stall_en_tx_fault_ram_enable_f()            |
		nvl_intr_stall_en_tx_fault_interface_enable_f()      |
		nvl_intr_stall_en_tx_fault_sublink_change_enable_f() |
		nvl_intr_stall_en_rx_fault_sublink_change_enable_f() |
		nvl_intr_stall_en_rx_fault_dl_protocol_enable_f()    |
		nvl_intr_stall_en_ltssm_fault_enable_f();

	DLPL_REG_WR32(g, link_id, nvl_intr_stall_en_r(), reg);

	/* Configure error threshold */
	reg = DLPL_REG_RD32(g, link_id, nvl_sl1_error_rate_ctrl_r());
	reg = set_field(reg, nvl_sl1_error_rate_ctrl_short_threshold_man_m(),
			nvl_sl1_error_rate_ctrl_short_threshold_man_f(0x2));
	reg = set_field(reg, nvl_sl1_error_rate_ctrl_long_threshold_man_m(),
			nvl_sl1_error_rate_ctrl_long_threshold_man_f(0x2));
	DLPL_REG_WR32(g, link_id, nvl_sl1_error_rate_ctrl_r(), reg);
}

/*
 * DLPL per-link isr
 */

#define DLPL_NON_FATAL_INTR_MASK (nvl_intr_tx_replay_f(1)           | \
				  nvl_intr_tx_recovery_short_f(1)   | \
				  nvl_intr_tx_recovery_long_f(1)    | \
				  nvl_intr_rx_short_error_rate_f(1) | \
				  nvl_intr_rx_long_error_rate_f(1)  | \
				  nvl_intr_rx_ila_trigger_f(1)      | \
				  nvl_intr_ltssm_protocol_f(1))

#define DLPL_FATAL_INTR_MASK (  nvl_intr_ltssm_fault_f(1) | \
				nvl_intr_rx_fault_dl_protocol_f(1) | \
				nvl_intr_rx_fault_sublink_change_f(1) | \
				nvl_intr_tx_fault_sublink_change_f(1) | \
				nvl_intr_tx_fault_interface_f(1) | \
				nvl_intr_tx_fault_ram_f(1))

static void gv100_nvlink_dlpl_isr(struct gk20a *g, u32 link_id)
{
	u32 non_fatal_mask = 0;
	u32 fatal_mask = 0;
	u32 intr = 0;
	bool retrain = false;
	int err;

	intr = DLPL_REG_RD32(g, link_id, nvl_intr_r()) &
		DLPL_REG_RD32(g, link_id, nvl_intr_stall_en_r());

	if (intr == 0U) {
		return;
	}

	fatal_mask = intr & DLPL_FATAL_INTR_MASK;
	non_fatal_mask = intr & DLPL_NON_FATAL_INTR_MASK;

	nvgpu_err(g, " handling DLPL %d isr. Fatal: %x non-Fatal: %x",
		link_id, fatal_mask, non_fatal_mask);

	/* Check if we are not handling an interupt */
	if (((fatal_mask | non_fatal_mask) & ~intr) != 0U) {
		nvgpu_err(g, "Unable to service DLPL intr on link %d", link_id);
	}

	if ((non_fatal_mask & nvl_intr_tx_recovery_long_f(1)) != 0U) {
		retrain = true;
	}
	if (fatal_mask != 0U) {
		retrain = false;
	}

	if (retrain) {
		err = nvgpu_nvlink_train(g, link_id, false);
		if (err != 0) {
			nvgpu_err(g, "failed to retrain link %d", link_id);
		}
	}

	/* Clear interrupts */
	DLPL_REG_WR32(g, link_id, nvl_intr_r(), (non_fatal_mask | fatal_mask));
	DLPL_REG_WR32(g, link_id, nvl_intr_sw2_r(), 0xffffffffU);
}

/*
 * Initialize MIF API with PROD settings
 */
void gv100_nvlink_init_mif_intr(struct gk20a *g, u32 link_id)
{
	u32 tmp;

	/* Enable MIF RX error */

	/* Containment (make fatal) */
	tmp = 0;
	tmp = set_field(tmp,
		ioctrlmif_rx_err_contain_en_0_rxramdataparityerr_m(),
		ioctrlmif_rx_err_contain_en_0_rxramdataparityerr__prod_f());
	tmp = set_field(tmp,
		ioctrlmif_rx_err_contain_en_0_rxramhdrparityerr_m(),
		ioctrlmif_rx_err_contain_en_0_rxramhdrparityerr__prod_f());
	MIF_REG_WR32(g, link_id, ioctrlmif_rx_err_contain_en_0_r(), tmp);

	/* Logging (do not ignore) */
	tmp = 0;
	tmp = set_field(tmp,
		ioctrlmif_rx_err_log_en_0_rxramdataparityerr_m(),
		ioctrlmif_rx_err_log_en_0_rxramdataparityerr_f(1));
	tmp = set_field(tmp,
		ioctrlmif_rx_err_log_en_0_rxramhdrparityerr_m(),
		ioctrlmif_rx_err_log_en_0_rxramhdrparityerr_f(1));
	MIF_REG_WR32(g, link_id, ioctrlmif_rx_err_log_en_0_r(), tmp);

	/* Tx Error */
	/* Containment (make fatal) */
	tmp = 0;
	tmp = set_field(tmp,
		ioctrlmif_tx_err_contain_en_0_txramdataparityerr_m(),
		ioctrlmif_tx_err_contain_en_0_txramdataparityerr__prod_f());
	tmp = set_field(tmp,
		ioctrlmif_tx_err_contain_en_0_txramhdrparityerr_m(),
		ioctrlmif_tx_err_contain_en_0_txramhdrparityerr__prod_f());
	MIF_REG_WR32(g, link_id, ioctrlmif_tx_err_contain_en_0_r(), tmp);

	/* Logging (do not ignore) */
	tmp = 0;
	tmp = set_field(tmp, ioctrlmif_tx_err_log_en_0_txramdataparityerr_m(),
		ioctrlmif_tx_err_log_en_0_txramdataparityerr_f(1));
	tmp = set_field(tmp, ioctrlmif_tx_err_log_en_0_txramhdrparityerr_m(),
		ioctrlmif_tx_err_log_en_0_txramhdrparityerr_f(1));
	MIF_REG_WR32(g, link_id, ioctrlmif_tx_err_log_en_0_r(), tmp);

	/* Credit release */
	MIF_REG_WR32(g, link_id, ioctrlmif_rx_ctrl_buffer_ready_r(), 0x1);
	MIF_REG_WR32(g, link_id, ioctrlmif_tx_ctrl_buffer_ready_r(), 0x1);
}

/*
 * Enable per-link MIF interrupts
 */
void gv100_nvlink_mif_intr_enable(struct gk20a *g, u32 link_id,	bool enable)
{
	u32 reg0 = 0, reg1 = 0;

	if (enable) {
		reg0 = set_field(reg0,
			ioctrlmif_rx_err_report_en_0_rxramdataparityerr_m(),
			ioctrlmif_rx_err_report_en_0_rxramdataparityerr_f(1));
		reg0 = set_field(reg0,
			ioctrlmif_rx_err_report_en_0_rxramhdrparityerr_m(),
			ioctrlmif_rx_err_report_en_0_rxramhdrparityerr_f(1));
		reg1 = set_field(reg1,
			ioctrlmif_tx_err_report_en_0_txramdataparityerr_m(),
			ioctrlmif_tx_err_report_en_0_txramdataparityerr_f(1));
		reg1 = set_field(reg1,
			ioctrlmif_tx_err_report_en_0_txramhdrparityerr_m(),
			ioctrlmif_tx_err_report_en_0_txramhdrparityerr_f(1));
	}

	MIF_REG_WR32(g, link_id, ioctrlmif_rx_err_report_en_0_r(), reg0);
	MIF_REG_WR32(g, link_id, ioctrlmif_tx_err_report_en_0_r(), reg1);
}

/*
 * Handle per-link MIF interrupts
 */
static void gv100_nvlink_mif_isr(struct gk20a *g, u32 link_id)
{
	u32 intr, fatal_mask = 0;

	/* RX Errors */
	intr = MIF_REG_RD32(g, link_id, ioctrlmif_rx_err_status_0_r());
	if (intr != 0U) {
		if ((intr & ioctrlmif_rx_err_status_0_rxramdataparityerr_m()) !=
									0U) {
			nvgpu_err(g, "Fatal MIF RX interrupt hit on link %d: RAM_DATA_PARITY",
				link_id);
			fatal_mask |= ioctrlmif_rx_err_status_0_rxramdataparityerr_f(1);
		}
		if ((intr & ioctrlmif_rx_err_status_0_rxramhdrparityerr_m()) !=
									0U) {
			nvgpu_err(g, "Fatal MIF RX interrupt hit on link %d: RAM_HDR_PARITY",
				link_id);
			fatal_mask |= ioctrlmif_rx_err_status_0_rxramhdrparityerr_f(1);
		}

		if (fatal_mask != 0U) {
			MIF_REG_WR32(g, link_id, ioctrlmif_rx_err_first_0_r(),
				fatal_mask);
			MIF_REG_WR32(g, link_id, ioctrlmif_rx_err_status_0_r(),
				fatal_mask);
		}
	}

	/* TX Errors */
	fatal_mask = 0;
	intr = MIF_REG_RD32(g, link_id, ioctrlmif_tx_err_status_0_r());
	if (intr != 0U) {
		if ((intr & ioctrlmif_tx_err_status_0_txramdataparityerr_m()) !=
									0U) {
			nvgpu_err(g, "Fatal MIF TX interrupt hit on link %d: RAM_DATA_PARITY",
				link_id);
			fatal_mask |= ioctrlmif_tx_err_status_0_txramdataparityerr_f(1);
		}
		if ((intr & ioctrlmif_tx_err_status_0_txramhdrparityerr_m()) !=
									0U) {
			nvgpu_err(g, "Fatal MIF TX interrupt hit on link %d: RAM_HDR_PARITY",
				link_id);
			fatal_mask |= ioctrlmif_tx_err_status_0_txramhdrparityerr_f(1);
		}

		if (fatal_mask != 0U) {
			MIF_REG_WR32(g, link_id, ioctrlmif_tx_err_first_0_r(),
				fatal_mask);
			MIF_REG_WR32(g, link_id, ioctrlmif_tx_err_status_0_r(),
				fatal_mask);
		}
	}
}

/*
 * NVLIPT IP initialization (per-link)
 */
void gv100_nvlink_init_nvlipt_intr(struct gk20a *g, u32 link_id)
{
	/* init persistent scratch registers */
	IPT_REG_WR32(g, nvlipt_scratch_cold_r(),
		nvlipt_scratch_cold_data_init_v());

	/*
	 * AErr settings (top level)
	 */

	/* UC first and status reg (W1C) need to be cleared byt arch */
	IPT_REG_WR32(g, IPT_ERR_UC_FIRST_LINK(link_id), IPT_ERR_UC_ACTIVE_BITS);
	IPT_REG_WR32(g, IPT_ERR_UC_STATUS_LINK(link_id), IPT_ERR_UC_ACTIVE_BITS);

	/* AErr Severity */
	IPT_REG_WR32(g, IPT_ERR_UC_SEVERITY_LINK(link_id), IPT_ERR_UC_ACTIVE_BITS);

	/* AErr Control settings */
	IPT_REG_WR32(g, IPT_ERR_CONTROL_LINK(link_id),
		nvlipt_err_control_link0_fatalenable_f(1) |
		nvlipt_err_control_link0_nonfatalenable_f(1));
}

/*
 * Enable NVLIPT interrupts
 */
static void gv100_nvlink_nvlipt_intr_enable(struct gk20a *g, u32 link_id,
								bool enable)
{
	u32 val = 0;
	u32 reg;

	if (enable) {
		val = 1;
	}

	reg = IPT_REG_RD32(g, IPT_INTR_CONTROL_LINK(link_id));
	reg = set_field(reg, nvlipt_intr_control_link0_stallenable_m(),
			nvlipt_intr_control_link0_stallenable_f(val));
	reg = set_field(reg, nvlipt_intr_control_link0_nostallenable_m(),
			nvlipt_intr_control_link0_nostallenable_f(val));
	IPT_REG_WR32(g, IPT_INTR_CONTROL_LINK(link_id), reg);
}

/*
 * Per-link NVLIPT ISR handler
 */
static void gv100_nvlink_nvlipt_isr(struct gk20a *g, u32 link_id)
{
	/*
	 * Interrupt handling happens in leaf handlers. Assume all interrupts
	 * were handled and clear roll ups/
	 */
	IPT_REG_WR32(g, IPT_ERR_UC_FIRST_LINK(link_id), IPT_ERR_UC_ACTIVE_BITS);
	IPT_REG_WR32(g, IPT_ERR_UC_STATUS_LINK(link_id), IPT_ERR_UC_ACTIVE_BITS);

	return;
}

/*
 *******************************************************************************
 * Interrupt handling functions                                                *
 *******************************************************************************
 */

/*
 * Enable common interrupts
 */
void gv100_nvlink_common_intr_enable(struct gk20a *g, unsigned long mask)
{
	u32 reg, link_id;
	unsigned long bit;

	/* Init IOCTRL */
	for_each_set_bit(bit, &mask, NVLINK_MAX_LINKS_SW) {
		link_id = (u32)bit;
		reg = IOCTRL_REG_RD32(g, ioctrl_link_intr_0_mask_r(link_id));
		reg |= (ioctrl_link_intr_0_mask_fatal_f(1)       |
			ioctrl_link_intr_0_mask_nonfatal_f(1)    |
			ioctrl_link_intr_0_mask_correctable_f(1) |
			ioctrl_link_intr_0_mask_intra_f(1));
		IOCTRL_REG_WR32(g, ioctrl_link_intr_0_mask_r(link_id), reg);
	}

	reg = IOCTRL_REG_RD32(g, ioctrl_common_intr_0_mask_r());
	reg |= (ioctrl_common_intr_0_mask_fatal_f(1)       |
		ioctrl_common_intr_0_mask_nonfatal_f(1)    |
		ioctrl_common_intr_0_mask_correctable_f(1) |
		ioctrl_common_intr_0_mask_intra_f(1));
	IOCTRL_REG_WR32(g, ioctrl_common_intr_0_mask_r(), reg);

	/* Init NVLIPT */
	IPT_REG_WR32(g, nvlipt_intr_control_common_r(),
			nvlipt_intr_control_common_stallenable_f(1) |
			nvlipt_intr_control_common_nonstallenable_f(1));
}

/*
 * Enable link specific interrupts (top-level)
 */
void gv100_nvlink_enable_link_intr(struct gk20a *g, u32 link_id, bool enable)
{
	g->ops.nvlink.minion.enable_link_intr(g, link_id, enable);
	gv100_nvlink_dlpl_intr_enable(g, link_id, enable);
	gv100_nvlink_tlc_intr_enable(g, link_id, enable);
	gv100_nvlink_mif_intr_enable(g, link_id, enable);
	gv100_nvlink_nvlipt_intr_enable(g, link_id, enable);
}

/*
 * Top level interrupt handler
 */
void gv100_nvlink_isr(struct gk20a *g)
{
	unsigned long links;
	u32 link_id;
	unsigned long bit;

	links = ioctrl_top_intr_0_status_link_v(
			IOCTRL_REG_RD32(g, ioctrl_top_intr_0_status_r()));

	links &= g->nvlink.enabled_links;
	/* As per ARCH minion must be serviced first */
	g->ops.nvlink.minion.isr(g);

	for_each_set_bit(bit, &links, NVLINK_MAX_LINKS_SW) {
		link_id = (u32)bit;
		/* Cache error logs from TLC, DL handler may clear them */
		gv100_nvlink_tlc_get_intr(g, link_id);
		gv100_nvlink_dlpl_isr(g, link_id);
		gv100_nvlink_tlc_isr(g, link_id);
		gv100_nvlink_mif_isr(g, link_id);

		/* NVLIPT is top-level. Do it last */
		gv100_nvlink_nvlipt_isr(g, link_id);
	}
	return;
}

#endif /* CONFIG_TEGRA_NVLINK */
