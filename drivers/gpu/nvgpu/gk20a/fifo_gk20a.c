/*
 * GK20A Graphics FIFO (gr host)
 *
 * Copyright (c) 2011-2018, NVIDIA CORPORATION.  All rights reserved.
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

#include <trace/events/gk20a.h>

#include <nvgpu/mm.h>
#include <nvgpu/dma.h>
#include <nvgpu/timers.h>
#include <nvgpu/semaphore.h>
#include <nvgpu/enabled.h>
#include <nvgpu/kmem.h>
#include <nvgpu/log.h>
#include <nvgpu/soc.h>
#include <nvgpu/atomic.h>
#include <nvgpu/bug.h>
#include <nvgpu/log2.h>
#include <nvgpu/debug.h>
#include <nvgpu/nvhost.h>
#include <nvgpu/barrier.h>
#include <nvgpu/ctxsw_trace.h>
#include <nvgpu/error_notifier.h>
#include <nvgpu/ptimer.h>
#include <nvgpu/io.h>
#include <nvgpu/utils.h>
#include <nvgpu/fifo.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/unit.h>
#include <nvgpu/types.h>
#include <nvgpu/vm_area.h>

#include "mm_gk20a.h"

#include <nvgpu/hw/gk20a/hw_fifo_gk20a.h>
#include <nvgpu/hw/gk20a/hw_pbdma_gk20a.h>
#include <nvgpu/hw/gk20a/hw_ccsr_gk20a.h>
#include <nvgpu/hw/gk20a/hw_ram_gk20a.h>
#include <nvgpu/hw/gk20a/hw_top_gk20a.h>
#include <nvgpu/hw/gk20a/hw_gr_gk20a.h>

#define FECS_METHOD_WFI_RESTORE 0x80000U
#define FECS_MAILBOX_0_ACK_RESTORE 0x4U

static const char *const pbdma_intr_fault_type_desc[] = {
	"MEMREQ timeout", "MEMACK_TIMEOUT", "MEMACK_EXTRA acks",
	"MEMDAT_TIMEOUT", "MEMDAT_EXTRA acks", "MEMFLUSH noack",
	"MEMOP noack", "LBCONNECT noack", "NONE - was LBREQ",
	"LBACK_TIMEOUT", "LBACK_EXTRA acks", "LBDAT_TIMEOUT",
	"LBDAT_EXTRA acks", "GPFIFO won't fit", "GPPTR invalid",
	"GPENTRY invalid", "GPCRC mismatch", "PBPTR get>put",
	"PBENTRY invld", "PBCRC mismatch", "NONE - was XBARC",
	"METHOD invld", "METHODCRC mismat", "DEVICE sw method",
	"[ENGINE]", "SEMAPHORE invlid", "ACQUIRE timeout",
	"PRI forbidden", "ILLEGAL SYNCPT", "[NO_CTXSW_SEG]",
	"PBSEG badsplit", "SIGNATURE bad"
};

u32 gk20a_fifo_get_engine_ids(struct gk20a *g,
		u32 engine_id[], u32 engine_id_sz,
		enum fifo_engine engine_enum)
{
	struct fifo_gk20a *f = NULL;
	u32 instance_cnt = 0;
	u32 engine_id_idx;
	u32 active_engine_id = 0;
	struct fifo_engine_info_gk20a *info = NULL;

	if ((g != NULL) &&
	    (engine_id_sz != 0U) &&
	    (engine_enum < ENGINE_INVAL_GK20A)) {
		f = &g->fifo;
		for (engine_id_idx = 0; engine_id_idx < f->num_engines; ++engine_id_idx) {
			active_engine_id = f->active_engines_list[engine_id_idx];
			info = &f->engine_info[active_engine_id];

			if (info->engine_enum == engine_enum) {
				if (instance_cnt < engine_id_sz) {
					engine_id[instance_cnt] = active_engine_id;
					++instance_cnt;
				} else {
					nvgpu_log_info(g, "warning engine_id table sz is small %d",
							engine_id_sz);
				}
			}
		}
	}
	return instance_cnt;
}

struct fifo_engine_info_gk20a *gk20a_fifo_get_engine_info(struct gk20a *g, u32 engine_id)
{
	struct fifo_gk20a *f = NULL;
	u32 engine_id_idx;
	struct fifo_engine_info_gk20a *info = NULL;

	if (g == NULL) {
		return info;
	}

	f = &g->fifo;

	if (engine_id < f->max_engines) {
		for (engine_id_idx = 0; engine_id_idx < f->num_engines; ++engine_id_idx) {
			if (engine_id == f->active_engines_list[engine_id_idx]) {
				info = &f->engine_info[engine_id];
				break;
			}
		}
	}

	if (info == NULL) {
		nvgpu_err(g, "engine_id is not in active list/invalid %d", engine_id);
	}

	return info;
}

bool gk20a_fifo_is_valid_engine_id(struct gk20a *g, u32 engine_id)
{
	struct fifo_gk20a *f = NULL;
	u32 engine_id_idx;
	bool valid = false;

	if (g == NULL) {
		return valid;
	}

	f = &g->fifo;

	if (engine_id < f->max_engines) {
		for (engine_id_idx = 0; engine_id_idx < f->num_engines; ++engine_id_idx) {
			if (engine_id == f->active_engines_list[engine_id_idx]) {
				valid = true;
				break;
			}
		}
	}

	if (!valid) {
		nvgpu_err(g, "engine_id is not in active list/invalid %d", engine_id);
	}

	return valid;
}

u32 gk20a_fifo_get_gr_engine_id(struct gk20a *g)
{
	u32 gr_engine_cnt = 0;
	u32 gr_engine_id = FIFO_INVAL_ENGINE_ID;

	/* Consider 1st available GR engine */
	gr_engine_cnt = gk20a_fifo_get_engine_ids(g, &gr_engine_id,
			1, ENGINE_GR_GK20A);

	if (gr_engine_cnt == 0U) {
		nvgpu_err(g, "No GR engine available on this device!");
	}

	return gr_engine_id;
}

u32 gk20a_fifo_get_all_ce_engine_reset_mask(struct gk20a *g)
{
	u32 reset_mask = 0;
	enum fifo_engine engine_enum = ENGINE_INVAL_GK20A;
	struct fifo_gk20a *f = NULL;
	u32 engine_id_idx;
	struct fifo_engine_info_gk20a *engine_info;
	u32 active_engine_id = 0;

	if (g == NULL) {
		return reset_mask;
	}

	f = &g->fifo;

	for (engine_id_idx = 0; engine_id_idx < f->num_engines; ++engine_id_idx) {
		active_engine_id = f->active_engines_list[engine_id_idx];
		engine_info = &f->engine_info[active_engine_id];
		engine_enum = engine_info->engine_enum;

		if ((engine_enum == ENGINE_GRCE_GK20A) ||
			(engine_enum == ENGINE_ASYNC_CE_GK20A)) {
				reset_mask |= engine_info->reset_mask;
		}
	}

	return reset_mask;
}

u32 gk20a_fifo_get_fast_ce_runlist_id(struct gk20a *g)
{
	u32 ce_runlist_id = gk20a_fifo_get_gr_runlist_id(g);
	enum fifo_engine engine_enum = ENGINE_INVAL_GK20A;
	struct fifo_gk20a *f = NULL;
	u32 engine_id_idx;
	struct fifo_engine_info_gk20a *engine_info;
	u32 active_engine_id = 0;

	if (g == NULL) {
		return ce_runlist_id;
	}

	f = &g->fifo;

	for (engine_id_idx = 0; engine_id_idx < f->num_engines; ++engine_id_idx) {
		active_engine_id = f->active_engines_list[engine_id_idx];
		engine_info = &f->engine_info[active_engine_id];
		engine_enum = engine_info->engine_enum;

		/* selecet last available ASYNC_CE if available */
		if (engine_enum == ENGINE_ASYNC_CE_GK20A) {
			ce_runlist_id = engine_info->runlist_id;
		}
	}

	return ce_runlist_id;
}

u32 gk20a_fifo_get_gr_runlist_id(struct gk20a *g)
{
	u32 gr_engine_cnt = 0;
	u32 gr_engine_id = FIFO_INVAL_ENGINE_ID;
	struct fifo_engine_info_gk20a *engine_info;
	u32 gr_runlist_id = U32_MAX;

	/* Consider 1st available GR engine */
	gr_engine_cnt = gk20a_fifo_get_engine_ids(g, &gr_engine_id,
			1, ENGINE_GR_GK20A);

	if (gr_engine_cnt == 0U) {
		nvgpu_err(g,
			"No GR engine available on this device!");
		goto end;
	}

	engine_info = gk20a_fifo_get_engine_info(g, gr_engine_id);

	if (engine_info != NULL) {
		gr_runlist_id = engine_info->runlist_id;
	} else {
		nvgpu_err(g,
			"gr_engine_id is not in active list/invalid %d", gr_engine_id);
	}

end:
	return gr_runlist_id;
}

bool gk20a_fifo_is_valid_runlist_id(struct gk20a *g, u32 runlist_id)
{
	struct fifo_gk20a *f = NULL;
	u32 engine_id_idx;
	u32 active_engine_id;
	struct fifo_engine_info_gk20a *engine_info;

	if (g == NULL) {
		return false;
	}

	f = &g->fifo;

	for (engine_id_idx = 0; engine_id_idx < f->num_engines; ++engine_id_idx) {
		active_engine_id = f->active_engines_list[engine_id_idx];
		engine_info = gk20a_fifo_get_engine_info(g, active_engine_id);
		if ((engine_info != NULL) &&
		    (engine_info->runlist_id == runlist_id)) {
			return true;
		}
	}

	return false;
}

/*
 * Link engine IDs to MMU IDs and vice versa.
 */

static inline u32 gk20a_engine_id_to_mmu_id(struct gk20a *g, u32 engine_id)
{
	u32 fault_id = FIFO_INVAL_ENGINE_ID;
	struct fifo_engine_info_gk20a *engine_info;

	engine_info = gk20a_fifo_get_engine_info(g, engine_id);

	if (engine_info != NULL) {
		fault_id = engine_info->fault_id;
	} else {
		nvgpu_err(g, "engine_id is not in active list/invalid %d", engine_id);
	}
	return fault_id;
}

static inline u32 gk20a_mmu_id_to_engine_id(struct gk20a *g, u32 fault_id)
{
	u32 engine_id;
	u32 active_engine_id;
	struct fifo_engine_info_gk20a *engine_info;
	struct fifo_gk20a *f = &g->fifo;

	for (engine_id = 0; engine_id < f->num_engines; engine_id++) {
		active_engine_id = f->active_engines_list[engine_id];
		engine_info = &g->fifo.engine_info[active_engine_id];

		if (engine_info->fault_id == fault_id) {
			break;
		}
		active_engine_id = FIFO_INVAL_ENGINE_ID;
	}
	return active_engine_id;
}

enum fifo_engine gk20a_fifo_engine_enum_from_type(struct gk20a *g,
					u32 engine_type, u32 *inst_id)
{
	enum fifo_engine ret = ENGINE_INVAL_GK20A;

	nvgpu_log_info(g, "engine type %d", engine_type);
	if (engine_type == top_device_info_type_enum_graphics_v()) {
		ret = ENGINE_GR_GK20A;
	} else if ((engine_type >= top_device_info_type_enum_copy0_v()) &&
		(engine_type <= top_device_info_type_enum_copy2_v())) {
		/* Lets consider all the CE engine have separate runlist at this point
		 * We can identify the ENGINE_GRCE_GK20A type CE using runlist_id
		 * comparsion logic with GR runlist_id in init_engine_info() */
			ret = ENGINE_ASYNC_CE_GK20A;
		/* inst_id starts from CE0 to CE2 */
		if (inst_id != NULL) {
			*inst_id = (engine_type - top_device_info_type_enum_copy0_v());
		}
	}

	return ret;
}

int gk20a_fifo_init_engine_info(struct fifo_gk20a *f)
{
	struct gk20a *g = f->g;
	u32 i;
	u32 max_info_entries = top_device_info__size_1_v();
	enum fifo_engine engine_enum = ENGINE_INVAL_GK20A;
	u32 engine_id = FIFO_INVAL_ENGINE_ID;
	u32 runlist_id = U32_MAX;
	u32 pbdma_id = U32_MAX;
	u32 intr_id = U32_MAX;
	u32 reset_id = U32_MAX;
	u32 inst_id  = 0;
	u32 pri_base = 0;
	u32 fault_id = 0;
	u32 gr_runlist_id = U32_MAX;
	bool found_pbdma_for_runlist = false;

	nvgpu_log_fn(g, " ");

	f->num_engines = 0;

	for (i = 0; i < max_info_entries; i++) {
		u32 table_entry = gk20a_readl(f->g, top_device_info_r(i));
		u32 entry = top_device_info_entry_v(table_entry);
		u32 runlist_bit;

		if (entry == top_device_info_entry_enum_v()) {
			if (top_device_info_engine_v(table_entry) != 0U) {
				engine_id =
					top_device_info_engine_enum_v(table_entry);
				nvgpu_log_info(g, "info: engine_id %d",
					top_device_info_engine_enum_v(table_entry));
			}


			if (top_device_info_runlist_v(table_entry) != 0U) {
				runlist_id =
					top_device_info_runlist_enum_v(table_entry);
				nvgpu_log_info(g, "gr info: runlist_id %d", runlist_id);

				runlist_bit = BIT32(runlist_id);

				found_pbdma_for_runlist = false;
				for (pbdma_id = 0; pbdma_id < f->num_pbdma;
								pbdma_id++) {
					if ((f->pbdma_map[pbdma_id] &
						runlist_bit) != 0U) {
						nvgpu_log_info(g,
						"gr info: pbdma_map[%d]=%d",
							pbdma_id,
							f->pbdma_map[pbdma_id]);
						found_pbdma_for_runlist = true;
						break;
					}
				}

				if (!found_pbdma_for_runlist) {
					nvgpu_err(g, "busted pbdma map");
					return -EINVAL;
				}
			}

			if (top_device_info_intr_v(table_entry) != 0U) {
				intr_id =
					top_device_info_intr_enum_v(table_entry);
				nvgpu_log_info(g, "gr info: intr_id %d", intr_id);
			}

			if (top_device_info_reset_v(table_entry) != 0U) {
				reset_id =
					top_device_info_reset_enum_v(table_entry);
				nvgpu_log_info(g, "gr info: reset_id %d",
						reset_id);
			}
		} else if (entry == top_device_info_entry_engine_type_v()) {
			u32 engine_type =
				top_device_info_type_enum_v(table_entry);
			engine_enum =
				g->ops.fifo.engine_enum_from_type(g,
						engine_type, &inst_id);
		} else if (entry == top_device_info_entry_data_v()) {
			/* gk20a doesn't support device_info_data packet parsing */
			if (g->ops.fifo.device_info_data_parse != NULL) {
				g->ops.fifo.device_info_data_parse(g,
					table_entry, &inst_id, &pri_base,
					&fault_id);
			}
		}

		if (top_device_info_chain_v(table_entry) ==
			top_device_info_chain_disable_v()) {
			if (engine_enum < ENGINE_INVAL_GK20A) {
				struct fifo_engine_info_gk20a *info =
					&g->fifo.engine_info[engine_id];

				info->intr_mask |= BIT(intr_id);
				info->reset_mask |= BIT(reset_id);
				info->runlist_id = runlist_id;
				info->pbdma_id = pbdma_id;
				info->inst_id  = inst_id;
				info->pri_base = pri_base;

				if (engine_enum == ENGINE_GR_GK20A) {
					gr_runlist_id = runlist_id;
				}

				/* GR and GR_COPY shares same runlist_id */
				if ((engine_enum == ENGINE_ASYNC_CE_GK20A) &&
					(gr_runlist_id == runlist_id)) {
						engine_enum = ENGINE_GRCE_GK20A;
				}

				info->engine_enum = engine_enum;

				if ((fault_id == 0U) &&
				    (engine_enum == ENGINE_GRCE_GK20A)) {
					fault_id = 0x1b;
				}
				info->fault_id = fault_id;

				/* engine_id starts from 0 to NV_HOST_NUM_ENGINES */
				f->active_engines_list[f->num_engines] = engine_id;

				++f->num_engines;

				engine_enum = ENGINE_INVAL_GK20A;
			}
		}
	}

	return 0;
}

u32 gk20a_fifo_act_eng_interrupt_mask(struct gk20a *g, u32 act_eng_id)
{
	struct fifo_engine_info_gk20a *engine_info = NULL;

	engine_info = gk20a_fifo_get_engine_info(g, act_eng_id);
	if (engine_info != NULL) {
		return engine_info->intr_mask;
	}

	return 0;
}

u32 gk20a_fifo_engine_interrupt_mask(struct gk20a *g)
{
	u32 eng_intr_mask = 0;
	unsigned int i;
	u32 active_engine_id = 0;
	enum fifo_engine engine_enum = ENGINE_INVAL_GK20A;

	for (i = 0; i < g->fifo.num_engines; i++) {
		u32 intr_mask;
		active_engine_id = g->fifo.active_engines_list[i];
		intr_mask = g->fifo.engine_info[active_engine_id].intr_mask;
		engine_enum = g->fifo.engine_info[active_engine_id].engine_enum;
		if (((engine_enum == ENGINE_GRCE_GK20A) ||
		     (engine_enum == ENGINE_ASYNC_CE_GK20A)) &&
		    ((g->ops.ce2.isr_stall == NULL) ||
		     (g->ops.ce2.isr_nonstall == NULL))) {
				continue;
		}

		eng_intr_mask |= intr_mask;
	}

	return eng_intr_mask;
}

void gk20a_fifo_delete_runlist(struct fifo_gk20a *f)
{
	u32 i;
	u32 runlist_id;
	struct fifo_runlist_info_gk20a *runlist;
	struct gk20a *g = NULL;

	if ((f == NULL) || (f->runlist_info == NULL)) {
		return;
	}

	g = f->g;

	for (runlist_id = 0; runlist_id < f->max_runlists; runlist_id++) {
		runlist = &f->runlist_info[runlist_id];
		for (i = 0; i < MAX_RUNLIST_BUFFERS; i++) {
			nvgpu_dma_free(g, &runlist->mem[i]);
		}

		nvgpu_kfree(g, runlist->active_channels);
		runlist->active_channels = NULL;

		nvgpu_kfree(g, runlist->active_tsgs);
		runlist->active_tsgs = NULL;

		nvgpu_mutex_destroy(&runlist->runlist_lock);

	}
	(void) memset(f->runlist_info, 0,
		(sizeof(struct fifo_runlist_info_gk20a) * f->max_runlists));

	nvgpu_kfree(g, f->runlist_info);
	f->runlist_info = NULL;
	f->max_runlists = 0;
}

static void gk20a_remove_fifo_support(struct fifo_gk20a *f)
{
	struct gk20a *g = f->g;
	unsigned int i = 0;

	nvgpu_log_fn(g, " ");

	nvgpu_channel_worker_deinit(g);
	/*
	 * Make sure all channels are closed before deleting them.
	 */
	for (; i < f->num_channels; i++) {
		struct channel_gk20a *c = f->channel + i;
		struct tsg_gk20a *tsg = f->tsg + i;

		/*
		 * Could race but worst that happens is we get an error message
		 * from gk20a_free_channel() complaining about multiple closes.
		 */
		if (c->referenceable) {
			__gk20a_channel_kill(c);
		}

		nvgpu_mutex_destroy(&tsg->event_id_list_lock);

		nvgpu_mutex_destroy(&c->ioctl_lock);
		nvgpu_mutex_destroy(&c->joblist.cleanup_lock);
		nvgpu_mutex_destroy(&c->joblist.pre_alloc.read_lock);
		nvgpu_mutex_destroy(&c->sync_lock);
#if defined(CONFIG_GK20A_CYCLE_STATS)
		nvgpu_mutex_destroy(&c->cyclestate.cyclestate_buffer_mutex);
		nvgpu_mutex_destroy(&c->cs_client_mutex);
#endif
		nvgpu_mutex_destroy(&c->dbg_s_lock);

	}

	nvgpu_vfree(g, f->channel);
	nvgpu_vfree(g, f->tsg);
	gk20a_fifo_free_userd_slabs(g);
	(void) nvgpu_vm_area_free(g->mm.bar1.vm, f->userd_gpu_va);
	f->userd_gpu_va = 0ULL;

	gk20a_fifo_delete_runlist(f);

	nvgpu_kfree(g, f->pbdma_map);
	f->pbdma_map = NULL;
	nvgpu_kfree(g, f->engine_info);
	f->engine_info = NULL;
	nvgpu_kfree(g, f->active_engines_list);
	f->active_engines_list = NULL;
}

static int init_runlist(struct gk20a *g, struct fifo_gk20a *f)
{
	struct fifo_runlist_info_gk20a *runlist;
	struct fifo_engine_info_gk20a *engine_info;
	unsigned int runlist_id;
	u32 i;
	size_t runlist_size;
	u32 active_engine_id, pbdma_id, engine_id;
	int err = 0;

	nvgpu_log_fn(g, " ");

	f->max_runlists = g->ops.fifo.eng_runlist_base_size();
	f->runlist_info = nvgpu_kzalloc(g,
					sizeof(struct fifo_runlist_info_gk20a) *
					f->max_runlists);
	if (f->runlist_info == NULL) {
		goto clean_up_runlist;
	}

	(void) memset(f->runlist_info, 0,
		(sizeof(struct fifo_runlist_info_gk20a) * f->max_runlists));

	for (runlist_id = 0; runlist_id < f->max_runlists; runlist_id++) {
		runlist = &f->runlist_info[runlist_id];

		runlist->active_channels =
			nvgpu_kzalloc(g, DIV_ROUND_UP(f->num_channels,
						      BITS_PER_BYTE));
		if (runlist->active_channels == NULL) {
			goto clean_up_runlist;
		}

		runlist->active_tsgs =
			nvgpu_kzalloc(g, DIV_ROUND_UP(f->num_channels,
						      BITS_PER_BYTE));
		if (runlist->active_tsgs == NULL) {
			goto clean_up_runlist;
		}

		runlist_size  = (size_t)f->runlist_entry_size *
				(size_t)f->num_runlist_entries;
		nvgpu_log(g, gpu_dbg_info,
				"runlist_entries %d runlist size %zu",
				f->num_runlist_entries, runlist_size);

		for (i = 0; i < MAX_RUNLIST_BUFFERS; i++) {
			err = nvgpu_dma_alloc_flags_sys(g,
					NVGPU_DMA_PHYSICALLY_ADDRESSED,
					runlist_size,
					&runlist->mem[i]);
			if (err != 0) {
				nvgpu_err(g, "memory allocation failed");
				goto clean_up_runlist;
			}
		}

		err = nvgpu_mutex_init(&runlist->runlist_lock);
		if (err != 0) {
			nvgpu_err(g,
				"Error in runlist_lock mutex initialization");
			goto clean_up_runlist;
		}

		/* None of buffers is pinned if this value doesn't change.
		    Otherwise, one of them (cur_buffer) must have been pinned. */
		runlist->cur_buffer = MAX_RUNLIST_BUFFERS;

		for (pbdma_id = 0; pbdma_id < f->num_pbdma; pbdma_id++) {
			if ((f->pbdma_map[pbdma_id] & BIT32(runlist_id)) != 0U) {
				runlist->pbdma_bitmask |= BIT32(pbdma_id);
			}
		}
		nvgpu_log(g, gpu_dbg_info, "runlist %d : pbdma bitmask 0x%x",
				 runlist_id, runlist->pbdma_bitmask);

		for (engine_id = 0; engine_id < f->num_engines; ++engine_id) {
			active_engine_id = f->active_engines_list[engine_id];
			engine_info = &f->engine_info[active_engine_id];

			if ((engine_info != NULL) &&
			    (engine_info->runlist_id == runlist_id)) {
				runlist->eng_bitmask |= BIT(active_engine_id);
			}
		}
		nvgpu_log(g, gpu_dbg_info, "runlist %d : act eng bitmask 0x%x",
				 runlist_id, runlist->eng_bitmask);
	}

	nvgpu_log_fn(g, "done");
	return 0;

clean_up_runlist:
	gk20a_fifo_delete_runlist(f);
	nvgpu_log_fn(g, "fail");
	return err;
}

u32 gk20a_fifo_intr_0_error_mask(struct gk20a *g)
{
	u32 intr_0_error_mask =
		fifo_intr_0_bind_error_pending_f() |
		fifo_intr_0_sched_error_pending_f() |
		fifo_intr_0_chsw_error_pending_f() |
		fifo_intr_0_fb_flush_timeout_pending_f() |
		fifo_intr_0_dropped_mmu_fault_pending_f() |
		fifo_intr_0_mmu_fault_pending_f() |
		fifo_intr_0_lb_error_pending_f() |
		fifo_intr_0_pio_error_pending_f();

	return intr_0_error_mask;
}

static u32 gk20a_fifo_intr_0_en_mask(struct gk20a *g)
{
	u32 intr_0_en_mask;

	intr_0_en_mask = g->ops.fifo.intr_0_error_mask(g);

	intr_0_en_mask |= fifo_intr_0_runlist_event_pending_f() |
				 fifo_intr_0_pbdma_intr_pending_f();

	return intr_0_en_mask;
}

int gk20a_init_fifo_reset_enable_hw(struct gk20a *g)
{
	u32 intr_stall;
	u32 mask;
	u32 timeout;
	unsigned int i;
	u32 host_num_pbdma = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_PBDMA);

	nvgpu_log_fn(g, " ");

	/* enable pmc pfifo */
	g->ops.mc.reset(g, g->ops.mc.reset_mask(g, NVGPU_UNIT_FIFO));

	if (g->ops.clock_gating.slcg_fifo_load_gating_prod != NULL) {
		g->ops.clock_gating.slcg_fifo_load_gating_prod(g,
				g->slcg_enabled);
	}
	if (g->ops.clock_gating.blcg_fifo_load_gating_prod != NULL) {
		g->ops.clock_gating.blcg_fifo_load_gating_prod(g,
				g->blcg_enabled);
	}

	timeout = gk20a_readl(g, fifo_fb_timeout_r());
	timeout = set_field(timeout, fifo_fb_timeout_period_m(),
			fifo_fb_timeout_period_max_f());
	nvgpu_log_info(g, "fifo_fb_timeout reg val = 0x%08x", timeout);
	gk20a_writel(g, fifo_fb_timeout_r(), timeout);

	/* write pbdma timeout value */
	for (i = 0; i < host_num_pbdma; i++) {
		timeout = gk20a_readl(g, pbdma_timeout_r(i));
		timeout = set_field(timeout, pbdma_timeout_period_m(),
				    pbdma_timeout_period_max_f());
		nvgpu_log_info(g, "pbdma_timeout reg val = 0x%08x", timeout);
		gk20a_writel(g, pbdma_timeout_r(i), timeout);
	}
	if (g->ops.fifo.apply_pb_timeout != NULL) {
		g->ops.fifo.apply_pb_timeout(g);
	}

	if (g->ops.fifo.apply_ctxsw_timeout_intr != NULL) {
		g->ops.fifo.apply_ctxsw_timeout_intr(g);
	} else {
		timeout = g->fifo_eng_timeout_us;
		timeout = scale_ptimer(timeout,
			ptimer_scalingfactor10x(g->ptimer_src_freq));
		timeout |= fifo_eng_timeout_detection_enabled_f();
		gk20a_writel(g, fifo_eng_timeout_r(), timeout);
	}

	/* clear and enable pbdma interrupt */
	for (i = 0; i < host_num_pbdma; i++) {
		gk20a_writel(g, pbdma_intr_0_r(i), 0xFFFFFFFF);
		gk20a_writel(g, pbdma_intr_1_r(i), 0xFFFFFFFF);

		intr_stall = gk20a_readl(g, pbdma_intr_stall_r(i));
		intr_stall &= ~pbdma_intr_stall_lbreq_enabled_f();
		gk20a_writel(g, pbdma_intr_stall_r(i), intr_stall);
		nvgpu_log_info(g, "pbdma id:%u, intr_en_0 0x%08x", i, intr_stall);
		gk20a_writel(g, pbdma_intr_en_0_r(i), intr_stall);
		intr_stall = gk20a_readl(g, pbdma_intr_stall_1_r(i));
		/*
		 * For bug 2082123
		 * Mask the unused HCE_RE_ILLEGAL_OP bit from the interrupt.
		 */
		intr_stall &= ~pbdma_intr_stall_1_hce_illegal_op_enabled_f();
		nvgpu_log_info(g, "pbdma id:%u, intr_en_1 0x%08x", i, intr_stall);
		gk20a_writel(g, pbdma_intr_en_1_r(i), intr_stall);
	}

	/* reset runlist interrupts */
	gk20a_writel(g, fifo_intr_runlist_r(), U32_MAX);

	/* clear and enable pfifo interrupt */
	gk20a_writel(g, fifo_intr_0_r(), 0xFFFFFFFF);
	mask = gk20a_fifo_intr_0_en_mask(g);
	nvgpu_log_info(g, "fifo_intr_en_0 0x%08x", mask);
	gk20a_writel(g, fifo_intr_en_0_r(), mask);
	nvgpu_log_info(g, "fifo_intr_en_1 = 0x80000000");
	gk20a_writel(g, fifo_intr_en_1_r(), 0x80000000);

	nvgpu_log_fn(g, "done");

	return 0;
}

int gk20a_init_fifo_setup_sw_common(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	unsigned int chid, i;
	int err = 0;

	nvgpu_log_fn(g, " ");

	f->g = g;

	err = nvgpu_mutex_init(&f->intr.isr.mutex);
	if (err != 0) {
		nvgpu_err(g, "failed to init isr.mutex");
		return err;
	}

	err = nvgpu_mutex_init(&f->gr_reset_mutex);
	if (err != 0) {
		nvgpu_err(g, "failed to init gr_reset_mutex");
		return err;
	}

	nvgpu_spinlock_init(&f->runlist_submit_lock);

	g->ops.fifo.init_pbdma_intr_descs(f); /* just filling in data/tables */

	f->num_channels = g->ops.fifo.get_num_fifos(g);
	f->runlist_entry_size =  g->ops.fifo.runlist_entry_size();
	f->num_runlist_entries = fifo_eng_runlist_length_max_v();
	f->num_pbdma = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_PBDMA);
	f->max_engines = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_ENGINES);

	f->userd_entry_size = BIT16(ram_userd_base_shift_v());

	f->channel = nvgpu_vzalloc(g, f->num_channels * sizeof(*f->channel));
	f->tsg = nvgpu_vzalloc(g, f->num_channels * sizeof(*f->tsg));
	f->pbdma_map = nvgpu_kzalloc(g, f->num_pbdma * sizeof(*f->pbdma_map));
	f->engine_info = nvgpu_kzalloc(g, f->max_engines *
				sizeof(*f->engine_info));
	f->active_engines_list = nvgpu_kzalloc(g, f->max_engines * sizeof(u32));

	if (!((f->channel != NULL) &&
	      (f->tsg != NULL) &&
	      (f->pbdma_map != NULL) &&
	      (f->engine_info != NULL) &&
	      (f->active_engines_list != NULL))) {
		err = -ENOMEM;
		goto clean_up;
	}
	(void) memset(f->active_engines_list, 0xff,
		(f->max_engines * sizeof(u32)));

	/* pbdma map needs to be in place before calling engine info init */
	for (i = 0; i < f->num_pbdma; ++i) {
		f->pbdma_map[i] = gk20a_readl(g, fifo_pbdma_map_r(i));
	}

	g->ops.fifo.init_engine_info(f);

	err = init_runlist(g, f);
	if (err != 0) {
		nvgpu_err(g, "failed to init runlist");
		goto clean_up;
	}

	nvgpu_init_list_node(&f->free_chs);

	err = nvgpu_mutex_init(&f->free_chs_mutex);
	if (err != 0) {
		nvgpu_err(g, "failed to init free_chs_mutex");
		goto clean_up;
	}

	for (chid = 0; chid < f->num_channels; chid++) {
		gk20a_init_channel_support(g, chid);
		gk20a_init_tsg_support(g, chid);
	}

	err = nvgpu_mutex_init(&f->tsg_inuse_mutex);
	if (err != 0) {
		nvgpu_err(g, "failed to init tsg_inuse_mutex");
		goto clean_up;
	}

	f->remove_support = gk20a_remove_fifo_support;

	f->deferred_reset_pending = false;

	err = nvgpu_mutex_init(&f->deferred_reset_mutex);
	if (err != 0) {
		nvgpu_err(g, "failed to init deferred_reset_mutex");
		goto clean_up;
	}

	nvgpu_log_fn(g, "done");
	return 0;

clean_up:
	nvgpu_err(g, "fail");

	nvgpu_vfree(g, f->channel);
	f->channel = NULL;
	nvgpu_vfree(g, f->tsg);
	f->tsg = NULL;
	nvgpu_kfree(g, f->pbdma_map);
	f->pbdma_map = NULL;
	nvgpu_kfree(g, f->engine_info);
	f->engine_info = NULL;
	nvgpu_kfree(g, f->active_engines_list);
	f->active_engines_list = NULL;

	return err;
}

int gk20a_fifo_init_userd_slabs(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	int err;

	err = nvgpu_mutex_init(&f->userd_mutex);
	if (err != 0) {
		nvgpu_err(g, "failed to init userd_mutex");
		return err;
	}

	f->num_channels_per_slab = PAGE_SIZE /  f->userd_entry_size;
	f->num_userd_slabs =
		DIV_ROUND_UP(f->num_channels, f->num_channels_per_slab);

	f->userd_slabs = nvgpu_big_zalloc(g, f->num_userd_slabs *
				          sizeof(struct nvgpu_mem));
	if (f->userd_slabs == NULL) {
		nvgpu_err(g, "could not allocate userd slabs");
		return -ENOMEM;
	}

	return 0;
}

int gk20a_fifo_init_userd(struct gk20a *g, struct channel_gk20a *c)
{
	struct fifo_gk20a *f = &g->fifo;
	struct nvgpu_mem *mem;
	u32 slab = c->chid / f->num_channels_per_slab;
	int err = 0;

	if (slab > f->num_userd_slabs) {
		nvgpu_err(g, "chid %u, slab %u out of range (max=%u)",
			c->chid, slab,  f->num_userd_slabs);
		return -EINVAL;
	}

	mem = &g->fifo.userd_slabs[slab];

	nvgpu_mutex_acquire(&f->userd_mutex);
	if (!nvgpu_mem_is_valid(mem)) {
		err = nvgpu_dma_alloc_sys(g, PAGE_SIZE, mem);
		if (err != 0) {
			nvgpu_err(g, "userd allocation failed, err=%d", err);
			goto done;
		}

		if (g->ops.mm.is_bar1_supported(g)) {
			mem->gpu_va = g->ops.mm.bar1_map_userd(g, mem,
							 slab * PAGE_SIZE);
		}
	}
	c->userd_mem = mem;
	c->userd_offset = (c->chid % f->num_channels_per_slab) *
				f->userd_entry_size;
	c->userd_iova = gk20a_channel_userd_addr(c);

	nvgpu_log(g, gpu_dbg_info,
		"chid=%u slab=%u mem=%p offset=%u addr=%llx gpu_va=%llx",
		c->chid, slab, mem, c->userd_offset,
		gk20a_channel_userd_addr(c),
		gk20a_channel_userd_gpu_va(c));

done:
	nvgpu_mutex_release(&f->userd_mutex);
	return err;
}

void gk20a_fifo_free_userd_slabs(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 slab;

	for (slab = 0; slab < f->num_userd_slabs; slab++) {
		nvgpu_dma_free(g, &f->userd_slabs[slab]);
	}
	nvgpu_big_free(g, f->userd_slabs);
	f->userd_slabs = NULL;
}

int gk20a_init_fifo_setup_sw(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	int err = 0;
	u32 size;
	u32 num_pages;

	nvgpu_log_fn(g, " ");

	if (f->sw_ready) {
		nvgpu_log_fn(g, "skip init");
		return 0;
	}

	err = gk20a_init_fifo_setup_sw_common(g);
	if (err != 0) {
		nvgpu_err(g, "fail: err: %d", err);
		return err;
	}

	err = gk20a_fifo_init_userd_slabs(g);
	if (err != 0) {
		nvgpu_err(g, "userd slabs init fail, err=%d", err);
		return err;
	}

	size = f->num_channels * f->userd_entry_size;
	num_pages = DIV_ROUND_UP(size, PAGE_SIZE);
	err = nvgpu_vm_area_alloc(g->mm.bar1.vm,
			num_pages, PAGE_SIZE, &f->userd_gpu_va, 0);
	if (err != 0) {
		nvgpu_err(g, "userd gpu va allocation failed, err=%d", err);
		goto clean_slabs;
	}

	err = nvgpu_channel_worker_init(g);
	if (err != 0) {
		nvgpu_err(g, "worker init fail, err=%d", err);
		goto clean_vm_area;
	}

	f->sw_ready = true;

	nvgpu_log_fn(g, "done");
	return 0;

clean_vm_area:
	(void) nvgpu_vm_area_free(g->mm.bar1.vm, f->userd_gpu_va);
	f->userd_gpu_va = 0ULL;

clean_slabs:
	gk20a_fifo_free_userd_slabs(g);
	return err;
}

void gk20a_fifo_handle_runlist_event(struct gk20a *g)
{
	u32 runlist_event = gk20a_readl(g, fifo_intr_runlist_r());

	nvgpu_log(g, gpu_dbg_intr, "runlist event %08x",
		  runlist_event);

	gk20a_writel(g, fifo_intr_runlist_r(), runlist_event);
}

int gk20a_init_fifo_setup_hw(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	u64 shifted_addr;

	nvgpu_log_fn(g, " ");

	/* set the base for the userd region now */
	shifted_addr = f->userd_gpu_va >> 12;
	if ((shifted_addr >> 32) != 0U) {
		nvgpu_err(g, "GPU VA > 32 bits %016llx\n", f->userd_gpu_va);
		return -EFAULT;
	}
	gk20a_writel(g, fifo_bar1_base_r(),
			fifo_bar1_base_ptr_f(u64_lo32(shifted_addr)) |
			fifo_bar1_base_valid_true_f());

	nvgpu_log_fn(g, "done");

	return 0;
}

int gk20a_init_fifo_support(struct gk20a *g)
{
	int err;

	err = g->ops.fifo.setup_sw(g);
	if (err != 0) {
		return err;
	}

	if (g->ops.fifo.init_fifo_setup_hw != NULL) {
		err = g->ops.fifo.init_fifo_setup_hw(g);
	}
	if (err != 0) {
		return err;
	}

	return err;
}

/* return with a reference to the channel, caller must put it back */
struct channel_gk20a *
gk20a_refch_from_inst_ptr(struct gk20a *g, u64 inst_ptr)
{
	struct fifo_gk20a *f = &g->fifo;
	unsigned int ci;
	if (unlikely(f->channel == NULL)) {
		return NULL;
	}
	for (ci = 0; ci < f->num_channels; ci++) {
		struct channel_gk20a *ch;
		u64 ch_inst_ptr;

		ch = gk20a_channel_from_id(g, ci);
		/* only alive channels are searched */
		if (ch == NULL) {
			continue;
		}

		ch_inst_ptr = nvgpu_inst_block_addr(g, &ch->inst_block);
		if (inst_ptr == ch_inst_ptr) {
			return ch;
		}

		gk20a_channel_put(ch);
	}
	return NULL;
}

/* fault info/descriptions.
 * tbd: move to setup
 *  */
static const char * const gk20a_fault_type_descs[] = {
	 "pde", /*fifo_intr_mmu_fault_info_type_pde_v() == 0 */
	 "pde size",
	 "pte",
	 "va limit viol",
	 "unbound inst",
	 "priv viol",
	 "ro viol",
	 "wo viol",
	 "pitch mask",
	 "work creation",
	 "bad aperture",
	 "compression failure",
	 "bad kind",
	 "region viol",
	 "dual ptes",
	 "poisoned",
};
/* engine descriptions */
static const char * const engine_subid_descs[] = {
	"gpc",
	"hub",
};

static const char * const gk20a_hub_client_descs[] = {
	"vip", "ce0", "ce1", "dniso", "fe", "fecs", "host", "host cpu",
	"host cpu nb", "iso", "mmu", "mspdec", "msppp", "msvld",
	"niso", "p2p", "pd", "perf", "pmu", "raster twod", "scc",
	"scc nb", "sec", "ssync", "gr copy", "xv", "mmu nb",
	"msenc", "d falcon", "sked", "a falcon", "n/a",
};

static const char * const gk20a_gpc_client_descs[] = {
	"l1 0", "t1 0", "pe 0",
	"l1 1", "t1 1", "pe 1",
	"l1 2", "t1 2", "pe 2",
	"l1 3", "t1 3", "pe 3",
	"rast", "gcc", "gpccs",
	"prop 0", "prop 1", "prop 2", "prop 3",
	"l1 4", "t1 4", "pe 4",
	"l1 5", "t1 5", "pe 5",
	"l1 6", "t1 6", "pe 6",
	"l1 7", "t1 7", "pe 7",
};

static const char * const does_not_exist[] = {
	"does not exist"
};

/* fill in mmu fault desc */
void gk20a_fifo_get_mmu_fault_desc(struct mmu_fault_info *mmfault)
{
	if (mmfault->fault_type >= ARRAY_SIZE(gk20a_fault_type_descs)) {
		WARN_ON(mmfault->fault_type >=
				ARRAY_SIZE(gk20a_fault_type_descs));
	} else {
		mmfault->fault_type_desc =
			 gk20a_fault_type_descs[mmfault->fault_type];
	}
}

/* fill in mmu fault client description */
void gk20a_fifo_get_mmu_fault_client_desc(struct mmu_fault_info *mmfault)
{
	if (mmfault->client_id >= ARRAY_SIZE(gk20a_hub_client_descs)) {
		WARN_ON(mmfault->client_id >=
				ARRAY_SIZE(gk20a_hub_client_descs));
	} else {
		mmfault->client_id_desc =
			 gk20a_hub_client_descs[mmfault->client_id];
	}
}

/* fill in mmu fault gpc description */
void gk20a_fifo_get_mmu_fault_gpc_desc(struct mmu_fault_info *mmfault)
{
	if (mmfault->client_id >= ARRAY_SIZE(gk20a_gpc_client_descs)) {
		WARN_ON(mmfault->client_id >=
				ARRAY_SIZE(gk20a_gpc_client_descs));
	} else {
		mmfault->client_id_desc =
			 gk20a_gpc_client_descs[mmfault->client_id];
	}
}

static void get_exception_mmu_fault_info(struct gk20a *g, u32 mmu_fault_id,
	struct mmu_fault_info *mmfault)
{
	g->ops.fifo.get_mmu_fault_info(g, mmu_fault_id, mmfault);

	/* parse info */
	mmfault->fault_type_desc =  does_not_exist[0];
	if (g->ops.fifo.get_mmu_fault_desc != NULL) {
		g->ops.fifo.get_mmu_fault_desc(mmfault);
	}

	if (mmfault->client_type >= ARRAY_SIZE(engine_subid_descs)) {
		WARN_ON(mmfault->client_type >= ARRAY_SIZE(engine_subid_descs));
		mmfault->client_type_desc = does_not_exist[0];
	} else {
		mmfault->client_type_desc =
				 engine_subid_descs[mmfault->client_type];
	}

	mmfault->client_id_desc = does_not_exist[0];
	if ((mmfault->client_type ==
		fifo_intr_mmu_fault_info_engine_subid_hub_v())
		&& (g->ops.fifo.get_mmu_fault_client_desc != NULL)) {
		g->ops.fifo.get_mmu_fault_client_desc(mmfault);
	} else if ((mmfault->client_type ==
			fifo_intr_mmu_fault_info_engine_subid_gpc_v())
			&& (g->ops.fifo.get_mmu_fault_gpc_desc != NULL)) {
		g->ops.fifo.get_mmu_fault_gpc_desc(mmfault);
	}
}

/* reads info from hardware and fills in mmu fault info record */
void gk20a_fifo_get_mmu_fault_info(struct gk20a *g, u32 mmu_fault_id,
	struct mmu_fault_info *mmfault)
{
	u32 fault_info;
	u32 addr_lo, addr_hi;

	nvgpu_log_fn(g, "mmu_fault_id %d", mmu_fault_id);

	(void) memset(mmfault, 0, sizeof(*mmfault));

	fault_info = gk20a_readl(g,
		fifo_intr_mmu_fault_info_r(mmu_fault_id));
	mmfault->fault_type =
		fifo_intr_mmu_fault_info_type_v(fault_info);
	mmfault->access_type =
		fifo_intr_mmu_fault_info_write_v(fault_info);
	mmfault->client_type =
		fifo_intr_mmu_fault_info_engine_subid_v(fault_info);
	mmfault->client_id =
		fifo_intr_mmu_fault_info_client_v(fault_info);

	addr_lo = gk20a_readl(g, fifo_intr_mmu_fault_lo_r(mmu_fault_id));
	addr_hi = gk20a_readl(g, fifo_intr_mmu_fault_hi_r(mmu_fault_id));
	mmfault->fault_addr = hi32_lo32_to_u64(addr_hi, addr_lo);
	/* note:ignoring aperture on gk20a... */
	mmfault->inst_ptr = fifo_intr_mmu_fault_inst_ptr_v(
		 gk20a_readl(g, fifo_intr_mmu_fault_inst_r(mmu_fault_id)));
	/* note: inst_ptr is a 40b phys addr.  */
	mmfault->inst_ptr <<= fifo_intr_mmu_fault_inst_ptr_align_shift_v();
}

void gk20a_fifo_reset_engine(struct gk20a *g, u32 engine_id)
{
	enum fifo_engine engine_enum = ENGINE_INVAL_GK20A;
	struct fifo_engine_info_gk20a *engine_info;

	nvgpu_log_fn(g, " ");

	if (g == NULL) {
		return;
	}

	engine_info = gk20a_fifo_get_engine_info(g, engine_id);

	if (engine_info != NULL) {
		engine_enum = engine_info->engine_enum;
	}

	if (engine_enum == ENGINE_INVAL_GK20A) {
		nvgpu_err(g, "unsupported engine_id %d", engine_id);
	}

	if (engine_enum == ENGINE_GR_GK20A) {
		if (g->support_pmu && g->can_elpg) {
			if (nvgpu_pmu_disable_elpg(g) != 0) {
				nvgpu_err(g, "failed to set disable elpg");
			}
		}

#ifdef CONFIG_GK20A_CTXSW_TRACE
		/*
		 * Resetting engine will alter read/write index. Need to flush
		 * circular buffer before re-enabling FECS.
		 */
		if (g->ops.fecs_trace.reset)
			g->ops.fecs_trace.reset(g);
#endif
		if (!nvgpu_platform_is_simulation(g)) {
			/*HALT_PIPELINE method, halt GR engine*/
			if (gr_gk20a_halt_pipe(g) != 0) {
				nvgpu_err(g, "failed to HALT gr pipe");
			}
			/*
			 * resetting engine using mc_enable_r() is not
			 * enough, we do full init sequence
			 */
			nvgpu_log(g, gpu_dbg_info, "resetting gr engine");
			gk20a_gr_reset(g);
		} else {
			nvgpu_log(g, gpu_dbg_info,
				"HALT gr pipe not supported and "
				"gr cannot be reset without halting gr pipe");
		}
		if (g->support_pmu && g->can_elpg) {
			nvgpu_pmu_enable_elpg(g);
		}
	}
	if ((engine_enum == ENGINE_GRCE_GK20A) ||
		(engine_enum == ENGINE_ASYNC_CE_GK20A)) {
			g->ops.mc.reset(g, engine_info->reset_mask);
	}
}

static void gk20a_fifo_handle_chsw_fault(struct gk20a *g)
{
	u32 intr;

	intr = gk20a_readl(g, fifo_intr_chsw_error_r());
	nvgpu_err(g, "chsw: %08x", intr);
	g->ops.gr.dump_gr_falcon_stats(g);
	gk20a_writel(g, fifo_intr_chsw_error_r(), intr);
}

static void gk20a_fifo_handle_dropped_mmu_fault(struct gk20a *g)
{
	u32 fault_id = gk20a_readl(g, fifo_intr_mmu_fault_id_r());
	nvgpu_err(g, "dropped mmu fault (0x%08x)", fault_id);
}

bool gk20a_is_fault_engine_subid_gpc(struct gk20a *g, u32 engine_subid)
{
	return (engine_subid == fifo_intr_mmu_fault_info_engine_subid_gpc_v());
}

bool gk20a_fifo_should_defer_engine_reset(struct gk20a *g, u32 engine_id,
		u32 engine_subid, bool fake_fault)
{
	enum fifo_engine engine_enum = ENGINE_INVAL_GK20A;
	struct fifo_engine_info_gk20a *engine_info;

	if (g == NULL) {
		return false;
	}

	engine_info = gk20a_fifo_get_engine_info(g, engine_id);

	if (engine_info != NULL) {
		engine_enum = engine_info->engine_enum;
	}

	if (engine_enum == ENGINE_INVAL_GK20A) {
		return false;
	}

	/* channel recovery is only deferred if an sm debugger
	   is attached and has MMU debug mode is enabled */
	if (!g->ops.gr.sm_debugger_attached(g) ||
	    !g->ops.fb.is_debug_mode_enabled(g)) {
		return false;
	}

	/* if this fault is fake (due to RC recovery), don't defer recovery */
	if (fake_fault) {
		return false;
	}

	if (engine_enum != ENGINE_GR_GK20A) {
		return false;
	}

	return g->ops.fifo.is_fault_engine_subid_gpc(g, engine_subid);
}

void gk20a_fifo_abort_tsg(struct gk20a *g, struct tsg_gk20a *tsg, bool preempt)
{
	struct channel_gk20a *ch = NULL;

	nvgpu_log_fn(g, " ");

	g->ops.fifo.disable_tsg(tsg);

	if (preempt) {
		g->ops.fifo.preempt_tsg(g, tsg);
	}

	nvgpu_rwsem_down_read(&tsg->ch_list_lock);
	nvgpu_list_for_each_entry(ch, &tsg->ch_list, channel_gk20a, ch_entry) {
		if (gk20a_channel_get(ch) != NULL) {
			gk20a_channel_set_timedout(ch);
			if (ch->g->ops.fifo.ch_abort_clean_up != NULL) {
				ch->g->ops.fifo.ch_abort_clean_up(ch);
			}
			gk20a_channel_put(ch);
		}
	}
	nvgpu_rwsem_up_read(&tsg->ch_list_lock);
}

int gk20a_fifo_deferred_reset(struct gk20a *g, struct channel_gk20a *ch)
{
	unsigned long engine_id, engines;

	nvgpu_mutex_acquire(&g->dbg_sessions_lock);
	gr_gk20a_disable_ctxsw(g);

	if (!g->fifo.deferred_reset_pending) {
		goto clean_up;
	}

	if (gk20a_is_channel_marked_as_tsg(ch)) {
		engines = g->ops.fifo.get_engines_mask_on_id(g,
				ch->tsgid, true);
	} else {
		engines = g->ops.fifo.get_engines_mask_on_id(g,
				ch->chid, false);
	}
	if (engines == 0U) {
		goto clean_up;
	}

	/*
	 * If deferred reset is set for an engine, and channel is running
	 * on that engine, reset it
	 */

	for_each_set_bit(engine_id, &g->fifo.deferred_fault_engines, 32UL) {
		if ((BIT64(engine_id) & engines) != 0ULL) {
			gk20a_fifo_reset_engine(g, (u32)engine_id);
		}
	}

	g->fifo.deferred_fault_engines = 0;
	g->fifo.deferred_reset_pending = false;

clean_up:
	gr_gk20a_enable_ctxsw(g);
	nvgpu_mutex_release(&g->dbg_sessions_lock);

	return 0;
}

static bool gk20a_fifo_handle_mmu_fault_locked(
	struct gk20a *g,
	u32 mmu_fault_engines, /* queried from HW if 0 */
	u32 hw_id, /* queried from HW if ~(u32)0 OR mmu_fault_engines == 0*/
	bool id_is_tsg)
{
	bool fake_fault;
	unsigned long fault_id;
	unsigned long engine_mmu_fault_id;
	bool verbose = true;
	u32 grfifo_ctl;

	nvgpu_log_fn(g, " ");

	g->fifo.deferred_reset_pending = false;

	/* Disable power management */
	if (g->support_pmu && g->can_elpg) {
		if (nvgpu_pmu_disable_elpg(g) != 0) {
			nvgpu_err(g, "failed to set disable elpg");
		}
	}
	if (g->ops.clock_gating.slcg_gr_load_gating_prod != NULL) {
		g->ops.clock_gating.slcg_gr_load_gating_prod(g,
				false);
	}
	if (g->ops.clock_gating.slcg_perf_load_gating_prod != NULL) {
		g->ops.clock_gating.slcg_perf_load_gating_prod(g,
				false);
	}
	if (g->ops.clock_gating.slcg_ltc_load_gating_prod != NULL) {
		g->ops.clock_gating.slcg_ltc_load_gating_prod(g,
				false);
	}

	gr_gk20a_init_cg_mode(g, ELCG_MODE, ELCG_RUN);

	/* Disable fifo access */
	grfifo_ctl = gk20a_readl(g, gr_gpfifo_ctl_r());
	grfifo_ctl &= ~gr_gpfifo_ctl_semaphore_access_f(1);
	grfifo_ctl &= ~gr_gpfifo_ctl_access_f(1);

	gk20a_writel(g, gr_gpfifo_ctl_r(),
		grfifo_ctl | gr_gpfifo_ctl_access_f(0) |
		gr_gpfifo_ctl_semaphore_access_f(0));

	if (mmu_fault_engines != 0U) {
		fault_id = mmu_fault_engines;
		fake_fault = true;
	} else {
		fault_id = gk20a_readl(g, fifo_intr_mmu_fault_id_r());
		fake_fault = false;
	}


	/* go through all faulted engines */
	for_each_set_bit(engine_mmu_fault_id, &fault_id, 32U) {
		/* bits in fifo_intr_mmu_fault_id_r do not correspond 1:1 to
		 * engines. Convert engine_mmu_id to engine_id */
		u32 engine_id = gk20a_mmu_id_to_engine_id(g,
					(u32)engine_mmu_fault_id);
		struct mmu_fault_info mmfault_info;
		struct channel_gk20a *ch = NULL;
		struct tsg_gk20a *tsg = NULL;
		struct channel_gk20a *refch = NULL;
		/* read and parse engine status */
		u32 status = gk20a_readl(g, fifo_engine_status_r(engine_id));
		u32 ctx_status = fifo_engine_status_ctx_status_v(status);
		bool ctxsw = (ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_switch_v()
				|| ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_save_v()
				|| ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_load_v());

		get_exception_mmu_fault_info(g, (u32)engine_mmu_fault_id,
						 &mmfault_info);
		trace_gk20a_mmu_fault(mmfault_info.fault_addr,
				      mmfault_info.fault_type,
				      mmfault_info.access_type,
				      mmfault_info.inst_ptr,
				      engine_id,
				      mmfault_info.client_type_desc,
				      mmfault_info.client_id_desc,
				      mmfault_info.fault_type_desc);
		nvgpu_err(g, "MMU fault @ address: 0x%llx %s",
			  mmfault_info.fault_addr,
			  fake_fault ? "[FAKE]" : "");
		nvgpu_err(g, "  Engine: %d  subid: %d (%s)",
			  (int)engine_id,
			  mmfault_info.client_type,
			  mmfault_info.client_type_desc);
		nvgpu_err(g, "  Client %d (%s), ",
			  mmfault_info.client_id,
			  mmfault_info.client_id_desc);
		nvgpu_err(g, "  Type %d (%s); access_type 0x%08x; inst_ptr 0x%llx",
			  mmfault_info.fault_type,
			  mmfault_info.fault_type_desc,
			  mmfault_info.access_type, mmfault_info.inst_ptr);

		if (ctxsw) {
			g->ops.gr.dump_gr_falcon_stats(g);
			nvgpu_err(g, "  gr_status_r: 0x%x",
				  gk20a_readl(g, gr_status_r()));
		}

		/* get the channel/TSG */
		if (fake_fault) {
			/* use next_id if context load is failing */
			u32 id, type;

			if (hw_id == ~(u32)0) {
				id = (ctx_status ==
				      fifo_engine_status_ctx_status_ctxsw_load_v()) ?
					fifo_engine_status_next_id_v(status) :
					fifo_engine_status_id_v(status);
				type = (ctx_status ==
					fifo_engine_status_ctx_status_ctxsw_load_v()) ?
					fifo_engine_status_next_id_type_v(status) :
					fifo_engine_status_id_type_v(status);
			} else {
				id = hw_id;
				type = id_is_tsg ?
					fifo_engine_status_id_type_tsgid_v() :
					fifo_engine_status_id_type_chid_v();
			}

			if (type == fifo_engine_status_id_type_tsgid_v()) {
				tsg = &g->fifo.tsg[id];
			} else if (type == fifo_engine_status_id_type_chid_v()) {
				ch = &g->fifo.channel[id];
				refch = gk20a_channel_get(ch);
			}
		} else {
			/* Look up channel from the inst block pointer. */
			ch = gk20a_refch_from_inst_ptr(g,
					mmfault_info.inst_ptr);
			refch = ch;
		}

		if ((ch != NULL) && gk20a_is_channel_marked_as_tsg(ch)) {
			tsg = &g->fifo.tsg[ch->tsgid];
		}

		/* check if engine reset should be deferred */
		if (engine_id != FIFO_INVAL_ENGINE_ID) {
			bool defer = gk20a_fifo_should_defer_engine_reset(g,
					engine_id, mmfault_info.client_type,
					fake_fault);
			if (((ch != NULL) || (tsg != NULL)) && defer) {
				g->fifo.deferred_fault_engines |= BIT(engine_id);

				/* handled during channel free */
				g->fifo.deferred_reset_pending = true;
				nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg,
					   "sm debugger attached,"
					   " deferring channel recovery to channel free");
			} else {
				/* if lock is already taken, a reset is taking place
				so no need to repeat */
				if (nvgpu_mutex_tryacquire(&g->fifo.gr_reset_mutex) != 0) {
					gk20a_fifo_reset_engine(g, engine_id);
					nvgpu_mutex_release(&g->fifo.gr_reset_mutex);
				}
			}
		}

#ifdef CONFIG_GK20A_CTXSW_TRACE
		/*
		 * For non fake mmu fault, both tsg and ch pointers
		 * could be valid. Check tsg first.
		 */
		if (tsg != NULL)
			gk20a_ctxsw_trace_tsg_reset(g, tsg);
		else if (ch)
			gk20a_ctxsw_trace_channel_reset(g, ch);
#endif

		/*
		 * Disable the channel/TSG from hw and increment syncpoints.
		 */
		if (tsg != NULL) {
			if (g->fifo.deferred_reset_pending) {
				gk20a_disable_tsg(tsg);
			} else {
				if (!fake_fault) {
					nvgpu_tsg_set_ctx_mmu_error(g, tsg);
				}
				verbose = nvgpu_tsg_mark_error(g, tsg);
				gk20a_fifo_abort_tsg(g, tsg, false);
			}

			/* put back the ref taken early above */
			if (refch != NULL) {
				gk20a_channel_put(ch);
			}
		} else if (ch != NULL) {
			if (refch != NULL) {
				if (g->fifo.deferred_reset_pending) {
					g->ops.fifo.disable_channel(ch);
				} else {
					if (!fake_fault) {
						nvgpu_channel_set_ctx_mmu_error(
							g, refch);
					}

					verbose = nvgpu_channel_mark_error(g,
							 refch);
					gk20a_channel_abort(ch, false);
				}
				gk20a_channel_put(ch);
			} else {
				nvgpu_err(g, "mmu error in freed channel %d",
					  ch->chid);
			}
		} else if (mmfault_info.inst_ptr ==
				nvgpu_inst_block_addr(g, &g->mm.bar1.inst_block)) {
			nvgpu_err(g, "mmu fault from bar1");
		} else if (mmfault_info.inst_ptr ==
				nvgpu_inst_block_addr(g, &g->mm.pmu.inst_block)) {
			nvgpu_err(g, "mmu fault from pmu");
		} else {
			nvgpu_err(g, "couldn't locate channel for mmu fault");
		}
	}

	if (!fake_fault) {
		gk20a_debug_dump(g);
	}

	/* clear interrupt */
	gk20a_writel(g, fifo_intr_mmu_fault_id_r(), (u32)fault_id);

	/* resume scheduler */
	gk20a_writel(g, fifo_error_sched_disable_r(),
		     gk20a_readl(g, fifo_error_sched_disable_r()));

	/* Re-enable fifo access */
	gk20a_writel(g, gr_gpfifo_ctl_r(),
		     gr_gpfifo_ctl_access_enabled_f() |
		     gr_gpfifo_ctl_semaphore_access_enabled_f());

	/* It is safe to enable ELPG again. */
	if (g->support_pmu && g->can_elpg) {
		nvgpu_pmu_enable_elpg(g);
	}

	return verbose;
}

static bool gk20a_fifo_handle_mmu_fault(
	struct gk20a *g,
	u32 mmu_fault_engines, /* queried from HW if 0 */
	u32 hw_id, /* queried from HW if ~(u32)0 OR mmu_fault_engines == 0*/
	bool id_is_tsg)
{
	u32 rlid;
	bool verbose;

	nvgpu_log_fn(g, " ");

	nvgpu_log_info(g, "acquire runlist_lock for all runlists");
	for (rlid = 0; rlid < g->fifo.max_runlists; rlid++) {
		nvgpu_mutex_acquire(&g->fifo.runlist_info[rlid].runlist_lock);
	}

	verbose = gk20a_fifo_handle_mmu_fault_locked(g, mmu_fault_engines,
			hw_id, id_is_tsg);

	nvgpu_log_info(g, "release runlist_lock for all runlists");
	for (rlid = 0; rlid < g->fifo.max_runlists; rlid++) {
		nvgpu_mutex_release(&g->fifo.runlist_info[rlid].runlist_lock);
	}
	return verbose;
}

static void gk20a_fifo_get_faulty_id_type(struct gk20a *g, u32 engine_id,
					  u32 *id, u32 *type)
{
	u32 status = gk20a_readl(g, fifo_engine_status_r(engine_id));
	u32 ctx_status = fifo_engine_status_ctx_status_v(status);

	/* use next_id if context load is failing */
	*id = (ctx_status ==
		fifo_engine_status_ctx_status_ctxsw_load_v()) ?
		fifo_engine_status_next_id_v(status) :
		fifo_engine_status_id_v(status);

	*type = (ctx_status ==
		fifo_engine_status_ctx_status_ctxsw_load_v()) ?
		fifo_engine_status_next_id_type_v(status) :
		fifo_engine_status_id_type_v(status);
}

u32 gk20a_fifo_engines_on_id(struct gk20a *g, u32 id, bool is_tsg)
{
	unsigned int i;
	u32 engines = 0;

	for (i = 0; i < g->fifo.num_engines; i++) {
		u32 active_engine_id = g->fifo.active_engines_list[i];
		u32 status = gk20a_readl(g, fifo_engine_status_r(active_engine_id));
		u32 ctx_status =
			fifo_engine_status_ctx_status_v(status);
		u32 ctx_id = (ctx_status ==
			fifo_engine_status_ctx_status_ctxsw_load_v()) ?
			fifo_engine_status_next_id_v(status) :
			fifo_engine_status_id_v(status);
		u32 type = (ctx_status ==
			fifo_engine_status_ctx_status_ctxsw_load_v()) ?
			fifo_engine_status_next_id_type_v(status) :
			fifo_engine_status_id_type_v(status);
		bool busy = fifo_engine_status_engine_v(status) ==
			fifo_engine_status_engine_busy_v();
		if (busy && ctx_id == id) {
			if ((is_tsg && type ==
					fifo_engine_status_id_type_tsgid_v()) ||
				    (!is_tsg && type ==
					fifo_engine_status_id_type_chid_v())) {
				engines |= BIT(active_engine_id);
			}
		}
	}

	return engines;
}

void gk20a_fifo_teardown_ch_tsg(struct gk20a *g, u32 __engine_ids,
			u32 hw_id, unsigned int id_type, unsigned int rc_type,
			 struct mmu_fault_info *mmfault)
{
	unsigned long engine_id, i;
	unsigned long _engine_ids = __engine_ids;
	unsigned long engine_ids = 0;
	u32 val;
	u32 mmu_fault_engines = 0;
	u32 ref_type;
	u32 ref_id;
	bool ref_id_is_tsg = false;
	bool id_is_known = (id_type != ID_TYPE_UNKNOWN) ? true : false;
	bool id_is_tsg = (id_type == ID_TYPE_TSG) ? true : false;
	u32 rlid;

	nvgpu_log_info(g, "acquire runlist_lock for all runlists");
	for (rlid = 0; rlid < g->fifo.max_runlists; rlid++) {
		nvgpu_mutex_acquire(&g->fifo.runlist_info[rlid].runlist_lock);
	}

	if (id_is_known) {
		engine_ids = g->ops.fifo.get_engines_mask_on_id(g,
				hw_id, id_is_tsg);
		ref_id = hw_id;
		ref_type = id_is_tsg ?
			fifo_engine_status_id_type_tsgid_v() :
			fifo_engine_status_id_type_chid_v();
		ref_id_is_tsg = id_is_tsg;
		/* atleast one engine will get passed during sched err*/
		engine_ids |= __engine_ids;
		for_each_set_bit(engine_id, &engine_ids, 32U) {
			u32 mmu_id = gk20a_engine_id_to_mmu_id(g,
							(u32)engine_id);

			if (mmu_id != FIFO_INVAL_ENGINE_ID) {
				mmu_fault_engines |= BIT(mmu_id);
			}
		}
	} else {
		/* store faulted engines in advance */
		for_each_set_bit(engine_id, &_engine_ids, 32U) {
			gk20a_fifo_get_faulty_id_type(g, (u32)engine_id,
						      &ref_id, &ref_type);
			if (ref_type == fifo_engine_status_id_type_tsgid_v()) {
				ref_id_is_tsg = true;
			} else {
				ref_id_is_tsg = false;
			}
			/* Reset *all* engines that use the
			 * same channel as faulty engine */
			for (i = 0; i < g->fifo.num_engines; i++) {
				u32 active_engine_id = g->fifo.active_engines_list[i];
				u32 type;
				u32 id;

				gk20a_fifo_get_faulty_id_type(g, active_engine_id, &id, &type);
				if (ref_type == type && ref_id == id) {
					u32 mmu_id = gk20a_engine_id_to_mmu_id(g, active_engine_id);

					engine_ids |= BIT(active_engine_id);
					if (mmu_id != FIFO_INVAL_ENGINE_ID) {
						mmu_fault_engines |= BIT(mmu_id);
					}
				}
			}
		}
	}

	if (mmu_fault_engines != 0U) {
		/*
		 * sched error prevents recovery, and ctxsw error will retrigger
		 * every 100ms. Disable the sched error to allow recovery.
		 */
		val = gk20a_readl(g, fifo_intr_en_0_r());
		val &= ~(fifo_intr_en_0_sched_error_m() |
			fifo_intr_en_0_mmu_fault_m());
		gk20a_writel(g, fifo_intr_en_0_r(), val);
		gk20a_writel(g, fifo_intr_0_r(),
				fifo_intr_0_sched_error_reset_f());

		g->ops.fifo.trigger_mmu_fault(g, engine_ids);
		gk20a_fifo_handle_mmu_fault_locked(g, mmu_fault_engines, ref_id,
				ref_id_is_tsg);

		val = gk20a_readl(g, fifo_intr_en_0_r());
		val |= fifo_intr_en_0_mmu_fault_f(1)
			| fifo_intr_en_0_sched_error_f(1);
		gk20a_writel(g, fifo_intr_en_0_r(), val);
	}

	nvgpu_log_info(g, "release runlist_lock for all runlists");
	for (rlid = 0; rlid < g->fifo.max_runlists; rlid++) {
		nvgpu_mutex_release(&g->fifo.runlist_info[rlid].runlist_lock);
	}
}

void gk20a_fifo_recover(struct gk20a *g, u32 engine_ids,
			u32 hw_id, bool id_is_tsg,
			bool id_is_known, bool verbose, u32 rc_type)
{
	unsigned int id_type;

	if (verbose) {
		gk20a_debug_dump(g);
	}

	if (g->ops.ltc.flush != NULL) {
		g->ops.ltc.flush(g);
	}

	if (id_is_known) {
		id_type = id_is_tsg ? ID_TYPE_TSG : ID_TYPE_CHANNEL;
	} else {
		id_type = ID_TYPE_UNKNOWN;
	}

	g->ops.fifo.teardown_ch_tsg(g, engine_ids, hw_id, id_type,
					 rc_type, NULL);
}

/* force reset channel and tsg (if it's part of one) */
int gk20a_fifo_force_reset_ch(struct channel_gk20a *ch,
				u32 err_code, bool verbose)
{
	struct channel_gk20a *ch_tsg = NULL;
	struct gk20a *g = ch->g;

	struct tsg_gk20a *tsg = tsg_gk20a_from_ch(ch);

	if (tsg != NULL) {

		nvgpu_rwsem_down_read(&tsg->ch_list_lock);

		nvgpu_list_for_each_entry(ch_tsg, &tsg->ch_list,
				channel_gk20a, ch_entry) {
			if (gk20a_channel_get(ch_tsg) != NULL) {
				g->ops.fifo.set_error_notifier(ch_tsg,
								err_code);
				gk20a_channel_put(ch_tsg);
			}
		}

		nvgpu_rwsem_up_read(&tsg->ch_list_lock);
		nvgpu_tsg_recover(g, tsg, verbose, RC_TYPE_FORCE_RESET);
	} else {
		g->ops.fifo.set_error_notifier(ch, err_code);
		nvgpu_channel_recover(g, ch, verbose,
				RC_TYPE_FORCE_RESET);
	}

	return 0;
}

int gk20a_fifo_tsg_unbind_channel_verify_status(struct channel_gk20a *ch)
{
	struct gk20a *g = ch->g;

	if (gk20a_fifo_channel_status_is_next(g, ch->chid)) {
		nvgpu_err(g, "Channel %d to be removed from TSG %d has NEXT set!",
			ch->chid, ch->tsgid);
		return -EINVAL;
	}

	if (g->ops.fifo.tsg_verify_status_ctx_reload != NULL) {
		g->ops.fifo.tsg_verify_status_ctx_reload(ch);
	}

	if (g->ops.fifo.tsg_verify_status_faulted != NULL) {
		g->ops.fifo.tsg_verify_status_faulted(ch);
	}

	return 0;
}

int gk20a_fifo_tsg_unbind_channel(struct channel_gk20a *ch)
{
	struct gk20a *g = ch->g;
	struct fifo_gk20a *f = &g->fifo;
	struct tsg_gk20a *tsg = &f->tsg[ch->tsgid];
	int err;
	bool tsg_timedout = false;

	/* If one channel in TSG times out, we disable all channels */
	nvgpu_rwsem_down_write(&tsg->ch_list_lock);
	tsg_timedout = gk20a_channel_check_timedout(ch);
	nvgpu_rwsem_up_write(&tsg->ch_list_lock);

	/* Disable TSG and examine status before unbinding channel */
	g->ops.fifo.disable_tsg(tsg);

	err = g->ops.fifo.preempt_tsg(g, tsg);
	if (err != 0) {
		goto fail_enable_tsg;
	}

	if ((g->ops.fifo.tsg_verify_channel_status != NULL) && !tsg_timedout) {
		err = g->ops.fifo.tsg_verify_channel_status(ch);
		if (err != 0) {
			goto fail_enable_tsg;
		}
	}

	/* Channel should be seen as TSG channel while updating runlist */
	err = channel_gk20a_update_runlist(ch, false);
	if (err != 0) {
		goto fail_enable_tsg;
	}

	/* Remove channel from TSG and re-enable rest of the channels */
	nvgpu_rwsem_down_write(&tsg->ch_list_lock);
	nvgpu_list_del(&ch->ch_entry);
	nvgpu_rwsem_up_write(&tsg->ch_list_lock);

	/*
	 * Don't re-enable all channels if TSG has timed out already
	 *
	 * Note that we can skip disabling and preempting TSG too in case of
	 * time out, but we keep that to ensure TSG is kicked out
	 */
	if (!tsg_timedout) {
		g->ops.fifo.enable_tsg(tsg);
	}

	if (ch->g->ops.fifo.ch_abort_clean_up != NULL) {
		ch->g->ops.fifo.ch_abort_clean_up(ch);
	}

	return 0;

fail_enable_tsg:
	if (!tsg_timedout) {
		g->ops.fifo.enable_tsg(tsg);
	}
	return err;
}

u32 gk20a_fifo_get_failing_engine_data(struct gk20a *g,
			u32 *__id, bool *__is_tsg)
{
	u32 engine_id;
	u32 id = U32_MAX;
	bool is_tsg = false;
	u32 mailbox2;
	u32 active_engine_id = FIFO_INVAL_ENGINE_ID;

	for (engine_id = 0; engine_id < g->fifo.num_engines; engine_id++) {
		u32 status;
		u32 ctx_status;
		bool failing_engine;

		active_engine_id = g->fifo.active_engines_list[engine_id];
		status = gk20a_readl(g, fifo_engine_status_r(active_engine_id));
		ctx_status = fifo_engine_status_ctx_status_v(status);

		/* we are interested in busy engines */
		failing_engine = fifo_engine_status_engine_v(status) ==
			fifo_engine_status_engine_busy_v();

		/* ..that are doing context switch */
		failing_engine = failing_engine &&
			(ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_switch_v()
			|| ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_save_v()
			|| ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_load_v());

		if (!failing_engine) {
		    active_engine_id = FIFO_INVAL_ENGINE_ID;
			continue;
		}

		if (ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_load_v()) {
			id = fifo_engine_status_next_id_v(status);
			is_tsg = fifo_engine_status_next_id_type_v(status) !=
				fifo_engine_status_next_id_type_chid_v();
		} else if (ctx_status ==
			       fifo_engine_status_ctx_status_ctxsw_switch_v()) {
			mailbox2 = gk20a_readl(g, gr_fecs_ctxsw_mailbox_r(2));
			if ((mailbox2 & FECS_METHOD_WFI_RESTORE) != 0U) {
				id = fifo_engine_status_next_id_v(status);
				is_tsg = fifo_engine_status_next_id_type_v(status) !=
					fifo_engine_status_next_id_type_chid_v();
			} else {
				id = fifo_engine_status_id_v(status);
				is_tsg = fifo_engine_status_id_type_v(status) !=
					fifo_engine_status_id_type_chid_v();
			}
		} else {
			id = fifo_engine_status_id_v(status);
			is_tsg = fifo_engine_status_id_type_v(status) !=
				fifo_engine_status_id_type_chid_v();
		}
		break;
	}

	*__id = id;
	*__is_tsg = is_tsg;

	return active_engine_id;
}

bool gk20a_fifo_handle_sched_error(struct gk20a *g)
{
	u32 sched_error;
	u32 engine_id;
	u32 id = U32_MAX;
	bool is_tsg = false;
	bool ret = false;
	struct channel_gk20a *ch = NULL;

	/* read the scheduler error register */
	sched_error = gk20a_readl(g, fifo_intr_sched_error_r());

	engine_id = gk20a_fifo_get_failing_engine_data(g, &id, &is_tsg);

	/* could not find the engine - should never happen */
	if (!gk20a_fifo_is_valid_engine_id(g, engine_id)) {
		nvgpu_err(g, "fifo sched error : 0x%08x, failed to find engine",
			sched_error);
		ret = false;
		goto err;
	}

	if (fifo_intr_sched_error_code_f(sched_error) ==
			fifo_intr_sched_error_code_ctxsw_timeout_v()) {
		struct fifo_gk20a *f = &g->fifo;
		u32 ms = 0;
		bool verbose = false;

		if (id > f->num_channels) {
			nvgpu_err(g, "fifo sched error : channel id invalid %u",
				  id);
			ret = false;
			goto err;
		}

		if (is_tsg) {
			ret = nvgpu_tsg_check_ctxsw_timeout(
					&f->tsg[id], &verbose, &ms);
		} else {
			ch = gk20a_channel_from_id(g, id);
			if (ch != NULL) {
				ret = g->ops.fifo.check_ch_ctxsw_timeout(
					ch, &verbose, &ms);

				gk20a_channel_put(ch);
			} else {
				/* skip recovery since channel is null */
				ret = false;
			}
		}

		if (ret) {
			nvgpu_err(g,
				"fifo sched ctxsw timeout error: "
				"engine=%u, %s=%d, ms=%u",
				engine_id, is_tsg ? "tsg" : "ch", id, ms);
			/*
			 * Cancel all channels' timeout since SCHED error might
			 * trigger multiple watchdogs at a time
			 */
			gk20a_channel_timeout_restart_all_channels(g);
			gk20a_fifo_recover(g, BIT(engine_id), id,
					is_tsg, true, verbose,
					RC_TYPE_CTXSW_TIMEOUT);
		} else {
			nvgpu_log_info(g,
				"fifo is waiting for ctx switch for %d ms, "
				"%s=%d", ms, is_tsg ? "tsg" : "ch", id);
		}
	} else {
		nvgpu_err(g,
			"fifo sched error : 0x%08x, engine=%u, %s=%d",
			sched_error, engine_id, is_tsg ? "tsg" : "ch", id);
	}

err:
	return ret;
}

static u32 fifo_error_isr(struct gk20a *g, u32 fifo_intr)
{
	u32 handled = 0;

	nvgpu_log_fn(g, "fifo_intr=0x%08x", fifo_intr);

	if ((fifo_intr & fifo_intr_0_pio_error_pending_f()) != 0U) {
		/* pio mode is unused.  this shouldn't happen, ever. */
		/* should we clear it or just leave it pending? */
		nvgpu_err(g, "fifo pio error!");
		BUG_ON(1);
	}

	if ((fifo_intr & fifo_intr_0_bind_error_pending_f()) != 0U) {
		u32 bind_error = gk20a_readl(g, fifo_intr_bind_error_r());
		nvgpu_err(g, "fifo bind error: 0x%08x", bind_error);
		handled |= fifo_intr_0_bind_error_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_sched_error_pending_f()) != 0U) {
		(void) g->ops.fifo.handle_sched_error(g);
		handled |= fifo_intr_0_sched_error_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_chsw_error_pending_f()) != 0U) {
		gk20a_fifo_handle_chsw_fault(g);
		handled |= fifo_intr_0_chsw_error_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_mmu_fault_pending_f()) != 0U) {
		(void) gk20a_fifo_handle_mmu_fault(g, 0, ~(u32)0, false);
		handled |= fifo_intr_0_mmu_fault_pending_f();
	}

	if ((fifo_intr & fifo_intr_0_dropped_mmu_fault_pending_f()) != 0U) {
		gk20a_fifo_handle_dropped_mmu_fault(g);
		handled |= fifo_intr_0_dropped_mmu_fault_pending_f();
	}

	return handled;
}

static inline void gk20a_fifo_reset_pbdma_header(struct gk20a *g, u32 pbdma_id)
{
	gk20a_writel(g, pbdma_pb_header_r(pbdma_id),
			pbdma_pb_header_first_true_f() |
			pbdma_pb_header_type_non_inc_f());
}

void gk20a_fifo_reset_pbdma_method(struct gk20a *g, u32 pbdma_id,
						u32 pbdma_method_index)
{
	u32 pbdma_method_stride;
	u32 pbdma_method_reg;

	pbdma_method_stride = pbdma_method1_r(pbdma_id) -
				pbdma_method0_r(pbdma_id);

	pbdma_method_reg = pbdma_method0_r(pbdma_id) +
		(pbdma_method_index * pbdma_method_stride);

	gk20a_writel(g, pbdma_method_reg,
			pbdma_method0_valid_true_f() |
			pbdma_method0_first_true_f() |
			pbdma_method0_addr_f(
			     pbdma_udma_nop_r() >> 2));
}

static bool gk20a_fifo_is_sw_method_subch(struct gk20a *g, u32 pbdma_id,
						u32 pbdma_method_index)
{
	u32 pbdma_method_stride;
	u32 pbdma_method_reg, pbdma_method_subch;

	pbdma_method_stride = pbdma_method1_r(pbdma_id) -
				pbdma_method0_r(pbdma_id);

	pbdma_method_reg = pbdma_method0_r(pbdma_id) +
			(pbdma_method_index * pbdma_method_stride);

	pbdma_method_subch = pbdma_method0_subch_v(
			gk20a_readl(g, pbdma_method_reg));

	if (pbdma_method_subch == 5U ||
	    pbdma_method_subch == 6U ||
	    pbdma_method_subch == 7U) {
		return true;
	}

	return false;
}

unsigned int gk20a_fifo_handle_pbdma_intr_0(struct gk20a *g, u32 pbdma_id,
			u32 pbdma_intr_0, u32 *handled, u32 *error_notifier)
{
	struct fifo_gk20a *f = &g->fifo;
	unsigned int rc_type = RC_TYPE_NO_RC;
	u32 i;
	unsigned long pbdma_intr_err;
	unsigned long bit;

	if (((f->intr.pbdma.device_fatal_0 |
	      f->intr.pbdma.channel_fatal_0 |
	      f->intr.pbdma.restartable_0) & pbdma_intr_0) != 0U) {

		pbdma_intr_err = (unsigned long)pbdma_intr_0;
		for_each_set_bit(bit, &pbdma_intr_err, 32U) {
			nvgpu_err(g, "PBDMA intr %s Error",
				pbdma_intr_fault_type_desc[bit]);
		}

		nvgpu_err(g,
			"pbdma_intr_0(%d):0x%08x PBH: %08x "
			"SHADOW: %08x gp shadow0: %08x gp shadow1: %08x"
			"M0: %08x %08x %08x %08x ",
			pbdma_id, pbdma_intr_0,
			gk20a_readl(g, pbdma_pb_header_r(pbdma_id)),
			gk20a_readl(g, pbdma_hdr_shadow_r(pbdma_id)),
			gk20a_readl(g, pbdma_gp_shadow_0_r(pbdma_id)),
			gk20a_readl(g, pbdma_gp_shadow_1_r(pbdma_id)),
			gk20a_readl(g, pbdma_method0_r(pbdma_id)),
			gk20a_readl(g, pbdma_method1_r(pbdma_id)),
			gk20a_readl(g, pbdma_method2_r(pbdma_id)),
			gk20a_readl(g, pbdma_method3_r(pbdma_id))
			);

		rc_type = RC_TYPE_PBDMA_FAULT;
		*handled |= ((f->intr.pbdma.device_fatal_0 |
			     f->intr.pbdma.channel_fatal_0 |
			     f->intr.pbdma.restartable_0) &
			    pbdma_intr_0);
	}

	if ((pbdma_intr_0 & pbdma_intr_0_acquire_pending_f()) != 0U) {
		u32 val = gk20a_readl(g, pbdma_acquire_r(pbdma_id));

		val &= ~pbdma_acquire_timeout_en_enable_f();
		gk20a_writel(g, pbdma_acquire_r(pbdma_id), val);
		if (nvgpu_is_timeouts_enabled(g)) {
			rc_type = RC_TYPE_PBDMA_FAULT;
			nvgpu_err(g,
				"semaphore acquire timeout!");
			*error_notifier = NVGPU_ERR_NOTIFIER_GR_SEMAPHORE_TIMEOUT;
		}
		*handled |= pbdma_intr_0_acquire_pending_f();
	}

	if ((pbdma_intr_0 & pbdma_intr_0_pbentry_pending_f()) != 0U) {
		gk20a_fifo_reset_pbdma_header(g, pbdma_id);
		gk20a_fifo_reset_pbdma_method(g, pbdma_id, 0);
		rc_type = RC_TYPE_PBDMA_FAULT;
	}

	if ((pbdma_intr_0 & pbdma_intr_0_method_pending_f()) != 0U) {
		gk20a_fifo_reset_pbdma_method(g, pbdma_id, 0);
		rc_type = RC_TYPE_PBDMA_FAULT;
	}

	if ((pbdma_intr_0 & pbdma_intr_0_pbcrc_pending_f()) != 0U) {
		*error_notifier =
			NVGPU_ERR_NOTIFIER_PBDMA_PUSHBUFFER_CRC_MISMATCH;
		rc_type = RC_TYPE_PBDMA_FAULT;
	}

	if ((pbdma_intr_0 & pbdma_intr_0_device_pending_f()) != 0U) {
		gk20a_fifo_reset_pbdma_header(g, pbdma_id);

		for (i = 0U; i < 4U; i++) {
			if (gk20a_fifo_is_sw_method_subch(g,
					pbdma_id, i)) {
				gk20a_fifo_reset_pbdma_method(g,
						pbdma_id, i);
			}
		}
		rc_type = RC_TYPE_PBDMA_FAULT;
	}

	return rc_type;
}

unsigned int gk20a_fifo_handle_pbdma_intr_1(struct gk20a *g,
			u32 pbdma_id, u32 pbdma_intr_1,
			u32 *handled, u32 *error_notifier)
{
	unsigned int rc_type = RC_TYPE_PBDMA_FAULT;

	/*
	 * all of the interrupts in _intr_1 are "host copy engine"
	 * related, which is not supported. For now just make them
	 * channel fatal.
	 */
	nvgpu_err(g, "hce err: pbdma_intr_1(%d):0x%08x",
		pbdma_id, pbdma_intr_1);
	*handled |= pbdma_intr_1;

	return rc_type;
}

static void gk20a_fifo_pbdma_fault_rc(struct gk20a *g,
			struct fifo_gk20a *f, u32 pbdma_id,
			u32 error_notifier)
{
	u32 status;
	u32 id;

	nvgpu_log(g, gpu_dbg_info, "pbdma id %d error notifier %d",
			pbdma_id, error_notifier);
	status = gk20a_readl(g, fifo_pbdma_status_r(pbdma_id));
	/* Remove channel from runlist */
	id = fifo_pbdma_status_id_v(status);
	if (fifo_pbdma_status_id_type_v(status)
			== fifo_pbdma_status_id_type_chid_v()) {
		struct channel_gk20a *ch = gk20a_channel_from_id(g, id);

		if (ch != NULL) {
			g->ops.fifo.set_error_notifier(ch, error_notifier);
			nvgpu_channel_recover(g, ch, true, RC_TYPE_PBDMA_FAULT);
			gk20a_channel_put(ch);
		}
	} else if (fifo_pbdma_status_id_type_v(status)
			== fifo_pbdma_status_id_type_tsgid_v()) {
		struct tsg_gk20a *tsg = &f->tsg[id];
		struct channel_gk20a *ch = NULL;

		nvgpu_rwsem_down_read(&tsg->ch_list_lock);
		nvgpu_list_for_each_entry(ch, &tsg->ch_list,
				channel_gk20a, ch_entry) {
			if (gk20a_channel_get(ch) != NULL) {
				g->ops.fifo.set_error_notifier(ch,
					error_notifier);
				gk20a_channel_put(ch);
			}
		}
		nvgpu_rwsem_up_read(&tsg->ch_list_lock);
		nvgpu_tsg_recover(g, tsg, true, RC_TYPE_PBDMA_FAULT);
	}
}

u32 gk20a_fifo_handle_pbdma_intr(struct gk20a *g, struct fifo_gk20a *f,
			u32 pbdma_id, unsigned int rc)
{
	u32 pbdma_intr_0 = gk20a_readl(g, pbdma_intr_0_r(pbdma_id));
	u32 pbdma_intr_1 = gk20a_readl(g, pbdma_intr_1_r(pbdma_id));

	u32 handled = 0;
	u32 error_notifier = NVGPU_ERR_NOTIFIER_PBDMA_ERROR;
	unsigned int rc_type = RC_TYPE_NO_RC;

	if (pbdma_intr_0 != 0U) {
		nvgpu_log(g, gpu_dbg_info | gpu_dbg_intr,
			"pbdma id %d intr_0 0x%08x pending",
			pbdma_id, pbdma_intr_0);

		if (g->ops.fifo.handle_pbdma_intr_0(g, pbdma_id, pbdma_intr_0,
			&handled, &error_notifier) != RC_TYPE_NO_RC) {
			rc_type = RC_TYPE_PBDMA_FAULT;
		}
		gk20a_writel(g, pbdma_intr_0_r(pbdma_id), pbdma_intr_0);
	}

	if (pbdma_intr_1 != 0U) {
		nvgpu_log(g, gpu_dbg_info | gpu_dbg_intr,
			"pbdma id %d intr_1 0x%08x pending",
			pbdma_id, pbdma_intr_1);

		if (g->ops.fifo.handle_pbdma_intr_1(g, pbdma_id, pbdma_intr_1,
			&handled, &error_notifier) != RC_TYPE_NO_RC) {
			rc_type = RC_TYPE_PBDMA_FAULT;
		}
		gk20a_writel(g, pbdma_intr_1_r(pbdma_id), pbdma_intr_1);
	}

	if (rc == RC_YES && rc_type == RC_TYPE_PBDMA_FAULT) {
		gk20a_fifo_pbdma_fault_rc(g, f, pbdma_id, error_notifier);
	}

	return handled;
}

static u32 fifo_pbdma_isr(struct gk20a *g, u32 fifo_intr)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 clear_intr = 0, i;
	u32 host_num_pbdma = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_PBDMA);
	u32 pbdma_pending = gk20a_readl(g, fifo_intr_pbdma_id_r());

	for (i = 0; i < host_num_pbdma; i++) {
		if (fifo_intr_pbdma_id_status_v(pbdma_pending, i) != 0U) {
			nvgpu_log(g, gpu_dbg_intr, "pbdma id %d intr pending", i);
			clear_intr |=
				gk20a_fifo_handle_pbdma_intr(g, f, i, RC_YES);
		}
	}
	return fifo_intr_0_pbdma_intr_pending_f();
}

void gk20a_fifo_isr(struct gk20a *g)
{
	u32 error_intr_mask;
	u32 clear_intr = 0;
	u32 fifo_intr = gk20a_readl(g, fifo_intr_0_r());

	error_intr_mask = g->ops.fifo.intr_0_error_mask(g);

	if (g->fifo.sw_ready) {
		/* note we're not actually in an "isr", but rather
		 * in a threaded interrupt context... */
		nvgpu_mutex_acquire(&g->fifo.intr.isr.mutex);

		nvgpu_log(g, gpu_dbg_intr, "fifo isr %08x\n", fifo_intr);

		/* handle runlist update */
		if ((fifo_intr & fifo_intr_0_runlist_event_pending_f()) != 0U) {
			gk20a_fifo_handle_runlist_event(g);
			clear_intr |= fifo_intr_0_runlist_event_pending_f();
		}
		if ((fifo_intr & fifo_intr_0_pbdma_intr_pending_f()) != 0U) {
			clear_intr |= fifo_pbdma_isr(g, fifo_intr);
		}

		if (g->ops.fifo.handle_ctxsw_timeout != NULL) {
			g->ops.fifo.handle_ctxsw_timeout(g, fifo_intr);
		}

		if (unlikely((fifo_intr & error_intr_mask) != 0U)) {
			clear_intr |= fifo_error_isr(g, fifo_intr);
		}

		nvgpu_mutex_release(&g->fifo.intr.isr.mutex);
	}
	gk20a_writel(g, fifo_intr_0_r(), clear_intr);

	return;
}

u32 gk20a_fifo_nonstall_isr(struct gk20a *g)
{
	u32 fifo_intr = gk20a_readl(g, fifo_intr_0_r());
	u32 clear_intr = 0;

	nvgpu_log(g, gpu_dbg_intr, "fifo nonstall isr %08x\n", fifo_intr);

	if ((fifo_intr & fifo_intr_0_channel_intr_pending_f()) != 0U) {
		clear_intr = fifo_intr_0_channel_intr_pending_f();
	}

	gk20a_writel(g, fifo_intr_0_r(), clear_intr);

	return GK20A_NONSTALL_OPS_WAKEUP_SEMAPHORE;
}

void gk20a_fifo_issue_preempt(struct gk20a *g, u32 id, bool is_tsg)
{
	if (is_tsg) {
		gk20a_writel(g, fifo_preempt_r(),
			fifo_preempt_id_f(id) |
			fifo_preempt_type_tsg_f());
	} else {
		gk20a_writel(g, fifo_preempt_r(),
			fifo_preempt_chid_f(id) |
			fifo_preempt_type_channel_f());
	}
}

static u32 gk20a_fifo_get_preempt_timeout(struct gk20a *g)
{
	/* Use fifo_eng_timeout converted to ms for preempt
	 * polling. gr_idle_timeout i.e 3000 ms is and not appropriate
	 * for polling preempt done as context switch timeout gets
	 * triggered every 100 ms and context switch recovery
	 * happens every 3000 ms */

	return g->fifo_eng_timeout_us / 1000U;
}

int gk20a_fifo_is_preempt_pending(struct gk20a *g, u32 id,
		unsigned int id_type)
{
	struct nvgpu_timeout timeout;
	u32 delay = GR_IDLE_CHECK_DEFAULT;
	int ret = -EBUSY;

	nvgpu_timeout_init(g, &timeout, gk20a_fifo_get_preempt_timeout(g),
			   NVGPU_TIMER_CPU_TIMER);
	do {
		if ((gk20a_readl(g, fifo_preempt_r()) &
				fifo_preempt_pending_true_f()) == 0U) {
			ret = 0;
			break;
		}

		nvgpu_usleep_range(delay, delay * 2U);
		delay = min_t(u32, delay << 1, GR_IDLE_CHECK_MAX);
	} while (nvgpu_timeout_expired(&timeout) == 0);

	if (ret != 0) {
		nvgpu_err(g, "preempt timeout: id: %u id_type: %d ",
			id, id_type);
	}
	return ret;
}

void gk20a_fifo_preempt_timeout_rc_tsg(struct gk20a *g, struct tsg_gk20a *tsg)
{
	struct channel_gk20a *ch = NULL;

	nvgpu_err(g, "preempt TSG %d timeout", tsg->tsgid);

	nvgpu_rwsem_down_read(&tsg->ch_list_lock);
	nvgpu_list_for_each_entry(ch, &tsg->ch_list,
			channel_gk20a, ch_entry) {
		if (gk20a_channel_get(ch) == NULL) {
			continue;
		}
		g->ops.fifo.set_error_notifier(ch,
			NVGPU_ERR_NOTIFIER_FIFO_ERROR_IDLE_TIMEOUT);
		gk20a_channel_put(ch);
	}
	nvgpu_rwsem_up_read(&tsg->ch_list_lock);
	nvgpu_tsg_recover(g, tsg, true, RC_TYPE_PREEMPT_TIMEOUT);
}

void gk20a_fifo_preempt_timeout_rc(struct gk20a *g, struct channel_gk20a *ch)
{
	nvgpu_err(g, "preempt channel %d timeout", ch->chid);

	g->ops.fifo.set_error_notifier(ch,
				NVGPU_ERR_NOTIFIER_FIFO_ERROR_IDLE_TIMEOUT);
	nvgpu_channel_recover(g, ch, true, RC_TYPE_PREEMPT_TIMEOUT);
}

int __locked_fifo_preempt(struct gk20a *g, u32 id, bool is_tsg)
{
	int ret;
	unsigned int id_type;

	nvgpu_log_fn(g, "id: %d is_tsg: %d", id, is_tsg);

	/* issue preempt */
	gk20a_fifo_issue_preempt(g, id, is_tsg);

	id_type = is_tsg ? ID_TYPE_TSG : ID_TYPE_CHANNEL;

	/* wait for preempt */
	ret = g->ops.fifo.is_preempt_pending(g, id, id_type);

	return ret;
}

int gk20a_fifo_preempt_channel(struct gk20a *g, struct channel_gk20a *ch)
{
	struct fifo_gk20a *f = &g->fifo;
	int ret = 0;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret = 0;
	u32 i;

	nvgpu_log_fn(g, "chid: %d", ch->chid);

	/* we have no idea which runlist we are using. lock all */
	for (i = 0; i < g->fifo.max_runlists; i++) {
		nvgpu_mutex_acquire(&f->runlist_info[i].runlist_lock);
	}

	mutex_ret = nvgpu_pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	ret = __locked_fifo_preempt(g, ch->chid, false);

	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}

	for (i = 0; i < g->fifo.max_runlists; i++) {
		nvgpu_mutex_release(&f->runlist_info[i].runlist_lock);
	}

	if (ret != 0) {
		if (nvgpu_platform_is_silicon(g)) {
			nvgpu_err(g, "preempt timed out for chid: %u, "
			"ctxsw timeout will trigger recovery if needed",
			ch->chid);
		} else {
			gk20a_fifo_preempt_timeout_rc(g, ch);
		}
	}



	return ret;
}

int gk20a_fifo_preempt_tsg(struct gk20a *g, struct tsg_gk20a *tsg)
{
	struct fifo_gk20a *f = &g->fifo;
	int ret = 0;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret = 0;
	u32 i;

	nvgpu_log_fn(g, "tsgid: %d", tsg->tsgid);

	/* we have no idea which runlist we are using. lock all */
	for (i = 0; i < g->fifo.max_runlists; i++) {
		nvgpu_mutex_acquire(&f->runlist_info[i].runlist_lock);
	}

	mutex_ret = nvgpu_pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	ret = __locked_fifo_preempt(g, tsg->tsgid, true);

	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}

	for (i = 0; i < g->fifo.max_runlists; i++) {
		nvgpu_mutex_release(&f->runlist_info[i].runlist_lock);
	}

	if (ret != 0) {
		if (nvgpu_platform_is_silicon(g)) {
			nvgpu_err(g, "preempt timed out for tsgid: %u, "
			"ctxsw timeout will trigger recovery if needed",
			tsg->tsgid);
		} else {
			gk20a_fifo_preempt_timeout_rc_tsg(g, tsg);
		}
	}

	return ret;
}

int gk20a_fifo_preempt(struct gk20a *g, struct channel_gk20a *ch)
{
	int err;
	struct tsg_gk20a *tsg = tsg_gk20a_from_ch(ch);

	if (tsg != NULL) {
		err = g->ops.fifo.preempt_tsg(ch->g, tsg);
	} else {
		err = g->ops.fifo.preempt_channel(ch->g, ch);
	}

	return err;
}

void gk20a_fifo_runlist_write_state(struct gk20a *g, u32 runlists_mask,
					 u32 runlist_state)
{
	u32 reg_val;
	u32 reg_mask = 0U;
	u32 i = 0U;

	while (runlists_mask != 0U) {
		if ((runlists_mask & BIT32(i)) != 0U) {
			reg_mask |= fifo_sched_disable_runlist_m(i);
		}
		runlists_mask &= ~BIT32(i);
		i++;
	}

	reg_val = gk20a_readl(g, fifo_sched_disable_r());

	if (runlist_state == RUNLIST_DISABLED) {
		reg_val |= reg_mask;
	} else {
		reg_val &= ~reg_mask;
	}

	gk20a_writel(g, fifo_sched_disable_r(), reg_val);

}

void gk20a_fifo_set_runlist_state(struct gk20a *g, u32 runlists_mask,
		u32 runlist_state)
{
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret;

	nvgpu_log(g, gpu_dbg_info, "runlist mask = 0x%08x state = 0x%08x",
			runlists_mask, runlist_state);

	mutex_ret = nvgpu_pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	g->ops.fifo.runlist_write_state(g, runlists_mask, runlist_state);

	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}
}

void gk20a_fifo_enable_tsg_sched(struct gk20a *g, struct tsg_gk20a *tsg)
{
	gk20a_fifo_set_runlist_state(g, BIT32(tsg->runlist_id),
			RUNLIST_ENABLED);

}

void gk20a_fifo_disable_tsg_sched(struct gk20a *g, struct tsg_gk20a *tsg)
{
	gk20a_fifo_set_runlist_state(g, BIT32(tsg->runlist_id),
			RUNLIST_DISABLED);
}

int gk20a_fifo_enable_engine_activity(struct gk20a *g,
				struct fifo_engine_info_gk20a *eng_info)
{
	nvgpu_log(g, gpu_dbg_info, "start");

	gk20a_fifo_set_runlist_state(g, BIT32(eng_info->runlist_id),
			RUNLIST_ENABLED);
	return 0;
}

int gk20a_fifo_enable_all_engine_activity(struct gk20a *g)
{
	unsigned int i;
	int err = 0, ret = 0;

	for (i = 0; i < g->fifo.num_engines; i++) {
		u32 active_engine_id = g->fifo.active_engines_list[i];
		err = gk20a_fifo_enable_engine_activity(g,
				&g->fifo.engine_info[active_engine_id]);
		if (err != 0) {
			nvgpu_err(g,
				"failed to enable engine %d activity", active_engine_id);
			ret = err;
		}
	}

	return ret;
}

int gk20a_fifo_disable_engine_activity(struct gk20a *g,
				struct fifo_engine_info_gk20a *eng_info,
				bool wait_for_idle)
{
	u32 gr_stat, pbdma_stat, chan_stat, eng_stat, ctx_stat;
	u32 pbdma_chid = FIFO_INVAL_CHANNEL_ID;
	u32 engine_chid = FIFO_INVAL_CHANNEL_ID;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret;
	struct channel_gk20a *ch = NULL;
	int err = 0;

	nvgpu_log_fn(g, " ");

	gr_stat =
		gk20a_readl(g, fifo_engine_status_r(eng_info->engine_id));
	if (fifo_engine_status_engine_v(gr_stat) ==
	    fifo_engine_status_engine_busy_v() && !wait_for_idle) {
		return -EBUSY;
	}

	mutex_ret = nvgpu_pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	gk20a_fifo_set_runlist_state(g, BIT32(eng_info->runlist_id),
			RUNLIST_DISABLED);

	/* chid from pbdma status */
	pbdma_stat = gk20a_readl(g, fifo_pbdma_status_r(eng_info->pbdma_id));
	chan_stat  = fifo_pbdma_status_chan_status_v(pbdma_stat);
	if (chan_stat == fifo_pbdma_status_chan_status_valid_v() ||
	    chan_stat == fifo_pbdma_status_chan_status_chsw_save_v()) {
		pbdma_chid = fifo_pbdma_status_id_v(pbdma_stat);
	} else if (chan_stat == fifo_pbdma_status_chan_status_chsw_load_v() ||
		 chan_stat == fifo_pbdma_status_chan_status_chsw_switch_v()) {
		pbdma_chid = fifo_pbdma_status_next_id_v(pbdma_stat);
	}

	if (pbdma_chid != FIFO_INVAL_CHANNEL_ID) {
		ch = gk20a_channel_from_id(g, pbdma_chid);
		if (ch != NULL) {
			err = g->ops.fifo.preempt_channel(g, ch);
			gk20a_channel_put(ch);
		}
		if (err != 0) {
			goto clean_up;
		}
	}

	/* chid from engine status */
	eng_stat = gk20a_readl(g, fifo_engine_status_r(eng_info->engine_id));
	ctx_stat  = fifo_engine_status_ctx_status_v(eng_stat);
	if (ctx_stat == fifo_engine_status_ctx_status_valid_v() ||
	    ctx_stat == fifo_engine_status_ctx_status_ctxsw_save_v()) {
		engine_chid = fifo_engine_status_id_v(eng_stat);
	} else if (ctx_stat == fifo_engine_status_ctx_status_ctxsw_load_v() ||
		 ctx_stat == fifo_engine_status_ctx_status_ctxsw_switch_v()) {
		engine_chid = fifo_engine_status_next_id_v(eng_stat);
	}

	if (engine_chid != FIFO_INVAL_ENGINE_ID && engine_chid != pbdma_chid) {
		ch = gk20a_channel_from_id(g, engine_chid);
		if (ch != NULL) {
			err = g->ops.fifo.preempt_channel(g, ch);
			gk20a_channel_put(ch);
		}
		if (err != 0) {
			goto clean_up;
		}
	}

clean_up:
	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}

	if (err != 0) {
		nvgpu_log_fn(g, "failed");
		if (gk20a_fifo_enable_engine_activity(g, eng_info) != 0) {
			nvgpu_err(g,
				"failed to enable gr engine activity");
		}
	} else {
		nvgpu_log_fn(g, "done");
	}
	return err;
}

int gk20a_fifo_disable_all_engine_activity(struct gk20a *g,
					   bool wait_for_idle)
{
	unsigned int i;
	int err = 0, ret = 0;
	u32 active_engine_id;

	for (i = 0; i < g->fifo.num_engines; i++) {
		active_engine_id = g->fifo.active_engines_list[i];
		err = gk20a_fifo_disable_engine_activity(g,
				&g->fifo.engine_info[active_engine_id],
				wait_for_idle);
		if (err != 0) {
			nvgpu_err(g, "failed to disable engine %d activity",
				active_engine_id);
			ret = err;
			break;
		}
	}

	if (err != 0) {
		while (i-- != 0U) {
			active_engine_id = g->fifo.active_engines_list[i];
			err = gk20a_fifo_enable_engine_activity(g,
					&g->fifo.engine_info[active_engine_id]);
			if (err != 0) {
				nvgpu_err(g,
					"failed to re-enable engine %d activity",
					active_engine_id);
			}
		}
	}

	return ret;
}

u32 gk20a_fifo_runlist_busy_engines(struct gk20a *g, u32 runlist_id)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 engines = 0;
	unsigned int i;

	for (i = 0; i < f->num_engines; i++) {
		u32 active_engine_id = f->active_engines_list[i];
		u32 engine_runlist = f->engine_info[active_engine_id].runlist_id;
		u32 status_reg = fifo_engine_status_r(active_engine_id);
		u32 status = gk20a_readl(g, status_reg);
		bool engine_busy = fifo_engine_status_engine_v(status) ==
			fifo_engine_status_engine_busy_v();

		if (engine_busy && engine_runlist == runlist_id) {
			engines |= BIT(active_engine_id);
		}
	}

	return engines;
}

static void gk20a_fifo_runlist_reset_engines(struct gk20a *g, u32 runlist_id)
{
	u32 engines = g->ops.fifo.runlist_busy_engines(g, runlist_id);

	if (engines != 0U) {
		gk20a_fifo_recover(g, engines, ~(u32)0, false, false, true,
				RC_TYPE_RUNLIST_UPDATE_TIMEOUT);
	}
}

int gk20a_fifo_runlist_wait_pending(struct gk20a *g, u32 runlist_id)
{
	struct nvgpu_timeout timeout;
	u32 delay = GR_IDLE_CHECK_DEFAULT;
	int ret = -ETIMEDOUT;

	nvgpu_timeout_init(g, &timeout, gk20a_get_gr_idle_timeout(g),
			   NVGPU_TIMER_CPU_TIMER);

	do {
		if ((gk20a_readl(g, fifo_eng_runlist_r(runlist_id)) &
				fifo_eng_runlist_pending_true_f()) == 0U) {
			ret = 0;
			break;
		}

		nvgpu_usleep_range(delay, delay * 2U);
		delay = min_t(u32, delay << 1, GR_IDLE_CHECK_MAX);
	} while (nvgpu_timeout_expired(&timeout) == 0);

	if (ret != 0) {
		nvgpu_err(g, "runlist wait timeout: runlist id: %u",
			runlist_id);
	}

	return ret;
}

void gk20a_get_tsg_runlist_entry(struct tsg_gk20a *tsg, u32 *runlist)
{

	u32 runlist_entry_0 = ram_rl_entry_id_f(tsg->tsgid) |
			ram_rl_entry_type_tsg_f() |
			ram_rl_entry_tsg_length_f(tsg->num_active_channels);

	if (tsg->timeslice_timeout != 0U) {
		runlist_entry_0 |=
			ram_rl_entry_timeslice_scale_f(tsg->timeslice_scale) |
			ram_rl_entry_timeslice_timeout_f(tsg->timeslice_timeout);
	} else {
		/* safety check before casting */
#if (NVGPU_FIFO_DEFAULT_TIMESLICE_SCALE & 0xffffffff00000000UL)
#error NVGPU_FIFO_DEFAULT_TIMESLICE_SCALE too large for u32 cast
#endif
#if (NVGPU_FIFO_DEFAULT_TIMESLICE_TIMEOUT & 0xffffffff00000000UL)
#error NVGPU_FIFO_DEFAULT_TIMESLICE_TIMEOUT too large for u32 cast
#endif
		runlist_entry_0 |=
			ram_rl_entry_timeslice_scale_f(
				(u32)NVGPU_FIFO_DEFAULT_TIMESLICE_SCALE) |
			ram_rl_entry_timeslice_timeout_f(
				(u32)NVGPU_FIFO_DEFAULT_TIMESLICE_TIMEOUT);
	}

	runlist[0] = runlist_entry_0;
	runlist[1] = 0;

}

u32 gk20a_fifo_default_timeslice_us(struct gk20a *g)
{
	u64 slice = (((u64)(NVGPU_FIFO_DEFAULT_TIMESLICE_TIMEOUT <<
				NVGPU_FIFO_DEFAULT_TIMESLICE_SCALE) *
			(u64)g->ptimer_src_freq) /
			(u64)PTIMER_REF_FREQ_HZ);

	BUG_ON(slice > U64(U32_MAX));

	return (u32)slice;
}

void gk20a_get_ch_runlist_entry(struct channel_gk20a *ch, u32 *runlist)
{
	runlist[0] = ram_rl_entry_chid_f(ch->chid);
	runlist[1] = 0;
}

static u32 nvgpu_runlist_append_tsg(struct gk20a *g,
		struct fifo_runlist_info_gk20a *runlist,
		u32 **runlist_entry,
		u32 *entries_left,
		struct tsg_gk20a *tsg)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 runlist_entry_words = f->runlist_entry_size / (u32)sizeof(u32);
	struct channel_gk20a *ch;
	u32 count = 0;

	nvgpu_log_fn(f->g, " ");

	if (*entries_left == 0U) {
		return RUNLIST_APPEND_FAILURE;
	}

	/* add TSG entry */
	nvgpu_log_info(g, "add TSG %d to runlist", tsg->tsgid);
	g->ops.fifo.get_tsg_runlist_entry(tsg, *runlist_entry);
	nvgpu_log_info(g, "tsg rl entries left %d runlist [0] %x [1] %x",
			*entries_left,
			(*runlist_entry)[0], (*runlist_entry)[1]);
	*runlist_entry += runlist_entry_words;
	count++;
	(*entries_left)--;

	nvgpu_rwsem_down_read(&tsg->ch_list_lock);
	/* add runnable channels bound to this TSG */
	nvgpu_list_for_each_entry(ch, &tsg->ch_list,
			channel_gk20a, ch_entry) {
		if (!test_bit((int)ch->chid,
			      runlist->active_channels)) {
			continue;
		}

		if (*entries_left == 0U) {
			nvgpu_rwsem_up_read(&tsg->ch_list_lock);
			return RUNLIST_APPEND_FAILURE;
		}

		nvgpu_log_info(g, "add channel %d to runlist",
			ch->chid);
		g->ops.fifo.get_ch_runlist_entry(ch, *runlist_entry);
		nvgpu_log_info(g, "rl entries left %d runlist [0] %x [1] %x",
			*entries_left,
			(*runlist_entry)[0], (*runlist_entry)[1]);
		count++;
		*runlist_entry += runlist_entry_words;
		(*entries_left)--;
	}
	nvgpu_rwsem_up_read(&tsg->ch_list_lock);

	return count;
}


static u32 nvgpu_runlist_append_prio(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left,
				u32 interleave_level)
{
	u32 count = 0;
	unsigned long tsgid;

	nvgpu_log_fn(f->g, " ");

	for_each_set_bit(tsgid, runlist->active_tsgs, f->num_channels) {
		struct tsg_gk20a *tsg = &f->tsg[tsgid];
		u32 entries;

		if (tsg->interleave_level == interleave_level) {
			entries = nvgpu_runlist_append_tsg(f->g, runlist,
					runlist_entry, entries_left, tsg);
			if (entries == RUNLIST_APPEND_FAILURE) {
				return RUNLIST_APPEND_FAILURE;
			}
			count += entries;
		}
	}

	return count;
}

static u32 nvgpu_runlist_append_hi(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left)
{
	nvgpu_log_fn(f->g, " ");

	/*
	 * No higher levels - this is where the "recursion" ends; just add all
	 * active TSGs at this level.
	 */
	return nvgpu_runlist_append_prio(f, runlist, runlist_entry,
			entries_left,
			NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_HIGH);
}

static u32 nvgpu_runlist_append_med(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left)
{
	u32 count = 0;
	unsigned long tsgid;

	nvgpu_log_fn(f->g, " ");

	for_each_set_bit(tsgid, runlist->active_tsgs, f->num_channels) {
		struct tsg_gk20a *tsg = &f->tsg[tsgid];
		u32 entries;

		if (tsg->interleave_level !=
				NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_MEDIUM) {
			continue;
		}

		/* LEVEL_MEDIUM list starts with a LEVEL_HIGH, if any */

		entries = nvgpu_runlist_append_hi(f, runlist,
				runlist_entry, entries_left);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;

		entries = nvgpu_runlist_append_tsg(f->g, runlist,
				runlist_entry, entries_left, tsg);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;
	}

	return count;
}

static u32 nvgpu_runlist_append_low(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left)
{
	u32 count = 0;
	unsigned long tsgid;

	nvgpu_log_fn(f->g, " ");

	for_each_set_bit(tsgid, runlist->active_tsgs, f->num_channels) {
		struct tsg_gk20a *tsg = &f->tsg[tsgid];
		u32 entries;

		if (tsg->interleave_level !=
				NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_LOW) {
			continue;
		}

		/* The medium level starts with the highs, if any. */

		entries = nvgpu_runlist_append_med(f, runlist,
				runlist_entry, entries_left);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;

		entries = nvgpu_runlist_append_hi(f, runlist,
				runlist_entry, entries_left);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;

		entries = nvgpu_runlist_append_tsg(f->g, runlist,
				runlist_entry, entries_left, tsg);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;
	}

	if (count == 0U) {
		/*
		 * No transitions to fill with higher levels, so add
		 * the next level once. If that's empty too, we have only
		 * LEVEL_HIGH jobs.
		 */
		count = nvgpu_runlist_append_med(f, runlist,
				runlist_entry, entries_left);
		if (count == 0U) {
			count = nvgpu_runlist_append_hi(f, runlist,
					runlist_entry, entries_left);
		}
	}

	return count;
}

static u32 nvgpu_runlist_append_flat(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 **runlist_entry,
				u32 *entries_left)
{
	u32 count = 0, entries, i;

	nvgpu_log_fn(f->g, " ");

	/* Group by priority but don't interleave. High comes first. */

	for (i = 0; i < NVGPU_FIFO_RUNLIST_INTERLEAVE_NUM_LEVELS; i++) {
		u32 level = NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_HIGH - i;

		entries = nvgpu_runlist_append_prio(f, runlist, runlist_entry,
				entries_left, level);
		if (entries == RUNLIST_APPEND_FAILURE) {
			return RUNLIST_APPEND_FAILURE;
		}
		count += entries;
	}

	return count;
}

u32 nvgpu_runlist_construct_locked(struct fifo_gk20a *f,
				struct fifo_runlist_info_gk20a *runlist,
				u32 buf_id,
				u32 max_entries)
{
	u32 *runlist_entry_base = runlist->mem[buf_id].cpu_va;

	nvgpu_log_fn(f->g, " ");

	/*
	 * The entry pointer and capacity counter that live on the stack here
	 * keep track of the current position and the remaining space when tsg
	 * and channel entries are ultimately appended.
	 */
	if (f->g->runlist_interleave) {
		return nvgpu_runlist_append_low(f, runlist,
				&runlist_entry_base, &max_entries);
	} else {
		return nvgpu_runlist_append_flat(f, runlist,
				&runlist_entry_base, &max_entries);
	}
}

int gk20a_fifo_set_runlist_interleave(struct gk20a *g,
				u32 id,
				u32 runlist_id,
				u32 new_level)
{
	nvgpu_log_fn(g, " ");

	g->fifo.tsg[id].interleave_level = new_level;

	return 0;
}

int gk20a_fifo_tsg_set_timeslice(struct tsg_gk20a *tsg, u32 timeslice)
{
	struct gk20a *g = tsg->g;

	if (timeslice < g->min_timeslice_us ||
		timeslice > g->max_timeslice_us) {
		return -EINVAL;
	}

	gk20a_channel_get_timescale_from_timeslice(g, timeslice,
			&tsg->timeslice_timeout, &tsg->timeslice_scale);

	tsg->timeslice_us = timeslice;

	return g->ops.fifo.update_runlist(g, tsg->runlist_id, ~0, true, true);
}

void gk20a_fifo_runlist_hw_submit(struct gk20a *g, u32 runlist_id,
	u32 count, u32 buffer_index)
{
	struct fifo_runlist_info_gk20a *runlist = NULL;
	u64 runlist_iova;

	runlist = &g->fifo.runlist_info[runlist_id];
	runlist_iova = nvgpu_mem_get_addr(g, &runlist->mem[buffer_index]);

	nvgpu_spinlock_acquire(&g->fifo.runlist_submit_lock);

	if (count != 0U) {
		gk20a_writel(g, fifo_runlist_base_r(),
			fifo_runlist_base_ptr_f(u64_lo32(runlist_iova >> 12)) |
			nvgpu_aperture_mask(g, &runlist->mem[buffer_index],
				fifo_runlist_base_target_sys_mem_ncoh_f(),
				fifo_runlist_base_target_sys_mem_coh_f(),
				fifo_runlist_base_target_vid_mem_f()));
	}

	gk20a_writel(g, fifo_runlist_r(),
		fifo_runlist_engine_f(runlist_id) |
		fifo_eng_runlist_length_f(count));

	nvgpu_spinlock_release(&g->fifo.runlist_submit_lock);
}

int gk20a_fifo_update_runlist_locked(struct gk20a *g, u32 runlist_id,
					    u32 chid, bool add,
					    bool wait_for_finish)
{
	int ret = 0;
	struct fifo_gk20a *f = &g->fifo;
	struct fifo_runlist_info_gk20a *runlist = NULL;
	u64 runlist_iova;
	u32 new_buf;
	struct channel_gk20a *ch = NULL;
	struct tsg_gk20a *tsg = NULL;

	runlist = &f->runlist_info[runlist_id];

	/* valid channel, add/remove it from active list.
	   Otherwise, keep active list untouched for suspend/resume. */
	if (chid != FIFO_INVAL_CHANNEL_ID) {
		ch = &f->channel[chid];
		if (gk20a_is_channel_marked_as_tsg(ch)) {
			tsg = &f->tsg[ch->tsgid];
		}

		if (add) {
			if (test_and_set_bit(chid,
				runlist->active_channels)) {
				return 0;
			}
			if ((tsg != NULL) && (++tsg->num_active_channels != 0U)) {
				set_bit((int)f->channel[chid].tsgid,
					runlist->active_tsgs);
			}
		} else {
			if (!test_and_clear_bit(chid,
				runlist->active_channels)) {
				return 0;
			}
			if ((tsg != NULL) &&
			    (--tsg->num_active_channels == 0U)) {
				clear_bit((int)f->channel[chid].tsgid,
					runlist->active_tsgs);
			}
		}
	}

	/* There just 2 buffers */
	new_buf = runlist->cur_buffer == 0U ? 1U : 0U;

	runlist_iova = nvgpu_mem_get_addr(g, &runlist->mem[new_buf]);

	nvgpu_log_info(g, "runlist_id : %d, switch to new buffer 0x%16llx",
		runlist_id, (u64)runlist_iova);

	if (runlist_iova == 0ULL) {
		ret = -EINVAL;
		goto clean_up;
	}

	if (chid != FIFO_INVAL_CHANNEL_ID || /* add/remove a valid channel */
	    add /* resume to add all channels back */) {
		u32 num_entries;

		num_entries = nvgpu_runlist_construct_locked(f,
						runlist,
						new_buf,
						f->num_runlist_entries);
		if (num_entries == RUNLIST_APPEND_FAILURE) {
			ret = -E2BIG;
			goto clean_up;
		}
		runlist->count = num_entries;
		WARN_ON(runlist->count > f->num_runlist_entries);
	} else {
		/* suspend to remove all channels */
		runlist->count = 0;
	}

	g->ops.fifo.runlist_hw_submit(g, runlist_id, runlist->count, new_buf);

	if (wait_for_finish) {
		ret = g->ops.fifo.runlist_wait_pending(g, runlist_id);

		if (ret == -ETIMEDOUT) {
			nvgpu_err(g, "runlist %d update timeout", runlist_id);
			/* trigger runlist update timeout recovery */
			return ret;

		} else if (ret == -EINTR) {
			nvgpu_err(g, "runlist update interrupted");
		}
	}

	runlist->cur_buffer = new_buf;

clean_up:
	return ret;
}

int gk20a_fifo_update_runlist_ids(struct gk20a *g, u32 runlist_ids, u32 chid,
				bool add, bool wait_for_finish)
{
	int ret = -EINVAL;
	unsigned long runlist_id = 0;
	int errcode;
	unsigned long ulong_runlist_ids = (unsigned long)runlist_ids;

	if (g == NULL) {
		goto end;
	}

	ret = 0;
	for_each_set_bit(runlist_id, &ulong_runlist_ids, 32U) {
		/* Capture the last failure error code */
		errcode = g->ops.fifo.update_runlist(g, (u32)runlist_id, chid,
						add, wait_for_finish);
		if (errcode != 0) {
			nvgpu_err(g,
				"failed to update_runlist %lu %d",
				runlist_id, errcode);
			ret = errcode;
		}
	}
end:
	return ret;
}

/* trigger host preempt of GR pending load ctx if that ctx is not for ch */
static int __locked_fifo_reschedule_preempt_next(struct channel_gk20a *ch,
		bool wait_preempt)
{
	struct gk20a *g = ch->g;
	struct fifo_runlist_info_gk20a *runlist =
		&g->fifo.runlist_info[ch->runlist_id];
	int ret = 0;
	u32 gr_eng_id = 0;
	u32 engstat = 0, ctxstat = 0, fecsstat0 = 0, fecsstat1 = 0;
	u32 preempt_id;
	u32 preempt_type = 0;

	if (1U != gk20a_fifo_get_engine_ids(
		g, &gr_eng_id, 1, ENGINE_GR_GK20A)) {
		return ret;
	}
	if ((runlist->eng_bitmask & BIT32(gr_eng_id)) == 0U) {
		return ret;
	}

	if (wait_preempt && ((gk20a_readl(g, fifo_preempt_r()) &
				fifo_preempt_pending_true_f()) != 0U)) {
		return ret;
	}

	fecsstat0 = gk20a_readl(g, gr_fecs_ctxsw_mailbox_r(0));
	engstat = gk20a_readl(g, fifo_engine_status_r(gr_eng_id));
	ctxstat = fifo_engine_status_ctx_status_v(engstat);
	if (ctxstat == fifo_engine_status_ctx_status_ctxsw_switch_v()) {
		/* host switching to next context, preempt that if needed */
		preempt_id = fifo_engine_status_next_id_v(engstat);
		preempt_type = fifo_engine_status_next_id_type_v(engstat);
	} else {
		return ret;
	}
	if ((preempt_id == ch->tsgid) && (preempt_type != 0U)) {
		return ret;
	}
	fecsstat1 = gk20a_readl(g, gr_fecs_ctxsw_mailbox_r(0));
	if (fecsstat0 != FECS_MAILBOX_0_ACK_RESTORE ||
		fecsstat1 != FECS_MAILBOX_0_ACK_RESTORE) {
		/* preempt useless if FECS acked save and started restore */
		return ret;
	}

	gk20a_fifo_issue_preempt(g, preempt_id, preempt_type != 0U);
#ifdef TRACEPOINTS_ENABLED
	trace_gk20a_reschedule_preempt_next(ch->chid, fecsstat0, engstat,
		fecsstat1, gk20a_readl(g, gr_fecs_ctxsw_mailbox_r(0)),
		gk20a_readl(g, fifo_preempt_r()));
#endif
	if (wait_preempt) {
		g->ops.fifo.is_preempt_pending(g, preempt_id, preempt_type);
	}
#ifdef TRACEPOINTS_ENABLED
	trace_gk20a_reschedule_preempted_next(ch->chid);
#endif
	return ret;
}

int gk20a_fifo_reschedule_runlist(struct channel_gk20a *ch, bool preempt_next)
{
	return nvgpu_fifo_reschedule_runlist(ch, preempt_next, true);
}

/* trigger host to expire current timeslice and reschedule runlist from front */
int nvgpu_fifo_reschedule_runlist(struct channel_gk20a *ch, bool preempt_next,
		bool wait_preempt)
{
	struct gk20a *g = ch->g;
	struct fifo_runlist_info_gk20a *runlist;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret;
	int ret = 0;

	runlist = &g->fifo.runlist_info[ch->runlist_id];
	if (nvgpu_mutex_tryacquire(&runlist->runlist_lock) == 0) {
		return -EBUSY;
	}

	mutex_ret = nvgpu_pmu_mutex_acquire(
		&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	g->ops.fifo.runlist_hw_submit(
		g, ch->runlist_id, runlist->count, runlist->cur_buffer);

	if (preempt_next) {
		__locked_fifo_reschedule_preempt_next(ch, wait_preempt);
	}

	gk20a_fifo_runlist_wait_pending(g, ch->runlist_id);

	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(
			&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}
	nvgpu_mutex_release(&runlist->runlist_lock);

	return ret;
}

/* add/remove a channel from runlist
   special cases below: runlist->active_channels will NOT be changed.
   (chid == ~0 && !add) means remove all active channels from runlist.
   (chid == ~0 &&  add) means restore all active channels on runlist. */
int gk20a_fifo_update_runlist(struct gk20a *g, u32 runlist_id, u32 chid,
			      bool add, bool wait_for_finish)
{
	struct fifo_runlist_info_gk20a *runlist = NULL;
	struct fifo_gk20a *f = &g->fifo;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	int mutex_ret;
	int ret = 0;

	nvgpu_log_fn(g, " ");

	runlist = &f->runlist_info[runlist_id];

	nvgpu_mutex_acquire(&runlist->runlist_lock);

	mutex_ret = nvgpu_pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	ret = gk20a_fifo_update_runlist_locked(g, runlist_id, chid, add,
					       wait_for_finish);

	if (mutex_ret == 0) {
		nvgpu_pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);
	}

	nvgpu_mutex_release(&runlist->runlist_lock);

	if (ret == -ETIMEDOUT) {
		gk20a_fifo_runlist_reset_engines(g, runlist_id);
	}

	return ret;
}

int gk20a_fifo_suspend(struct gk20a *g)
{
	nvgpu_log_fn(g, " ");

	/* stop bar1 snooping */
	if (g->ops.mm.is_bar1_supported(g)) {
		gk20a_writel(g, fifo_bar1_base_r(),
			fifo_bar1_base_valid_false_f());
	}

	/* disable fifo intr */
	gk20a_writel(g, fifo_intr_en_0_r(), 0);
	gk20a_writel(g, fifo_intr_en_1_r(), 0);

	nvgpu_log_fn(g, "done");
	return 0;
}

bool gk20a_fifo_mmu_fault_pending(struct gk20a *g)
{
	if ((gk20a_readl(g, fifo_intr_0_r()) &
	     fifo_intr_0_mmu_fault_pending_f()) != 0U) {
		return true;
	} else {
		return false;
	}
}

bool gk20a_fifo_is_engine_busy(struct gk20a *g)
{
	u32 i, host_num_engines;

	host_num_engines = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_ENGINES);

	for (i = 0; i < host_num_engines; i++) {
		u32 status = gk20a_readl(g, fifo_engine_status_r(i));
		if (fifo_engine_status_engine_v(status) ==
			fifo_engine_status_engine_busy_v()) {
			return true;
		}
	}
	return false;
}

int gk20a_fifo_wait_engine_idle(struct gk20a *g)
{
	struct nvgpu_timeout timeout;
	u32 delay = GR_IDLE_CHECK_DEFAULT;
	int ret = -ETIMEDOUT;
	u32 i, host_num_engines;

	nvgpu_log_fn(g, " ");

	host_num_engines =
		 nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_ENGINES);

	nvgpu_timeout_init(g, &timeout, gk20a_get_gr_idle_timeout(g),
			   NVGPU_TIMER_CPU_TIMER);

	for (i = 0; i < host_num_engines; i++) {
		do {
			u32 status = gk20a_readl(g, fifo_engine_status_r(i));
			if (fifo_engine_status_engine_v(status) ==
				fifo_engine_status_engine_idle_v()) {
				ret = 0;
				break;
			}

			nvgpu_usleep_range(delay, delay * 2U);
			delay = min_t(u32,
					delay << 1, GR_IDLE_CHECK_MAX);
		} while (nvgpu_timeout_expired(&timeout) == 0);

		if (ret != 0) {
			nvgpu_log_info(g, "cannot idle engine %u", i);
			break;
		}
	}

	nvgpu_log_fn(g, "done");

	return ret;
}

u32 gk20a_fifo_get_pbdma_signature(struct gk20a *g)
{
	return pbdma_signature_hw_valid_f() | pbdma_signature_sw_zero_f();
}

static const char * const ccsr_chan_status_str[] = {
	"idle",
	"pending",
	"pending_ctx_reload",
	"pending_acquire",
	"pending_acq_ctx_reload",
	"on_pbdma",
	"on_pbdma_and_eng",
	"on_eng",
	"on_eng_pending_acquire",
	"on_eng_pending",
	"on_pbdma_ctx_reload",
	"on_pbdma_and_eng_ctx_reload",
	"on_eng_ctx_reload",
	"on_eng_pending_ctx_reload",
	"on_eng_pending_acq_ctx_reload",
};

static const char * const pbdma_chan_eng_ctx_status_str[] = {
	"invalid",
	"valid",
	"NA",
	"NA",
	"NA",
	"load",
	"save",
	"switch",
};

static const char * const not_found_str[] = {
	"NOT FOUND"
};

const char *gk20a_decode_ccsr_chan_status(u32 index)
{
	if (index >= ARRAY_SIZE(ccsr_chan_status_str)) {
		return not_found_str[0];
	} else {
		return ccsr_chan_status_str[index];
	}
}

const char *gk20a_decode_pbdma_chan_eng_ctx_status(u32 index)
{
	if (index >= ARRAY_SIZE(pbdma_chan_eng_ctx_status_str)) {
		return not_found_str[0];
	} else {
		return pbdma_chan_eng_ctx_status_str[index];
	}
}

bool gk20a_fifo_channel_status_is_next(struct gk20a *g, u32 chid)
{
	u32 channel = gk20a_readl(g, ccsr_channel_r(chid));

	return ccsr_channel_next_v(channel) == ccsr_channel_next_true_v();
}

bool gk20a_fifo_channel_status_is_ctx_reload(struct gk20a *g, u32 chid)
{
	u32 channel = gk20a_readl(g, ccsr_channel_r(chid));
	u32 status = ccsr_channel_status_v(channel);

	return (status == ccsr_channel_status_pending_ctx_reload_v() ||
		status == ccsr_channel_status_pending_acq_ctx_reload_v() ||
		status == ccsr_channel_status_on_pbdma_ctx_reload_v() ||
		status == ccsr_channel_status_on_pbdma_and_eng_ctx_reload_v() ||
		status == ccsr_channel_status_on_eng_ctx_reload_v() ||
		status == ccsr_channel_status_on_eng_pending_ctx_reload_v() ||
		status == ccsr_channel_status_on_eng_pending_acq_ctx_reload_v());
}

void gk20a_capture_channel_ram_dump(struct gk20a *g,
		struct channel_gk20a *ch,
		struct nvgpu_channel_dump_info *info)
{
	struct nvgpu_mem *mem = &ch->inst_block;

	info->channel_reg = gk20a_readl(g, ccsr_channel_r(ch->chid));

	info->inst.pb_top_level_get = nvgpu_mem_rd32_pair(g, mem,
			ram_fc_pb_top_level_get_w(),
			ram_fc_pb_top_level_get_hi_w());
	info->inst.pb_put = nvgpu_mem_rd32_pair(g, mem,
			ram_fc_pb_put_w(),
			ram_fc_pb_put_hi_w());
	info->inst.pb_get = nvgpu_mem_rd32_pair(g, mem,
			ram_fc_pb_get_w(),
			ram_fc_pb_get_hi_w());
	info->inst.pb_fetch = nvgpu_mem_rd32_pair(g, mem,
			ram_fc_pb_fetch_w(),
			ram_fc_pb_fetch_hi_w());
	info->inst.pb_header = nvgpu_mem_rd32(g, mem,
			ram_fc_pb_header_w());
	info->inst.pb_count = nvgpu_mem_rd32(g, mem,
			ram_fc_pb_count_w());
	info->inst.syncpointa = nvgpu_mem_rd32(g, mem,
			ram_fc_syncpointa_w());
	info->inst.syncpointb = nvgpu_mem_rd32(g, mem,
			ram_fc_syncpointb_w());
	info->inst.semaphorea = nvgpu_mem_rd32(g, mem,
			ram_fc_semaphorea_w());
	info->inst.semaphoreb = nvgpu_mem_rd32(g, mem,
			ram_fc_semaphoreb_w());
	info->inst.semaphorec = nvgpu_mem_rd32(g, mem,
			ram_fc_semaphorec_w());
	info->inst.semaphored = nvgpu_mem_rd32(g, mem,
			ram_fc_semaphored_w());
}

void gk20a_dump_channel_status_ramfc(struct gk20a *g,
				     struct gk20a_debug_output *o,
				     struct nvgpu_channel_dump_info *info)
{
	u32 status;
	u32 syncpointa, syncpointb;

	status = ccsr_channel_status_v(info->channel_reg);

	syncpointa = info->inst.syncpointa;
	syncpointb = info->inst.syncpointb;

	gk20a_debug_output(o, "Channel ID: %d, TSG ID: %u, pid %d, refs %d; deterministic = %s",
			   info->chid,
			   info->tsgid,
			   info->pid,
			   info->refs,
			   info->deterministic ? "yes" : "no");
	gk20a_debug_output(o, "  In use: %-3s  busy: %-3s  status: %s",
			   (ccsr_channel_enable_v(info->channel_reg) ==
			    ccsr_channel_enable_in_use_v()) ? "yes" : "no",
			   (ccsr_channel_busy_v(info->channel_reg) ==
			    ccsr_channel_busy_true_v()) ? "yes" : "no",
			   gk20a_decode_ccsr_chan_status(status));
	gk20a_debug_output(o,
			   "  TOP       %016llx"
			   "  PUT       %016llx  GET %016llx",
			   info->inst.pb_top_level_get,
			   info->inst.pb_put,
			   info->inst.pb_get);
	gk20a_debug_output(o,
			   "  FETCH     %016llx"
			   "  HEADER    %08x          COUNT %08x",
			   info->inst.pb_fetch,
			   info->inst.pb_header,
			   info->inst.pb_count);
	gk20a_debug_output(o,
			   "  SYNCPOINT %08x %08x "
			   "SEMAPHORE %08x %08x %08x %08x",
			   syncpointa,
			   syncpointb,
			   info->inst.semaphorea,
			   info->inst.semaphoreb,
			   info->inst.semaphorec,
			   info->inst.semaphored);

	if (info->sema.addr == 0ULL) {
		gk20a_debug_output(o,
			"  SEMA STATE: val: %u next_val: %u addr: 0x%010llx",
			info->sema.value,
			info->sema.next,
			info->sema.addr);
	}

#ifdef CONFIG_TEGRA_GK20A_NVHOST
	if ((pbdma_syncpointb_op_v(syncpointb) == pbdma_syncpointb_op_wait_v())
		&& (pbdma_syncpointb_wait_switch_v(syncpointb) ==
			pbdma_syncpointb_wait_switch_en_v()))
		gk20a_debug_output(o, "%s on syncpt %u (%s) val %u",
			(status == 3 || status == 8) ? "Waiting" : "Waited",
			pbdma_syncpointb_syncpt_index_v(syncpointb),
			nvgpu_nvhost_syncpt_get_name(g->nvhost_dev,
				pbdma_syncpointb_syncpt_index_v(syncpointb)),
			pbdma_syncpointa_payload_v(syncpointa));
#endif

	gk20a_debug_output(o, " ");
}

void gk20a_debug_dump_all_channel_status_ramfc(struct gk20a *g,
		 struct gk20a_debug_output *o)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 chid;
	struct nvgpu_channel_dump_info **infos;

	infos = nvgpu_kzalloc(g, sizeof(*infos) * f->num_channels);
	if (infos == NULL) {
		gk20a_debug_output(o, "cannot alloc memory for channels\n");
		return;
	}

	for (chid = 0; chid < f->num_channels; chid++) {
		struct channel_gk20a *ch = gk20a_channel_from_id(g, chid);

		if (ch != NULL) {
			struct nvgpu_channel_dump_info *info;

			info = nvgpu_kzalloc(g, sizeof(*info));

			/* ref taken stays to below loop with
			 * successful allocs */
			if (info == NULL) {
				gk20a_channel_put(ch);
			} else {
				infos[chid] = info;
			}
		}
	}

	for (chid = 0; chid < f->num_channels; chid++) {
		struct channel_gk20a *ch = &f->channel[chid];
		struct nvgpu_channel_dump_info *info = infos[chid];
		struct nvgpu_semaphore_int *hw_sema = ch->hw_sema;

		/* if this info exists, the above loop took a channel ref */
		if (info == NULL) {
			continue;
		}

		info->chid = ch->chid;
		info->tsgid = ch->tsgid;
		info->pid = ch->pid;
		info->refs = nvgpu_atomic_read(&ch->ref_count);
		info->deterministic = ch->deterministic;

		if (hw_sema != NULL) {
			info->sema.value = __nvgpu_semaphore_read(hw_sema);
			info->sema.next =
				(u32)nvgpu_atomic_read(&hw_sema->next_value);
			info->sema.addr = nvgpu_hw_sema_addr(hw_sema);
		}

		g->ops.fifo.capture_channel_ram_dump(g, ch, info);

		gk20a_channel_put(ch);
	}

	gk20a_debug_output(o, "Channel Status - chip %-5s", g->name);
	gk20a_debug_output(o, "---------------------------");
	for (chid = 0; chid < f->num_channels; chid++) {
		struct nvgpu_channel_dump_info *info = infos[chid];

		if (info != NULL) {
			g->ops.fifo.dump_channel_status_ramfc(g, o, info);
			nvgpu_kfree(g, info);
		}
	}
	gk20a_debug_output(o, " ");

	nvgpu_kfree(g, infos);
}

void gk20a_dump_pbdma_status(struct gk20a *g,
				 struct gk20a_debug_output *o)
{
	u32 i, host_num_pbdma;

	host_num_pbdma = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_PBDMA);

	gk20a_debug_output(o, "PBDMA Status - chip %-5s", g->name);
	gk20a_debug_output(o, "-------------------------");

	for (i = 0; i < host_num_pbdma; i++) {
		u32 status = gk20a_readl(g, fifo_pbdma_status_r(i));
		u32 chan_status = fifo_pbdma_status_chan_status_v(status);

		gk20a_debug_output(o, "pbdma %d:", i);
		gk20a_debug_output(o,
			"  id: %d - %-9s next_id: - %d %-9s | status: %s",
			fifo_pbdma_status_id_v(status),
			(fifo_pbdma_status_id_type_v(status) ==
			 fifo_pbdma_status_id_type_tsgid_v()) ?
				   "[tsg]" : "[channel]",
			fifo_pbdma_status_next_id_v(status),
			(fifo_pbdma_status_next_id_type_v(status) ==
			 fifo_pbdma_status_next_id_type_tsgid_v()) ?
				   "[tsg]" : "[channel]",
			gk20a_decode_pbdma_chan_eng_ctx_status(chan_status));
		gk20a_debug_output(o,
			"  PBDMA_PUT %016llx PBDMA_GET %016llx",
			(u64)gk20a_readl(g, pbdma_put_r(i)) +
			((u64)gk20a_readl(g, pbdma_put_hi_r(i)) << 32ULL),
			(u64)gk20a_readl(g, pbdma_get_r(i)) +
			((u64)gk20a_readl(g, pbdma_get_hi_r(i)) << 32ULL));
		gk20a_debug_output(o,
			"  GP_PUT    %08x  GP_GET  %08x  "
			"FETCH   %08x HEADER %08x",
			gk20a_readl(g, pbdma_gp_put_r(i)),
			gk20a_readl(g, pbdma_gp_get_r(i)),
			gk20a_readl(g, pbdma_gp_fetch_r(i)),
			gk20a_readl(g, pbdma_pb_header_r(i)));
		gk20a_debug_output(o,
			"  HDR       %08x  SHADOW0 %08x  SHADOW1 %08x",
			gk20a_readl(g, pbdma_hdr_shadow_r(i)),
			gk20a_readl(g, pbdma_gp_shadow_0_r(i)),
			gk20a_readl(g, pbdma_gp_shadow_1_r(i)));
	}

	gk20a_debug_output(o, " ");
}

void gk20a_dump_eng_status(struct gk20a *g,
				 struct gk20a_debug_output *o)
{
	u32 i, host_num_engines;

	host_num_engines = nvgpu_get_litter_value(g, GPU_LIT_HOST_NUM_ENGINES);

	gk20a_debug_output(o, "Engine status - chip %-5s", g->name);
	gk20a_debug_output(o, "--------------------------");

	for (i = 0; i < host_num_engines; i++) {
		u32 status = gk20a_readl(g, fifo_engine_status_r(i));
		u32 ctx_status = fifo_engine_status_ctx_status_v(status);

		gk20a_debug_output(o,
			"Engine %d | "
			"ID: %d - %-9s next_id: %d %-9s | status: %s",
			i,
			fifo_engine_status_id_v(status),
			(fifo_engine_status_id_type_v(status) ==
				fifo_engine_status_id_type_tsgid_v()) ?
				"[tsg]" : "[channel]",
			fifo_engine_status_next_id_v(status),
			(fifo_engine_status_next_id_type_v(status) ==
				fifo_engine_status_next_id_type_tsgid_v()) ?
				"[tsg]" : "[channel]",
			gk20a_decode_pbdma_chan_eng_ctx_status(ctx_status));

		if (fifo_engine_status_faulted_v(status) != 0U) {
			gk20a_debug_output(o, "  State: faulted");
		}
		if (fifo_engine_status_engine_v(status) != 0U) {
			gk20a_debug_output(o, "  State: busy");
		}
	}
	gk20a_debug_output(o, "\n");
}

void gk20a_fifo_enable_channel(struct channel_gk20a *ch)
{
	gk20a_writel(ch->g, ccsr_channel_r(ch->chid),
		gk20a_readl(ch->g, ccsr_channel_r(ch->chid)) |
		ccsr_channel_enable_set_true_f());
}

void gk20a_fifo_disable_channel(struct channel_gk20a *ch)
{
	gk20a_writel(ch->g, ccsr_channel_r(ch->chid),
		gk20a_readl(ch->g,
			ccsr_channel_r(ch->chid)) |
			ccsr_channel_enable_clr_true_f());
}

void gk20a_fifo_channel_unbind(struct channel_gk20a *ch_gk20a)
{
	struct gk20a *g = ch_gk20a->g;

	nvgpu_log_fn(g, " ");

	if (nvgpu_atomic_cmpxchg(&ch_gk20a->bound, (int)true, (int)false) != 0) {
		gk20a_writel(g, ccsr_channel_inst_r(ch_gk20a->chid),
			ccsr_channel_inst_ptr_f(0) |
			ccsr_channel_inst_bind_false_f());
	}
}

static int gk20a_fifo_commit_userd(struct channel_gk20a *c)
{
	u32 addr_lo;
	u32 addr_hi;
	struct gk20a *g = c->g;

	nvgpu_log_fn(g, " ");

	addr_lo = u64_lo32(c->userd_iova >> ram_userd_base_shift_v());
	addr_hi = u64_hi32(c->userd_iova);

	nvgpu_log_info(g, "channel %d : set ramfc userd 0x%16llx",
		c->chid, (u64)c->userd_iova);

	nvgpu_mem_wr32(g, &c->inst_block,
		       ram_in_ramfc_w() + ram_fc_userd_w(),
		       nvgpu_aperture_mask(g, c->userd_mem,
					   pbdma_userd_target_sys_mem_ncoh_f(),
					   pbdma_userd_target_sys_mem_coh_f(),
					   pbdma_userd_target_vid_mem_f()) |
		       pbdma_userd_addr_f(addr_lo));

	nvgpu_mem_wr32(g, &c->inst_block,
		       ram_in_ramfc_w() + ram_fc_userd_hi_w(),
		       pbdma_userd_hi_addr_f(addr_hi));

	return 0;
}

int gk20a_fifo_setup_ramfc(struct channel_gk20a *c,
			u64 gpfifo_base, u32 gpfifo_entries,
			unsigned long timeout,
			u32 flags)
{
	struct gk20a *g = c->g;
	struct nvgpu_mem *mem = &c->inst_block;
	unsigned long limit2_val;

	nvgpu_log_fn(g, " ");

	nvgpu_memset(g, mem, 0, 0, ram_fc_size_val_v());

	nvgpu_mem_wr32(g, mem, ram_fc_gp_base_w(),
		pbdma_gp_base_offset_f(
		u64_lo32(gpfifo_base >> pbdma_gp_base_rsvd_s())));

	limit2_val = ilog2(gpfifo_entries);
	if (u64_hi32(limit2_val) != 0U) {
		nvgpu_err(g,  "Unable to cast pbdma limit2 value");
		return -EOVERFLOW;
	}
	nvgpu_mem_wr32(g, mem, ram_fc_gp_base_hi_w(),
		pbdma_gp_base_hi_offset_f(u64_hi32(gpfifo_base)) |
		pbdma_gp_base_hi_limit2_f((u32)limit2_val));

	nvgpu_mem_wr32(g, mem, ram_fc_signature_w(),
		 c->g->ops.fifo.get_pbdma_signature(c->g));

	nvgpu_mem_wr32(g, mem, ram_fc_formats_w(),
		pbdma_formats_gp_fermi0_f() |
		pbdma_formats_pb_fermi1_f() |
		pbdma_formats_mp_fermi0_f());

	nvgpu_mem_wr32(g, mem, ram_fc_pb_header_w(),
		pbdma_pb_header_priv_user_f() |
		pbdma_pb_header_method_zero_f() |
		pbdma_pb_header_subchannel_zero_f() |
		pbdma_pb_header_level_main_f() |
		pbdma_pb_header_first_true_f() |
		pbdma_pb_header_type_inc_f());

	nvgpu_mem_wr32(g, mem, ram_fc_subdevice_w(),
		pbdma_subdevice_id_f(1) |
		pbdma_subdevice_status_active_f() |
		pbdma_subdevice_channel_dma_enable_f());

	nvgpu_mem_wr32(g, mem, ram_fc_target_w(), pbdma_target_engine_sw_f());

	nvgpu_mem_wr32(g, mem, ram_fc_acquire_w(),
		g->ops.fifo.pbdma_acquire_val(timeout));

	nvgpu_mem_wr32(g, mem, ram_fc_runlist_timeslice_w(),
		fifo_runlist_timeslice_timeout_128_f() |
		fifo_runlist_timeslice_timescale_3_f() |
		fifo_runlist_timeslice_enable_true_f());

	nvgpu_mem_wr32(g, mem, ram_fc_pb_timeslice_w(),
		fifo_pb_timeslice_timeout_16_f() |
		fifo_pb_timeslice_timescale_0_f() |
		fifo_pb_timeslice_enable_true_f());

	nvgpu_mem_wr32(g, mem, ram_fc_chid_w(), ram_fc_chid_id_f(c->chid));

	if (c->is_privileged_channel) {
		gk20a_fifo_setup_ramfc_for_privileged_channel(c);
	}

	return gk20a_fifo_commit_userd(c);
}

void gk20a_fifo_setup_ramfc_for_privileged_channel(struct channel_gk20a *c)
{
	struct gk20a *g = c->g;
	struct nvgpu_mem *mem = &c->inst_block;

	nvgpu_log_info(g, "channel %d : set ramfc privileged_channel", c->chid);

	/* Enable HCE priv mode for phys mode transfer */
	nvgpu_mem_wr32(g, mem, ram_fc_hce_ctrl_w(),
		pbdma_hce_ctrl_hce_priv_mode_yes_f());
}

int gk20a_fifo_setup_userd(struct channel_gk20a *c)
{
	struct gk20a *g = c->g;
	struct nvgpu_mem *mem = c->userd_mem;
	u32 offset = c->userd_offset / U32(sizeof(u32));

	nvgpu_log_fn(g, " ");

	nvgpu_mem_wr32(g, mem, offset + ram_userd_put_w(), 0);
	nvgpu_mem_wr32(g, mem, offset + ram_userd_get_w(), 0);
	nvgpu_mem_wr32(g, mem, offset + ram_userd_ref_w(), 0);
	nvgpu_mem_wr32(g, mem, offset + ram_userd_put_hi_w(), 0);
	nvgpu_mem_wr32(g, mem, offset + ram_userd_gp_top_level_get_w(), 0);
	nvgpu_mem_wr32(g, mem, offset + ram_userd_gp_top_level_get_hi_w(), 0);
	nvgpu_mem_wr32(g, mem, offset + ram_userd_get_hi_w(), 0);
	nvgpu_mem_wr32(g, mem, offset + ram_userd_gp_get_w(), 0);
	nvgpu_mem_wr32(g, mem, offset + ram_userd_gp_put_w(), 0);

	return 0;
}

int gk20a_fifo_alloc_inst(struct gk20a *g, struct channel_gk20a *ch)
{
	int err;

	nvgpu_log_fn(g, " ");

	err = g->ops.mm.alloc_inst_block(g, &ch->inst_block);
	if (err != 0) {
		return err;
	}

	nvgpu_log_info(g, "channel %d inst block physical addr: 0x%16llx",
		ch->chid, nvgpu_inst_block_addr(g, &ch->inst_block));

	nvgpu_log_fn(g, "done");
	return 0;
}

void gk20a_fifo_free_inst(struct gk20a *g, struct channel_gk20a *ch)
{
	nvgpu_free_inst_block(g, &ch->inst_block);
}

u32 gk20a_fifo_userd_gp_get(struct gk20a *g, struct channel_gk20a *c)
{
	u64 userd_gpu_va = gk20a_channel_userd_gpu_va(c);
	u64 addr = userd_gpu_va + sizeof(u32) * ram_userd_gp_get_w();

	BUG_ON(u64_hi32(addr) != 0U);

	return gk20a_bar1_readl(g, (u32)addr);
}

u64 gk20a_fifo_userd_pb_get(struct gk20a *g, struct channel_gk20a *c)
{
	u64 userd_gpu_va = gk20a_channel_userd_gpu_va(c);
	u64 lo_addr = userd_gpu_va + sizeof(u32) * ram_userd_get_w();
	u64 hi_addr = userd_gpu_va + sizeof(u32) * ram_userd_get_hi_w();
	u32 lo, hi;

	BUG_ON((u64_hi32(lo_addr) != 0U) || (u64_hi32(hi_addr) != 0U));
	lo = gk20a_bar1_readl(g, (u32)lo_addr);
	hi = gk20a_bar1_readl(g, (u32)hi_addr);

	return ((u64)hi << 32) | lo;
}

void gk20a_fifo_userd_gp_put(struct gk20a *g, struct channel_gk20a *c)
{
	u64 userd_gpu_va = gk20a_channel_userd_gpu_va(c);
	u64 addr = userd_gpu_va + sizeof(u32) * ram_userd_gp_put_w();

	BUG_ON(u64_hi32(addr) != 0U);
	gk20a_bar1_writel(g, (u32)addr, c->gpfifo.put);
}

u32 gk20a_fifo_pbdma_acquire_val(u64 timeout)
{
	u32 val, exponent, mantissa;
	unsigned int val_len;
	u64 tmp;

	val = pbdma_acquire_retry_man_2_f() |
		pbdma_acquire_retry_exp_2_f();

	if (timeout == 0ULL) {
		return val;
	}

	timeout *= 80UL;
	do_div(timeout, 100U); /* set acquire timeout to 80% of channel wdt */
	timeout *= 1000000UL; /* ms -> ns */
	do_div(timeout, 1024U); /* in unit of 1024ns */
	tmp = fls(timeout >> 32U);
	BUG_ON(tmp > U64(U32_MAX));
	val_len = (u32)tmp + 32U;
	if (val_len == 32U) {
		val_len = (u32)fls(timeout);
	}
	if (val_len > 16U + pbdma_acquire_timeout_exp_max_v()) { /* man: 16bits */
		exponent = pbdma_acquire_timeout_exp_max_v();
		mantissa = pbdma_acquire_timeout_man_max_v();
	} else if (val_len > 16U) {
		exponent = val_len - 16U;
		BUG_ON((timeout >> exponent) > U64(U32_MAX));
		mantissa = (u32)(timeout >> exponent);
	} else {
		exponent = 0;
		BUG_ON(timeout > U64(U32_MAX));
		mantissa = (u32)timeout;
	}

	val |= pbdma_acquire_timeout_exp_f(exponent) |
		pbdma_acquire_timeout_man_f(mantissa) |
		pbdma_acquire_timeout_en_enable_f();

	return val;
}

const char *gk20a_fifo_interleave_level_name(u32 interleave_level)
{
	const char *ret_string = NULL;

	switch (interleave_level) {
	case NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_LOW:
		ret_string = "LOW";
		break;

	case NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_MEDIUM:
		ret_string = "MEDIUM";
		break;

	case NVGPU_FIFO_RUNLIST_INTERLEAVE_LEVEL_HIGH:
		ret_string = "HIGH";
		break;

	default:
		ret_string = "?";
		break;
	}

	return ret_string;
}

u32 gk20a_fifo_get_sema_wait_cmd_size(void)
{
	return 8;
}

u32 gk20a_fifo_get_sema_incr_cmd_size(void)
{
	return 10;
}

void gk20a_fifo_add_sema_cmd(struct gk20a *g,
	struct nvgpu_semaphore *s, u64 sema_va,
	struct priv_cmd_entry *cmd,
	u32 off, bool acquire, bool wfi)
{
	nvgpu_log_fn(g, " ");

	/* semaphore_a */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010004U);
	/* offset_upper */
	nvgpu_mem_wr32(g, cmd->mem, off++, (u32)(sema_va >> 32) & 0xffU);
	/* semaphore_b */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010005U);
	/* offset */
	nvgpu_mem_wr32(g, cmd->mem, off++, (u32)sema_va & 0xffffffff);

	if (acquire) {
		/* semaphore_c */
		nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010006U);
		/* payload */
		nvgpu_mem_wr32(g, cmd->mem, off++,
			       nvgpu_semaphore_get_value(s));
		/* semaphore_d */
		nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010007U);
		/* operation: acq_geq, switch_en */
		nvgpu_mem_wr32(g, cmd->mem, off++, 0x4U | BIT32(12));
	} else {
		/* semaphore_c */
		nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010006U);
		/* payload */
		nvgpu_mem_wr32(g, cmd->mem, off++,
			       nvgpu_semaphore_get_value(s));
		/* semaphore_d */
		nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010007U);
		/* operation: release, wfi */
		nvgpu_mem_wr32(g, cmd->mem, off++,
				0x2UL | ((wfi ? 0x0UL : 0x1UL) << 20));
		/* non_stall_int */
		nvgpu_mem_wr32(g, cmd->mem, off++, 0x20010008U);
		/* ignored */
		nvgpu_mem_wr32(g, cmd->mem, off++, 0U);
	}
}

#ifdef CONFIG_TEGRA_GK20A_NVHOST
void gk20a_fifo_add_syncpt_wait_cmd(struct gk20a *g,
		struct priv_cmd_entry *cmd, u32 off,
		u32 id, u32 thresh, u64 gpu_va)
{
	nvgpu_log_fn(g, " ");

	off = cmd->off + off;
	/* syncpoint_a */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001CU);
	/* payload */
	nvgpu_mem_wr32(g, cmd->mem, off++, thresh);
	/* syncpoint_b */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001DU);
	/* syncpt_id, switch_en, wait */
	nvgpu_mem_wr32(g, cmd->mem, off++, (id << 8U) | 0x10U);
}

u32 gk20a_fifo_get_syncpt_wait_cmd_size(void)
{
	return 4;
}

u32 gk20a_fifo_get_syncpt_incr_per_release(void)
{
	return 2;
}

void gk20a_fifo_add_syncpt_incr_cmd(struct gk20a *g,
		bool wfi_cmd, struct priv_cmd_entry *cmd,
		u32 id, u64 gpu_va)
{
	u32 off = cmd->off;

	nvgpu_log_fn(g, " ");
	if (wfi_cmd) {
		/* wfi */
		nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001EU);
		/* handle, ignored */
		nvgpu_mem_wr32(g, cmd->mem, off++, 0x00000000U);
	}
	/* syncpoint_a */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001CU);
	/* payload, ignored */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0U);
	/* syncpoint_b */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001DU);
	/* syncpt_id, incr */
	nvgpu_mem_wr32(g, cmd->mem, off++, (id << 8U) | 0x1U);
	/* syncpoint_b */
	nvgpu_mem_wr32(g, cmd->mem, off++, 0x2001001DU);
	/* syncpt_id, incr */
	nvgpu_mem_wr32(g, cmd->mem, off++, (id << 8U) | 0x1U);

}

u32 gk20a_fifo_get_syncpt_incr_cmd_size(bool wfi_cmd)
{
	if (wfi_cmd)
		return 8;
	else
		return 6;
}

void gk20a_fifo_free_syncpt_buf(struct channel_gk20a *c,
				struct nvgpu_mem *syncpt_buf)
{

}

int gk20a_fifo_alloc_syncpt_buf(struct channel_gk20a *c,
			u32 syncpt_id, struct nvgpu_mem *syncpt_buf)
{
	return 0;
}
#endif
