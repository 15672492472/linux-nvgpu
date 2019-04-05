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
#include <nvgpu/pmuif/nvgpu_gpmu_cmdif.h>
#include <nvgpu/io.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/bug.h>
#include <nvgpu/boardobjgrp.h>
#include <nvgpu/boardobjgrp_e32.h>
#include <nvgpu/string.h>
#include <nvgpu/pmuif/ctrlvolt.h>
#include <nvgpu/pmu/clk/clk.h>

#include "gp106/bios_gp106.h"
#include "clk_vin.h"

struct nvgpu_clk_vin_rpc_pmucmdhandler_params {
	struct nv_pmu_clk_rpc *prpccall;
	u32 success;
};

void nvgpu_clk_vin_rpc_pmucmdhandler(struct gk20a *g, struct pmu_msg *msg,
		void *param, u32 handle, u32 status)
{
	struct nvgpu_clk_vin_rpc_pmucmdhandler_params *phandlerparams =
		(struct nvgpu_clk_vin_rpc_pmucmdhandler_params *)param;

	nvgpu_log_info(g, " ");

	if (msg->msg.clk.msg_type != NV_PMU_CLK_MSG_ID_RPC) {
		nvgpu_err(g, "unsupported msg for CLK LOAD RPC %x",
			  msg->msg.clk.msg_type);
		return;
	}

	if (phandlerparams->prpccall->b_supported) {
		phandlerparams->success = 1;
	}
}

static int devinit_get_vin_device_table(struct gk20a *g,
		struct nvgpu_avfsvinobjs *pvinobjs);

static int vin_device_construct_v10(struct gk20a *g,
					struct boardobj **ppboardobj,
					size_t size, void *pargs);
static int vin_device_construct_v20(struct gk20a *g,
					struct boardobj **ppboardobj,
					size_t size, void *pargs);
static int vin_device_construct_super(struct gk20a *g,
					struct boardobj **ppboardobj,
					size_t size, void *pargs);
static struct nvgpu_vin_device *construct_vin_device(
		struct gk20a *g, void *pargs);

static int vin_device_init_pmudata_v10(struct gk20a *g,
				  struct boardobj *board_obj_ptr,
				  struct nv_pmu_boardobj *ppmudata);
static int vin_device_init_pmudata_v20(struct gk20a *g,
				  struct boardobj *board_obj_ptr,
				  struct nv_pmu_boardobj *ppmudata);
static int vin_device_init_pmudata_super(struct gk20a *g,
				  struct boardobj *board_obj_ptr,
				  struct nv_pmu_boardobj *ppmudata);

static struct nvgpu_vin_device *clk_get_vin_from_index(
		struct nvgpu_avfsvinobjs *pvinobjs, u8 idx)
{
	return ((struct nvgpu_vin_device *)BOARDOBJGRP_OBJ_GET_BY_IDX(
		((struct boardobjgrp *)&(pvinobjs->super.super)), idx));
}

static int nvgpu_clk_avfs_get_vin_cal_fuse_v20(struct gk20a *g,
					struct nvgpu_avfsvinobjs *pvinobjs,
					struct vin_device_v20 *pvindev)
{
	int status = 0;
	s8 gain, offset;
	u8 i;

	if (pvinobjs->calibration_rev_vbios ==
			g->ops.fuse.read_vin_cal_fuse_rev(g)) {
		BOARDOBJGRP_FOR_EACH(&(pvinobjs->super.super),
				struct vin_device_v20 *, pvindev, i) {
			gain = 0;
			offset = 0;
			pvindev = (struct vin_device_v20 *)(void *)
				g->clk_pmu->clk_get_vin(pvinobjs, i);
			status = g->ops.fuse.read_vin_cal_gain_offset_fuse(g,
					pvindev->super.id, &gain, &offset);
			if (status != 0) {
				nvgpu_err(g,
				"err reading vin cal for id %x", pvindev->super.id);
				return status;
			}
			pvindev->data.vin_cal.cal_v20.gain = gain;
			pvindev->data.vin_cal.cal_v20.offset = offset;
		}
	}
	return status;

}

static int _clk_vin_devgrp_pmudatainit_super(struct gk20a *g,
					     struct boardobjgrp *pboardobjgrp,
					     struct nv_pmu_boardobjgrp_super *pboardobjgrppmu)
{
	struct nv_pmu_clk_clk_vin_device_boardobjgrp_set_header *pset =
		(struct nv_pmu_clk_clk_vin_device_boardobjgrp_set_header *)
		pboardobjgrppmu;
	struct nvgpu_avfsvinobjs *pvin_obbj = (struct nvgpu_avfsvinobjs *)pboardobjgrp;
	int status = 0;

	nvgpu_log_info(g, " ");

	status = boardobjgrp_pmudatainit_e32(g, pboardobjgrp, pboardobjgrppmu);

	pset->b_vin_is_disable_allowed = pvin_obbj->vin_is_disable_allowed;

	nvgpu_log_info(g, " Done");
	return status;
}

static int _clk_vin_devgrp_pmudata_instget(struct gk20a *g,
					   struct nv_pmu_boardobjgrp *pmuboardobjgrp,
					   struct nv_pmu_boardobj **ppboardobjpmudata,
					   u8 idx)
{
	struct nv_pmu_clk_clk_vin_device_boardobj_grp_set *pgrp_set =
		(struct nv_pmu_clk_clk_vin_device_boardobj_grp_set *)
		pmuboardobjgrp;

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

static int _clk_vin_devgrp_pmustatus_instget(struct gk20a *g,
					     void *pboardobjgrppmu,
					     struct nv_pmu_boardobj_query **ppboardobjpmustatus,
					     u8 idx)
{
	struct nv_pmu_clk_clk_vin_device_boardobj_grp_get_status *pgrp_get_status =
		(struct nv_pmu_clk_clk_vin_device_boardobj_grp_get_status *)
		pboardobjgrppmu;

	/*check whether pmuboardobjgrp has a valid boardobj in index*/
	if (((u32)BIT(idx) &
		pgrp_get_status->hdr.data.super.obj_mask.super.data[0]) == 0U) {
		return -EINVAL;
	}

	*ppboardobjpmustatus = (struct nv_pmu_boardobj_query *)
			&pgrp_get_status->objects[idx].data.board_obj;
	return 0;
}

int nvgpu_clk_vin_sw_setup(struct gk20a *g)
{
	int status;
	struct boardobjgrp *pboardobjgrp = NULL;
	struct vin_device_v20 *pvindev = NULL;
	struct nvgpu_avfsvinobjs *pvinobjs;

	nvgpu_log_info(g, " ");

	status = boardobjgrpconstruct_e32(g, &g->clk_pmu->avfs_vinobjs->super);
	if (status != 0) {
		nvgpu_err(g,
			"error creating boardobjgrp for clk vin, statu - 0x%x",
			status);
		goto done;
	}

	pboardobjgrp = &g->clk_pmu->avfs_vinobjs->super.super;
	pvinobjs = g->clk_pmu->avfs_vinobjs;

	BOARDOBJGRP_PMU_CONSTRUCT(pboardobjgrp, CLK, VIN_DEVICE);

	status = BOARDOBJGRP_PMU_CMD_GRP_SET_CONSTRUCT(g, pboardobjgrp,
			clk, CLK, clk_vin_device, CLK_VIN_DEVICE);
	if (status != 0) {
		nvgpu_err(g,
			"error constructing PMU_BOARDOBJ_CMD_GRP_SET interface - 0x%x",
			status);
		goto done;
	}

	pboardobjgrp->pmudatainit  = _clk_vin_devgrp_pmudatainit_super;
	pboardobjgrp->pmudatainstget  = _clk_vin_devgrp_pmudata_instget;
	pboardobjgrp->pmustatusinstget  = _clk_vin_devgrp_pmustatus_instget;

	status = devinit_get_vin_device_table(g, g->clk_pmu->avfs_vinobjs);
	if (status != 0) {
		goto done;
	}

	/*update vin calibration to fuse */
	nvgpu_clk_avfs_get_vin_cal_fuse_v20(g, pvinobjs, pvindev);

	status = BOARDOBJGRP_PMU_CMD_GRP_GET_STATUS_CONSTRUCT(g,
				&g->clk_pmu->avfs_vinobjs->super.super,
				clk, CLK, clk_vin_device, CLK_VIN_DEVICE);
	if (status != 0) {
		nvgpu_err(g,
			"error constructing PMU_BOARDOBJ_CMD_GRP_SET interface - 0x%x",
			status);
		goto done;
	}

done:
	nvgpu_log_info(g, " done status %x", status);
	return status;
}

int nvgpu_clk_vin_pmu_setup(struct gk20a *g)
{
	int status;
	struct boardobjgrp *pboardobjgrp = NULL;

	nvgpu_log_info(g, " ");

	pboardobjgrp = &g->clk_pmu->avfs_vinobjs->super.super;

	if (!pboardobjgrp->bconstructed) {
		return -EINVAL;
	}

	status = pboardobjgrp->pmuinithandle(g, pboardobjgrp);

	nvgpu_log_info(g, "Done");
	return status;
}

static int devinit_get_vin_device_table(struct gk20a *g,
		struct nvgpu_avfsvinobjs *pvinobjs)
{
	int status = 0;
	u8 *vin_table_ptr = NULL;
	struct vin_descriptor_header_10 vin_desc_table_header = { 0 };
	struct vin_descriptor_entry_10 vin_desc_table_entry = { 0 };
	u8 *vin_tbl_entry_ptr = NULL;
	u32 index = 0;
	u32 slope=0, intercept=0;
	s8 offset = 0, gain = 0;
	struct nvgpu_vin_device *pvin_dev;
	u32 cal_type;

	union {
		struct boardobj boardobj;
		struct nvgpu_vin_device vin_device;
		struct vin_device_v10 vin_device_v10;
		struct vin_device_v20 vin_device_v20;
	} vin_device_data;

	nvgpu_log_info(g, " ");

	vin_table_ptr = (u8 *)nvgpu_bios_get_perf_table_ptrs(g,
			g->bios.clock_token, VIN_TABLE);
	if (vin_table_ptr == NULL) {
		status = -1;
		goto done;
	}

	nvgpu_memcpy((u8 *)&vin_desc_table_header, vin_table_ptr,
	       sizeof(struct vin_descriptor_header_10));

	pvinobjs->calibration_rev_vbios =
			BIOS_GET_FIELD(u8, vin_desc_table_header.flags0,
				NV_VIN_DESC_FLAGS0_VIN_CAL_REVISION);
	pvinobjs->vin_is_disable_allowed =
			BIOS_GET_FIELD(bool, vin_desc_table_header.flags0,
				NV_VIN_DESC_FLAGS0_DISABLE_CONTROL);
	cal_type = BIOS_GET_FIELD(u32, vin_desc_table_header.flags0,
				NV_VIN_DESC_FLAGS0_VIN_CAL_TYPE);
	if (cal_type == 0U) {
		cal_type = CTRL_CLK_VIN_CAL_TYPE_V10;
	}

	switch (cal_type) {
	case CTRL_CLK_VIN_CAL_TYPE_V10:
		/* VIN calibration slope: XX.YYY mV/code => XXYYY uV/code*/
		slope = ((BIOS_GET_FIELD(u32, vin_desc_table_header.vin_cal,
				NV_VIN_DESC_VIN_CAL_SLOPE_INTEGER) * 1000U)) +
			((BIOS_GET_FIELD(u32, vin_desc_table_header.vin_cal,
				NV_VIN_DESC_VIN_CAL_SLOPE_FRACTION)));

		/* VIN calibration intercept: ZZZ.W mV => ZZZW00 uV */
		intercept = ((BIOS_GET_FIELD(u32, vin_desc_table_header.vin_cal,
			NV_VIN_DESC_VIN_CAL_INTERCEPT_INTEGER) * 1000U)) +
			    ((BIOS_GET_FIELD(u32, vin_desc_table_header.vin_cal,
			NV_VIN_DESC_VIN_CAL_INTERCEPT_FRACTION) * 100U));

		break;
	case CTRL_CLK_VIN_CAL_TYPE_V20:
		offset = BIOS_GET_FIELD(s8, vin_desc_table_header.vin_cal,
                                NV_VIN_DESC_VIN_CAL_OFFSET);
		gain = BIOS_GET_FIELD(s8, vin_desc_table_header.vin_cal,
                                NV_VIN_DESC_VIN_CAL_GAIN);
		break;
	default:
		status = -1;
		goto done;
	}
	/* Read table entries*/
	vin_tbl_entry_ptr = vin_table_ptr + vin_desc_table_header.header_sizee;
	for (index = 0; index < vin_desc_table_header.entry_count; index++) {
		nvgpu_memcpy((u8 *)&vin_desc_table_entry, vin_tbl_entry_ptr,
		       sizeof(struct vin_descriptor_entry_10));

		if (vin_desc_table_entry.vin_device_type ==
				CTRL_CLK_VIN_TYPE_DISABLED) {
			continue;
		}

		vin_device_data.boardobj.type =
			(u8)vin_desc_table_entry.vin_device_type;
		vin_device_data.vin_device.id = (u8)vin_desc_table_entry.vin_device_id;
		vin_device_data.vin_device.volt_domain_vbios =
			(u8)vin_desc_table_entry.volt_domain_vbios;
		vin_device_data.vin_device.flls_shared_mask = 0;

		switch (vin_device_data.boardobj.type) {
		case CTRL_CLK_VIN_TYPE_V10:
			vin_device_data.vin_device_v10.data.vin_cal.slope = slope;
			vin_device_data.vin_device_v10.data.vin_cal.intercept = intercept;
			break;
		case CTRL_CLK_VIN_TYPE_V20:
			vin_device_data.vin_device_v20.data.cal_type = (u8) cal_type;
			vin_device_data.vin_device_v20.data.vin_cal.cal_v20.offset = offset;
			vin_device_data.vin_device_v20.data.vin_cal.cal_v20.gain = gain;
			vin_device_data.vin_device_v20.data.vin_cal.cal_v20.offset_vfe_idx =
					CTRL_CLK_VIN_VFE_IDX_INVALID;
			break;
		default:
			status = -1;
			goto done;
		};

		pvin_dev = construct_vin_device(g, (void *)&vin_device_data);

		status = boardobjgrp_objinsert(&pvinobjs->super.super,
				(struct boardobj *)pvin_dev, index);

		vin_tbl_entry_ptr += vin_desc_table_header.entry_size;
	}

done:
	nvgpu_log_info(g, " done status %x", status);
	return status;
}

static int vin_device_construct_v10(struct gk20a *g,
					struct boardobj **ppboardobj,
					size_t size, void *pargs)
{
	struct boardobj *ptmpobj = (struct boardobj *)pargs;
	struct vin_device_v10 *pvin_device_v10;
	struct vin_device_v10 *ptmpvin_device_v10 = (struct vin_device_v10 *)pargs;
	int status = 0;

	if (BOARDOBJ_GET_TYPE(pargs) != CTRL_CLK_VIN_TYPE_V10) {
		return -EINVAL;
	}

	ptmpobj->type_mask |= BIT32(CTRL_CLK_VIN_TYPE_V10);
	status = vin_device_construct_super(g, ppboardobj, size, pargs);
	if (status != 0) {
		return -EINVAL;
	}

	pvin_device_v10 = (struct vin_device_v10 *)*ppboardobj;

	pvin_device_v10->super.super.pmudatainit =
			vin_device_init_pmudata_v10;

	pvin_device_v10->data.vin_cal.slope = ptmpvin_device_v10->data.vin_cal.slope;
	pvin_device_v10->data.vin_cal.intercept = ptmpvin_device_v10->data.vin_cal.intercept;

	return status;
}

static int vin_device_construct_v20(struct gk20a *g,
					struct boardobj **ppboardobj,
					size_t size, void *pargs)
{
	struct boardobj *ptmpobj = (struct boardobj *)pargs;
	struct vin_device_v20 *pvin_device_v20;
	struct vin_device_v20 *ptmpvin_device_v20 = (struct vin_device_v20 *)pargs;
	int status = 0;

	if (BOARDOBJ_GET_TYPE(pargs) != CTRL_CLK_VIN_TYPE_V20) {
		return -EINVAL;
	}

	ptmpobj->type_mask |= BIT32(CTRL_CLK_VIN_TYPE_V20);
	status = vin_device_construct_super(g, ppboardobj, size, pargs);
	if (status != 0) {
		return -EINVAL;
	}

	pvin_device_v20 = (struct vin_device_v20 *)*ppboardobj;

	pvin_device_v20->super.super.pmudatainit =
			vin_device_init_pmudata_v20;

	pvin_device_v20->data.cal_type = ptmpvin_device_v20->data.cal_type;
	pvin_device_v20->data.vin_cal.cal_v20.offset = ptmpvin_device_v20->data.vin_cal.cal_v20.offset;
	pvin_device_v20->data.vin_cal.cal_v20.gain = ptmpvin_device_v20->data.vin_cal.cal_v20.gain;
	pvin_device_v20->data.vin_cal.cal_v20.offset_vfe_idx = ptmpvin_device_v20->data.vin_cal.cal_v20.offset_vfe_idx;

	return status;
}
static int vin_device_construct_super(struct gk20a *g,
					struct boardobj **ppboardobj,
					size_t size, void *pargs)
{
	struct nvgpu_vin_device *pvin_device;
	struct nvgpu_vin_device *ptmpvin_device =
		(struct nvgpu_vin_device *)pargs;
	int status = 0;
	status = boardobj_construct_super(g, ppboardobj, size, pargs);

	if (status != 0) {
		return -EINVAL;
	}

	pvin_device = (struct nvgpu_vin_device *)*ppboardobj;

	pvin_device->super.pmudatainit =
			vin_device_init_pmudata_super;

	pvin_device->id = ptmpvin_device->id;
	pvin_device->volt_domain_vbios = ptmpvin_device->volt_domain_vbios;
	pvin_device->flls_shared_mask = ptmpvin_device->flls_shared_mask;
	pvin_device->volt_domain = CTRL_VOLT_DOMAIN_LOGIC;

	return status;
}
static struct nvgpu_vin_device *construct_vin_device(
		struct gk20a *g, void *pargs)
{
	struct boardobj *board_obj_ptr = NULL;
	int status;

	nvgpu_log_info(g, " %d", BOARDOBJ_GET_TYPE(pargs));
	switch (BOARDOBJ_GET_TYPE(pargs)) {
	case CTRL_CLK_VIN_TYPE_V10:
		status = vin_device_construct_v10(g, &board_obj_ptr,
			sizeof(struct vin_device_v10), pargs);
		break;

	case CTRL_CLK_VIN_TYPE_V20:
		status = vin_device_construct_v20(g, &board_obj_ptr,
			sizeof(struct vin_device_v20), pargs);
		break;

	default:
		return NULL;
	};

	if (status != 0) {
		return NULL;
	}

	nvgpu_log_info(g, " Done");

	return (struct nvgpu_vin_device *)board_obj_ptr;
}



static int vin_device_init_pmudata_v10(struct gk20a *g,
					 struct boardobj *board_obj_ptr,
					 struct nv_pmu_boardobj *ppmudata)
{
	int status = 0;
	struct vin_device_v20 *pvin_dev_v20;
	struct nv_pmu_clk_clk_vin_device_v10_boardobj_set *perf_pmu_data;

	nvgpu_log_info(g, " ");

	status = vin_device_init_pmudata_super(g, board_obj_ptr, ppmudata);
	if (status != 0) {
		return status;
	}

	pvin_dev_v20 = (struct vin_device_v20 *)board_obj_ptr;
	perf_pmu_data = (struct nv_pmu_clk_clk_vin_device_v10_boardobj_set *)
		ppmudata;

	perf_pmu_data->data.vin_cal.intercept = pvin_dev_v20->data.vin_cal.cal_v10.intercept;
	perf_pmu_data->data.vin_cal.slope = pvin_dev_v20->data.vin_cal.cal_v10.slope;

	nvgpu_log_info(g, " Done");

	return status;
}

static int vin_device_init_pmudata_v20(struct gk20a *g,
					 struct boardobj *board_obj_ptr,
					 struct nv_pmu_boardobj *ppmudata)
{
	int status = 0;
	struct vin_device_v20 *pvin_dev_v20;
	struct nv_pmu_clk_clk_vin_device_v20_boardobj_set *perf_pmu_data;

	nvgpu_log_info(g, " ");

	status = vin_device_init_pmudata_super(g, board_obj_ptr, ppmudata);
	if (status != 0) {
		return status;
	}

	pvin_dev_v20 = (struct vin_device_v20 *)board_obj_ptr;
	perf_pmu_data = (struct nv_pmu_clk_clk_vin_device_v20_boardobj_set *)
		ppmudata;

	perf_pmu_data->data.cal_type = pvin_dev_v20->data.cal_type;
	perf_pmu_data->data.vin_cal.cal_v20.offset = pvin_dev_v20->data.vin_cal.cal_v20.offset;
	perf_pmu_data->data.vin_cal.cal_v20.gain = pvin_dev_v20->data.vin_cal.cal_v20.gain;
	perf_pmu_data->data.vin_cal.cal_v20.offset_vfe_idx =
			pvin_dev_v20->data.vin_cal.cal_v20.offset_vfe_idx;

	nvgpu_log_info(g, " Done");

	return status;
}

static int vin_device_init_pmudata_super(struct gk20a *g,
					 struct boardobj *board_obj_ptr,
					 struct nv_pmu_boardobj *ppmudata)
{
	int status = 0;
	struct nvgpu_vin_device *pvin_dev;
	struct nv_pmu_clk_clk_vin_device_boardobj_set *perf_pmu_data;

	nvgpu_log_info(g, " ");

	status = boardobj_pmudatainit_super(g, board_obj_ptr, ppmudata);
	if (status != 0) {
		return status;
	}

	pvin_dev = (struct nvgpu_vin_device *)board_obj_ptr;
	perf_pmu_data = (struct nv_pmu_clk_clk_vin_device_boardobj_set *)
		ppmudata;

	perf_pmu_data->id = pvin_dev->id;
	perf_pmu_data->volt_domain = pvin_dev->volt_domain;
	perf_pmu_data->flls_shared_mask = pvin_dev->flls_shared_mask;

	nvgpu_log_info(g, " Done");

	return status;
}

int nvgpu_clk_pmu_vin_load(struct gk20a *g)
{
	struct pmu_cmd cmd;
	struct pmu_payload payload;
	int status;
	struct nv_pmu_clk_rpc rpccall;
	struct nvgpu_clk_vin_rpc_pmucmdhandler_params handler;
	struct nv_pmu_clk_load *clkload;

	(void) memset(&payload, 0, sizeof(struct pmu_payload));
	(void) memset(&rpccall, 0, sizeof(struct nv_pmu_clk_rpc));
	(void) memset(&handler, 0,
			sizeof(struct nvgpu_clk_vin_rpc_pmucmdhandler_params));

	rpccall.function = NV_PMU_CLK_RPC_ID_LOAD;
	clkload = &rpccall.params.clk_load;
	clkload->feature = NV_NV_PMU_CLK_LOAD_FEATURE_VIN;
	clkload->action_mask =
		NV_NV_PMU_CLK_LOAD_ACTION_MASK_VIN_HW_CAL_PROGRAM_YES << 4;

	cmd.hdr.unit_id = PMU_UNIT_CLK;
	cmd.hdr.size =  (u32)sizeof(struct nv_pmu_clk_cmd) +
			(u32)sizeof(struct pmu_hdr);

	cmd.cmd.clk.cmd_type = NV_PMU_CLK_CMD_ID_RPC;
	cmd.cmd.clk.generic.b_perf_daemon_cmd = false;

	payload.in.buf = (u8 *)&rpccall;
	payload.in.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.in.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.in.offset = (u32)NV_PMU_CLK_CMD_RPC_ALLOC_OFFSET;

	payload.out.buf = (u8 *)&rpccall;
	payload.out.size = (u32)sizeof(struct nv_pmu_clk_rpc);
	payload.out.fb_size = PMU_CMD_SUBMIT_PAYLOAD_PARAMS_FB_SIZE_UNUSED;
	nvgpu_assert(NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET < U64(U32_MAX));
	payload.out.offset = (u32)NV_PMU_CLK_MSG_RPC_ALLOC_OFFSET;

	handler.prpccall = &rpccall;
	handler.success = 0;
	status = nvgpu_pmu_cmd_post(g, &cmd, &payload,
			PMU_COMMAND_QUEUE_LPQ,
			nvgpu_clk_vin_rpc_pmucmdhandler, (void *)&handler);

	if (status != 0) {
		nvgpu_err(g, "unable to post clk RPC cmd %x",
			cmd.cmd.clk.cmd_type);
		goto done;
	}

	pmu_wait_message_cond(&g->pmu, nvgpu_get_poll_timeout(g),
			&handler.success, 1);

	if (handler.success == 0U) {
		nvgpu_err(g, "rpc call to load vin cal failed");
		status = -EINVAL;
	}

done:
	return status;
}

int nvgpu_clk_vin_init_pmupstate(struct gk20a *g)
{
	/* If already allocated, do not re-allocate */
	if (g->clk_pmu->avfs_vinobjs != NULL) {
		return 0;
	}

	g->clk_pmu->avfs_vinobjs = nvgpu_kzalloc(g,
			sizeof(*g->clk_pmu->avfs_vinobjs));
	if (g->clk_pmu->avfs_vinobjs == NULL) {
		return -ENOMEM;
	}

	g->clk_pmu->clk_get_vin = clk_get_vin_from_index;

	return 0;
}

void nvgpu_clk_vin_free_pmupstate(struct gk20a *g)
{
	nvgpu_kfree(g, g->clk_pmu->avfs_vinobjs);
	g->clk_pmu->avfs_vinobjs = NULL;
}
