/*
 * GP10B master
 *
 * Copyright (c) 2014-2019, NVIDIA CORPORATION.  All rights reserved.
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
#include <nvgpu/io.h>
#include <nvgpu/mc.h>
#include <nvgpu/ltc.h>
#include <nvgpu/engines.h>
#include <nvgpu/power_features/pg.h>
#include <nvgpu/gops_mc.h>

#include "mc_gp10b.h"

#include <nvgpu/atomic.h>

#include <nvgpu/hw/gp10b/hw_mc_gp10b.h>

void mc_gp10b_intr_mask(struct gk20a *g)
{
	nvgpu_writel(g, mc_intr_en_clear_r(NVGPU_MC_INTR_STALLING),
				U32_MAX);
	g->mc.intr_mask_restore[NVGPU_MC_INTR_STALLING] = 0;

	nvgpu_writel(g, mc_intr_en_clear_r(NVGPU_MC_INTR_NONSTALLING),
				U32_MAX);
	g->mc.intr_mask_restore[NVGPU_MC_INTR_NONSTALLING] = 0;
}

static u32 mc_gp10b_intr_pending_f(struct gk20a *g, u32 unit)
{
	u32 intr_pending_f = 0;

	switch (unit) {
	case MC_INTR_UNIT_BUS:
		intr_pending_f = mc_intr_pbus_pending_f();
		break;
	case MC_INTR_UNIT_PRIV_RING:
		intr_pending_f = mc_intr_priv_ring_pending_f();
		break;
	case MC_INTR_UNIT_FIFO:
		intr_pending_f = mc_intr_pfifo_pending_f();
		break;
	case MC_INTR_UNIT_LTC:
		intr_pending_f = mc_intr_ltc_pending_f();
		break;
	case MC_INTR_UNIT_GR:
		intr_pending_f = nvgpu_gr_engine_interrupt_mask(g);
		break;
	case MC_INTR_UNIT_PMU:
		intr_pending_f = mc_intr_pmu_pending_f();
		break;
	case MC_INTR_UNIT_HUB:
		intr_pending_f = mc_intr_replayable_fault_pending_f();
		break;
	case MC_INTR_UNIT_CE:
		intr_pending_f = nvgpu_ce_engine_interrupt_mask(g);
		break;
	default:
		nvgpu_err(g, "Invalid MC interrupt unit specified !!!");
		break;
	}

	return intr_pending_f;
}

static void mc_gp10b_isr_stall_secondary_1(struct gk20a *g, u32 mc_intr_0)
{
	if ((mc_intr_0 & mc_intr_ltc_pending_f()) != 0U) {
		g->ops.mc.ltc_isr(g);
	}
	if ((mc_intr_0 & mc_intr_pbus_pending_f()) != 0U) {
		g->ops.bus.isr(g);
	}
#ifdef CONFIG_NVGPU_DGPU
	if ((g->ops.mc.is_intr_nvlink_pending != NULL) &&
			g->ops.mc.is_intr_nvlink_pending(g, mc_intr_0)) {
		g->ops.nvlink.intr.isr(g);
	}
	if ((mc_intr_0 & mc_intr_pfb_pending_f()) != 0U &&
			(g->ops.mc.fbpa_isr != NULL)) {
		g->ops.mc.fbpa_isr(g);
	}
#endif
}

static void mc_gp10b_isr_stall_secondary_0(struct gk20a *g, u32 mc_intr_0)
{
	if ((g->ops.mc.is_intr_hub_pending != NULL) &&
			g->ops.mc.is_intr_hub_pending(g, mc_intr_0)) {
		g->ops.fb.intr.isr(g);
	}
	if ((mc_intr_0 & mc_intr_pfifo_pending_f()) != 0U) {
		g->ops.fifo.intr_0_isr(g);
	}
	if ((mc_intr_0 & mc_intr_pmu_pending_f()) != 0U) {
		g->ops.pmu.pmu_isr(g);
	}
	if ((mc_intr_0 & mc_intr_priv_ring_pending_f()) != 0U) {
		g->ops.priv_ring.isr(g);
	}
}

static void mc_gp10b_isr_stall_engine(struct gk20a *g,
			enum nvgpu_fifo_engine engine_enum, u32 engine_id)
{
	/* GR Engine */
	if (engine_enum == NVGPU_ENGINE_GR) {
		int ret_err = nvgpu_pg_elpg_protected_call(g,
					g->ops.gr.intr.stall_isr(g));
		if (ret_err != 0) {
			nvgpu_err(g, "Unable to handle gr interrupt");
		}
	}

	/* CE Engine */
	if (((engine_enum == NVGPU_ENGINE_GRCE) ||
			(engine_enum == NVGPU_ENGINE_ASYNC_CE)) &&
			(g->ops.ce.isr_stall != NULL)) {
		g->ops.ce.isr_stall(g,
			g->fifo.engine_info[engine_id].inst_id,
			g->fifo.engine_info[engine_id].pri_base);
	}
}

void mc_gp10b_intr_stall_unit_config(struct gk20a *g, u32 unit, bool enable)
{
	u32 unit_pending_f = mc_gp10b_intr_pending_f(g, unit);
	u32 reg = 0U;

	if (enable) {
		reg = mc_intr_en_set_r(NVGPU_MC_INTR_STALLING);
		g->mc.intr_mask_restore[NVGPU_MC_INTR_STALLING] |=
			unit_pending_f;
		nvgpu_writel(g, reg, unit_pending_f);
	} else {
		reg = mc_intr_en_clear_r(NVGPU_MC_INTR_STALLING);
		g->mc.intr_mask_restore[NVGPU_MC_INTR_STALLING] &=
			~unit_pending_f;
		nvgpu_writel(g, reg, unit_pending_f);
	}
}

void mc_gp10b_intr_nonstall_unit_config(struct gk20a *g, u32 unit, bool enable)
{
	u32 unit_pending_f = mc_gp10b_intr_pending_f(g, unit);
	u32 reg = 0U;

	if (enable) {
		reg = mc_intr_en_set_r(NVGPU_MC_INTR_NONSTALLING);
		g->mc.intr_mask_restore[NVGPU_MC_INTR_NONSTALLING] |=
			unit_pending_f;
		nvgpu_writel(g, reg, unit_pending_f);
	} else {
		reg = mc_intr_en_clear_r(NVGPU_MC_INTR_NONSTALLING);
		g->mc.intr_mask_restore[NVGPU_MC_INTR_NONSTALLING] &=
			~unit_pending_f;
		nvgpu_writel(g, reg, unit_pending_f);
	}
}

void mc_gp10b_isr_stall(struct gk20a *g)
{
	u32 mc_intr_0;
	u32 i;
	u32 engine_id = 0U;
	enum nvgpu_fifo_engine engine_enum;

	mc_intr_0 = nvgpu_readl(g, mc_intr_r(0));

	nvgpu_log(g, gpu_dbg_intr, "stall intr 0x%08x", mc_intr_0);

	for (i = 0U; i < g->fifo.num_engines; i++) {
		engine_id = g->fifo.active_engines_list[i];

		if ((mc_intr_0 &
			g->fifo.engine_info[engine_id].intr_mask) == 0U) {
			continue;
		}
		engine_enum = g->fifo.engine_info[engine_id].engine_enum;
		mc_gp10b_isr_stall_engine(g, engine_enum, engine_id);
	}

	mc_gp10b_isr_stall_secondary_0(g, mc_intr_0);
	mc_gp10b_isr_stall_secondary_1(g, mc_intr_0);
	nvgpu_log(g, gpu_dbg_intr, "stall intr done 0x%08x", mc_intr_0);

}

u32 mc_gp10b_intr_stall(struct gk20a *g)
{
	return nvgpu_readl(g, mc_intr_r(NVGPU_MC_INTR_STALLING));
}

void mc_gp10b_intr_stall_pause(struct gk20a *g)
{
	nvgpu_writel(g, mc_intr_en_clear_r(NVGPU_MC_INTR_STALLING), U32_MAX);
}

void mc_gp10b_intr_stall_resume(struct gk20a *g)
{
	nvgpu_writel(g, mc_intr_en_set_r(NVGPU_MC_INTR_STALLING),
			g->mc.intr_mask_restore[NVGPU_MC_INTR_STALLING]);
}

u32 mc_gp10b_intr_nonstall(struct gk20a *g)
{
	return nvgpu_readl(g, mc_intr_r(NVGPU_MC_INTR_NONSTALLING));
}

void mc_gp10b_intr_nonstall_pause(struct gk20a *g)
{
	nvgpu_writel(g, mc_intr_en_clear_r(NVGPU_MC_INTR_NONSTALLING),
		     U32_MAX);
}

void mc_gp10b_intr_nonstall_resume(struct gk20a *g)
{
	nvgpu_writel(g, mc_intr_en_set_r(NVGPU_MC_INTR_NONSTALLING),
			g->mc.intr_mask_restore[NVGPU_MC_INTR_NONSTALLING]);
}

bool mc_gp10b_is_intr1_pending(struct gk20a *g,
				      enum nvgpu_unit unit, u32 mc_intr_1)
{
	u32 mask;
	bool is_pending;

	switch (unit) {
	case NVGPU_UNIT_FIFO:
		mask = mc_intr_pfifo_pending_f();
		break;
	default:
		mask = 0U;
		break;
	}

	if (mask == 0U) {
		nvgpu_err(g, "unknown unit %d", unit);
		is_pending = false;
	} else {
		is_pending = ((mc_intr_1 & mask) != 0U);
	}

	return is_pending;
}

#ifdef CONFIG_NVGPU_NON_FUSA
void mc_gp10b_log_pending_intrs(struct gk20a *g)
{
	u32 i, intr;

	for (i = 0U; i < MAX_MC_INTR_REGS; i++) {
		intr = nvgpu_readl(g, mc_intr_r(i));
		if (intr == 0U) {
			continue;
		}
		nvgpu_info(g, "Pending intr%d=0x%08x", i, intr);
	}

}
#endif

void mc_gp10b_ltc_isr(struct gk20a *g)
{
	u32 mc_intr;
	u32 ltc;

	mc_intr = nvgpu_readl(g, mc_intr_ltc_r());
	nvgpu_err(g, "mc_ltc_intr: %08x", mc_intr);
	for (ltc = 0U; ltc < nvgpu_ltc_get_ltc_count(g); ltc++) {
		if ((mc_intr & BIT32(ltc)) == 0U) {
			continue;
		}
		g->ops.ltc.intr.isr(g, ltc);
	}
}
