/*
 * general clock structures & definitions
 *
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
#ifndef NVGPU_CLK_CLK_H
#define NVGPU_CLK_CLK_H

#include <nvgpu/types.h>

#include "clk_vin.h"
#include "clk_fll.h"
#include "clk_domain.h"
#include "clk_prog.h"
#include "clk_mclk.h"
#include "clk_freq_controller.h"
#include "clk_freq_domain.h"

#define NV_PERF_DOMAIN_4X_CLOCK_DOMAIN_SKIP		0x10U
#define NV_PERF_DOMAIN_4X_CLOCK_DOMAIN_MASK		0x1FU
#define NV_PERF_DOMAIN_4X_CLOCK_DOMAIN_SHIFT		0U
#define BOOT_GPCCLK_MHZ					952U

struct gk20a;

int clk_set_boot_fll_clk(struct gk20a *g);

struct clockentry {
		u8 vbios_clk_domain;
		u8 clk_which;
		u8 perf_index;
		u32 api_clk_domain;
};

struct change_fll_clk {
		u32 api_clk_domain;
		u16 clkmhz;
		u32 voltuv;
};

#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_MAX_NUMCLKS         9U

struct vbios_clock_domain {
	u8 clock_type;
	u8 num_domains;
	struct clockentry clock_entry[NV_PERF_HEADER_4X_CLOCKS_DOMAINS_MAX_NUMCLKS];
};

struct vbios_clocks_table_1x_hal_clock_entry {
	u32 domain;
	bool b_noise_aware_capable;
	u8 clk_vf_curve_count;
};

#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_4_GPC2CLK           0U
#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_4_XBAR2CLK          1U
#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_4_DRAMCLK           2U
#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_4_SYS2CLK           3U
#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_4_HUB2CLK           4U
#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_4_MSDCLK            5U
#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_4_PWRCLK            6U
#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_4_DISPCLK           7U
#define NV_PERF_HEADER_4X_CLOCKS_DOMAINS_4_NUMCLKS           8U

#define PERF_CLK_MCLK           0U
#define PERF_CLK_DISPCLK        1U
#define PERF_CLK_GPC2CLK        2U
#define PERF_CLK_HOSTCLK        3U
#define PERF_CLK_LTC2CLK        4U
#define PERF_CLK_SYS2CLK        5U
#define PERF_CLK_HUB2CLK        6U
#define PERF_CLK_LEGCLK         7U
#define PERF_CLK_MSDCLK         8U
#define PERF_CLK_XCLK           9U
#define PERF_CLK_PWRCLK         10U
#define PERF_CLK_XBAR2CLK       11U
#define PERF_CLK_PCIEGENCLK     12U
#define PERF_CLK_NUM            13U

struct nvgpu_set_fll_clk;

int clk_domain_print_vf_table(struct gk20a *g, u32 clkapidomain);
int clk_domain_get_f_or_v(struct gk20a *g, u32 clkapidomain,
	u16 *pclkmhz, u32 *pvoltuv, u8 railidx);
int clk_domain_freq_to_volt(struct gk20a *g, u8 clkdomain_idx,
	u32 *pclkmhz, u32 *pvoltuv, u8 railidx);
int clk_domain_volt_to_freq( struct gk20a *g, u8 clkdomain_idx,
	u32 *pclkmhz, u32 *pvoltuv, u8 railidx);
int clk_set_fll_clks(struct gk20a *g, struct nvgpu_set_fll_clk *setfllclk);
int clk_pmu_freq_controller_load(struct gk20a *g, bool bload, u8 bit_idx);
int clk_pmu_freq_effective_avg_load(struct gk20a *g, bool bload);
int clk_freq_effective_avg(struct gk20a *g, u32 *freqkHz, u32  clkDomainMask);
#endif /* NVGPU_CLK_CLK_H */