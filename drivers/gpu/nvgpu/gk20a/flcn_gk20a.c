/*
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <nvgpu/falcon.h>
#include <nvgpu/pmu.h>

#include "gk20a/gk20a.h"

#include <nvgpu/hw/gk20a/hw_falcon_gk20a.h>

static int gk20a_flcn_reset(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;
	u32 unit_status = 0;
	int status = 0;

	if (flcn->flcn_engine_dep_ops.reset_eng)
		/* falcon & engine reset */
		status = flcn->flcn_engine_dep_ops.reset_eng(g);
	else {
		/* do falcon CPU hard reset */
		unit_status = gk20a_readl(g, base_addr +
				falcon_falcon_cpuctl_r());
		gk20a_writel(g, base_addr + falcon_falcon_cpuctl_r(),
			(unit_status | falcon_falcon_cpuctl_hreset_f(1)));
	}

	return status;
}

static bool gk20a_flcn_clear_halt_interrupt_status(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;
	u32 data = 0;
	bool status = false;

	gk20a_writel(g, base_addr + falcon_falcon_irqsclr_r(),
		gk20a_readl(g, base_addr + falcon_falcon_irqsclr_r()) |
		(0x10));
	data = gk20a_readl(g, (base_addr + falcon_falcon_irqstat_r()));

	if ((data & falcon_falcon_irqstat_halt_true_f()) !=
		falcon_falcon_irqstat_halt_true_f())
		/*halt irq is clear*/
		status = true;

	return status;
}

static void gk20a_flcn_set_irq(struct nvgpu_falcon *flcn, bool enable)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;

	if (!flcn->is_interrupt_enabled) {
		nvgpu_warn(g, "Interrupt not supported on flcn 0x%x ",
			flcn->flcn_id);
		/* Keep interrupt disabled */
		enable = false;
	}

	if (enable) {
		gk20a_writel(g, base_addr + falcon_falcon_irqmset_r(),
			flcn->intr_mask);
		gk20a_writel(g, base_addr + falcon_falcon_irqdest_r(),
			flcn->intr_dest);
	} else
		gk20a_writel(g, base_addr + falcon_falcon_irqmclr_r(),
			0xffffffff);
}

static bool gk20a_is_falcon_cpu_halted(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;

	return (gk20a_readl(g, base_addr + falcon_falcon_cpuctl_r()) &
			falcon_falcon_cpuctl_halt_intr_m() ?
			true : false);
}

static bool gk20a_is_falcon_idle(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;
	u32 unit_status = 0;
	bool status = false;

	unit_status = gk20a_readl(g,
		base_addr + falcon_falcon_idlestate_r());

	if (falcon_falcon_idlestate_falcon_busy_v(unit_status) == 0 &&
		falcon_falcon_idlestate_ext_busy_v(unit_status) == 0)
		status = true;
	else
		status = false;

	return status;
}

static bool gk20a_is_falcon_scrubbing_done(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;
	u32 unit_status = 0;
	bool status = false;

	unit_status = gk20a_readl(g,
		base_addr + falcon_falcon_dmactl_r());

	if (unit_status & (falcon_falcon_dmactl_dmem_scrubbing_m() |
		 falcon_falcon_dmactl_imem_scrubbing_m()))
		status = false;
	else
		status = true;

	return status;
}

static u32 gk20a_falcon_get_mem_size(struct nvgpu_falcon *flcn, u32 mem_type)
{
	struct gk20a *g = flcn->g;
	u32 mem_size = 0;
	u32 hw_cfg_reg = gk20a_readl(g,
		flcn->flcn_base + falcon_falcon_hwcfg_r());

	if (mem_type == MEM_DMEM)
		mem_size = falcon_falcon_hwcfg_dmem_size_v(hw_cfg_reg)
			<< GK20A_PMU_DMEM_BLKSIZE2;
	else
		mem_size = falcon_falcon_hwcfg_imem_size_v(hw_cfg_reg)
			<< GK20A_PMU_DMEM_BLKSIZE2;

	return mem_size;
}

static int flcn_mem_overflow_check(struct nvgpu_falcon *flcn,
		u32 offset, u32 size, u32 mem_type)
{
	struct gk20a *g = flcn->g;
	u32 mem_size = 0;

	if (size == 0) {
		nvgpu_err(g, "size is zero");
		return -EINVAL;
	}

	if (offset & 0x3) {
		nvgpu_err(g, "offset (0x%08x) not 4-byte aligned", offset);
		return -EINVAL;
	}

	mem_size = gk20a_falcon_get_mem_size(flcn, mem_type);
	if (!(offset <= mem_size && (offset + size) <= mem_size)) {
		nvgpu_err(g, "flcn-id 0x%x, copy overflow ",
			flcn->flcn_id);
		nvgpu_err(g, "total size 0x%x, offset 0x%x, copy size 0x%x",
			mem_size, offset, size);
		return -EINVAL;
	}

	return 0;
}

static int gk20a_flcn_copy_from_dmem(struct nvgpu_falcon *flcn,
		u32 src, u8 *dst, u32 size, u8 port)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;
	u32 i, words, bytes;
	u32 data, addr_mask;
	u32 *dst_u32 = (u32 *)dst;

	nvgpu_log_fn(g, " src dmem offset - %x, size - %x", src, size);

	if (flcn_mem_overflow_check(flcn, src, size, MEM_DMEM)) {
		nvgpu_err(g, "incorrect parameters");
		return -EINVAL;
	}

	nvgpu_mutex_acquire(&flcn->copy_lock);

	words = size >> 2;
	bytes = size & 0x3;

	addr_mask = falcon_falcon_dmemc_offs_m() |
			    falcon_falcon_dmemc_blk_m();

	src &= addr_mask;

	gk20a_writel(g, base_addr + falcon_falcon_dmemc_r(port),
		src | falcon_falcon_dmemc_aincr_f(1));

	for (i = 0; i < words; i++)
		dst_u32[i] = gk20a_readl(g,
			base_addr + falcon_falcon_dmemd_r(port));

	if (bytes > 0) {
		data = gk20a_readl(g, base_addr + falcon_falcon_dmemd_r(port));
		for (i = 0; i < bytes; i++)
			dst[(words << 2) + i] = ((u8 *)&data)[i];
	}

	nvgpu_mutex_release(&flcn->copy_lock);
	return 0;
}

static int gk20a_flcn_copy_to_dmem(struct nvgpu_falcon *flcn,
		u32 dst, u8 *src, u32 size, u8 port)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;
	u32 i, words, bytes;
	u32 data, addr_mask;
	u32 *src_u32 = (u32 *)src;

	nvgpu_log_fn(g, "dest dmem offset - %x, size - %x", dst, size);

	if (flcn_mem_overflow_check(flcn, dst, size, MEM_DMEM)) {
		nvgpu_err(g, "incorrect parameters");
		return -EINVAL;
	}

	nvgpu_mutex_acquire(&flcn->copy_lock);

	words = size >> 2;
	bytes = size & 0x3;

	addr_mask = falcon_falcon_dmemc_offs_m() |
		falcon_falcon_dmemc_blk_m();

	dst &= addr_mask;

	gk20a_writel(g, base_addr + falcon_falcon_dmemc_r(port),
		dst | falcon_falcon_dmemc_aincw_f(1));

	for (i = 0; i < words; i++)
		gk20a_writel(g,
			base_addr + falcon_falcon_dmemd_r(port), src_u32[i]);

	if (bytes > 0) {
		data = 0;
		for (i = 0; i < bytes; i++)
			((u8 *)&data)[i] = src[(words << 2) + i];
		gk20a_writel(g, base_addr + falcon_falcon_dmemd_r(port), data);
	}

	size = ALIGN(size, 4);
	data = gk20a_readl(g,
		base_addr + falcon_falcon_dmemc_r(port)) & addr_mask;
	if (data != ((dst + size) & addr_mask)) {
		nvgpu_warn(g, "copy failed. bytes written %d, expected %d",
			data - dst, size);
	}

	nvgpu_mutex_release(&flcn->copy_lock);

	return 0;
}

static int gk20a_flcn_copy_to_imem(struct nvgpu_falcon *flcn, u32 dst,
		u8 *src, u32 size, u8 port, bool sec, u32 tag)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;
	u32 *src_u32 = (u32 *)src;
	u32 words = 0;
	u32 blk = 0;
	u32 i = 0;

	nvgpu_log_info(g, "upload %d bytes to 0x%x", size, dst);

	if (flcn_mem_overflow_check(flcn, dst, size, MEM_IMEM)) {
		nvgpu_err(g, "incorrect parameters");
		return -EINVAL;
	}

	nvgpu_mutex_acquire(&flcn->copy_lock);

	words = size >> 2;
	blk = dst >> 8;

	nvgpu_log_info(g, "upload %d words to 0x%x block %d, tag 0x%x",
			words, dst, blk, tag);

	gk20a_writel(g, base_addr + falcon_falcon_imemc_r(port),
		falcon_falcon_imemc_offs_f(dst >> 2) |
		falcon_falcon_imemc_blk_f(blk) |
		/* Set Auto-Increment on write */
		falcon_falcon_imemc_aincw_f(1) |
		sec << 28);

	for (i = 0; i < words; i++) {
		if (i % 64 == 0) {
			/* tag is always 256B aligned */
			gk20a_writel(g, base_addr + falcon_falcon_imemt_r(0),
				tag);
			tag++;
		}

		gk20a_writel(g, base_addr + falcon_falcon_imemd_r(port),
			src_u32[i]);
	}

	/* WARNING : setting remaining bytes in block to 0x0 */
	while (i % 64) {
		gk20a_writel(g, base_addr + falcon_falcon_imemd_r(port), 0);
		i++;
	}

	nvgpu_mutex_release(&flcn->copy_lock);

	return 0;
}

static int gk20a_falcon_bootstrap(struct nvgpu_falcon *flcn,
	u32 boot_vector)
{
	struct gk20a *g = flcn->g;
	u32 base_addr = flcn->flcn_base;

	nvgpu_log_info(g, "boot vec 0x%x", boot_vector);

	gk20a_writel(g, base_addr + falcon_falcon_dmactl_r(),
		falcon_falcon_dmactl_require_ctx_f(0));

	gk20a_writel(g, base_addr + falcon_falcon_bootvec_r(),
		falcon_falcon_bootvec_vec_f(boot_vector));

	gk20a_writel(g, base_addr + falcon_falcon_cpuctl_r(),
		falcon_falcon_cpuctl_startcpu_f(1));

	return 0;
}

static void gk20a_falcon_engine_dependency_ops(struct nvgpu_falcon *flcn)
{
	struct nvgpu_falcon_engine_dependency_ops *flcn_eng_dep_ops =
			&flcn->flcn_engine_dep_ops;

	switch (flcn->flcn_id) {
	case FALCON_ID_PMU:
		flcn_eng_dep_ops->reset_eng = nvgpu_pmu_reset;
		break;
	default:
		/* NULL assignment make sure
		 * CPU hard reset in gk20a_flcn_reset() gets execute
		 * if falcon doesn't need specific reset implementation
		 */
		flcn_eng_dep_ops->reset_eng = NULL;
		break;
	}
}

void gk20a_falcon_ops(struct nvgpu_falcon *flcn)
{
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;

	flcn_ops->reset = gk20a_flcn_reset;
	flcn_ops->set_irq = gk20a_flcn_set_irq;
	flcn_ops->clear_halt_interrupt_status =
		gk20a_flcn_clear_halt_interrupt_status;
	flcn_ops->is_falcon_cpu_halted =  gk20a_is_falcon_cpu_halted;
	flcn_ops->is_falcon_idle =  gk20a_is_falcon_idle;
	flcn_ops->is_falcon_scrubbing_done =  gk20a_is_falcon_scrubbing_done;
	flcn_ops->copy_from_dmem = gk20a_flcn_copy_from_dmem;
	flcn_ops->copy_to_dmem = gk20a_flcn_copy_to_dmem;
	flcn_ops->copy_to_imem = gk20a_flcn_copy_to_imem;
	flcn_ops->bootstrap = gk20a_falcon_bootstrap;

	gk20a_falcon_engine_dependency_ops(flcn);
}

static void gk20a_falcon_hal_sw_init(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = flcn->g;

	switch (flcn->flcn_id) {
	case FALCON_ID_PMU:
		flcn->flcn_base = FALCON_PWR_BASE;
		flcn->is_falcon_supported = true;
		flcn->is_interrupt_enabled = true;
		break;
	case FALCON_ID_SEC2:
		flcn->flcn_base = FALCON_SEC_BASE;
		flcn->is_falcon_supported = false;
		flcn->is_interrupt_enabled = false;
		break;
	case FALCON_ID_FECS:
		flcn->flcn_base = FALCON_FECS_BASE;
		flcn->is_falcon_supported = true;
		flcn->is_interrupt_enabled = false;
	break;
	case FALCON_ID_GPCCS:
		flcn->flcn_base = FALCON_GPCCS_BASE;
		flcn->is_falcon_supported = true;
		flcn->is_interrupt_enabled = false;
	break;
	default:
		flcn->is_falcon_supported = false;
		nvgpu_err(g, "Invalid flcn request");
		break;
	}

	if (flcn->is_falcon_supported) {
		nvgpu_mutex_init(&flcn->copy_lock);
		gk20a_falcon_ops(flcn);
	} else
		nvgpu_log_info(g, "falcon 0x%x not supported on %s",
			flcn->flcn_id, g->name);
}

void gk20a_falcon_init_hal(struct gpu_ops *gops)
{
	gops->falcon.falcon_hal_sw_init = gk20a_falcon_hal_sw_init;
}
