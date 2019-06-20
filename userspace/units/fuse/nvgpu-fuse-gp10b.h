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

#ifndef __UNIT_NVGPU_FUSE_GP10B_H__
#define __UNIT_NVGPU_FUSE_GP10B_H__

extern struct fuse_test_args gp10b_init_args;

int test_fuse_gp10b_check_sec(struct unit_module *m,
			      struct gk20a *g, void *__args);
int test_fuse_gp10b_check_gcplex_fail(struct unit_module *m,
				      struct gk20a *g, void *__args);
int test_fuse_gp10b_check_sec_invalid_gcplex(struct unit_module *m,
					     struct gk20a *g, void *__args);
int test_fuse_gp10b_check_non_sec(struct unit_module *m,
				  struct gk20a *g, void *__args);
int test_fuse_gp10b_ecc(struct unit_module *m,
			struct gk20a *g, void *__args);
int test_fuse_gp10b_feature_override_disable(struct unit_module *m,
					     struct gk20a *g, void *__args);
#ifdef CONFIG_NVGPU_SIM
int test_fuse_gp10b_check_fmodel(struct unit_module *m,
				 struct gk20a *g, void *__args);
#endif
#endif /* __UNIT_NVGPU_FUSE_GP10B_H__ */
