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

#ifndef NVGPU_CLK_DOMAIN_H
#define NVGPU_CLK_DOMAIN_H

#include <nvgpu/pmu/pmuif/ctrlboardobj.h>
#include <nvgpu/pmu/pmuif/nvgpu_cmdif.h>
#include <nvgpu/pmu/clk/clk_domain.h>

#define CLK_DOMAIN_BOARDOBJGRP_VERSION 0x30
#define CLK_DOMAIN_BOARDOBJGRP_VERSION_35 0x35

#define CLK_TABLE_HAL_ENTRY_GP 0x02
#define CLK_TABLE_HAL_ENTRY_GV 0x03

#define CLK_CLKMON_VFE_INDEX_INVALID 0xFF

struct nvgpu_clk_domains;
struct nvgpu_clk_domain;

typedef int clkgetslaveclk(struct gk20a *g, struct nvgpu_clk_pmupstate *pclk,
			struct nvgpu_clk_domain *pdomain, u16 *clkmhz,
			u16 masterclkmhz);

struct clk_domain_3x {
	struct nvgpu_clk_domain super;
	bool b_noise_aware_capable;
};

struct clk_domain_3x_fixed {
	struct clk_domain_3x super;
	u16  freq_mhz;
};

struct clk_domain_3x_prog {
	struct clk_domain_3x super;
	u8  clk_prog_idx_first;
	u8  clk_prog_idx_last;
	bool b_force_noise_unaware_ordering;
	struct ctrl_clk_freq_delta factory_delta;
	short freq_delta_min_mhz;
	short freq_delta_max_mhz;
	struct ctrl_clk_clk_delta deltas;
	u8 noise_unaware_ordering_index;
	u8 noise_aware_ordering_index;
};

struct clk_domain_35_prog {
	struct clk_domain_3x_prog super;
	u8 pre_volt_ordering_index;
	u8 post_volt_ordering_index;
	u8 clk_pos;
	u8 clk_vf_curve_count;
	struct ctrl_clk_domain_info_35_prog_clk_mon clkmon_info;
	struct ctrl_clk_domain_control_35_prog_clk_mon clkmon_ctrl;
	u32 por_volt_delta_uv[CTRL_VOLT_VOLT_RAIL_CLIENT_MAX_RAILS];
};

struct clk_domain_3x_master {
	struct clk_domain_3x_prog super;
	u32 slave_idxs_mask;
};

struct clk_domain_35_master {
	struct clk_domain_35_prog super;
	struct clk_domain_3x_master master;
	struct boardobjgrpmask_e32 master_slave_domains_grp_mask;
};

struct clk_domain_3x_slave {
	struct clk_domain_3x_prog super;
	u8 master_idx;
	clkgetslaveclk *clkdomainclkgetslaveclk;
};

struct clk_domain_30_slave {
	u8 rsvd;
	u8 master_idx;
	clkgetslaveclk *clkdomainclkgetslaveclk;
};

struct clk_domain_35_slave {
	struct clk_domain_35_prog super;
	struct clk_domain_30_slave slave;
};

#endif /* NVGPU_CLK_DOMAIN_H */