/*
 * Virtualized GPU Runlist
 *
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

struct gk20a;
struct channel_gk20a;

int vgpu_runlist_update_for_channel(struct gk20a *g, u32 runlist_id,
					struct channel_gk20a *ch,
					bool add, bool wait_for_finish);
int vgpu_runlist_reload(struct gk20a *g, u32 runlist_id,
				bool add, bool wait_for_finish);
int vgpu_runlist_set_interleave(struct gk20a *g,
					u32 id,
					u32 runlist_id,
					u32 new_level);
u32 vgpu_runlist_length_max(struct gk20a *g);
u32 vgpu_runlist_entry_size(struct gk20a *g);
