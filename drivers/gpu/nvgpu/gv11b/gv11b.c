/*
 * GV11B Graphics
 *
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
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

#include "gk20a/gk20a.h"

#include "gv11b/gv11b.h"

int gv11b_init_gpu_characteristics(struct gk20a *g)
{
	gk20a_init_gpu_characteristics(g);
	g->gpu_characteristics.flags |=
		NVGPU_GPU_FLAGS_SUPPORT_TSG_SUBCONTEXTS |
		NVGPU_GPU_FLAGS_SUPPORT_IO_COHERENCE;
	return 0;
}
