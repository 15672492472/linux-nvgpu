/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/gk20a.h>
#include <nvgpu/io.h>
#include <nvgpu/gr/zbc.h>

#include "zbc_gm20b.h"

#include <nvgpu/hw/gm20b/hw_gr_gm20b.h>

int gm20b_gr_zbc_add_color(struct gk20a *g,
			   struct nvgpu_gr_zbc_entry *color_val, u32 index)
{
	/* update ds table */
	nvgpu_writel(g, gr_ds_zbc_color_r_r(),
		gr_ds_zbc_color_r_val_f(color_val->color_ds[0]));
	nvgpu_writel(g, gr_ds_zbc_color_g_r(),
		gr_ds_zbc_color_g_val_f(color_val->color_ds[1]));
	nvgpu_writel(g, gr_ds_zbc_color_b_r(),
		gr_ds_zbc_color_b_val_f(color_val->color_ds[2]));
	nvgpu_writel(g, gr_ds_zbc_color_a_r(),
		gr_ds_zbc_color_a_val_f(color_val->color_ds[3]));

	nvgpu_writel(g, gr_ds_zbc_color_fmt_r(),
		gr_ds_zbc_color_fmt_val_f(color_val->format));

	nvgpu_writel(g, gr_ds_zbc_tbl_index_r(),
		gr_ds_zbc_tbl_index_val_f(index + NVGPU_GR_ZBC_STARTOF_TABLE));

	/* trigger the write */
	nvgpu_writel(g, gr_ds_zbc_tbl_ld_r(),
		gr_ds_zbc_tbl_ld_select_c_f() |
		gr_ds_zbc_tbl_ld_action_write_f() |
		gr_ds_zbc_tbl_ld_trigger_active_f());

	return 0;
}

int gm20b_gr_zbc_add_depth(struct gk20a *g,
			   struct nvgpu_gr_zbc_entry *depth_val, u32 index)
{
	/* update ds table */
	nvgpu_writel(g, gr_ds_zbc_z_r(),
		gr_ds_zbc_z_val_f(depth_val->depth));

	nvgpu_writel(g, gr_ds_zbc_z_fmt_r(),
		gr_ds_zbc_z_fmt_val_f(depth_val->format));

	nvgpu_writel(g, gr_ds_zbc_tbl_index_r(),
		gr_ds_zbc_tbl_index_val_f(index + NVGPU_GR_ZBC_STARTOF_TABLE));

	/* trigger the write */
	nvgpu_writel(g, gr_ds_zbc_tbl_ld_r(),
		gr_ds_zbc_tbl_ld_select_z_f() |
		gr_ds_zbc_tbl_ld_action_write_f() |
		gr_ds_zbc_tbl_ld_trigger_active_f());

	return 0;
}
