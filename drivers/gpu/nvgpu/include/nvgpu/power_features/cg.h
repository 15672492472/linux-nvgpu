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


#ifndef NVGPU_POWER_FEATURES_CG_H
#define NVGPU_POWER_FEATURES_CG_H

/**
 * @file
 * @page unit-cg Unit Clock Gating (CG)
 *
 * Overview
 * ========
 *
 * The Clock Gating (CG) unit is responsible for programming the register
 * configuration for Second Level Clock Gating (SLCG), Block Level
 * Clock Gating (BLCG) and Engine Level Clock Gating (ELCG).
 *
 * Chip specific clock gating register configurations are available
 * in the files, hal/power_features/cg/<chip>_gating_reglist.c.
 *
 * Various domains/units in the GPU have individual clock gating
 * configuration registers that are programmed at instances during
 * nvgpu power on as given below:
 *
 * SLCG:
 *   + FB - MM init.
 *   + LTC - MM init.
 *   + PRIV RING - Enabling PRIV RING.
 *   + FIFO - FIFO init.
 *   + PMU - Programmed while resetting the PMU engine.
 *   + CE - CE init.
 *   + bus - GR init.
 *   + Chiplet - GR init.
 *   + GR - GR init.
 *   + CTXSW firmware - GR init.
 *   + PERF - GR init.
 *   + XBAR - GR init.
 *   + HSHUB - GR init.
 *
 * BLCG:
 *   + FB - MM init.
 *   + LTC - MM init.
 *   + FIFO - FIFO init.
 *   + PMU - Programmed while resetting the PMU engine.
 *   + CE - CE init.
 *   + GR - Golden context creation, GR init.
 *   + bus -  GR init.
 *   + CTXSW firmware - GR init.
 *   + XBAR - GR init.
 *   + HSHUB - GR init.
 *
 * ELCG:
 *   + Graphics - GR init.
 *   + CE - GR init.
 *
 * Static Design
 * =============
 *
 * Clock Gating Initialization
 * ---------------------------
 * During nvgpu power on, each component like GR, FIFO, CE, PMU will load the
 * SLCG and BLCG clock gating values in the registers as specified in the
 * clock gating register configurations for the corresponding chips.
 *
 * SLCG will be enabled by loading the gating registers with prod values.
 *
 * BLCG has two level control, first is to load the gating registers and
 * second is to setup the BLCG mode in the engine gate ctrl registers.
 * By default engine gate ctrl register will have BLCG_AUTO mode
 * enabled.
 *
 * ELCG will be off (ELCG_RUN) by default. nvgpu programs engine gate_ctrl
 * registers to enable ELCG (ELCG_AUTO). ELCG will be enabled during GR
 * initialization.
 *
 * External APIs
 * -------------
 *   + nvgpu_cg_init_gr_load_gating_prod()
 *   + nvgpu_cg_elcg_enable_no_wait()
 *   + nvgpu_cg_elcg_disable_no_wait()
 *   + nvgpu_cg_blcg_fb_ltc_load_enable()
 *   + nvgpu_cg_blcg_fifo_load_enable()
 *   + nvgpu_cg_blcg_pmu_load_enable()
 *   + nvgpu_cg_blcg_ce_load_enable()
 *   + nvgpu_cg_blcg_gr_load_enable()
 *   + nvgpu_cg_slcg_fb_ltc_load_enable()
 *   + nvgpu_cg_slcg_priring_load_enable()
 *   + nvgpu_cg_slcg_fifo_load_enable()
 *   + nvgpu_cg_slcg_pmu_load_enable()
 *   + nvgpu_cg_slcg_ce2_load_enable()
 *
 */

#include <nvgpu/types.h>
#include <nvgpu/bitops.h>

/**
 * Parameters for init_elcg_mode/init_blcg_mode.
 */

/** Engine level clk always running, i.e. disable elcg. */
#define ELCG_RUN	BIT32(0U)
/** Engine level clk is stopped. */
#define ELCG_STOP	BIT32(1U)
/** Engine level clk will run when non-idle, i.e. standard elcg mode. */
#define ELCG_AUTO	BIT32(2U)

/** Block level clk always running, i.e. disable blcg. */
#define BLCG_RUN	BIT32(0U)
/** Block level clk will run when non-idle, i.e. standard blcg mode. */
#define BLCG_AUTO	BIT32(1U)

/**
 * Mode to be configured in engine gate ctrl registers.
 */

/** Engine Level Clock Gating (ELCG) Mode. */
#define ELCG_MODE	BIT32(0U)
/** Block Level Clock Gating (BLCG) Mode. */
#define BLCG_MODE	BIT32(1U)
/** Invalid Mode. */
#define INVALID_MODE	BIT32(2U)

struct gk20a;
struct nvgpu_fifo;

/**
 * @brief Load register configuration for ELCG and BLCG for GR related units.
 *
 * @param g [in] The GPU driver struct.
 *
 * This function programs ELCG configuration for bus, chiplet, gr, perf,
 * ctxsw_firmware, xbar, hshub units and BLCG for bus, gr, ctxsw_firmware,
 * xbar and hshub. This is called in #nvgpu_gr_enable_hw after resetting GR
 * engine.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #slcg_enabled is set, else skip SLCG programming.
 * - Load SLCG prod settings for bus, chiplet, gr, ctxsw_firmware, perf,
 *   xbar, hshub.
 * - Check if #blcg_enabled is set, else skip BLCG programming.
 * - Load BLCG prod settings for bus, gr, ctxsw_firmware, xbar, hshub.
 * - Load GR pg prod settings.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_init_gr_load_gating_prod(struct gk20a *g);

/**
 * @brief Enable ELCG for engines without waiting for GR init to complete.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability elcg_enabled and programs the
 * engine gate_ctrl registers with ELCG_AUTO mode configuration. This is
 * called in #nvgpu_gr_init_support.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #elcg_enabled is set, else skip ELCG programming.
 * - For each engine in the FIFO get the active engine id.
 *   - Set ELCG_MODE to ELCG_AUTO for active engine id calling
 *     therm.init_elcg_mode from therm unit.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_elcg_enable_no_wait(struct gk20a *g);

/**
 * @brief Disable ELCG for engines without waiting for GR init to complete.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability elcg_enabled and programs the
 * engine gate_ctrl registers with ELCG_RUN mode configuration. This is
 * called in #nvgpu_gr_enable_hw until it gets enabled in
 * #nvgpu_gr_init_support.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #elcg_enabled is set, else skip ELCG programming.
 * - For each engine in the FIFO get the active engine id.
 *   - Set ELCG_MODE to ELCG_RUN for active engine id calling
 *     therm.init_elcg_mode from therm unit.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_elcg_disable_no_wait(struct gk20a *g);

/**
 * @brief Load register configuration for BLCG for FB and LTC.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability blcg_enabled and programs registers
 * for configuring production gating values for BLCG for FB and LTC. This is
 * called in #nvgpu_init_mm_support.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #blcg_enabled is set, else skip BLCG programming.
 * - Load BLCG prod settings for fb and ltc.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_blcg_fb_ltc_load_enable(struct gk20a *g);

/**
 * @brief Load register configuration for BLCG for FIFO.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability blcg_enabled and programs registers
 * for configuring production gating values for BLCG for FIFO. This is called
 * in fifo.reset_enable_hw from #nvgpu_finalize_poweron.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #blcg_enabled is set, else skip BLCG programming.
 * - Load BLCG prod settings for fifo.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_blcg_fifo_load_enable(struct gk20a *g);

/**
 * @brief Load register configuration for BLCG for PMU.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability blcg_enabled and programs registers
 * for configuring production gating values for BLCG for PMU. This is called
 * in #nvgpu_pmu_reset after resetting PMU engine.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #blcg_enabled is set, else skip BLCG programming.
 * - Load BLCG prod settings for PMU.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_blcg_pmu_load_enable(struct gk20a *g);

/**
 * @brief Load register configuration for BLCG for CE.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability blcg_enabled and programs registers
 * for configuring production gating values for BLCG for CE. This is called
 * in #nvgpu_ce_init_support.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #blcg_enabled is set, else skip BLCG programming.
 * - Load BLCG prod settings for CE.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_blcg_ce_load_enable(struct gk20a *g);

/**
 * @brief Load register configuration for BLCG for GR.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability blcg_enabled and programs registers
 * for configuring production gating values for BLCG for GR. This is called
 * in #nvgpu_gr_obj_ctx_alloc_golden_ctx_image.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #blcg_enabled is set, else skip BLCG programming.
 * - Load BLCG prod settings for GR.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_blcg_gr_load_enable(struct gk20a *g);

/**
 * @brief Load register configuration for SLCG for FB and LTC.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability slcg_enabled and programs registers
 * for configuring production gating values for SLCG for FB and LTC. This is
 * called in #nvgpu_init_mm_support.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #slcg_enabled is set, else skip SLCG programming.
 * - Load SLCG prod settings for fb and ltc.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_slcg_fb_ltc_load_enable(struct gk20a *g);

/**
 * @brief Load register configuration for SLCG for PRIV RING.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability slcg_enabled and programs registers
 * for configuring production gating values for SLCG for PRIV RING. This is
 * called while enabling PRIV RING in #nvgpu_finalize_poweron.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #slcg_enabled is set, else skip SLCG programming.
 * - Load SLCG prod settings for PRIV RING.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_slcg_priring_load_enable(struct gk20a *g);

/**
 * @brief Load register configuration for SLCG for FIFO.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability slcg_enabled and programs registers
 * for configuring production gating values for SLCG for FIFO. This is called
 * in fifo.reset_enable_hw from #nvgpu_finalize_poweron.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #slcg_enabled is set, else skip SLCG programming.
 * - Load SLCG prod settings for fifo.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_slcg_fifo_load_enable(struct gk20a *g);

/**
 * @brief Load register configuration for SLCG for PMU.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability slcg_enabled and programs registers
 * for configuring production gating values for SLCG for PMU. This is called
 * in #nvgpu_pmu_reset after resetting PMU engine.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #slcg_enabled is set, else skip SLCG programming.
 * - Load SLCG prod settings for PMU.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_slcg_pmu_load_enable(struct gk20a *g);

/**
 * @brief Load register configuration for SLCG for CE2.
 *
 * @param g [in] The GPU driver struct.
 *
 * Checks the platform software capability slcg_enabled and programs registers
 * for configuring production gating values for SLCG for CE2. This is called
 * in #nvgpu_ce_init_support.
 *
 * Steps:
 * - Acquire the mutex #cg_pg_lock.
 * - Check if #slcg_enabled is set, else skip SLCG programming.
 * - Load SLCG prod settings for CE2.
 * - Release the mutex #cg_pg_lock.
 */
void nvgpu_cg_slcg_ce2_load_enable(struct gk20a *g);

#ifdef CONFIG_NVGPU_NON_FUSA

void nvgpu_cg_elcg_enable(struct gk20a *g);
void nvgpu_cg_elcg_disable(struct gk20a *g);
void nvgpu_cg_elcg_set_elcg_enabled(struct gk20a *g, bool enable);

void nvgpu_cg_blcg_mode_enable(struct gk20a *g);
void nvgpu_cg_blcg_mode_disable(struct gk20a *g);
void nvgpu_cg_blcg_set_blcg_enabled(struct gk20a *g, bool enable);

void nvgpu_cg_slcg_gr_perf_ltc_load_enable(struct gk20a *g);
void nvgpu_cg_slcg_gr_perf_ltc_load_disable(struct gk20a *g);
void nvgpu_cg_slcg_set_slcg_enabled(struct gk20a *g, bool enable);

#endif
#endif /*NVGPU_POWER_FEATURES_CG_H*/
