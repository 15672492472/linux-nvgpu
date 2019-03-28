/*
 * GV11B Fifo
 *
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

#ifndef FIFO_GV11B_H
#define FIFO_GV11B_H

#define PBDMA_SUBDEVICE_ID  1U

#define FIFO_INVAL_PBDMA_ID	(~U32(0U))
#define FIFO_INVAL_VEID		(~U32(0U))

/* can be removed after runque support is added */

#define GR_RUNQUE			0U	/* pbdma 0 */
#define ASYNC_CE_RUNQUE			2U	/* pbdma 2 */

#define CHANNEL_INFO_VEID0		0U

#define MAX_PRE_SI_RETRIES		200000U	/* 1G/500KHz * 100 */

struct gpu_ops;

void gv11b_fifo_reset_pbdma_and_eng_faulted(struct gk20a *g,
			struct tsg_gk20a *tsg,
			u32 faulted_pbdma, u32 faulted_engine);
void gv11b_mmu_fault_id_to_eng_pbdma_id_and_veid(struct gk20a *g,
	u32 mmu_fault_id, u32 *active_engine_id, u32 *veid, u32 *pbdma_id);

void gv11b_dump_channel_status_ramfc(struct gk20a *g,
				     struct gk20a_debug_output *o,
				     struct nvgpu_channel_dump_info *info);
int gv11b_fifo_is_preempt_pending(struct gk20a *g, u32 id,
		 unsigned int id_type);
int gv11b_fifo_preempt_channel(struct gk20a *g, struct channel_gk20a *ch);
int gv11b_fifo_preempt_tsg(struct gk20a *g, struct tsg_gk20a *tsg);
int gv11b_fifo_enable_tsg(struct tsg_gk20a *tsg);
void gv11b_fifo_teardown_ch_tsg(struct gk20a *g, u32 act_eng_bitmask,
			u32 id, unsigned int id_type, unsigned int rc_type,
			 struct mmu_fault_info *mmfault);
void gv11b_fifo_teardown_mask_intr(struct gk20a *g);
void gv11b_fifo_teardown_unmask_intr(struct gk20a *g);
void gv11b_fifo_init_pbdma_intr_descs(struct fifo_gk20a *f);
int gv11b_init_fifo_reset_enable_hw(struct gk20a *g);
void gv11b_fifo_init_eng_method_buffers(struct gk20a *g,
					struct tsg_gk20a *tsg);
void gv11b_fifo_deinit_eng_method_buffers(struct gk20a *g,
					struct tsg_gk20a *tsg);
int gv11b_init_fifo_setup_hw(struct gk20a *g);

void gv11b_fifo_tsg_verify_status_faulted(struct channel_gk20a *ch);
u32 gv11b_fifo_get_preempt_timeout(struct gk20a *g);

void gv11b_fifo_init_ramfc_eng_method_buffer(struct gk20a *g,
			struct channel_gk20a *ch, struct nvgpu_mem *mem);
void gv11b_ring_channel_doorbell(struct channel_gk20a *c);
u64 gv11b_fifo_usermode_base(struct gk20a *g);
u32 gv11b_fifo_doorbell_token(struct channel_gk20a *c);
#endif
