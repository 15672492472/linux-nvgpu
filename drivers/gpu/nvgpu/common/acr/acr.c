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

#include <nvgpu/types.h>
#include <nvgpu/dma.h>
#include <nvgpu/firmware.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/acr.h>

#include "acr_priv.h"
#ifdef CONFIG_NVGPU_ACR_LEGACY
#include "acr_sw_gm20b.h"
#include "acr_sw_gp10b.h"
#endif
#include "acr_sw_gv11b.h"
#ifdef CONFIG_NVGPU_DGPU
#include "acr_sw_gv100.h"
#include "acr_sw_tu104.h"
#endif

/* ACR public API's */
bool nvgpu_acr_is_lsf_lazy_bootstrap(struct gk20a *g, struct nvgpu_acr *acr,
	u32 falcon_id)
{
	if (nvgpu_is_enabled(g, NVGPU_IS_FMODEL) || acr == NULL) {
		return false;
	}

	return acr->lsf[falcon_id].is_lazy_bootstrap;
}

int nvgpu_acr_alloc_blob_prerequisite(struct gk20a *g, struct nvgpu_acr *acr,
	size_t size)
{
	if (nvgpu_is_enabled(g, NVGPU_IS_FMODEL)) {
		return 0;
	}

	if (acr == NULL) {
		return -EINVAL;
	}

	return acr->alloc_blob_space(g, size, &acr->ucode_blob);
}

/* ACR blob construct & bootstrap */
int nvgpu_acr_bootstrap_hs_acr(struct gk20a *g, struct nvgpu_acr *acr)
{
	int err = 0;

	if (nvgpu_is_enabled(g, NVGPU_IS_FMODEL)) {
		return 0;
	}

	if (acr == NULL) {
		return -EINVAL;
	}

	err = acr->bootstrap_hs_acr(g, acr, &acr->acr);
	if (err != 0) {
		nvgpu_err(g, "ACR bootstrap failed");
	}

	return err;
}

int nvgpu_acr_construct_execute(struct gk20a *g, struct nvgpu_acr *acr)
{
	int err = 0;

	if (nvgpu_is_enabled(g, NVGPU_IS_FMODEL)) {
		return 0;
	}

	if (acr == NULL) {
		return -EINVAL;
	}

	err = acr->prepare_ucode_blob(g);
	if (err != 0) {
		nvgpu_err(g, "ACR ucode blob prepare failed");
		goto done;
	}

	err = nvgpu_acr_bootstrap_hs_acr(g, acr);
	if (err != 0) {
		nvgpu_err(g, "Bootstrap HS ACR failed");
	}

done:
	return err;
}

/* ACR init */
int nvgpu_acr_init(struct gk20a *g, struct nvgpu_acr **acr)
{
	u32 ver = g->params.gpu_arch + g->params.gpu_impl;
	int err = 0;

	if (nvgpu_is_enabled(g, NVGPU_IS_FMODEL)) {
		goto done;
	}

	if (*acr != NULL) {
		/*
		 * Recovery/unrailgate case, we do not need to do ACR init as ACR is
		 * set during cold boot & doesn't execute ACR clean up as part off
		 * sequence, so reuse to perform faster boot.
		 */
		return err;
	}

	*acr = (struct nvgpu_acr *) nvgpu_kzalloc(g, sizeof(struct nvgpu_acr));
	if (g->acr == NULL) {
		err = -ENOMEM;
		goto done;
	}

	switch (ver) {
#ifdef CONFIG_NVGPU_ACR_LEGACY
	case GK20A_GPUID_GM20B:
	case GK20A_GPUID_GM20B_B:
		nvgpu_gm20b_acr_sw_init(g, *acr);
		break;
	case NVGPU_GPUID_GP10B:
		nvgpu_gp10b_acr_sw_init(g, *acr);
		break;
#endif
	case NVGPU_GPUID_GV11B:
		nvgpu_gv11b_acr_sw_init(g, *acr);
		break;
#ifdef CONFIG_NVGPU_DGPU
	case NVGPU_GPUID_GV100:
		nvgpu_gv100_acr_sw_init(g, *acr);
		break;
	case NVGPU_GPUID_TU104:
		nvgpu_tu104_acr_sw_init(g, *acr);
		break;
#endif
	default:
		nvgpu_kfree(g, *acr);
		err = -EINVAL;
		nvgpu_err(g, "no support for GPUID %x", ver);
		break;
	}

done:
	return err;
}
