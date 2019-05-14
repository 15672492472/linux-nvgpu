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
#include <nvgpu/pmu.h>
#include <nvgpu/dma.h>
#include <nvgpu/firmware.h>
#include <nvgpu/pmu/fw.h>

static int pmu_prepare_ns_ucode_blob(struct gk20a *g)
{
	struct nvgpu_pmu *pmu = &g->pmu;
	struct mm_gk20a *mm = &g->mm;
	struct vm_gk20a *vm = mm->pmu.vm;
	struct pmu_ucode_desc *desc;
	struct pmu_rtos_fw *rtos_fw = pmu->fw;
	u32 *ucode_image = NULL;
	int err = 0;

	nvgpu_log_fn(g, " ");

	desc = (struct pmu_ucode_desc *)(void *)rtos_fw->fw_image->data;
	ucode_image = (u32 *)(void *)((u8 *)desc + desc->descriptor_size);

	err = nvgpu_dma_alloc_map_sys(vm, GK20A_PMU_UCODE_SIZE_MAX,
			&pmu->fw->ucode);
	if (err != 0) {
		goto exit;
	}

	nvgpu_mem_wr_n(g, &pmu->fw->ucode, 0, ucode_image,
		desc->app_start_offset + desc->app_size);

exit:
	return err;
}

int nvgpu_pmu_ns_fw_bootstrap(struct gk20a *g, struct nvgpu_pmu *pmu)
{
	int err;
	u32 args_offset = 0;

	/* prepare blob for non-secure PMU boot */
	err = pmu_prepare_ns_ucode_blob(g);
	if (err != 0) {
		nvgpu_err(g, "non secure ucode blop consrtuct failed");
		return err;
	}

	/* Do non-secure PMU boot */
	nvgpu_mutex_acquire(&pmu->isr_mutex);
	nvgpu_falcon_reset(&pmu->flcn);
	pmu->isr_enabled = true;
	nvgpu_mutex_release(&pmu->isr_mutex);

	g->ops.pmu.setup_apertures(g);

	pmu->fw->ops.set_cmd_line_args_trace_size(
		pmu, GK20A_PMU_TRACE_BUFSIZE);
	pmu->fw->ops.set_cmd_line_args_trace_dma_base(pmu);
	pmu->fw->ops.set_cmd_line_args_trace_dma_idx(
		pmu, GK20A_PMU_DMAIDX_VIRT);

	pmu->fw->ops.set_cmd_line_args_cpu_freq(pmu,
		g->ops.clk.get_rate(g, CTRL_CLK_DOMAIN_PWRCLK));

	nvgpu_pmu_fw_get_cmd_line_args_offset(g, &args_offset);

	nvgpu_falcon_copy_to_dmem(&pmu->flcn, args_offset,
		(u8 *)(pmu->fw->ops.get_cmd_line_args_ptr(pmu)),
		pmu->fw->ops.get_cmd_line_args_size(pmu), 0);

	return g->ops.pmu.pmu_ns_bootstrap(g, pmu, args_offset);
}
