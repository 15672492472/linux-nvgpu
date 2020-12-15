/*
 * GK20A Graphics
 *
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
#include <nvgpu/kmem.h>
#include <nvgpu/allocator.h>
#include <nvgpu/timers.h>
#include <nvgpu/soc.h>
#include <nvgpu/enabled.h>
#include <nvgpu/gmmu.h>
#include <nvgpu/vidmem.h>
#include <nvgpu/soc.h>
#include <nvgpu/mc.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel_sync.h>
#include <nvgpu/nvgpu_init.h>

#ifdef CONFIG_NVGPU_TRACE
#include <trace/events/gk20a.h>
#endif
#ifdef CONFIG_NVGPU_LS_PMU
#include <nvgpu/pmu/pmu_pstate.h>
#endif

bool is_nvgpu_gpu_state_valid(struct gk20a *g)
{
	u32 boot_0 = nvgpu_mc_boot_0(g, NULL, NULL, NULL);

	if (boot_0 == 0xffffffffU) {
		nvgpu_err(g, "GPU has disappeared from bus!!");
		return false;
	}
	return true;
}

void nvgpu_check_gpu_state(struct gk20a *g)
{
	if (!is_nvgpu_gpu_state_valid(g)) {
		nvgpu_err(g, "Rebooting system!!");
		nvgpu_kernel_restart(NULL);
	}
}

static void gk20a_mask_interrupts(struct gk20a *g)
{
	if (g->ops.mc.intr_mask != NULL) {
		g->ops.mc.intr_mask(g);
	}

	if (g->ops.mc.log_pending_intrs != NULL) {
		g->ops.mc.log_pending_intrs(g);
	}
}

#ifndef CONFIG_NVGPU_RECOVERY
static int nvgpu_sw_quiesce_thread(void *data)
{
	struct gk20a *g = data;
	int err = 0;

	/* wait until all SW quiesce is requested */
	NVGPU_COND_WAIT(&g->sw_quiesce_cond,
		g->sw_quiesce_pending ||
		nvgpu_thread_should_stop(&g->sw_quiesce_thread), 0U);

	if (nvgpu_thread_should_stop(&g->sw_quiesce_thread)) {
		goto done;
	}

	nvgpu_err(g, "sw quiesce in progress");

	nvgpu_mutex_acquire(&g->power_lock);

	if (!g->power_on || g->is_virtual) {
		err = -EINVAL;
		goto idle;
	}

	nvgpu_start_gpu_idle(g);
	nvgpu_disable_irqs(g);
	gk20a_mask_interrupts(g);
	nvgpu_fifo_sw_quiesce(g);

idle:
	nvgpu_mutex_release(&g->power_lock);
	nvgpu_err(g, "sw quiesce done, err=%d", err);

done:
	nvgpu_log_info(g, "done");
	return err;
}

static int nvgpu_sw_quiesce_init_support(struct gk20a *g)
{
	int err;

	nvgpu_cond_init(&g->sw_quiesce_cond);
	g->sw_quiesce_pending = false;

	err = nvgpu_thread_create(&g->sw_quiesce_thread, g,
			nvgpu_sw_quiesce_thread, "sw-quiesce");
	if (err != 0) {
		return err;
	}

	return 0;
}

static void nvgpu_sw_quiesce_remove_support(struct gk20a *g)
{
	nvgpu_thread_stop(&g->sw_quiesce_thread);
	nvgpu_cond_destroy(&g->sw_quiesce_cond);
}
#endif

void nvgpu_sw_quiesce(struct gk20a *g)
{
#ifndef CONFIG_NVGPU_RECOVERY
	if (g->is_virtual) {
		goto fail;
	}

	nvgpu_err(g, "SW quiesce requested");

	/*
	 * When this flag is set, interrupt handlers should
	 * exit after masking interrupts. This should mitigate
	 * interrupt storm cases.
	 */
	g->sw_quiesce_pending = true;

	nvgpu_cond_signal(&g->sw_quiesce_cond);
	return;

fail:
#endif
	nvgpu_err(g, "sw quiesce not supported");
}

int nvgpu_prepare_poweroff(struct gk20a *g)
{
	int tmp_ret, ret = 0;

	nvgpu_log_fn(g, " ");

	if (g->ops.channel.suspend_all_serviceable_ch != NULL) {
		ret = g->ops.channel.suspend_all_serviceable_ch(g);
		if (ret != 0) {
			return ret;
		}
	}

#ifdef CONFIG_NVGPU_LS_PMU
	/* disable elpg before gr or fifo suspend */
	if (g->support_ls_pmu) {
		ret = g->ops.pmu.pmu_destroy(g, g->pmu);
	}
#endif

#ifdef CONFIG_NVGPU_DGPU
	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_SEC2_RTOS)) {
		tmp_ret = g->ops.sec2.sec2_destroy(g);
		if ((tmp_ret != 0) && (ret == 0)) {
			ret = tmp_ret;
		}
	}
#endif
	tmp_ret = g->ops.gr.gr_suspend(g);
	if ((tmp_ret != 0) && (ret == 0)) {
		ret = tmp_ret;
	}
	tmp_ret = g->ops.mm.mm_suspend(g);
	if ((tmp_ret != 0) && (ret == 0)) {
		ret = tmp_ret;
	}
	tmp_ret = g->ops.fifo.fifo_suspend(g);
	if ((tmp_ret != 0) && (ret == 0)) {
		ret = tmp_ret;
	}

	g->ops.falcon.falcon_sw_free(g, FALCON_ID_PMU);
	g->ops.falcon.falcon_sw_free(g, FALCON_ID_FECS);

#ifdef CONFIG_NVGPU_DGPU
	g->ops.falcon.falcon_sw_free(g, FALCON_ID_GSPLITE);
	g->ops.falcon.falcon_sw_free(g, FALCON_ID_NVDEC);
	g->ops.falcon.falcon_sw_free(g, FALCON_ID_SEC2);

	g->ops.ce.ce_app_suspend(g);
#endif

#ifdef CONFIG_NVGPU_DGPU
	if (g->ops.bios.bios_sw_deinit != NULL) {
		/* deinit the bios */
		g->ops.bios.bios_sw_deinit(g, g->bios);
	}
#endif

	/* Disable GPCPLL */
	if (g->ops.clk.suspend_clk_support != NULL) {
		g->ops.clk.suspend_clk_support(g);
	}
#ifdef CONFIG_NVGPU_CLK_ARB
	if (g->ops.clk_arb.stop_clk_arb_threads != NULL) {
		g->ops.clk_arb.stop_clk_arb_threads(g);
	}
#endif
	gk20a_mask_interrupts(g);

	g->power_on = false;

	return ret;
}

int nvgpu_finalize_poweron(struct gk20a *g)
{
	int err = 0;
	u32 fuse_status;
#if defined(CONFIG_TEGRA_GK20A_NVHOST)
	u64 nr_pages;
#endif

	nvgpu_log_fn(g, " ");

	if (g->power_on) {
		return 0;
	}

	g->power_on = true;

#ifdef CONFIG_NVGPU_RECOVERY
	nvgpu_set_enabled(g, NVGPU_SUPPORT_FAULT_RECOVERY, true);
#else
	nvgpu_set_enabled(g, NVGPU_SUPPORT_FAULT_RECOVERY, false);
	err = nvgpu_sw_quiesce_init_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init sw-quiesce support");
		goto done;
	}
#endif

#ifdef CONFIG_NVGPU_DGPU
	/*
	 * Before probing the GPU make sure the GPU's state is cleared. This is
	 * relevant for rebind operations.
	 */
	if ((g->ops.xve.reset_gpu != NULL) && !g->gpu_reset_done) {
		g->ops.xve.reset_gpu(g);
		g->gpu_reset_done = true;
	}
#endif

	/*
	 * Do this early so any early VMs that get made are capable of mapping
	 * buffers.
	 */
	err = g->ops.mm.pd_cache_init(g);
	if (err != 0) {
		return err;
	}

	/* init interface layer support for PMU falcon */
	err = g->ops.falcon.falcon_sw_init(g, FALCON_ID_PMU);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_PMU");
		goto exit;
	}

	err = g->ops.falcon.falcon_sw_init(g, FALCON_ID_FECS);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_FECS");
		goto done_pmu;
	}

#ifdef CONFIG_NVGPU_DGPU
	err = g->ops.falcon.falcon_sw_init(g, FALCON_ID_SEC2);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_SEC2");
		goto done_fecs;
	}

	err = g->ops.falcon.falcon_sw_init(g, FALCON_ID_NVDEC);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_NVDEC");
		goto done_sec2;
	}

	err = g->ops.falcon.falcon_sw_init(g, FALCON_ID_GSPLITE);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_GSPLITE");
		goto done_nvdec;
	}
#endif

	err = g->ops.pmu.pmu_early_init(g, &g->pmu);
	if (err != 0) {
		nvgpu_err(g, "failed to early init pmu sw");
		goto done;
	}

#ifdef CONFIG_NVGPU_DGPU
	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_SEC2_RTOS)) {
		err = g->ops.sec2.init_sec2_setup_sw(g, &g->sec2);
		if (err != 0) {
			nvgpu_err(g, "failed to init sec2 sw setup");
			goto done;
		}
	}
#endif
	if (nvgpu_is_enabled(g, NVGPU_SEC_PRIVSECURITY)) {
		/* Init chip specific ACR properties */
		err = g->ops.acr.acr_init(g, &g->acr);
		if (err != 0) {
			nvgpu_err(g, "ACR init failed %d", err);
			goto done;
		}
	}

#ifdef CONFIG_NVGPU_DGPU
	err = g->ops.bios.bios_sw_init(g, &g->bios);
	if (err != 0) {
		nvgpu_err(g, "BIOS SW init failed %d", err);
		goto done;
	}
#endif
	g->ops.bus.init_hw(g);

	if (g->ops.clk.disable_slowboot != NULL) {
		g->ops.clk.disable_slowboot(g);
	}

	g->ops.priv_ring.enable_priv_ring(g);

	/* TBD: move this after graphics init in which blcg/slcg is enabled.
	   This function removes SlowdownOnBoot which applies 32x divider
	   on gpcpll bypass path. The purpose of slowdown is to save power
	   during boot but it also significantly slows down gk20a init on
	   simulation and emulation. We should remove SOB after graphics power
	   saving features (blcg/slcg) are enabled. For now, do it here. */
	if (g->ops.clk.init_clk_support != NULL) {
		err = g->ops.clk.init_clk_support(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init gk20a clk");
			goto done;
		}
	}

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_NVLINK)) {
		err = g->ops.nvlink.init(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init nvlink");
			goto done;
		}
	}

	if (g->ops.fb.init_fbpa != NULL) {
		err = g->ops.fb.init_fbpa(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init fbpa");
			goto done;
		}
	}

	if (g->ops.fb.mem_unlock != NULL && !g->is_fusa_sku) {
		err = g->ops.fb.mem_unlock(g);
		if (err != 0) {
			nvgpu_err(g, "failed to unlock memory");
			goto done;
		}
	}

	err = g->ops.fifo.reset_enable_hw(g);

	if (err != 0) {
		nvgpu_err(g, "failed to reset gk20a fifo");
		goto done;
	}

	err = g->ops.ltc.init_ltc_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init ltc");
		goto done;
	}

	err = g->ops.mm.init_mm_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a mm");
		goto done;
	}

	err = g->ops.fifo.fifo_init_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a fifo");
		goto done;
	}

	if (g->ops.therm.elcg_init_idle_filters != NULL) {
		err = g->ops.therm.elcg_init_idle_filters(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init elcg idle filters");
			goto done;
		}
	}

	g->ops.mc.intr_enable(g);

	/*
	 *  Power gate the chip as per the TPC PG mask
	 *  and the fuse_status register.
	 *  If TPC PG mask is invalid halt the GPU poweron.
	 */
	g->can_tpc_powergate = false;
	fuse_status = g->ops.fuse.fuse_status_opt_tpc_gpc(g, 0);

	if (g->ops.tpc.tpc_powergate) {
		err = g->ops.tpc.tpc_powergate(g, fuse_status);
	}

	if (err) {
		nvgpu_err(g, "failed to power ON GPU");
		goto done;
	}

	nvgpu_mutex_acquire(&g->tpc_pg_lock);

#ifdef CONFIG_NVGPU_DEBUGGER
	if (g->can_tpc_powergate) {
		if (g->ops.gr.powergate_tpc != NULL) {
			g->ops.gr.powergate_tpc(g);
		}
	}
#endif

	/* prepare portion of sw required for enable hw */
	err = g->ops.gr.gr_prepare_sw(g);
	if (err != 0) {
		nvgpu_err(g, "failed to prepare sw");
		nvgpu_mutex_release(&g->tpc_pg_lock);
		goto done;
	}

	err = g->ops.gr.gr_enable_hw(g);
	if (err != 0) {
		nvgpu_err(g, "failed to enable gr");
		nvgpu_mutex_release(&g->tpc_pg_lock);
		goto done;
	}

	if (nvgpu_is_enabled(g, NVGPU_SEC_PRIVSECURITY)) {
		/* construct ucode blob, load & bootstrap LSF's using HS ACR */
		err = g->ops.acr.acr_construct_execute(g, g->acr);
		if (err != 0) {
			nvgpu_mutex_release(&g->tpc_pg_lock);
			goto done;
		}
	}

#ifdef CONFIG_NVGPU_DGPU
	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_SEC2_RTOS)) {
		err = g->ops.sec2.init_sec2_support(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init sec2");
			nvgpu_mutex_release(&g->tpc_pg_lock);
			goto done;
		}
	}
#endif

#ifdef CONFIG_NVGPU_LS_PMU
	err = g->ops.pmu.pmu_init(g, g->pmu);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a pmu");
		nvgpu_mutex_release(&g->tpc_pg_lock);
		goto done;
	}
#endif

	err = g->ops.fbp.fbp_init_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a fbp");
		nvgpu_mutex_release(&g->tpc_pg_lock);
		goto done;
	}

	err = g->ops.gr.gr_init_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a gr");
		nvgpu_mutex_release(&g->tpc_pg_lock);
		goto done;
	}

	if (g->ops.gr.ecc.ecc_init_support != NULL) {
		err = g->ops.gr.ecc.ecc_init_support(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init ecc");
			nvgpu_mutex_release(&g->tpc_pg_lock);
			goto done;
		}
	}

	nvgpu_mutex_release(&g->tpc_pg_lock);

#ifdef CONFIG_NVGPU_LS_PMU
	if (nvgpu_is_enabled(g, NVGPU_PMU_PSTATE)) {
		err = g->ops.pmu.pmu_pstate_sw_setup(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init pstates");
			nvgpu_mutex_release(&g->tpc_pg_lock);
			goto done;
		}

		err = g->ops.pmu.pmu_pstate_pmu_setup(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init pstates");
			goto done;
		}
	}

	if (nvgpu_is_enabled(g, NVGPU_PMU_PSTATE) &&
		(g->pmu->fw->ops.clk.clk_set_boot_clk != NULL)) {
		err = g->pmu->fw->ops.clk.clk_set_boot_clk(g);
		if (err != 0) {
			nvgpu_err(g, "failed to set boot clk");
			goto done;
		}
	} else
#endif
	{
#ifdef CONFIG_NVGPU_CLK_ARB
		err = g->ops.clk_arb.clk_arb_init_arbiter(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init clk arb");
			goto done;
		}
#endif
	}

	err = g->ops.therm.init_therm_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a therm");
		goto done;
	}

#ifdef CONFIG_NVGPU_COMPRESSION
	err = g->ops.cbc.cbc_init_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init cbc");
		goto done;
	}
#endif

	g->ops.chip_init_gpu_characteristics(g);

#ifdef CONFIG_NVGPU_DEBUGGER
	/* Restore the debug setting */
	g->ops.fb.set_debug_mode(g, g->mmu_debug_ctrl);
#endif

	err = g->ops.ce.ce_init_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init ce");
		goto done;
	}

#ifdef CONFIG_NVGPU_DGPU
	err = g->ops.ce.ce_app_init_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init ce app");
		goto done;
	}

	if (g->ops.xve.available_speeds != NULL) {
		u32 speed;

		if (!nvgpu_is_enabled(g, NVGPU_SUPPORT_ASPM) &&
				(g->ops.xve.disable_aspm != NULL)) {
			g->ops.xve.disable_aspm(g);
		}

		g->ops.xve.available_speeds(g, &speed);

		/* Set to max speed */
		speed = (u32)nvgpu_fls(speed);

		if (speed > 0U) {
			speed = BIT32((speed - 1U));
		} else {
			speed = BIT32(speed);
		}

		err = g->ops.xve.set_speed(g, speed);
		if (err != 0) {
			nvgpu_err(g, "Failed to set PCIe bus speed!");
			goto done;
		}
	}
#endif

#if defined(CONFIG_TEGRA_GK20A_NVHOST)
	if (nvgpu_has_syncpoints(g) && (g->syncpt_unit_size != 0UL)) {
		if (!nvgpu_mem_is_valid(&g->syncpt_mem)) {
			nr_pages = U64(DIV_ROUND_UP(g->syncpt_unit_size,
						    PAGE_SIZE));
			err = nvgpu_mem_create_from_phys(g, &g->syncpt_mem,
					g->syncpt_unit_base, nr_pages);
			if (err != 0) {
				nvgpu_err(g, "Failed to create syncpt mem");
				goto done;
			}
		}
	}
#endif

	if (g->ops.channel.resume_all_serviceable_ch != NULL) {
		g->ops.channel.resume_all_serviceable_ch(g);
	}

	goto exit;

done:
#ifdef CONFIG_NVGPU_DGPU
	g->ops.falcon.falcon_sw_free(g, FALCON_ID_GSPLITE);
done_nvdec:
	g->ops.falcon.falcon_sw_free(g, FALCON_ID_NVDEC);
done_sec2:
	g->ops.falcon.falcon_sw_free(g, FALCON_ID_SEC2);
done_fecs:
#endif
	g->ops.falcon.falcon_sw_free(g, FALCON_ID_FECS);
done_pmu:
	g->ops.falcon.falcon_sw_free(g, FALCON_ID_PMU);
exit:
	if (err != 0) {
		g->power_on = false;
	}

	return err;
}

/*
 * Check if the device can go busy. Basically if the driver is currently
 * in the process of dying then do not let new places make the driver busy.
 */
int nvgpu_can_busy(struct gk20a *g)
{
	/* Can't do anything if the system is rebooting/shutting down
	 * or the driver is restarting
	 */
	if (nvgpu_is_enabled(g, NVGPU_KERNEL_IS_DYING) ||
		nvgpu_is_enabled(g, NVGPU_DRIVER_IS_DYING)) {
		return 0;
	} else {
		return 1;
	}
}

void nvgpu_init_gpu_characteristics(struct gk20a *g)
{
#ifdef NV_BUILD_CONFIGURATION_IS_SAFETY
	nvgpu_set_enabled(g, NVGPU_DRIVER_REDUCED_PROFILE, true);
#endif
	nvgpu_set_enabled(g, NVGPU_SUPPORT_MAP_DIRECT_KIND_CTRL, true);
	nvgpu_set_enabled(g, NVGPU_SUPPORT_MAP_BUFFER_BATCH, true);
	nvgpu_set_enabled(g, NVGPU_SUPPORT_SPARSE_ALLOCS, true);

	/*
	 * Fast submits are supported as long as the user doesn't request
	 * anything that depends on job tracking. (Here, fast means strictly no
	 * metadata, just the gpfifo contents are copied and gp_put updated).
	 */
	nvgpu_set_enabled(g,
			NVGPU_SUPPORT_DETERMINISTIC_SUBMIT_NO_JOBTRACKING,
			true);

	/*
	 * Sync framework requires deferred job cleanup, wrapping syncs in FDs,
	 * and other heavy stuff, which prevents deterministic submits. This is
	 * supported otherwise, provided that the user doesn't request anything
	 * that depends on deferred cleanup.
	 */
	if (!nvgpu_channel_sync_needs_os_fence_framework(g)) {
		nvgpu_set_enabled(g,
				NVGPU_SUPPORT_DETERMINISTIC_SUBMIT_FULL,
				true);
	}

	nvgpu_set_enabled(g, NVGPU_SUPPORT_TSG, true);

#ifdef CONFIG_NVGPU_CLK_ARB
	if (g->ops.clk_arb.check_clk_arb_support != NULL) {
		if (g->ops.clk_arb.check_clk_arb_support(g)) {
			nvgpu_set_enabled(g, NVGPU_SUPPORT_CLOCK_CONTROLS,
					true);
		}
	}
#endif

	g->ops.gr.init.detect_sm_arch(g);

#ifdef CONFIG_NVGPU_CYCLESTATS
	if (g->ops.gr.init_cyclestats != NULL) {
		g->ops.gr.init_cyclestats(g);
	}
#endif
}

static struct gk20a *gk20a_from_refcount(struct nvgpu_ref *refcount)
{
	return (struct gk20a *)((uintptr_t)refcount -
				offsetof(struct gk20a, refcount));
}

/*
 * Free the gk20a struct.
 */
static void gk20a_free_cb(struct nvgpu_ref *refcount)
{
	struct gk20a *g = gk20a_from_refcount(refcount);

	nvgpu_log(g, gpu_dbg_shutdown, "Freeing GK20A struct!");

#ifdef CONFIG_NVGPU_DGPU
	if (g->ops.ce.ce_app_destroy != NULL) {
		g->ops.ce.ce_app_destroy(g);
	}
#endif

#ifdef CONFIG_NVGPU_COMPRESSION
	if (g->ops.cbc.cbc_remove_support != NULL) {
		g->ops.cbc.cbc_remove_support(g);
	}
#endif

	if (g->ops.gr.ecc.ecc_remove_support != NULL) {
		g->ops.gr.ecc.ecc_remove_support(g);
	}

	if (g->remove_support != NULL) {
		g->remove_support(g);
	}

	if (g->ops.ltc.ltc_remove_support != NULL) {
		g->ops.ltc.ltc_remove_support(g);
	}

#ifndef CONFIG_NVGPU_RECOVERY
	nvgpu_sw_quiesce_remove_support(g);
#endif

	if (g->gfree != NULL) {
		g->gfree(g);
	}
}

struct gk20a * __must_check nvgpu_get(struct gk20a *g)
{
	int success;

	/*
	 * Handle the possibility we are still freeing the gk20a struct while
	 * nvgpu_get() is called. Unlikely but plausible race condition. Ideally
	 * the code will never be in such a situation that this race is
	 * possible.
	 */
	success = nvgpu_ref_get_unless_zero(&g->refcount);

	nvgpu_log(g, gpu_dbg_shutdown, "GET: refs currently %d %s",
		nvgpu_atomic_read(&g->refcount.refcount),
			(success != 0) ? "" : "(FAILED)");

	return (success != 0) ? g : NULL;
}

void nvgpu_put(struct gk20a *g)
{
	/*
	 * Note - this is racy, two instances of this could run before the
	 * actual kref_put(0 runs, you could see something like:
	 *
	 *  ... PUT: refs currently 2
	 *  ... PUT: refs currently 2
	 *  ... Freeing GK20A struct!
	 */
	nvgpu_log(g, gpu_dbg_shutdown, "PUT: refs currently %d",
		nvgpu_atomic_read(&g->refcount.refcount));

	nvgpu_ref_put(&g->refcount, gk20a_free_cb);
}
