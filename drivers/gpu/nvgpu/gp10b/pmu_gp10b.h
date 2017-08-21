/*
 * GP10B PMU
 *
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __PMU_GP10B_H_
#define __PMU_GP10B_H_

struct gk20a;


bool gp10b_is_lazy_bootstrap(u32 falcon_id);
bool gp10b_is_priv_load(u32 falcon_id);
bool gp10b_is_pmu_supported(struct gk20a *g);
int gp10b_init_pmu_setup_hw1(struct gk20a *g);
void gp10b_pmu_elpg_statistics(struct gk20a *g, u32 pg_engine_id,
		struct pmu_pg_stats_data *pg_stat_data);
int gp10b_pmu_setup_elpg(struct gk20a *g);
void pmu_dump_security_fuses_gp10b(struct gk20a *g);
int gp10b_load_falcon_ucode(struct gk20a *g, u32 falconidmask);
int gp10b_pg_gr_init(struct gk20a *g, u32 pg_engine_id);
void gp10b_write_dmatrfbase(struct gk20a *g, u32 addr);

#endif /*__PMU_GP10B_H_*/
