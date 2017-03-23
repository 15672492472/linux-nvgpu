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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef BUS_GK20A_H
#define BUS_GK20A_H

struct gk20a;
struct gpu_ops;
struct mem_desc;

void gk20a_init_bus(struct gpu_ops *gops);

void gk20a_bus_isr(struct gk20a *g);
int gk20a_read_ptimer(struct gk20a *g, u64 *value);
void gk20a_bus_init_hw(struct gk20a *g);

#endif /* GK20A_H */
