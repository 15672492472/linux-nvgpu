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

#include <nvgpu/bios.h>
#include <nvgpu/kmem.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/boardobjgrp.h>
#include <nvgpu/boardobjgrp_e32.h>
#include <nvgpu/boardobjgrp_e255.h>
#include <nvgpu/string.h>
#include <nvgpu/pmu/pmuif/ctrlclk.h>
#include <nvgpu/pmu/pmuif/ctrlvolt.h>
#include <nvgpu/pmu/clk/clk.h>
#include <nvgpu/pmu/clk/clk_vf_point.h>

#include "clk_prog.h"

static struct clk_prog *construct_clk_prog(struct gk20a *g, void *pargs);
static int devinit_get_clk_prog_table(struct gk20a *g,
	struct nvgpu_clk_progs *pprogobjs);
static int vfflatten_prog_1x_master(struct gk20a *g,
		struct nvgpu_clk_pmupstate *pclk,
		struct clk_prog_1x_master *p1xmaster,
		u8 clk_domain_idx, u16 *pfreqmaxlastmhz);
static int vflookup_prog_1x_master(struct gk20a *g,
		struct nvgpu_clk_pmupstate *pclk,
		struct clk_prog_1x_master *p1xmaster,
		u8 *slave_clk_domain,
		u16 *pclkmhz,
		u32 *pvoltuv,
		u8 rail);
static int getfpoints_prog_1x_master(struct gk20a *g,
		struct nvgpu_clk_pmupstate *pclk,
		struct clk_prog_1x_master *p1xmaster,
		u32 *pfpointscount,
		u16 **ppfreqpointsinmhz,
		u8 rail);
static int getslaveclk_prog_1x_master(struct gk20a *g,
		struct nvgpu_clk_pmupstate *pclk,
		struct clk_prog_1x_master *p1xmaster,
		u8 slave_clk_domain,
		u16 *pclkmhz,
		u16 masterclkmhz);

static int _clk_progs_pmudatainit(struct gk20a *g,
		struct boardobjgrp *pboardobjgrp,
		struct nv_pmu_boardobjgrp_super *pboardobjgrppmu)
{
	struct nv_pmu_clk_clk_prog_boardobjgrp_set_header *pset =
		(struct nv_pmu_clk_clk_prog_boardobjgrp_set_header *)
		pboardobjgrppmu;
	struct nvgpu_clk_progs *pprogs = (struct nvgpu_clk_progs *)pboardobjgrp;
	int status = 0;

	status = boardobjgrp_pmudatainit_e32(g, pboardobjgrp, pboardobjgrppmu);
	if (status != 0) {
		nvgpu_err(g, "error updating pmu boardobjgrp for clk prog 0x%x",
			  status);
		goto done;
	}
	pset->slave_entry_count = pprogs->slave_entry_count;
	pset->vf_entry_count = pprogs->vf_entry_count;
	pset->vf_sec_entry_count = pprogs->vf_sec_entry_count;

done:
	return status;
}

static int _clk_progs_pmudata_instget(struct gk20a *g,
		struct nv_pmu_boardobjgrp *pmuboardobjgrp,
		struct nv_pmu_boardobj **ppboardobjpmudata,
		u8 idx)
{
	struct nv_pmu_clk_clk_prog_boardobj_grp_set  *pgrp_set =
		(struct nv_pmu_clk_clk_prog_boardobj_grp_set *)pmuboardobjgrp;

	nvgpu_log_info(g, " ");

	/*check whether pmuboardobjgrp has a valid boardobj in index*/
	if (((u32)BIT(idx) &
		pgrp_set->hdr.data.super.obj_mask.super.data[0]) == 0U) {
		return -EINVAL;
	}

	*ppboardobjpmudata = (struct nv_pmu_boardobj *)
		&pgrp_set->objects[idx].data.board_obj;
	nvgpu_log_info(g, " Done");
	return 0;
}

int nvgpu_clk_prog_sw_setup(struct gk20a *g)
{
	int status;
	struct boardobjgrp *pboardobjgrp = NULL;
	struct nvgpu_clk_progs *pclkprogobjs;

	nvgpu_log_info(g, " ");

	status = nvgpu_boardobjgrp_construct_e255(g,
			&g->pmu.clk_pmu->clk_progobjs->super);
	if (status != 0) {
		nvgpu_err(g,
			"error creating boardobjgrp for clk prog, status- 0x%x",
			status);
		goto done;
	}

	pboardobjgrp = &g->pmu.clk_pmu->clk_progobjs->super.super;
	pclkprogobjs = g->pmu.clk_pmu->clk_progobjs;

	BOARDOBJGRP_PMU_CONSTRUCT(pboardobjgrp, CLK, CLK_PROG);

	status = BOARDOBJGRP_PMU_CMD_GRP_SET_CONSTRUCT(g, pboardobjgrp,
			clk, CLK, clk_prog, CLK_PROG);
	if (status != 0) {
		nvgpu_err(g,
			"error constructing PMU_BOARDOBJ_CMD_GRP_SET interface - 0x%x",
			status);
		goto done;
	}

	pboardobjgrp->pmudatainit = _clk_progs_pmudatainit;
	pboardobjgrp->pmudatainstget  = _clk_progs_pmudata_instget;

	status = devinit_get_clk_prog_table(g, pclkprogobjs);
	if (status != 0) {
		nvgpu_err(g, "Error parsing the clk prog Vbios tables");
		goto done;
	}

	status = g->pmu.clk_pmu->clk_domain_clk_prog_link(g, g->pmu.clk_pmu);
	if (status != 0) {
		nvgpu_err(g, "error constructing VF point board objects");
		goto done;
	}

done:
	nvgpu_log_info(g, " done status %x", status);
	return status;
}

int nvgpu_clk_prog_pmu_setup(struct gk20a *g)
{
	int status;
	struct boardobjgrp *pboardobjgrp = NULL;

	nvgpu_log_info(g, " ");

	pboardobjgrp = &g->pmu.clk_pmu->clk_progobjs->super.super;

	if (!pboardobjgrp->bconstructed) {
		return -EINVAL;
	}

	status = pboardobjgrp->pmuinithandle(g, pboardobjgrp);

	nvgpu_log_info(g, "Done");
	return status;
}

static int devinit_get_clk_prog_table_35(struct gk20a *g,
		struct nvgpu_clk_progs *pclkprogobjs,
		u8 *clkprogs_tbl_ptr)
{
	int status = 0;
	struct vbios_clock_programming_table_35_header header = { 0 };
	struct vbios_clock_programming_table_1x_entry prog = { 0 };
	struct vbios_clock_programming_table_1x_slave_entry slaveprog = { 0 };
	struct vbios_clock_programming_table_35_vf_entry vfprog = { 0 };
	struct vbios_clock_programming_table_35_vf_sec_entry vfsecprog = { 0 };
	u8 *entry = NULL;
	u8 *slaveentry = NULL;
	u8 *vfentry = NULL;
	u8 *vfsecentry = NULL;
	u32 i, j, k = 0;
	struct clk_prog *pprog;
	u8 prog_type;
	u8 src_type;
	u32 szfmt = VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_SIZE_0D;
	u32 hszfmt = VBIOS_CLOCK_PROGRAMMING_TABLE_35_HEADER_SIZE_0A;
	u32 slaveszfmt = VBIOS_CLOCK_PROGRAMMING_TABLE_1X_SLAVE_ENTRY_SIZE_03;
	u32 vfszfmt = VBIOS_CLOCK_PROGRAMMING_TABLE_35_VF_ENTRY_SIZE_01;
	u32 vfsecszfmt = VBIOS_CLOCK_PROGRAMMING_TABLE_35_VF_SEC_ENTRY_SIZE_02;
	struct ctrl_clk_clk_prog_1x_master_vf_entry
		vfentries[CTRL_CLK_CLK_PROG_1X_MASTER_VF_ENTRY_MAX_ENTRIES];
	struct ctrl_clk_clk_prog_35_master_sec_vf_entry_voltrail
		voltrailsecvfentries[
			CTRL_CLK_CLK_PROG_1X_MASTER_VF_ENTRY_MAX_ENTRIES];
	struct ctrl_clk_clk_prog_1x_master_ratio_slave_entry
		ratioslaveentries[CTRL_CLK_PROG_1X_MASTER_MAX_SLAVE_ENTRIES];
	struct ctrl_clk_clk_prog_1x_master_table_slave_entry
		tableslaveentries[CTRL_CLK_PROG_1X_MASTER_MAX_SLAVE_ENTRIES];
	union {
		struct boardobj board_obj;
		struct clk_prog clkprog;
		struct clk_prog_1x v1x;
		struct clk_prog_35_master v35_master;
		struct clk_prog_35_master_ratio v35_master_ratio;
		struct clk_prog_35_master_table v35_master_table;
	} prog_data;

	nvgpu_log_info(g, " ");

	if (clkprogs_tbl_ptr == NULL) {
		status = -EINVAL;
		goto done;
	}

	nvgpu_memcpy((u8 *)&header, clkprogs_tbl_ptr, hszfmt);
	if (header.header_size < hszfmt) {
		status = -EINVAL;
		goto done;
	}
	hszfmt = header.header_size;

	if (header.entry_size < szfmt) {
		status = -EINVAL;
		goto done;
	}
	szfmt = header.entry_size;

	if (header.vf_entry_size < vfszfmt) {
		status = -EINVAL;
		goto done;
	}
	vfszfmt = header.vf_entry_size;

	if (header.slave_entry_size < slaveszfmt) {
		status = -EINVAL;
		goto done;
	}
	slaveszfmt = header.slave_entry_size;

	if (header.vf_entry_count > CTRL_CLK_CLK_DELTA_MAX_VOLT_RAILS) {
		status = -EINVAL;
		goto done;
	}

	if (header.vf_sec_entry_size < vfsecszfmt) {
		status = -EINVAL;
		goto done;
	}
	vfsecszfmt = header.vf_sec_entry_size;

	pclkprogobjs->slave_entry_count = header.slave_entry_count;
	pclkprogobjs->vf_entry_count = header.vf_entry_count;
	pclkprogobjs->vf_sec_entry_count = header.vf_sec_entry_count;

	for (i = 0; i < header.entry_count; i++) {
		memset(&prog_data, 0x0, (u32)sizeof(prog_data));

		/* Read table entries*/
		entry = clkprogs_tbl_ptr + hszfmt +
			(i * (szfmt + (header.slave_entry_count * slaveszfmt) +
			(header.vf_entry_count * vfszfmt) +
			(header.vf_sec_entry_count * vfsecszfmt)));

		nvgpu_memcpy((u8 *)&prog, entry, szfmt);
		memset(vfentries, 0xFF,
			sizeof(struct ctrl_clk_clk_prog_1x_master_vf_entry) *
			CTRL_CLK_CLK_PROG_1X_MASTER_VF_ENTRY_MAX_ENTRIES);
		memset(voltrailsecvfentries, 0xFF,
			sizeof(struct ctrl_clk_clk_prog_35_master_sec_vf_entry_voltrail) *
			CTRL_CLK_CLK_PROG_1X_MASTER_VF_ENTRY_MAX_ENTRIES);
		memset(ratioslaveentries, 0xFF,
			sizeof(struct ctrl_clk_clk_prog_1x_master_ratio_slave_entry) *
			CTRL_CLK_PROG_1X_MASTER_MAX_SLAVE_ENTRIES);
		memset(tableslaveentries, 0xFF,
			sizeof(struct ctrl_clk_clk_prog_1x_master_table_slave_entry) *
			CTRL_CLK_PROG_1X_MASTER_MAX_SLAVE_ENTRIES);

		prog_type = BIOS_GET_FIELD(u8, prog.flags0,
			NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_TYPE);
		nvgpu_log_info(g, "Prog_type (master, slave type): 0x%x",
								prog_type);
		if (prog_type == NV_VBIOS_CLOCK_PROGRAMMING_TABLE_35_ENTRY_FLAGS0_TYPE_DISABLED) {
			nvgpu_log_info(g, "Skipped Entry");
			continue;
		}

		src_type = BIOS_GET_FIELD(u8, prog.flags0,
			NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_SOURCE);
		nvgpu_log_info(g, "source type: 0x%x", src_type);
		switch (src_type) {
		case NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_SOURCE_PLL:
			nvgpu_log_info(g, "Source type is PLL");
			prog_data.v1x.source = CTRL_CLK_PROG_1X_SOURCE_PLL;
			prog_data.v1x.source_data.pll.pll_idx =
				BIOS_GET_FIELD(u8, prog.param0,
					NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_PARAM0_PLL_PLL_INDEX);
			prog_data.v1x.source_data.pll.freq_step_size_mhz =
				BIOS_GET_FIELD(u8, prog.param1,
					NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_PARAM1_PLL_FREQ_STEP_SIZE);
			nvgpu_log_info(g, "pll_index: 0x%x freq_step_size: %d",
				prog_data.v1x.source_data.pll.pll_idx,
				prog_data.v1x.source_data.pll.freq_step_size_mhz);
			break;

		case NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_SOURCE_ONE_SOURCE:
			nvgpu_log_info(g, "Source type is ONE_SOURCE");
			prog_data.v1x.source = CTRL_CLK_PROG_1X_SOURCE_ONE_SOURCE;
			break;

		case NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_SOURCE_FLL:
			nvgpu_log_info(g, "Source type is FLL");
			prog_data.v1x.source = CTRL_CLK_PROG_1X_SOURCE_FLL;
			break;

		default:
			nvgpu_err(g, "invalid source %d", prog_type);
			status = -EINVAL;
			break;
		}

		if (status != 0) {
			goto done;
		}
		prog_data.v1x.freq_max_mhz = (u16)prog.freq_max_mhz;
		nvgpu_log_info(g, "Max freq: %d", prog_data.v1x.freq_max_mhz);

		slaveentry = entry + szfmt;
		vfentry = entry + szfmt + header.slave_entry_count * slaveszfmt;
		vfsecentry = entry + szfmt +
					header.slave_entry_count * slaveszfmt +
					header.vf_entry_count * vfszfmt;

		switch (prog_type) {
		case NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_TYPE_MASTER_RATIO:
		case NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_TYPE_MASTER_TABLE:
			prog_data.v35_master.master.b_o_c_o_v_enabled = false;
			for (j = 0; j < header.vf_entry_count; j++) {
				nvgpu_memcpy((u8 *)&vfprog, vfentry, vfszfmt);

				vfentries[j].vfe_idx = (u8)vfprog.vfe_idx;
				vfentries[j].gain_vfe_idx = CTRL_BOARDOBJ_IDX_INVALID;
				vfentry += vfszfmt;

				for (k = 0; k < header.vf_sec_entry_count; k++) {
					nvgpu_memcpy((u8 *)&vfsecprog,
						vfsecentry, vfsecszfmt);

					voltrailsecvfentries[j].sec_vf_entries[k].vfe_idx = (u8)vfsecprog.sec_vfe_idx;
					if (prog_data.v1x.source == CTRL_CLK_PROG_1X_SOURCE_FLL) {
						voltrailsecvfentries[j].sec_vf_entries[k].dvco_offset_vfe_idx =
							BIOS_GET_FIELD(u8,
							vfsecprog.param0,
							NV_VBIOS_CLOCK_PROGRAMMING_TABLE_35_SEC_VF_ENTRY_PARAM0_FLL_DVCO_OFFSET_VFE_IDX);
					} else {
						voltrailsecvfentries[j].sec_vf_entries[k].dvco_offset_vfe_idx = CTRL_BOARDOBJ_IDX_INVALID;
					}
					vfsecentry += vfsecszfmt;
					nvgpu_log_info(g, "Sec_VF_entry %d: vfe_idx: 0x%x "
						"dcvo_offset_vfe_idx: 0x%x", j,
							voltrailsecvfentries[j].sec_vf_entries[k].vfe_idx,
							voltrailsecvfentries[j].sec_vf_entries[k].dvco_offset_vfe_idx);
				}
			}
			prog_data.v35_master.master.p_vf_entries = vfentries;
			prog_data.v35_master.p_voltrail_sec_vf_entries = voltrailsecvfentries;

			for (j = 0; j < header.slave_entry_count; j++) {
				nvgpu_memcpy((u8 *)&slaveprog, slaveentry,
					slaveszfmt);
				if (prog_type == NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_TYPE_MASTER_RATIO) {
					ratioslaveentries[j].clk_dom_idx =
						(u8)slaveprog.clk_dom_idx;
					ratioslaveentries[j].ratio =
						BIOS_GET_FIELD(u8,
						slaveprog.param0,
						NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_SLAVE_ENTRY_PARAM0_MASTER_RATIO_RATIO);
				} else {
					tableslaveentries[j].clk_dom_idx =
						(u8)slaveprog.clk_dom_idx;
					tableslaveentries[j].freq_mhz =
						BIOS_GET_FIELD(u16,
						slaveprog.param0,
						NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_SLAVE_ENTRY_PARAM0_MASTER_TABLE_FREQ);
				}
				slaveentry += slaveszfmt;
			}

			if (prog_type == NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_TYPE_MASTER_RATIO) {
				prog_data.board_obj.type = CTRL_CLK_CLK_PROG_TYPE_35_MASTER_RATIO;
				prog_data.v35_master_ratio.ratio.p_slave_entries =
					ratioslaveentries;
			} else {
				prog_data.board_obj.type = CTRL_CLK_CLK_PROG_TYPE_35_MASTER_TABLE;

				prog_data.v35_master_table.table.p_slave_entries =
					tableslaveentries;
			}
			break;

		case NV_VBIOS_CLOCK_PROGRAMMING_TABLE_1X_ENTRY_FLAGS0_TYPE_SLAVE:
			prog_data.board_obj.type = CTRL_CLK_CLK_PROG_TYPE_35;
			break;

		default:
			nvgpu_err(g, "Wrong Prog entry type %d", prog_type);
			status = -EINVAL;
			break;
		}

		if (status != 0) {
			goto done;
		}
		pprog = construct_clk_prog(g, (void *)&prog_data);
		if (pprog == NULL) {
			nvgpu_err(g,
				  "error constructing clk_prog boardobj %d", i);
			status = -EINVAL;
			goto done;
		}

		status = boardobjgrp_objinsert(&pclkprogobjs->super.super,
			(struct boardobj *)(void *)pprog, i);
		if (status != 0) {
			nvgpu_err(g, "error adding clk_prog boardobj %d", i);
			status = -EINVAL;
			goto done;
		}
	}
done:
	nvgpu_log_info(g, " done status %x", status);
	return status;
}

static int devinit_get_clk_prog_table(struct gk20a *g,
		struct nvgpu_clk_progs *pprogobjs)
{
	int status = 0;
	u8 *clkprogs_tbl_ptr = NULL;
	struct vbios_clock_programming_table_1x_header header = { 0 };
	nvgpu_log_info(g, " ");

	clkprogs_tbl_ptr = (u8 *)nvgpu_bios_get_perf_table_ptrs(g,
			g->bios.clock_token, CLOCK_PROGRAMMING_TABLE);
	if (clkprogs_tbl_ptr == NULL) {
		return -EINVAL;
	}
	nvgpu_memcpy((u8 *)&header, clkprogs_tbl_ptr,
			VBIOS_CLOCK_PROGRAMMING_TABLE_1X_HEADER_SIZE_08);

	if (header.version ==
			VBIOS_CLOCK_PROGRAMMING_TABLE_35_HEADER_VERSION) {
		status = devinit_get_clk_prog_table_35(g, pprogobjs,
							clkprogs_tbl_ptr);
	} else {
		nvgpu_err(g, "Invalid Clock Prog Table Header version\n");
		status = -EINVAL;
	}

	return status;
}

static int clk_prog_pmudatainit_super(struct gk20a *g,
		struct boardobj *board_obj_ptr,
		struct nv_pmu_boardobj *ppmudata)
{
	int status = 0;

	nvgpu_log_info(g, " ");

	status = nvgpu_boardobj_pmu_data_init_super(g, board_obj_ptr, ppmudata);
	return status;
}

static int clk_prog_pmudatainit_1x(struct gk20a *g,
		struct boardobj *board_obj_ptr,
		struct nv_pmu_boardobj *ppmudata)
{
	int status = 0;
	struct clk_prog_1x *pclk_prog_1x;
	struct nv_pmu_clk_clk_prog_1x_boardobj_set *pset;

	nvgpu_log_info(g, " ");

	status = clk_prog_pmudatainit_super(g, board_obj_ptr, ppmudata);
	if (status != 0) {
		return status;
	}

	pclk_prog_1x = (struct clk_prog_1x *)(void *)board_obj_ptr;

	pset = (struct nv_pmu_clk_clk_prog_1x_boardobj_set *)(void *)
		ppmudata;

	pset->source = pclk_prog_1x->source;
	pset->freq_max_mhz = pclk_prog_1x->freq_max_mhz;
	pset->source_data = pclk_prog_1x->source_data;

	return status;
}

static int clk_prog_pmudatainit_1x_master(struct gk20a *g,
		struct boardobj *board_obj_ptr,
		struct nv_pmu_boardobj *ppmudata)
{
	int status = 0;
	struct clk_prog_1x_master *pclk_prog_1x_master;
	struct nv_pmu_clk_clk_prog_1x_master_boardobj_set *pset;
	size_t vfsize = sizeof(struct ctrl_clk_clk_prog_1x_master_vf_entry) *
		g->pmu.clk_pmu->clk_progobjs->vf_entry_count;

	nvgpu_log_info(g, " ");

	status = clk_prog_pmudatainit_1x(g, board_obj_ptr, ppmudata);

	pclk_prog_1x_master =
		(struct clk_prog_1x_master *)(void *)board_obj_ptr;

	pset = (struct nv_pmu_clk_clk_prog_1x_master_boardobj_set *)(void *)
		ppmudata;

	nvgpu_memcpy((u8 *)pset->vf_entries,
		(u8 *)pclk_prog_1x_master->p_vf_entries, vfsize);

	pset->b_o_c_o_v_enabled = pclk_prog_1x_master->b_o_c_o_v_enabled;
	pset->source_data = pclk_prog_1x_master->source_data;

	nvgpu_memcpy((u8 *)&pset->deltas, (u8 *)&pclk_prog_1x_master->deltas,
		(u32) sizeof(struct ctrl_clk_clk_delta));

	return status;
}

static int clk_prog_pmudatainit_35_master(struct gk20a *g,
		struct boardobj *board_obj_ptr,
		struct nv_pmu_boardobj *ppmudata)
{
	int status = 0;
	struct clk_prog_35_master *pclk_prog_35_master;
	struct nv_pmu_clk_clk_prog_35_master_boardobj_set *pset;
	size_t voltrail_sec_vfsize =
		sizeof(struct ctrl_clk_clk_prog_35_master_sec_vf_entry_voltrail)
			* g->pmu.clk_pmu->clk_progobjs->vf_sec_entry_count;

	nvgpu_log_info(g, " ");

	status = clk_prog_pmudatainit_1x_master(g, board_obj_ptr, ppmudata);

	pclk_prog_35_master =
		(struct clk_prog_35_master *)(void *)board_obj_ptr;

	pset = (struct nv_pmu_clk_clk_prog_35_master_boardobj_set *)(void *)
		ppmudata;

	nvgpu_memcpy((u8 *)pset->voltrail_sec_vf_entries,
		(u8 *)pclk_prog_35_master->p_voltrail_sec_vf_entries,
		voltrail_sec_vfsize);

	return status;
}

static int clk_prog_pmudatainit_35_master_ratio(struct gk20a *g,
		struct boardobj *board_obj_ptr,
		struct nv_pmu_boardobj *ppmudata)
{
	int status = 0;
	struct clk_prog_35_master_ratio *pclk_prog_35_master_ratio;
	struct nv_pmu_clk_clk_prog_35_master_ratio_boardobj_set *pset;
	size_t slavesize = sizeof(struct ctrl_clk_clk_prog_1x_master_ratio_slave_entry) *
		g->pmu.clk_pmu->clk_progobjs->slave_entry_count;

	nvgpu_log_info(g, " ");

	status = clk_prog_pmudatainit_35_master(g, board_obj_ptr, ppmudata);
	if (status != 0) {
		return status;
	}

	pclk_prog_35_master_ratio =
		(struct clk_prog_35_master_ratio *)(void *)board_obj_ptr;

	pset = (struct nv_pmu_clk_clk_prog_35_master_ratio_boardobj_set *)
				(void *)ppmudata;

	nvgpu_memcpy((u8 *)pset->ratio.slave_entries,
		(u8 *)pclk_prog_35_master_ratio->ratio.p_slave_entries,
		slavesize);

	return status;
}

static int clk_prog_pmudatainit_35_master_table(struct gk20a *g,
		struct boardobj *board_obj_ptr,
		struct nv_pmu_boardobj *ppmudata)
{
	int status = 0;
	struct clk_prog_35_master_table *pclk_prog_35_master_table;
	struct nv_pmu_clk_clk_prog_35_master_table_boardobj_set *pset;
	size_t slavesize = sizeof(
			struct ctrl_clk_clk_prog_1x_master_ratio_slave_entry) *
		g->pmu.clk_pmu->clk_progobjs->slave_entry_count;

	nvgpu_log_info(g, " ");

	status = clk_prog_pmudatainit_35_master(g, board_obj_ptr, ppmudata);
	if (status != 0) {
		return status;
	}

	pclk_prog_35_master_table =
		(struct clk_prog_35_master_table *)(void *)board_obj_ptr;

	pset = (struct nv_pmu_clk_clk_prog_35_master_table_boardobj_set *)
				(void *)ppmudata;
	nvgpu_memcpy((u8 *)pset->table.slave_entries,
		(u8 *)pclk_prog_35_master_table->table.p_slave_entries,
		slavesize);

	return status;
}

static int _clk_prog_1x_master_rail_construct_vf_point(struct gk20a *g,
		struct nvgpu_clk_pmupstate *pclk,
		struct clk_prog_1x_master *p1xmaster,
		struct ctrl_clk_clk_prog_1x_master_vf_entry *p_vf_rail,
		struct clk_vf_point *p_vf_point_tmp, u8 *p_vf_point_idx)
{
	struct clk_vf_point *p_vf_point;
	int status;

	nvgpu_log_info(g, " ");

	p_vf_point = nvgpu_construct_clk_vf_point(g, (void *)p_vf_point_tmp);
	if (p_vf_point == NULL) {
		status = -ENOMEM;
		goto done;
	}
	status = pclk->clk_vf_pointobjs->super.super.objinsert(
				&pclk->clk_vf_pointobjs->super.super,
				&p_vf_point->super,
				*p_vf_point_idx);
	if (status != 0) {
		goto done;
	}

	p_vf_rail->vf_point_idx_last = (*p_vf_point_idx)++;

done:
	nvgpu_log_info(g, "done status %x", status);
	return status;
}

static int clk_prog_construct_super(struct gk20a *g,
		struct boardobj **ppboardobj, size_t size, void *pargs)
{
	struct clk_prog *pclkprog;
	int status = 0;

	status = nvgpu_boardobj_construct_super(g, ppboardobj,
		size, pargs);
	if (status != 0) {
		return -EINVAL;
	}

	pclkprog = (struct clk_prog *)*ppboardobj;

	pclkprog->super.pmudatainit =
			clk_prog_pmudatainit_super;
	return status;
}


static int clk_prog_construct_1x(struct gk20a *g, struct boardobj **ppboardobj,
		size_t size, void *pargs)
{
	struct boardobj *ptmpobj = (struct boardobj *)pargs;
	struct clk_prog_1x *pclkprog;
	struct clk_prog_1x *ptmpprog =
			(struct clk_prog_1x *)pargs;
	int status = 0;

	nvgpu_log_info(g, " ");
	ptmpobj->type_mask |= (u32)BIT(CTRL_CLK_CLK_PROG_TYPE_1X);
	status = clk_prog_construct_super(g, ppboardobj, size, pargs);
	if (status != 0) {
		return -EINVAL;
	}

	pclkprog = (struct clk_prog_1x *)(void *)*ppboardobj;

	pclkprog->super.super.pmudatainit =
			clk_prog_pmudatainit_1x;

	pclkprog->source = ptmpprog->source;
	pclkprog->freq_max_mhz = ptmpprog->freq_max_mhz;
	pclkprog->source_data = ptmpprog->source_data;

	return status;
}

static int clk_prog_construct_35(struct gk20a *g, struct boardobj **ppboardobj,
		size_t size, void *pargs)
{
	struct boardobj *ptmpobj = (struct boardobj *)pargs;
	struct clk_prog_1x *pclkprog;
	struct clk_prog_1x *ptmpprog =
			(struct clk_prog_1x *)pargs;
	int status = 0;

	nvgpu_log_info(g, " ");
	ptmpobj->type_mask |= (u32)BIT(CTRL_CLK_CLK_PROG_TYPE_35);
	status = clk_prog_construct_super(g, ppboardobj, size, pargs);
	if (status != 0) {
		return -EINVAL;
	}

	pclkprog = (struct clk_prog_1x *)(void *)*ppboardobj;

	pclkprog->super.super.pmudatainit =
			clk_prog_pmudatainit_1x;

	pclkprog->source = ptmpprog->source;
	pclkprog->freq_max_mhz = ptmpprog->freq_max_mhz;
	pclkprog->source_data = ptmpprog->source_data;

	return status;
}

static int clk_prog_construct_1x_master(struct gk20a *g,
		struct boardobj **ppboardobj, size_t size, void *pargs)
{
	struct boardobj *ptmpobj = (struct boardobj *)pargs;
	struct clk_prog_1x_master *pclkprog;
	struct clk_prog_1x_master *ptmpprog =
			(struct clk_prog_1x_master *)pargs;
	int status = 0;
	size_t vfsize = sizeof(struct ctrl_clk_clk_prog_1x_master_vf_entry) *
		g->pmu.clk_pmu->clk_progobjs->vf_entry_count;
	u8 railidx;

	nvgpu_log_info(g, " type - %x", BOARDOBJ_GET_TYPE(pargs));

	ptmpobj->type_mask |= (u32)BIT(CTRL_CLK_CLK_PROG_TYPE_1X_MASTER);
	status = clk_prog_construct_1x(g, ppboardobj, size, pargs);
	if (status != 0) {
		return -EINVAL;
	}

	pclkprog = (struct clk_prog_1x_master *)(void *)*ppboardobj;

	pclkprog->super.super.super.pmudatainit =
			clk_prog_pmudatainit_1x_master;

	pclkprog->vfflatten =
			vfflatten_prog_1x_master;

	pclkprog->vflookup =
			vflookup_prog_1x_master;

	pclkprog->getfpoints =
			getfpoints_prog_1x_master;

	pclkprog->getslaveclk =
			getslaveclk_prog_1x_master;

	pclkprog->p_vf_entries = (struct ctrl_clk_clk_prog_1x_master_vf_entry *)
		nvgpu_kzalloc(g, vfsize);

	nvgpu_memcpy((u8 *)pclkprog->p_vf_entries,
		(u8 *)ptmpprog->p_vf_entries, vfsize);

	pclkprog->b_o_c_o_v_enabled = ptmpprog->b_o_c_o_v_enabled;

	for (railidx = 0;
	     railidx < g->pmu.clk_pmu->clk_progobjs->vf_entry_count;
	     railidx++) {
		pclkprog->p_vf_entries[railidx].vf_point_idx_first =
			CTRL_CLK_CLK_VF_POINT_IDX_INVALID;
		pclkprog->p_vf_entries[railidx].vf_point_idx_last =
			CTRL_CLK_CLK_VF_POINT_IDX_INVALID;
	}

	return status;
}

static int clk_prog_construct_35_master(struct gk20a *g,
		struct boardobj **ppboardobj, size_t size, void *pargs)
{
	struct boardobj *ptmpobj = (struct boardobj *)pargs;
	struct clk_prog_35_master *pclkprog;
	struct clk_prog_35_master *ptmpprog =
			(struct clk_prog_35_master *)pargs;
	int status = 0;
	size_t voltrail_sec_vfsize =
		sizeof(struct ctrl_clk_clk_prog_35_master_sec_vf_entry_voltrail)
			* CTRL_CLK_CLK_PROG_1X_MASTER_VF_ENTRY_MAX_ENTRIES;

	nvgpu_log_info(g, " type - %x", BOARDOBJ_GET_TYPE(pargs));

	ptmpobj->type_mask |= (u32)BIT(CTRL_CLK_CLK_PROG_TYPE_35_MASTER);
	status = clk_prog_construct_1x_master(g, ppboardobj, size, pargs);
	if (status != 0) {
		return -EINVAL;
	}

	pclkprog = (struct clk_prog_35_master *)(void *)*ppboardobj;

	pclkprog->super.super.super.pmudatainit =
			clk_prog_pmudatainit_35_master;

	pclkprog->p_voltrail_sec_vf_entries =
		(struct ctrl_clk_clk_prog_35_master_sec_vf_entry_voltrail *)
				nvgpu_kzalloc(g, voltrail_sec_vfsize);

	memset(pclkprog->p_voltrail_sec_vf_entries,
		CTRL_CLK_CLK_DOMAIN_INDEX_INVALID, voltrail_sec_vfsize);

	nvgpu_memcpy((u8 *)pclkprog->p_voltrail_sec_vf_entries,
		(u8 *)ptmpprog->p_voltrail_sec_vf_entries, voltrail_sec_vfsize);

	return status;
}

static int clk_prog_construct_35_master_ratio(struct gk20a *g,
		struct boardobj **ppboardobj, size_t size, void *pargs)
{
	struct boardobj *ptmpobj = (struct boardobj *)pargs;
	struct clk_prog_35_master_ratio *pclkprog;
	struct clk_prog_35_master_ratio *ptmpprog =
			(struct clk_prog_35_master_ratio *)pargs;
	int status = 0;
	size_t slavesize = sizeof(
			struct ctrl_clk_clk_prog_1x_master_ratio_slave_entry) *
		g->pmu.clk_pmu->clk_progobjs->slave_entry_count;

	if (BOARDOBJ_GET_TYPE(pargs) != CTRL_CLK_CLK_PROG_TYPE_35_MASTER_RATIO) {
		return -EINVAL;
	}

	ptmpobj->type_mask |= (u32)BIT(CTRL_CLK_CLK_PROG_TYPE_35_MASTER_RATIO);
	status = clk_prog_construct_35_master(g, ppboardobj, size, pargs);
	if (status != 0) {
		return -EINVAL;
	}

	pclkprog = (struct clk_prog_35_master_ratio *)(void *)*ppboardobj;

	pclkprog->super.super.super.super.pmudatainit =
			clk_prog_pmudatainit_35_master_ratio;

	pclkprog->ratio.p_slave_entries =
		(struct ctrl_clk_clk_prog_1x_master_ratio_slave_entry *)
		nvgpu_kzalloc(g, slavesize);
	if (pclkprog->ratio.p_slave_entries == NULL) {
		return -ENOMEM;
	}

	(void) memset(pclkprog->ratio.p_slave_entries,
			CTRL_CLK_CLK_DOMAIN_INDEX_INVALID, slavesize);

	nvgpu_memcpy((u8 *)pclkprog->ratio.p_slave_entries,
		(u8 *)ptmpprog->ratio.p_slave_entries, slavesize);

	return status;
}

static int clk_prog_construct_35_master_table(struct gk20a *g,
		struct boardobj **ppboardobj, size_t size, void *pargs)
{
	struct boardobj *ptmpobj = (struct boardobj *)pargs;
	struct clk_prog_35_master_table *pclkprog;
	struct clk_prog_35_master_table *ptmpprog =
			(struct clk_prog_35_master_table *)pargs;
	int status = 0;
	size_t slavesize =
		sizeof(struct ctrl_clk_clk_prog_1x_master_table_slave_entry) *
		g->pmu.clk_pmu->clk_progobjs->slave_entry_count;

	nvgpu_log_info(g, "type - %x", BOARDOBJ_GET_TYPE(pargs));

	if (BOARDOBJ_GET_TYPE(pargs) != CTRL_CLK_CLK_PROG_TYPE_35_MASTER_TABLE) {
		return -EINVAL;
	}

	ptmpobj->type_mask |= (u32)BIT(CTRL_CLK_CLK_PROG_TYPE_35_MASTER_TABLE);
	status = clk_prog_construct_35_master(g, ppboardobj, size, pargs);
	if (status != 0) {
		return -EINVAL;
	}

	pclkprog = (struct clk_prog_35_master_table *)(void *)*ppboardobj;

	pclkprog->super.super.super.super.pmudatainit =
			clk_prog_pmudatainit_35_master_table;

	pclkprog->table.p_slave_entries =
		(struct ctrl_clk_clk_prog_1x_master_table_slave_entry *)
		nvgpu_kzalloc(g, slavesize);

	if (pclkprog->table.p_slave_entries == NULL) {
		status = -ENOMEM;
		goto exit;
	}

	memset(pclkprog->table.p_slave_entries,
			CTRL_CLK_CLK_DOMAIN_INDEX_INVALID, slavesize);

	nvgpu_memcpy((u8 *)pclkprog->table.p_slave_entries,
		(u8 *)ptmpprog->table.p_slave_entries, slavesize);

exit:
	if (status != 0) {
		status = (*ppboardobj)->destruct(*ppboardobj);
	}

	return status;
}

static struct clk_prog *construct_clk_prog(struct gk20a *g, void *pargs)
{
	struct boardobj *board_obj_ptr = NULL;
	int status;

	nvgpu_log_info(g, " type - %x", BOARDOBJ_GET_TYPE(pargs));
	switch (BOARDOBJ_GET_TYPE(pargs)) {
	case CTRL_CLK_CLK_PROG_TYPE_35:
		status = clk_prog_construct_35(g, &board_obj_ptr,
			sizeof(struct clk_prog_1x), pargs);
		break;

	case CTRL_CLK_CLK_PROG_TYPE_35_MASTER_TABLE:
		status = clk_prog_construct_35_master_table(g, &board_obj_ptr,
			sizeof(struct clk_prog_35_master_table), pargs);
		break;

	case CTRL_CLK_CLK_PROG_TYPE_35_MASTER_RATIO:
		status = clk_prog_construct_35_master_ratio(g, &board_obj_ptr,
			sizeof(struct clk_prog_35_master_ratio), pargs);
		break;
	default:
		return NULL;
	}

	if (status != 0) {
		if (board_obj_ptr != NULL) {
			board_obj_ptr->destruct(board_obj_ptr);
		}
		return NULL;
	}

	nvgpu_log_info(g, " Done");

	return (struct clk_prog *)board_obj_ptr;
}

static int vfflatten_prog_1x_master(struct gk20a *g,
		struct nvgpu_clk_pmupstate *pclk,
		struct clk_prog_1x_master *p1xmaster,
		u8 clk_domain_idx, u16 *pfreqmaxlastmhz)
{
	struct ctrl_clk_clk_prog_1x_master_vf_entry *p_vf_rail;
	union {
		struct boardobj board_obj;
		struct clk_vf_point vf_point;
		struct clk_vf_point_freq freq;
		struct clk_vf_point_volt volt;
	} vf_point_data;
	int status = 0;
	u8 step_count;
	u8 freq_step_size_mhz = 0;
	u8 vf_point_idx;
	u8 vf_rail_idx;

	nvgpu_log_info(g, " ");
	(void) memset(&vf_point_data, 0x0, sizeof(vf_point_data));

	vf_point_idx = BOARDOBJGRP_NEXT_EMPTY_IDX(
			&pclk->clk_vf_pointobjs->super.super);

	for (vf_rail_idx = 0;
	     vf_rail_idx < pclk->clk_progobjs->vf_entry_count;
	     vf_rail_idx++) {
		u32 voltage_min_uv;
		u32 voltage_step_size_uv;
		u8  i;

		p_vf_rail = &p1xmaster->p_vf_entries[vf_rail_idx];
		if (p_vf_rail->vfe_idx == CTRL_BOARDOBJ_IDX_INVALID) {
			continue;
		}

		p_vf_rail->vf_point_idx_first = vf_point_idx;

		vf_point_data.vf_point.vfe_equ_idx = p_vf_rail->vfe_idx;
		vf_point_data.vf_point.volt_rail_idx = vf_rail_idx;

		step_count = 0;

		switch (p1xmaster->super.source) {
		case CTRL_CLK_PROG_1X_SOURCE_PLL:
			freq_step_size_mhz =
				p1xmaster->super.source_data.pll.freq_step_size_mhz;
			step_count = (freq_step_size_mhz == 0U) ? 0U :
					(u8)(p1xmaster->super.freq_max_mhz -
						*pfreqmaxlastmhz - 1U) /
					freq_step_size_mhz;
			/* Intentional fall-through.*/

		case CTRL_CLK_PROG_1X_SOURCE_ONE_SOURCE:
			vf_point_data.board_obj.type =
					CTRL_CLK_CLK_VF_POINT_TYPE_35_FREQ;
			 do {
				 clkvfpointfreqmhzset(g, &vf_point_data.vf_point,
					p1xmaster->super.freq_max_mhz -
					  U16(step_count) *
					  U16(freq_step_size_mhz));

				status = _clk_prog_1x_master_rail_construct_vf_point(g, pclk,
					p1xmaster, p_vf_rail,
					&vf_point_data.vf_point, &vf_point_idx);
				if (status != 0) {
					goto done;
				}
			} while (step_count-- > 0U);
			break;

		case CTRL_CLK_PROG_1X_SOURCE_FLL:
			voltage_min_uv =
				g->pmu.clk_pmu->get_fll_lut_min_volt(pclk);
			voltage_step_size_uv =
				g->pmu.clk_pmu->get_fll_lut_step_size(pclk);
			step_count = g->pmu.clk_pmu->
				get_fll_lut_vf_num_entries(pclk);

			/* FLL sources use a voltage-based VF_POINT.*/
			vf_point_data.board_obj.type =
					CTRL_CLK_CLK_VF_POINT_TYPE_35_VOLT;
			for (i = 0; i < step_count; i++) {
				vf_point_data.volt.source_voltage_uv =
					voltage_min_uv + i * voltage_step_size_uv;

				status = _clk_prog_1x_master_rail_construct_vf_point(g, pclk,
					p1xmaster, p_vf_rail,
					&vf_point_data.vf_point, &vf_point_idx);
				if (status != 0) {
					goto done;
				}
			}
			break;
		}
	}

	*pfreqmaxlastmhz = p1xmaster->super.freq_max_mhz;

done:
	nvgpu_log_info(g, "done status %x", status);
	return status;
}

static int vflookup_prog_1x_master(struct gk20a *g,
		struct nvgpu_clk_pmupstate *pclk,
		struct clk_prog_1x_master *p1xmaster, u8 *slave_clk_domain,
		u16 *pclkmhz, u32 *pvoltuv, u8 rail)
{
	u32 j;
	struct ctrl_clk_clk_prog_1x_master_vf_entry
		*pvfentry;
	struct clk_vf_point *pvfpoint;
	struct nvgpu_clk_progs *pclkprogobjs;
	struct clk_prog_1x_master_ratio *p1xmasterratio;
	u16 clkmhz;
	u32 voltuv;
	u8 slaveentrycount;
	u32 i;
	struct ctrl_clk_clk_prog_1x_master_ratio_slave_entry *pslaveents;

	if ((*pclkmhz != 0U) && (*pvoltuv != 0U)) {
		return -EINVAL;
	}

	pclkprogobjs = pclk->clk_progobjs;

	slaveentrycount = pclkprogobjs->slave_entry_count;

	if (pclkprogobjs->vf_entry_count >
		CTRL_CLK_CLK_PROG_1X_MASTER_VF_ENTRY_MAX_ENTRIES) {
		return -EINVAL;
	}

	if (rail >= pclkprogobjs->vf_entry_count) {
		return -EINVAL;
	}

	pvfentry =  p1xmaster->p_vf_entries;

	pvfentry = (struct ctrl_clk_clk_prog_1x_master_vf_entry *)(
			(u8 *)pvfentry +
			(sizeof(struct ctrl_clk_clk_prog_1x_master_vf_entry) *
			rail));

	clkmhz = *pclkmhz;
	voltuv = *pvoltuv;

	/*if domain is slave domain and freq is input
		then derive master clk */
	if ((slave_clk_domain != NULL) && (*pclkmhz != 0U)) {
		if (p1xmaster->super.super.super.implements(g,
			&p1xmaster->super.super.super,
			CTRL_CLK_CLK_PROG_TYPE_1X_MASTER_RATIO)) {

			p1xmasterratio =
			(struct clk_prog_1x_master_ratio *)p1xmaster;
			pslaveents = p1xmasterratio->p_slave_entries;
			for (i = 0; i < slaveentrycount;  i++) {
				if (pslaveents->clk_dom_idx ==
					*slave_clk_domain) {
					break;
				}
				pslaveents++;
			}
			if (i == slaveentrycount) {
				return -EINVAL;
			}
			clkmhz = (clkmhz * 100U)/pslaveents->ratio;
		} else {
			/* only support ratio for now */
			return -EINVAL;
		}
	}

	/* if both volt and clks are zero simply print*/
	if ((*pvoltuv == 0U) && (*pclkmhz == 0U)) {
		for (j = pvfentry->vf_point_idx_first;
			j <= pvfentry->vf_point_idx_last; j++) {
			pvfpoint = CLK_CLK_VF_POINT_GET(pclk, j);
			nvgpu_err(g, "v %x c %x",
				clkvfpointvoltageuvget(g, pvfpoint),
				clkvfpointfreqmhzget(g, pvfpoint));
		}
		return -EINVAL;
	}
	/* start looking up f for v for v for f */
	/* looking for volt? */
	if (*pvoltuv == 0U) {
		pvfpoint = CLK_CLK_VF_POINT_GET(pclk,
				pvfentry->vf_point_idx_last);
		/* above range? */
		if (clkmhz > clkvfpointfreqmhzget(g, pvfpoint)) {
			return -EINVAL;
		}

		for (j = pvfentry->vf_point_idx_last;
			j >= pvfentry->vf_point_idx_first; j--) {
			pvfpoint = CLK_CLK_VF_POINT_GET(pclk, j);
			if (clkmhz <= clkvfpointfreqmhzget(g, pvfpoint)) {
				voltuv = clkvfpointvoltageuvget(g, pvfpoint);
			} else {
				break;
			}
		}
	} else {	/* looking for clk? */

		pvfpoint = CLK_CLK_VF_POINT_GET(pclk,
				pvfentry->vf_point_idx_first);
		/* below range? */
		if (voltuv < clkvfpointvoltageuvget(g, pvfpoint)) {
			return -EINVAL;
		}

		for (j = pvfentry->vf_point_idx_first;
			j <= pvfentry->vf_point_idx_last; j++) {
			pvfpoint = CLK_CLK_VF_POINT_GET(pclk, j);
			if (voltuv >= clkvfpointvoltageuvget(g, pvfpoint)) {
				clkmhz = clkvfpointfreqmhzget(g, pvfpoint);
			} else {
				break;
			}
		}
	}

	/*if domain is slave domain and freq was looked up
		then derive slave clk */
	if ((slave_clk_domain != NULL) && (*pclkmhz == 0U)) {
		if (p1xmaster->super.super.super.implements(g,
			&p1xmaster->super.super.super,
			CTRL_CLK_CLK_PROG_TYPE_1X_MASTER_RATIO)) {

			p1xmasterratio =
			(struct clk_prog_1x_master_ratio *)p1xmaster;
			pslaveents = p1xmasterratio->p_slave_entries;
			for (i = 0; i < slaveentrycount;  i++) {
				if (pslaveents->clk_dom_idx ==
					*slave_clk_domain) {
					break;
				}
				pslaveents++;
			}
			if (i == slaveentrycount) {
				return -EINVAL;
			}
			clkmhz = (clkmhz * pslaveents->ratio)/100U;
		} else {
			/* only support ratio for now */
			return -EINVAL;
		}
	}
	*pclkmhz = clkmhz;
	*pvoltuv = voltuv;
	if ((clkmhz == 0U) || (voltuv == 0U)) {
		return -EINVAL;
	}
	return 0;
}

static int getfpoints_prog_1x_master(struct gk20a *g,
		struct nvgpu_clk_pmupstate *pclk,
		struct clk_prog_1x_master *p1xmaster,
		u32 *pfpointscount, u16 **ppfreqpointsinmhz, u8 rail)
{

	struct ctrl_clk_clk_prog_1x_master_vf_entry
		*pvfentry;
	struct clk_vf_point *pvfpoint;
	struct nvgpu_clk_progs *pclkprogobjs;
	u8 j;
	u32 fpointscount = 0;

	if (pfpointscount == NULL) {
		return -EINVAL;
	}

	pclkprogobjs = pclk->clk_progobjs;

	if (pclkprogobjs->vf_entry_count >
		CTRL_CLK_CLK_PROG_1X_MASTER_VF_ENTRY_MAX_ENTRIES) {
		return -EINVAL;
	}

	if (rail >= pclkprogobjs->vf_entry_count) {
		return -EINVAL;
	}

	pvfentry =  p1xmaster->p_vf_entries;

	pvfentry = (struct ctrl_clk_clk_prog_1x_master_vf_entry *)(
			(u8 *)pvfentry +
			((u8)sizeof(struct ctrl_clk_clk_prog_1x_master_vf_entry) *
			rail));

	fpointscount = (u32)pvfentry->vf_point_idx_last -
		(u32)pvfentry->vf_point_idx_first + 1U;

	/* if pointer for freq data is NULL simply return count */
	if (*ppfreqpointsinmhz == NULL) {
		goto done;
	}

	if (fpointscount > *pfpointscount) {
		return -ENOMEM;
	}
	for (j = pvfentry->vf_point_idx_first;
		j <= pvfentry->vf_point_idx_last; j++) {
		pvfpoint = CLK_CLK_VF_POINT_GET(pclk, j);
		**ppfreqpointsinmhz = clkvfpointfreqmhzget(g, pvfpoint);
		(*ppfreqpointsinmhz)++;
	}
done:
	*pfpointscount = fpointscount;
	return 0;
}

static int getslaveclk_prog_1x_master(struct gk20a *g,
		struct nvgpu_clk_pmupstate *pclk,
		struct clk_prog_1x_master *p1xmaster,
		u8 slave_clk_domain, u16 *pclkmhz, u16 masterclkmhz
)
{
	struct nvgpu_clk_progs *pclkprogobjs;
	struct clk_prog_1x_master_ratio *p1xmasterratio;
	struct clk_prog_35_master_ratio *p35masterratio;
	u8 slaveentrycount;
	u8 i;
	struct ctrl_clk_clk_prog_1x_master_ratio_slave_entry *pslaveents;
	u32 ver = g->params.gpu_arch + g->params.gpu_impl;
	if (pclkmhz == NULL) {
		return -EINVAL;
	}

	if (masterclkmhz == 0U) {
		return -EINVAL;
	}

	*pclkmhz = 0;
	pclkprogobjs = pclk->clk_progobjs;

	slaveentrycount = pclkprogobjs->slave_entry_count;
	if(ver == NVGPU_GPUID_GV100) {
		if (p1xmaster->super.super.super.implements(g,
			&p1xmaster->super.super.super,
			CTRL_CLK_CLK_PROG_TYPE_1X_MASTER_RATIO)) {
			p1xmasterratio =
			(struct clk_prog_1x_master_ratio *)p1xmaster;
			pslaveents = p1xmasterratio->p_slave_entries;
			for (i = 0; i < slaveentrycount;  i++) {
				if (pslaveents->clk_dom_idx ==
					slave_clk_domain) {
					break;
				}
				pslaveents++;
			}
			if (i == slaveentrycount) {
				return -EINVAL;
			}
			*pclkmhz = (masterclkmhz * pslaveents->ratio)/100U;
		} else {
			/* only support ratio for now */
			return -EINVAL;
		}
	} else {
		if (p1xmaster->super.super.super.implements(g,
			&p1xmaster->super.super.super,
			CTRL_CLK_CLK_PROG_TYPE_35_MASTER_RATIO)) {
			p35masterratio =
			(struct clk_prog_35_master_ratio *)p1xmaster;
			pslaveents = p35masterratio->ratio.p_slave_entries;
			for (i = 0; i < slaveentrycount;  i++) {
				if (pslaveents->clk_dom_idx ==
					slave_clk_domain) {
					break;
				}
				pslaveents++;
			}
			if (i == slaveentrycount) {
				return -EINVAL;
			}
			*pclkmhz = (masterclkmhz * pslaveents->ratio)/100U;
			/* Floor/Quantize all the slave clocks to the multiple of step size*/
			*pclkmhz = (*pclkmhz / FREQ_STEP_SIZE_MHZ) * FREQ_STEP_SIZE_MHZ;
		} else {
			/* only support ratio for now */
			return -EINVAL;
		}
	}
	return 0;
}

int nvgpu_clk_prog_init_pmupstate(struct gk20a *g)
{
	/* If already allocated, do not re-allocate */
	if (g->pmu.clk_pmu->clk_progobjs != NULL) {
		return 0;
	}

	g->pmu.clk_pmu->clk_progobjs = nvgpu_kzalloc(g,
			sizeof(*g->pmu.clk_pmu->clk_progobjs));
	if (g->pmu.clk_pmu->clk_progobjs == NULL) {
		return -ENOMEM;
	}

	return 0;
}

void nvgpu_clk_prog_free_pmupstate(struct gk20a *g)
{
	nvgpu_kfree(g, g->pmu.clk_pmu->clk_progobjs);
	g->pmu.clk_pmu->clk_progobjs = NULL;
}
