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

#ifndef __NVGPU_MODULE_T19X_H__
#define __NVGPU_MODULE_T19X_H__

struct gk20a;

void t19x_init_support(struct gk20a *g);
void t19x_remove_support(struct gk20a *g);
void t19x_lockout_registers(struct gk20a *g);
void t19x_restore_registers(struct gk20a *g);

#endif
