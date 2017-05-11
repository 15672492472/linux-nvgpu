/*
 * GV11B FB
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
 */

#ifndef _NVGPU_GV11B_FB
#define _NVGPU_GV11B_FB
struct gpu_ops;

#define	STALL_REG_INDEX			0
#define	NONSTALL_REG_INDEX		1

#define	NONREPLAY_REG_INDEX		0
#define	REPLAY_REG_INDEX		1

#define	FAULT_BUF_DISABLED		0
#define	FAULT_BUF_ENABLED		1

#define	FAULT_BUF_VALID			1
#define	CHECK_NEXT_FAULT_BUF		1

#define	HUB_INTR_TYPE_OTHER		1	/* bit 0 */
#define	HUB_INTR_TYPE_NONREPLAY		2	/* bit 1 */
#define	HUB_INTR_TYPE_REPLAY		4	/* bit 2 */
#define	HUB_INTR_TYPE_ECC_UNCORRECTED	8	/* bit 3 */
#define	HUB_INTR_TYPE_ACCESS_COUNTER	16	/* bit 4 */
#define	HUB_INTR_TYPE_ALL		(HUB_INTR_TYPE_OTHER | \
					 HUB_INTR_TYPE_NONREPLAY | \
					 HUB_INTR_TYPE_REPLAY | \
					 HUB_INTR_TYPE_ECC_UNCORRECTED | \
					 HUB_INTR_TYPE_ACCESS_COUNTER)

void gv11b_fb_enable_hub_intr(struct gk20a *g,
	 unsigned int index, unsigned int intr_type);
void gv11b_fb_disable_hub_intr(struct gk20a *g,
	 unsigned int index, unsigned int intr_type);
void gv11b_init_fb(struct gpu_ops *gops);
#endif
