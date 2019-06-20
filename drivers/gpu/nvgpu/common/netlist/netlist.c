/*
 * Copyright (c) 2011-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/nvgpu_common.h>
#include <nvgpu/sim.h>
#include <nvgpu/kmem.h>
#include <nvgpu/log.h>
#include <nvgpu/firmware.h>
#include <nvgpu/enabled.h>
#include <nvgpu/io.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/netlist.h>
#include <nvgpu/string.h>

#include "netlist_priv.h"
#include "netlist_defs.h"

/*
 * Need to support multiple ARCH in same GPU family
 * then need to provide path like ARCH/NETIMAGE to
 * point to correct netimage within GPU family,
 * Example, gm20x can support gm204 or gm206,so path
 * for netimage is gm204/NETC_img.bin, and '/' char
 * will inserted at null terminator char of "GAxxx"
 * to get complete path like gm204/NETC_img.bin
 */

#define MAX_NETLIST_NAME (sizeof("GAxxx/") + sizeof("NET?_img.bin"))

struct netlist_av *nvgpu_netlist_alloc_av_list(struct gk20a *g,
						struct netlist_av_list *avl)
{
	avl->l = nvgpu_kzalloc(g, avl->count * sizeof(*avl->l));
	return avl->l;
}

struct netlist_av64 *nvgpu_netlist_alloc_av64_list(struct gk20a *g,
						struct netlist_av64_list *avl)
{
	avl->l = nvgpu_kzalloc(g, avl->count * sizeof(*avl->l));
	return avl->l;
}

struct netlist_aiv *nvgpu_netlist_alloc_aiv_list(struct gk20a *g,
				       struct netlist_aiv_list *aivl)
{
	aivl->l = nvgpu_kzalloc(g, aivl->count * sizeof(*aivl->l));
	return aivl->l;
}

u32 *nvgpu_netlist_alloc_u32_list(struct gk20a *g,
					struct netlist_u32_list *u32l)
{
	u32l->l = nvgpu_kzalloc(g, u32l->count * sizeof(*u32l->l));
	return u32l->l;
}

static int nvgpu_netlist_alloc_load_u32_list(struct gk20a *g, u8 *src, u32 len,
			struct netlist_u32_list *u32_list)
{
	u32_list->count = (len + U32(sizeof(u32)) - 1U) / U32(sizeof(u32));
	if (nvgpu_netlist_alloc_u32_list(g, u32_list) == NULL) {
		return -ENOMEM;
	}

	nvgpu_memcpy((u8 *)u32_list->l, src, len);

	return 0;
}

static int nvgpu_netlist_alloc_load_av_list(struct gk20a *g, u8 *src, u32 len,
			struct netlist_av_list *av_list)
{
	av_list->count = len / U32(sizeof(struct netlist_av));
	if (nvgpu_netlist_alloc_av_list(g, av_list) == NULL) {
		return -ENOMEM;
	}

	nvgpu_memcpy((u8 *)av_list->l, src, len);

	return 0;
}

static int nvgpu_netlist_alloc_load_av_list64(struct gk20a *g, u8 *src, u32 len,
			struct netlist_av64_list *av64_list)
{
	av64_list->count = len / U32(sizeof(struct netlist_av64));
	if (nvgpu_netlist_alloc_av64_list(g, av64_list) == NULL) {
		return -ENOMEM;
	}

	nvgpu_memcpy((u8 *)av64_list->l, src, len);

	return 0;
}

static int nvgpu_netlist_alloc_load_aiv_list(struct gk20a *g, u8 *src, u32 len,
			struct netlist_aiv_list *aiv_list)
{
	aiv_list->count = len / U32(sizeof(struct netlist_aiv));
	if (nvgpu_netlist_alloc_aiv_list(g, aiv_list) == NULL) {
		return -ENOMEM;
	}

	nvgpu_memcpy((u8 *)aiv_list->l, src, len);

	return 0;
}

static int nvgpu_netlist_init_ctx_vars_fw(struct gk20a *g)
{
	struct nvgpu_netlist_vars *netlist_vars = g->netlist_vars;
	struct nvgpu_firmware *netlist_fw;
	struct netlist_image *netlist = NULL;
	char name[MAX_NETLIST_NAME];
	u32 i, major_v = ~U32(0U), major_v_hw, netlist_num;
	int net, max_netlist_num, err = -ENOENT;

	nvgpu_log_fn(g, " ");

	if (g->ops.netlist.is_fw_defined()) {
		net = NETLIST_FINAL;
		max_netlist_num = 0;
		major_v_hw = ~U32(0U);
		netlist_vars->dynamic = false;
	} else {
		net = NETLIST_SLOT_A;
		max_netlist_num = MAX_NETLIST;
		major_v_hw =
		g->ops.gr.falcon.get_fecs_ctx_state_store_major_rev_id(g);
		netlist_vars->dynamic = true;
	}

	for (; net < max_netlist_num; net++) {
		if (g->ops.netlist.get_netlist_name(g, net, name) != 0) {
			nvgpu_warn(g, "invalid netlist index %d", net);
			continue;
		}

		netlist_fw = nvgpu_request_firmware(g, name, 0);
		if (netlist_fw == NULL) {
			nvgpu_warn(g, "failed to load netlist %s", name);
			continue;
		}

		netlist = (struct netlist_image *)(uintptr_t)netlist_fw->data;

		for (i = 0; i < netlist->header.regions; i++) {
			u8 *src = ((u8 *)netlist + netlist->regions[i].data_offset);
			u32 size = netlist->regions[i].data_size;

			switch (netlist->regions[i].region_id) {
			case NETLIST_REGIONID_FECS_UCODE_DATA:
				nvgpu_log_info(g, "NETLIST_REGIONID_FECS_UCODE_DATA");
				err = nvgpu_netlist_alloc_load_u32_list(g,
					src, size, &netlist_vars->ucode.fecs.data);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_FECS_UCODE_INST:
				nvgpu_log_info(g, "NETLIST_REGIONID_FECS_UCODE_INST");
				err = nvgpu_netlist_alloc_load_u32_list(g,
					src, size, &netlist_vars->ucode.fecs.inst);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_GPCCS_UCODE_DATA:
				nvgpu_log_info(g, "NETLIST_REGIONID_GPCCS_UCODE_DATA");
				err = nvgpu_netlist_alloc_load_u32_list(g,
					src, size, &netlist_vars->ucode.gpccs.data);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_GPCCS_UCODE_INST:
				nvgpu_log_info(g, "NETLIST_REGIONID_GPCCS_UCODE_INST");
				err = nvgpu_netlist_alloc_load_u32_list(g,
					src, size, &netlist_vars->ucode.gpccs.inst);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_SW_BUNDLE_INIT:
				nvgpu_log_info(g, "NETLIST_REGIONID_SW_BUNDLE_INIT");
				err = nvgpu_netlist_alloc_load_av_list(g,
					src, size, &netlist_vars->sw_bundle_init);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_SW_METHOD_INIT:
				nvgpu_log_info(g, "NETLIST_REGIONID_SW_METHOD_INIT");
				err = nvgpu_netlist_alloc_load_av_list(g,
					src, size, &netlist_vars->sw_method_init);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_SW_CTX_LOAD:
				nvgpu_log_info(g, "NETLIST_REGIONID_SW_CTX_LOAD");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->sw_ctx_load);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_SW_NON_CTX_LOAD:
				nvgpu_log_info(g, "NETLIST_REGIONID_SW_NON_CTX_LOAD");
				err = nvgpu_netlist_alloc_load_av_list(g,
					src, size, &netlist_vars->sw_non_ctx_load);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_SWVEIDBUNDLEINIT:
				nvgpu_log_info(g,
					"NETLIST_REGIONID_SW_VEID_BUNDLE_INIT");
				err = nvgpu_netlist_alloc_load_av_list(g,
					src, size,
					&netlist_vars->sw_veid_bundle_init);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_SW_BUNDLE64_INIT:
				nvgpu_log_info(g, "NETLIST_REGIONID_SW_BUNDLE64_INIT");
				err = nvgpu_netlist_alloc_load_av_list64(g,
					src, size,
					&netlist_vars->sw_bundle64_init);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_BUFFER_SIZE:
				netlist_vars->buffer_size = *src;
				nvgpu_log_info(g, "NETLIST_REGIONID_BUFFER_SIZE : %d",
					netlist_vars->buffer_size);
				break;
			case NETLIST_REGIONID_CTXSW_REG_BASE_INDEX:
				netlist_vars->regs_base_index = *src;
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXSW_REG_BASE_INDEX : %u",
					netlist_vars->regs_base_index);
				break;
			case NETLIST_REGIONID_MAJORV:
				major_v = *src;
				nvgpu_log_info(g, "NETLIST_REGIONID_MAJORV : %d",
					major_v);
				break;
			case NETLIST_REGIONID_NETLIST_NUM:
				netlist_num = *src;
				nvgpu_log_info(g, "NETLIST_REGIONID_NETLIST_NUM : %d",
					netlist_num);
				break;
#ifdef CONFIG_NVGPU_DEBUGGER
			case NETLIST_REGIONID_CTXREG_SYS:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_SYS");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.sys);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_GPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_GPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.gpc);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_TPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_TPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.tpc);
				if (err != 0) {
					goto clean_up;
				}
				break;
#ifdef CONFIG_NVGPU_GRAPHICS
			case NETLIST_REGIONID_CTXREG_ZCULL_GPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_ZCULL_GPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.zcull_gpc);
				if (err != 0) {
					goto clean_up;
				}
				break;
#endif
			case NETLIST_REGIONID_CTXREG_PPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_PPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.ppc);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_PM_SYS:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_PM_SYS");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.pm_sys);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_PM_GPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_PM_GPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.pm_gpc);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_PM_TPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_PM_TPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.pm_tpc);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_PMPPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_PMPPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.pm_ppc);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_NVPERF_CTXREG_SYS:
				nvgpu_log_info(g, "NETLIST_REGIONID_NVPERF_CTXREG_SYS");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.perf_sys);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_NVPERF_FBP_CTXREGS:
				nvgpu_log_info(g, "NETLIST_REGIONID_NVPERF_FBP_CTXREGS");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.fbp);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_NVPERF_CTXREG_GPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_NVPERF_CTXREG_GPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.perf_gpc);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_NVPERF_FBP_ROUTER:
				nvgpu_log_info(g, "NETLIST_REGIONID_NVPERF_FBP_ROUTER");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.fbp_router);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_NVPERF_GPC_ROUTER:
				nvgpu_log_info(g, "NETLIST_REGIONID_NVPERF_GPC_ROUTER");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.gpc_router);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_PMLTC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_PMLTC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.pm_ltc);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_PMFBPA:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_PMFBPA");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.pm_fbpa);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_NVPERF_SYS_ROUTER:
				nvgpu_log_info(g, "NETLIST_REGIONID_NVPERF_SYS_ROUTER");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.perf_sys_router);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_NVPERF_PMA:
				nvgpu_log_info(g, "NETLIST_REGIONID_NVPERF_PMA");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.perf_pma);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_PMROP:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_PMROP");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.pm_rop);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_PMUCGPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_PMUCGPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.pm_ucgpc);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_CTXREG_ETPC:
				nvgpu_log_info(g, "NETLIST_REGIONID_CTXREG_ETPC");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size, &netlist_vars->ctxsw_regs.etpc);
				if (err != 0) {
					goto clean_up;
				}
				break;
			case NETLIST_REGIONID_NVPERF_PMCAU:
				nvgpu_log_info(g, "NETLIST_REGIONID_NVPERF_PMCAU");
				err = nvgpu_netlist_alloc_load_aiv_list(g,
					src, size,
					&netlist_vars->ctxsw_regs.pm_cau);
				if (err != 0) {
					goto clean_up;
				}
				break;
#endif /* CONFIG_NVGPU_DEBUGGER */

			default:
				nvgpu_log_info(g, "unrecognized region %d skipped", i);
				break;
			}
		}

		if (net != NETLIST_FINAL && major_v != major_v_hw) {
			nvgpu_log_info(g, "skip %s: major_v 0x%08x doesn't match hw 0x%08x",
				name, major_v, major_v_hw);
			goto clean_up;
		}

		g->netlist_valid = true;

		nvgpu_release_firmware(g, netlist_fw);
		nvgpu_log_fn(g, "done");
		goto done;

clean_up:
		g->netlist_valid = false;
		nvgpu_kfree(g, netlist_vars->ucode.fecs.inst.l);
		nvgpu_kfree(g, netlist_vars->ucode.fecs.data.l);
		nvgpu_kfree(g, netlist_vars->ucode.gpccs.inst.l);
		nvgpu_kfree(g, netlist_vars->ucode.gpccs.data.l);
		nvgpu_kfree(g, netlist_vars->sw_bundle_init.l);
		nvgpu_kfree(g, netlist_vars->sw_bundle64_init.l);
		nvgpu_kfree(g, netlist_vars->sw_method_init.l);
		nvgpu_kfree(g, netlist_vars->sw_ctx_load.l);
		nvgpu_kfree(g, netlist_vars->sw_non_ctx_load.l);
		nvgpu_kfree(g, netlist_vars->sw_veid_bundle_init.l);
#ifdef CONFIG_NVGPU_DEBUGGER
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.sys.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.gpc.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.tpc.l);
#ifdef CONFIG_NVGPU_GRAPHICS
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.zcull_gpc.l);
#endif
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.ppc.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_sys.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_gpc.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_tpc.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_ppc.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.perf_sys.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.fbp.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.perf_gpc.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.fbp_router.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.gpc_router.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_ltc.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_fbpa.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.perf_sys_router.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.perf_pma.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_rop.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_ucgpc.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.etpc.l);
		nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_cau.l);
#endif /* CONFIG_NVGPU_DEBUGGER */
		nvgpu_release_firmware(g, netlist_fw);
		err = -ENOENT;
	}

done:
	if (g->netlist_valid) {
		nvgpu_log_info(g, "netlist image %s loaded", name);
		return 0;
	} else {
		nvgpu_err(g, "failed to load netlist image!!");
		return err;
	}
}

int nvgpu_netlist_init_ctx_vars(struct gk20a *g)
{
	if (g->netlist_valid == true) {
		return 0;
	}

	g->netlist_vars = nvgpu_kzalloc(g, sizeof(*g->netlist_vars));
	if (g->netlist_vars == NULL) {
		return -ENOMEM;
	}

#ifdef CONFIG_NVGPU_SIM
	if (nvgpu_is_enabled(g, NVGPU_IS_FMODEL)) {
		return nvgpu_init_sim_netlist_ctx_vars(g);
	} else
#endif
	{
		return nvgpu_netlist_init_ctx_vars_fw(g);
	}
}

void nvgpu_netlist_deinit_ctx_vars(struct gk20a *g)
{
	struct nvgpu_netlist_vars *netlist_vars = g->netlist_vars;

	g->netlist_valid = false;
	nvgpu_kfree(g, netlist_vars->ucode.fecs.inst.l);
	nvgpu_kfree(g, netlist_vars->ucode.fecs.data.l);
	nvgpu_kfree(g, netlist_vars->ucode.gpccs.inst.l);
	nvgpu_kfree(g, netlist_vars->ucode.gpccs.data.l);
	nvgpu_kfree(g, netlist_vars->sw_bundle_init.l);
	nvgpu_kfree(g, netlist_vars->sw_bundle64_init.l);
	nvgpu_kfree(g, netlist_vars->sw_veid_bundle_init.l);
	nvgpu_kfree(g, netlist_vars->sw_method_init.l);
	nvgpu_kfree(g, netlist_vars->sw_ctx_load.l);
	nvgpu_kfree(g, netlist_vars->sw_non_ctx_load.l);
#ifdef CONFIG_NVGPU_DEBUGGER
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.sys.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.gpc.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.tpc.l);
#ifdef CONFIG_NVGPU_GRAPHICS
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.zcull_gpc.l);
#endif
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.ppc.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_sys.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_gpc.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_tpc.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_ppc.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.perf_sys.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.fbp.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.perf_gpc.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.fbp_router.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.gpc_router.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_ltc.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_fbpa.l);
	nvgpu_kfree(g, netlist_vars->ctxsw_regs.pm_cau.l);
#endif /* CONFIG_NVGPU_DEBUGGER */

	nvgpu_kfree(g, netlist_vars);
	g->netlist_vars = NULL;
}

struct netlist_av_list *nvgpu_netlist_get_sw_non_ctx_load_av_list(
							struct gk20a *g)
{
	return &g->netlist_vars->sw_non_ctx_load;
}

struct netlist_aiv_list *nvgpu_netlist_get_sw_ctx_load_aiv_list(
							struct gk20a *g)
{
	return &g->netlist_vars->sw_ctx_load;
}

struct netlist_av_list *nvgpu_netlist_get_sw_method_init_av_list(
							struct gk20a *g)
{
	return &g->netlist_vars->sw_method_init;
}

struct netlist_av_list *nvgpu_netlist_get_sw_bundle_init_av_list(
							struct gk20a *g)
{
	return &g->netlist_vars->sw_bundle_init;
}

struct netlist_av_list *nvgpu_netlist_get_sw_veid_bundle_init_av_list(
							struct gk20a *g)
{
	return &g->netlist_vars->sw_veid_bundle_init;
}

struct netlist_av64_list *nvgpu_netlist_get_sw_bundle64_init_av64_list(
							struct gk20a *g)
{
	return &g->netlist_vars->sw_bundle64_init;
}

u32 nvgpu_netlist_get_fecs_inst_count(struct gk20a *g)
{
	return g->netlist_vars->ucode.fecs.inst.count;
}

u32 nvgpu_netlist_get_fecs_data_count(struct gk20a *g)
{
	return g->netlist_vars->ucode.fecs.data.count;
}

u32 nvgpu_netlist_get_gpccs_inst_count(struct gk20a *g)
{
	return g->netlist_vars->ucode.gpccs.inst.count;
}

u32 nvgpu_netlist_get_gpccs_data_count(struct gk20a *g)
{
	return g->netlist_vars->ucode.gpccs.data.count;
}

void nvgpu_netlist_set_fecs_inst_count(struct gk20a *g, u32 count)
{
	g->netlist_vars->ucode.fecs.inst.count = count;
}

void nvgpu_netlist_set_fecs_data_count(struct gk20a *g, u32 count)
{
	g->netlist_vars->ucode.fecs.data.count = count;
}

void nvgpu_netlist_set_gpccs_inst_count(struct gk20a *g, u32 count)
{
	g->netlist_vars->ucode.gpccs.inst.count = count;
}

void nvgpu_netlist_set_gpccs_data_count(struct gk20a *g, u32 count)
{
	g->netlist_vars->ucode.gpccs.data.count = count;
}

u32 *nvgpu_netlist_get_fecs_inst_list(struct gk20a *g)
{
	return g->netlist_vars->ucode.fecs.inst.l;
}

u32 *nvgpu_netlist_get_fecs_data_list(struct gk20a *g)
{
	return g->netlist_vars->ucode.fecs.data.l;
}

u32 *nvgpu_netlist_get_gpccs_inst_list(struct gk20a *g)
{
	return g->netlist_vars->ucode.gpccs.inst.l;
}

u32 *nvgpu_netlist_get_gpccs_data_list(struct gk20a *g)
{
	return g->netlist_vars->ucode.gpccs.data.l;
}

struct netlist_u32_list *nvgpu_netlist_get_fecs_inst(struct gk20a *g)
{
	return &g->netlist_vars->ucode.fecs.inst;
}

struct netlist_u32_list *nvgpu_netlist_get_fecs_data(struct gk20a *g)
{
	return &g->netlist_vars->ucode.fecs.data;
}

struct netlist_u32_list *nvgpu_netlist_get_gpccs_inst(struct gk20a *g)
{
	return &g->netlist_vars->ucode.gpccs.inst;
}

struct netlist_u32_list *nvgpu_netlist_get_gpccs_data(struct gk20a *g)
{
	return &g->netlist_vars->ucode.gpccs.data;
}

#ifdef CONFIG_NVGPU_DEBUGGER
struct netlist_aiv_list *nvgpu_netlist_get_sys_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.sys;
}

struct netlist_aiv_list *nvgpu_netlist_get_gpc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.gpc;
}

struct netlist_aiv_list *nvgpu_netlist_get_tpc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.tpc;
}

#ifdef CONFIG_NVGPU_GRAPHICS
struct netlist_aiv_list *nvgpu_netlist_get_zcull_gpc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.zcull_gpc;
}
#endif

struct netlist_aiv_list *nvgpu_netlist_get_ppc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.ppc;
}

struct netlist_aiv_list *nvgpu_netlist_get_pm_sys_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.pm_sys;
}

struct netlist_aiv_list *nvgpu_netlist_get_pm_gpc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.pm_gpc;
}

struct netlist_aiv_list *nvgpu_netlist_get_pm_tpc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.pm_tpc;
}

struct netlist_aiv_list *nvgpu_netlist_get_pm_ppc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.pm_ppc;
}

struct netlist_aiv_list *nvgpu_netlist_get_perf_sys_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.perf_sys;
}

struct netlist_aiv_list *nvgpu_netlist_get_perf_gpc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.perf_gpc;
}

struct netlist_aiv_list *nvgpu_netlist_get_fbp_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.fbp;
}

struct netlist_aiv_list *nvgpu_netlist_get_fbp_router_ctxsw_regs(
							struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.fbp_router;
}

struct netlist_aiv_list *nvgpu_netlist_get_gpc_router_ctxsw_regs(
							struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.gpc_router;
}

struct netlist_aiv_list *nvgpu_netlist_get_pm_ltc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.pm_ltc;
}

struct netlist_aiv_list *nvgpu_netlist_get_pm_fbpa_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.pm_fbpa;
}

struct netlist_aiv_list *nvgpu_netlist_get_perf_sys_router_ctxsw_regs(
							struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.perf_sys_router;
}

struct netlist_aiv_list *nvgpu_netlist_get_perf_pma_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.perf_pma;
}

struct netlist_aiv_list *nvgpu_netlist_get_pm_rop_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.pm_rop;
}

struct netlist_aiv_list *nvgpu_netlist_get_pm_ucgpc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.pm_ucgpc;
}

struct netlist_aiv_list *nvgpu_netlist_get_etpc_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.etpc;
}

struct netlist_aiv_list *nvgpu_netlist_get_pm_cau_ctxsw_regs(struct gk20a *g)
{
	return &g->netlist_vars->ctxsw_regs.pm_cau;
}
#endif /* CONFIG_NVGPU_DEBUGGER */

void nvgpu_netlist_vars_set_dynamic(struct gk20a *g, bool set)
{
	g->netlist_vars->dynamic = set;
}

void nvgpu_netlist_vars_set_buffer_size(struct gk20a *g, u32 size)
{
	g->netlist_vars->buffer_size = size;
}

void nvgpu_netlist_vars_set_regs_base_index(struct gk20a *g, u32 index)
{
	g->netlist_vars->regs_base_index = index;
}
