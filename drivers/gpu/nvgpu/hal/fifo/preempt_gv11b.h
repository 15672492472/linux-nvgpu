/*
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
#ifndef FIFO_PREEMPT_GV11B_H
#define FIFO_PREEMPT_GV11B_H

#include <nvgpu/types.h>

#define PREEMPT_PENDING_POLL_PRE_SI_RETRIES	200000U	/* 1G/500KHz * 100 */

struct gk20a;
struct channel_gk20a;
struct tsg_gk20a;

void gv11b_fifo_preempt_trigger(struct gk20a *g, u32 id, unsigned int id_type);
int  gv11b_fifo_preempt_channel(struct gk20a *g, struct channel_gk20a *ch);
int  gv11b_fifo_preempt_tsg(struct gk20a *g, struct tsg_gk20a *tsg);
int  gv11b_fifo_is_preempt_pending(struct gk20a *g, u32 id,
			unsigned int id_type);
void gv11b_fifo_preempt_runlists_for_rc(struct gk20a *g, u32 runlists_mask);

int gv11b_fifo_preempt_poll_pbdma(struct gk20a *g, u32 tsgid, u32 pbdma_id);

#endif /* FIFO_PREEMPT_GV11B_H */
