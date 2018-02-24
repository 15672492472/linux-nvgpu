/*
 * GK20A Master Control
 *
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved.
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

#include "gk20a.h"
#include "mc_gk20a.h"

#include <nvgpu/timers.h>
#include <nvgpu/atomic.h>
#include <nvgpu/unit.h>

#include <nvgpu/hw/gk20a/hw_mc_gk20a.h>

void mc_gk20a_isr_stall(struct gk20a *g)
{
	u32 mc_intr_0;
	u32 engine_id_idx;
	u32 active_engine_id = 0;
	u32 engine_enum = ENGINE_INVAL_GK20A;

	mc_intr_0 = g->ops.mc.intr_stall(g);

	gk20a_dbg(gpu_dbg_intr, "stall intr %08x\n", mc_intr_0);

	for (engine_id_idx = 0; engine_id_idx < g->fifo.num_engines; engine_id_idx++) {
		active_engine_id = g->fifo.active_engines_list[engine_id_idx];

		if (mc_intr_0 & g->fifo.engine_info[active_engine_id].intr_mask) {
			engine_enum = g->fifo.engine_info[active_engine_id].engine_enum;
			/* GR Engine */
			if (engine_enum == ENGINE_GR_GK20A) {
				gr_gk20a_elpg_protected_call(g, gk20a_gr_isr(g));
			}

			/* CE Engine */
			if (((engine_enum == ENGINE_GRCE_GK20A) ||
				(engine_enum == ENGINE_ASYNC_CE_GK20A)) &&
				g->ops.ce2.isr_stall){
					g->ops.ce2.isr_stall(g,
					g->fifo.engine_info[active_engine_id].inst_id,
					g->fifo.engine_info[active_engine_id].pri_base);
			}
		}
	}
	if (mc_intr_0 & mc_intr_0_pfifo_pending_f())
		gk20a_fifo_isr(g);
	if (mc_intr_0 & mc_intr_0_pmu_pending_f())
		gk20a_pmu_isr(g);
	if (mc_intr_0 & mc_intr_0_priv_ring_pending_f())
		g->ops.priv_ring.isr(g);
	if (mc_intr_0 & mc_intr_0_ltc_pending_f())
		g->ops.ltc.isr(g);
	if (mc_intr_0 & mc_intr_0_pbus_pending_f())
		g->ops.bus.isr(g);
}

void mc_gk20a_intr_enable(struct gk20a *g)
{
	u32 eng_intr_mask = gk20a_fifo_engine_interrupt_mask(g);

	gk20a_writel(g, mc_intr_mask_1_r(),
		     mc_intr_0_pfifo_pending_f()
		     | eng_intr_mask);
	gk20a_writel(g, mc_intr_en_1_r(),
		mc_intr_en_1_inta_hardware_f());

	gk20a_writel(g, mc_intr_mask_0_r(),
		     mc_intr_0_pfifo_pending_f()
		     | mc_intr_0_priv_ring_pending_f()
		     | mc_intr_0_ltc_pending_f()
		     | mc_intr_0_pbus_pending_f()
		     | eng_intr_mask);
	gk20a_writel(g, mc_intr_en_0_r(),
		mc_intr_en_0_inta_hardware_f());
}

void mc_gk20a_intr_unit_config(struct gk20a *g, bool enable,
		bool is_stalling, u32 mask)
{
	u32 mask_reg = (is_stalling ? mc_intr_mask_0_r() :
					mc_intr_mask_1_r());

	if (enable) {
		gk20a_writel(g, mask_reg,
			gk20a_readl(g, mask_reg) |
			mask);
	} else {
		gk20a_writel(g, mask_reg,
			gk20a_readl(g, mask_reg) &
			~mask);
	}
}

void mc_gk20a_intr_stall_pause(struct gk20a *g)
{
	gk20a_writel(g, mc_intr_en_0_r(),
		mc_intr_en_0_inta_disabled_f());

	/* flush previous write */
	gk20a_readl(g, mc_intr_en_0_r());
}

void mc_gk20a_intr_stall_resume(struct gk20a *g)
{
	gk20a_writel(g, mc_intr_en_0_r(),
		mc_intr_en_0_inta_hardware_f());

	/* flush previous write */
	gk20a_readl(g, mc_intr_en_0_r());
}

void mc_gk20a_intr_nonstall_pause(struct gk20a *g)
{
	gk20a_writel(g, mc_intr_en_1_r(),
		mc_intr_en_0_inta_disabled_f());

	/* flush previous write */
	gk20a_readl(g, mc_intr_en_1_r());
}

void mc_gk20a_intr_nonstall_resume(struct gk20a *g)
{
	gk20a_writel(g, mc_intr_en_1_r(),
		mc_intr_en_0_inta_hardware_f());

	/* flush previous write */
	gk20a_readl(g, mc_intr_en_1_r());
}

u32 mc_gk20a_intr_stall(struct gk20a *g)
{
	return gk20a_readl(g, mc_intr_0_r());
}

u32 mc_gk20a_intr_nonstall(struct gk20a *g)
{
	return gk20a_readl(g, mc_intr_1_r());
}

void gk20a_mc_disable(struct gk20a *g, u32 units)
{
	u32 pmc;

	gk20a_dbg(gpu_dbg_info, "pmc disable: %08x\n", units);

	nvgpu_spinlock_acquire(&g->mc_enable_lock);
	pmc = gk20a_readl(g, mc_enable_r());
	pmc &= ~units;
	gk20a_writel(g, mc_enable_r(), pmc);
	nvgpu_spinlock_release(&g->mc_enable_lock);
}

void gk20a_mc_enable(struct gk20a *g, u32 units)
{
	u32 pmc;

	gk20a_dbg(gpu_dbg_info, "pmc enable: %08x\n", units);

	nvgpu_spinlock_acquire(&g->mc_enable_lock);
	pmc = gk20a_readl(g, mc_enable_r());
	pmc |= units;
	gk20a_writel(g, mc_enable_r(), pmc);
	gk20a_readl(g, mc_enable_r());
	nvgpu_spinlock_release(&g->mc_enable_lock);

	nvgpu_udelay(20);
}

void gk20a_mc_reset(struct gk20a *g, u32 units)
{
	g->ops.mc.disable(g, units);
	if (units & gk20a_fifo_get_all_ce_engine_reset_mask(g))
		nvgpu_udelay(500);
	else
		nvgpu_udelay(20);
	g->ops.mc.enable(g, units);
}

u32 gk20a_mc_boot_0(struct gk20a *g, u32 *arch, u32 *impl, u32 *rev)
{
	u32 val = __nvgpu_readl(g, mc_boot_0_r());

	if (val == 0xffffffff)
		return val;

	if (arch)
		*arch = mc_boot_0_architecture_v(val) <<
			NVGPU_GPU_ARCHITECTURE_SHIFT;

	if (impl)
		*impl = mc_boot_0_implementation_v(val);

	if (rev)
		*rev = (mc_boot_0_major_revision_v(val) << 4) |
			mc_boot_0_minor_revision_v(val);

	return val;
}

bool mc_gk20a_is_intr1_pending(struct gk20a *g,
			       enum nvgpu_unit unit, u32 mc_intr_1)
{
	u32 mask = 0;
	bool is_pending;

	switch (unit) {
	case NVGPU_UNIT_FIFO:
		mask = mc_intr_0_pfifo_pending_f();
		break;
	default:
		break;
	}

	if (mask == 0) {
		nvgpu_err(g, "unknown unit %d", unit);
		is_pending = false;
	} else {
		is_pending = (mc_intr_1 & mask) ? true : false;
	}

	return is_pending;
}
