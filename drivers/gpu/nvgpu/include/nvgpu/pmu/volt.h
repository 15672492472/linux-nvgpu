/*
* Copyright (c) 2016-2019, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef NVGPU_PMU_VOLT_H
#define NVGPU_PMU_VOLT_H

#include <nvgpu/types.h>
#include <nvgpu/boardobjgrp_e32.h>

struct gk20a;
#define CTRL_VOLT_RAIL_VOLT_DELTA_MAX_ENTRIES	0x04U

#define VOLT_GET_VOLT_RAIL(pvolt, rail_idx)	\
	((struct voltage_rail *)BOARDOBJGRP_OBJ_GET_BY_IDX( \
		&((pvolt)->volt_rail_metadata.volt_rails.super), (rail_idx)))

#define VOLT_RAIL_INDEX_IS_VALID(pvolt, rail_idx)	\
	(boardobjgrp_idxisvalid( \
		&((pvolt)->volt_rail_metadata.volt_rails.super), (rail_idx)))

#define VOLT_RAIL_VOLT_3X_SUPPORTED(pvolt) \
	(!BOARDOBJGRP_IS_EMPTY(&((pvolt)->volt_rail_metadata.volt_rails.super)))

/*!
 * metadata of voltage rail functionality.
 */
struct voltage_rail_metadata {
	u8 volt_domain_hal;
	u8 pct_delta;
	u32 ext_rel_delta_uv[CTRL_VOLT_RAIL_VOLT_DELTA_MAX_ENTRIES];
	u8 logic_rail_idx;
	u8 sram_rail_idx;
	struct boardobjgrp_e32 volt_rails;
};

struct voltage_device_metadata {
	struct boardobjgrp_e32 volt_devices;
};

struct voltage_policy_metadata {
	u8 perf_core_vf_seq_policy_idx;
	struct boardobjgrp_e32 volt_policies;
};

struct obj_volt {
	struct voltage_rail_metadata volt_rail_metadata;
	struct voltage_device_metadata volt_dev_metadata;
	struct voltage_policy_metadata volt_policy_metadata;
};
struct voltage_rail {
	struct boardobj super;
	u32 boot_voltage_uv;
	u8 rel_limit_vfe_equ_idx;
	u8 alt_rel_limit_vfe_equ_idx;
	u8 ov_limit_vfe_equ_idx;
	u8 pwr_equ_idx;
	u8 volt_scale_exp_pwr_equ_idx;
	u8 volt_dev_idx_default;
	u8 volt_dev_idx_ipc_vmin;
	u8 boot_volt_vfe_equ_idx;
	u8 vmin_limit_vfe_equ_idx;
	u8 volt_margin_limit_vfe_equ_idx;
	u32 volt_margin_limit_vfe_equ_mon_handle;
	u32 rel_limit_vfe_equ_mon_handle;
	u32 alt_rel_limit_vfe_equ_mon_handle;
	u32 ov_limit_vfe_equ_mon_handle;
	struct boardobjgrpmask_e32 volt_dev_mask;
	s32  volt_delta_uv[CTRL_VOLT_RAIL_VOLT_DELTA_MAX_ENTRIES];
	u32 vmin_limitu_v;
};

int nvgpu_volt_set_voltage(struct gk20a *g, u32 logic_voltage_uv,
		u32 sram_voltage_uv);
int nvgpu_volt_get_voltage(struct gk20a *g, u32 volt_domain, u32 *voltage_uv);
int nvgpu_volt_send_load_cmd_to_pmu(struct gk20a *g);

int nvgpu_volt_dev_sw_setup(struct gk20a *g);
int nvgpu_volt_dev_pmu_setup(struct gk20a *g);

int nvgpu_volt_policy_sw_setup(struct gk20a *g);
int nvgpu_volt_policy_pmu_setup(struct gk20a *g);

int nvgpu_volt_rail_sw_setup(struct gk20a *g);
int nvgpu_volt_rail_pmu_setup(struct gk20a *g);
u8 nvgpu_volt_rail_volt_domain_convert_to_idx(struct gk20a *g, u8 volt_domain);
int nvgpu_volt_get_vmin_ps35(struct gk20a *g, u32 *vmin_uv);
u8 nvgpu_volt_get_vmargin_ps35(struct gk20a *g);
int nvgpu_volt_rail_volt_dev_register(struct gk20a *g, struct voltage_rail
	*pvolt_rail, u8 volt_dev_idx, u8 operation_type);
u8 nvgpu_volt_rail_vbios_volt_domain_convert_to_internal
	(struct gk20a *g, u8 vbios_volt_domain);

#endif /* NVGPU_PMU_VOLT_H */
