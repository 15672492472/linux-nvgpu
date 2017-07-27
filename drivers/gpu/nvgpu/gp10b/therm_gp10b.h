/*
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
#ifndef THERM_GP10B_H
#define THERM_GP10B_H

struct gk20a;
int gp10b_init_therm_setup_hw(struct gk20a *g);
int gp10b_elcg_init_idle_filters(struct gk20a *g);

#endif /* THERM_GP10B_H */
