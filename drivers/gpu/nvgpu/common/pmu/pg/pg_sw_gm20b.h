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

#ifndef NVGPU_PG_SW_GM20B_H
#define NVGPU_PG_SW_GM20B_H

#include <nvgpu/types.h>

struct gk20a;
struct pmu_pg_stats_data;

#define ZBC_MASK(i)			U16(~(~(0U) << ((i)+1U)) & 0xfffeU)

u32 gm20b_pmu_pg_engines_list(struct gk20a *g);
u32 gm20b_pmu_pg_feature_list(struct gk20a *g, u32 pg_engine_id);
void gm20b_pmu_save_zbc(struct gk20a *g, u32 entries);
int gm20b_pmu_elpg_statistics(struct gk20a *g, u32 pg_engine_id,
	struct pmu_pg_stats_data *pg_stat_data);

#endif /* NVGPU_PG_SW_GM20B_H */
