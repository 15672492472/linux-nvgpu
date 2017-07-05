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
#include <nvgpu/lock.h>
#include <nvgpu/timers.h>
#include <nvgpu/falcon.h>

#include "gk20a/gk20a.h"

int nvgpu_flcn_wait_idle(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = flcn->g;
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;
	struct nvgpu_timeout timeout;
	u32 idle_stat;

	if (!flcn_ops->is_falcon_idle) {
		nvgpu_warn(g, "Invalid op on falcon 0x%x ", flcn->flcn_id);
		return -EINVAL;
	}

	nvgpu_timeout_init(g, &timeout, 2000, NVGPU_TIMER_RETRY_TIMER);

	/* wait for falcon idle */
	do {
		idle_stat = flcn_ops->is_falcon_idle(flcn);

		if (idle_stat)
			break;

		if (nvgpu_timeout_expired_msg(&timeout,
			"waiting for falcon idle: 0x%08x", idle_stat))
			return -EBUSY;

		nvgpu_usleep_range(100, 200);
	} while (1);

	return 0;
}

int nvgpu_flcn_reset(struct nvgpu_falcon *flcn)
{
	int status = -EINVAL;

	if (flcn->flcn_ops.reset)
		status = flcn->flcn_ops.reset(flcn);
	else
		nvgpu_warn(flcn->g, "Invalid op on falcon 0x%x ",
			flcn->flcn_id);

	return status;
}

void nvgpu_flcn_set_irq(struct nvgpu_falcon *flcn, bool enable,
	u32 intr_mask, u32 intr_dest)
{
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;

	if (flcn_ops->set_irq) {
		flcn->intr_mask = intr_mask;
		flcn->intr_dest = intr_dest;
		flcn_ops->set_irq(flcn, enable);
	} else
		nvgpu_warn(flcn->g, "Invalid op on falcon 0x%x ",
			flcn->flcn_id);
}

bool nvgpu_flcn_get_mem_scrubbing_status(struct nvgpu_falcon *flcn)
{
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;
	bool status = false;

	if (flcn_ops->is_falcon_scrubbing_done)
		status = flcn_ops->is_falcon_scrubbing_done(flcn);
	else
		nvgpu_warn(flcn->g, "Invalid op on falcon 0x%x ",
			flcn->flcn_id);

	return status;
}

bool nvgpu_flcn_get_cpu_halted_status(struct nvgpu_falcon *flcn)
{
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;
	bool status = false;

	if (flcn_ops->is_falcon_cpu_halted)
		status = flcn_ops->is_falcon_cpu_halted(flcn);
	else
		nvgpu_warn(flcn->g, "Invalid op on falcon 0x%x ",
			flcn->flcn_id);

	return status;
}

int nvgpu_flcn_wait_for_halt(struct nvgpu_falcon *flcn, unsigned int timeout)
{
	struct gk20a *g = flcn->g;
	struct nvgpu_timeout to;
	int status = 0;

	nvgpu_timeout_init(g, &to, timeout, NVGPU_TIMER_CPU_TIMER);
	do {
		if (nvgpu_flcn_get_cpu_halted_status(flcn))
			break;

		nvgpu_udelay(10);
	} while (!nvgpu_timeout_expired(&to));

	if (nvgpu_timeout_peek_expired(&to))
		status = -EBUSY;

	return status;
}

int nvgpu_flcn_clear_halt_intr_status(struct nvgpu_falcon *flcn,
	unsigned int timeout)
{
	struct gk20a *g = flcn->g;
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;
	struct nvgpu_timeout to;
	int status = 0;

	if (!flcn_ops->clear_halt_interrupt_status) {
		nvgpu_warn(flcn->g, "Invalid op on falcon 0x%x ",
			flcn->flcn_id);
		return -EINVAL;
	}

	nvgpu_timeout_init(g, &to, timeout, NVGPU_TIMER_CPU_TIMER);
	do {
		if (flcn_ops->clear_halt_interrupt_status(flcn))
			break;

		nvgpu_udelay(1);
	} while (!nvgpu_timeout_expired(&to));

	if (nvgpu_timeout_peek_expired(&to))
		status = -EBUSY;

	return status;
}

bool nvgpu_flcn_get_idle_status(struct nvgpu_falcon *flcn)
{
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;
	bool status = false;

	if (flcn_ops->is_falcon_idle)
		status = flcn_ops->is_falcon_idle(flcn);
	else
		nvgpu_warn(flcn->g, "Invalid op on falcon 0x%x ",
			flcn->flcn_id);

	return status;
}

int nvgpu_flcn_copy_from_dmem(struct nvgpu_falcon *flcn,
	u32 src, u8 *dst, u32 size, u8 port)
{
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;

	return flcn_ops->copy_from_dmem(flcn, src, dst, size, port);
}

int nvgpu_flcn_copy_to_dmem(struct nvgpu_falcon *flcn,
	u32 dst, u8 *src, u32 size, u8 port)
{
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;

	return flcn_ops->copy_to_dmem(flcn, dst, src, size, port);
}

int nvgpu_flcn_copy_to_imem(struct nvgpu_falcon *flcn,
	u32 dst, u8 *src, u32 size, u8 port, bool sec, u32 tag)
{
	struct nvgpu_falcon_ops *flcn_ops = &flcn->flcn_ops;
	int status = -EINVAL;

	if (flcn_ops->copy_to_imem)
		status = flcn_ops->copy_to_imem(flcn, dst, src, size, port,
					sec, tag);
	else
		nvgpu_warn(flcn->g, "Invalid op on falcon 0x%x ",
			flcn->flcn_id);

	return status;
}

void nvgpu_flcn_sw_init(struct gk20a *g, u32 flcn_id)
{
	struct nvgpu_falcon *flcn = NULL;
	struct gpu_ops *gops = &g->ops;

	switch (flcn_id) {
	case FALCON_ID_PMU:
		flcn = &g->pmu_flcn;
		flcn->flcn_id = flcn_id;
		g->pmu.flcn = &g->pmu_flcn;
		g->pmu.g = g;
		break;
	case FALCON_ID_SEC2:
		flcn = &g->sec2_flcn;
		flcn->flcn_id = flcn_id;
		break;
	case FALCON_ID_FECS:
		flcn = &g->fecs_flcn;
		flcn->flcn_id = flcn_id;
		break;
	case FALCON_ID_GPCCS:
		flcn = &g->gpccs_flcn;
		flcn->flcn_id = flcn_id;
		break;
	default:
		nvgpu_err(g, "Invalid/Unsupported falcon ID %x", flcn_id);
		break;
	};

	/* call to HAL method to assign flcn base & ops to selected falcon */
	if (flcn) {
		flcn->g = g;
		gops->falcon.falcon_hal_sw_init(flcn);
	}
}
