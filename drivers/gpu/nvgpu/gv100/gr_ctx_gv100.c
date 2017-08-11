/*
 * GV100 Graphics Context
 *
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
 */

#include "gk20a/gk20a.h"
#include "gr_ctx_gv100.h"

int gr_gv100_get_netlist_name(struct gk20a *g, int index, char *name)
{
	u32 ver = g->gpu_characteristics.arch + g->gpu_characteristics.impl;

	switch (ver) {
		case NVGPU_GPUID_GV100:
			sprintf(name, "%s/%s", "gv100",
					GV100_NETLIST_IMAGE_FW_NAME);
			break;
		default:
			nvgpu_err(g, "no support for GPUID %x", ver);
	}

	return 0;
}

bool gr_gv100_is_firmware_defined(void)
{
	return true;
}
