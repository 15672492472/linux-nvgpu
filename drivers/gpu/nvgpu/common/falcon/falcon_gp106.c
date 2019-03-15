/*
 * Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
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
#include <nvgpu/falcon.h>

#include "falcon_gk20a.h"
#include "falcon_gp106.h"

static void gp106_falcon_engine_dependency_ops(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = flcn->g;
	struct nvgpu_falcon_engine_dependency_ops *flcn_eng_dep_ops =
			&flcn->flcn_engine_dep_ops;

	switch (flcn->flcn_id) {
	case FALCON_ID_PMU:
		flcn_eng_dep_ops->reset_eng = g->ops.pmu.pmu_reset;
		break;
	case FALCON_ID_SEC2:
		flcn_eng_dep_ops->reset_eng = g->ops.sec2.sec2_reset;
		break;
	default:
		flcn_eng_dep_ops->reset_eng = NULL;
		break;
	}
}

static void gp106_falcon_ops(struct nvgpu_falcon *flcn)
{
	gk20a_falcon_ops(flcn);
	gp106_falcon_engine_dependency_ops(flcn);
}

int gp106_falcon_hal_sw_init(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = flcn->g;
	int err = 0;

	switch (flcn->flcn_id) {
	case FALCON_ID_PMU:
		flcn->flcn_base = g->ops.pmu.falcon_base_addr();
		flcn->is_falcon_supported = true;
		flcn->is_interrupt_enabled = true;
		break;
	case FALCON_ID_SEC2:
		flcn->flcn_base = g->ops.sec2.falcon_base_addr();
		flcn->is_falcon_supported = true;
		flcn->is_interrupt_enabled = false;
		break;
	case FALCON_ID_FECS:
		flcn->flcn_base = g->ops.gr.fecs_falcon_base_addr();
		flcn->is_falcon_supported = true;
		flcn->is_interrupt_enabled = false;
		break;
	case FALCON_ID_GPCCS:
		flcn->flcn_base = g->ops.gr.gpccs_falcon_base_addr();
		flcn->is_falcon_supported = true;
		flcn->is_interrupt_enabled = false;
		break;
	case FALCON_ID_NVDEC:
		flcn->flcn_base = g->ops.nvdec.falcon_base_addr();
		flcn->is_falcon_supported = true;
		flcn->is_interrupt_enabled = true;
		break;
	default:
		flcn->is_falcon_supported = false;
		break;
	}

	if (flcn->is_falcon_supported) {
		gp106_falcon_ops(flcn);
	} else {
		nvgpu_info(g, "falcon 0x%x not supported on %s",
			flcn->flcn_id, g->name);
	}

	return err;
}
