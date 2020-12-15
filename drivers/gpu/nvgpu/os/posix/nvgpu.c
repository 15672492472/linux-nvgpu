/*
 * Copyright (c) 2018-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include <nvgpu/bug.h>
#include <nvgpu/types.h>
#include <nvgpu/atomic.h>
#include <nvgpu/nvgpu_common.h>
#include <nvgpu/nvgpu_init.h>
#include <nvgpu/os_sched.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/enabled.h>

#include <nvgpu/posix/probe.h>

#include "os_posix.h"

#ifdef NVGPU_UNITTEST_FAULT_INJECTION_ENABLEMENT
#include <nvgpu/posix/posix-fault-injection.h>
#endif

#ifdef NVGPU_UNITTEST_FAULT_INJECTION_ENABLEMENT
_Thread_local struct nvgpu_posix_fault_inj nvgpu_fi;

struct nvgpu_posix_fault_inj *nvgpu_nvgpu_get_fault_injection(void)
{
	return &nvgpu_fi;
}
#endif

/*
 * Somewhat meaningless in userspace...
 */
void nvgpu_kernel_restart(void *cmd)
{
	BUG();
}

void nvgpu_start_gpu_idle(struct gk20a *g)
{
	nvgpu_set_enabled(g, NVGPU_DRIVER_IS_DYING, true);
}

int nvgpu_enable_irqs(struct gk20a *g)
{
	return 0;
}

void nvgpu_disable_irqs(struct gk20a *g)
{
}

/*
 * We have no runtime PM stuff in userspace so these are really just noops.
 */
void gk20a_busy_noresume(struct gk20a *g)
{
}

void gk20a_idle_nosuspend(struct gk20a *g)
{
}

int gk20a_busy(struct gk20a *g)
{
#ifdef NVGPU_UNITTEST_FAULT_INJECTION_ENABLEMENT
	if (nvgpu_posix_fault_injection_handle_call(&nvgpu_fi)) {
		return -ENODEV;
	}
#endif
	nvgpu_atomic_inc(&g->usage_count);

	return 0;
}

void gk20a_idle(struct gk20a *g)
{
	nvgpu_atomic_dec(&g->usage_count);
}

/*
 * This function aims to initialize enough stuff to make unit testing worth
 * while. There are several interfaces and APIs that rely on the struct gk20a's
 * state in order to function: logging, for example, but there are many other
 * things, too.
 *
 * Initialize as much of that as possible here. This is meant to be equivalent
 * to the kernel space driver's probe function.
 */
struct gk20a *nvgpu_posix_probe(void)
{
	struct gk20a *g;
	struct nvgpu_os_posix *p;

#ifdef NVGPU_UNITTEST_FAULT_INJECTION_ENABLEMENT
	if (nvgpu_posix_fault_injection_handle_call(&nvgpu_fi)) {
		return NULL;
	}
#endif

	p = malloc(sizeof(*p));

	if (p == NULL) {
		return NULL;
	}

	(void) memset(p, 0, sizeof(*p));

	g = &p->g;
	g->log_mask = 0;
	g->mm.g = g;

	if (nvgpu_kmem_init(g) != 0) {
		goto fail_kmem;
	}

	if (nvgpu_init_enabled_flags(g) != 0) {
		goto fail_enabled_flags;
	}

	return g;

fail_enabled_flags:
	nvgpu_kmem_fini(g, 0);
fail_kmem:
	free(p);

	return NULL;
}

void nvgpu_posix_cleanup(struct gk20a *g)
{
	struct nvgpu_os_posix *p = nvgpu_os_posix_from_gk20a(g);

	nvgpu_kmem_fini(g, 0);
	nvgpu_free_enabled_flags(g);
	free(p);
}
