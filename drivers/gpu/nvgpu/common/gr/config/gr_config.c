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

#include <nvgpu/gk20a.h>
#include <nvgpu/io.h>
#include <nvgpu/gr/config.h>

struct nvgpu_gr_config *nvgpu_gr_config_init(struct gk20a *g)
{
	struct nvgpu_gr_config *config;
	u32 gpc_index, pes_index;
	u32 pes_tpc_mask;
	u32 pes_tpc_count;
	u32 pes_heavy_index;
	u32 gpc_new_skip_mask;

	config = nvgpu_kzalloc(g, sizeof(*config));
	if (config == NULL) {
		return NULL;;
	}

	config->max_gpc_count = g->ops.top.get_max_gpc_count(g);

	config->max_tpc_per_gpc_count = g->ops.top.get_max_tpc_per_gpc_count(g);

	config->max_tpc_count = config->max_gpc_count *
				config->max_tpc_per_gpc_count;

	config->gpc_count = g->ops.priv_ring.get_gpc_count(g);
	if (config->gpc_count == 0U) {
		nvgpu_err(g, "gpc_count==0!");
		goto clean_up;
	}

	if (g->ops.gr.config.get_gpc_mask != NULL) {
		config->gpc_mask = g->ops.gr.config.get_gpc_mask(g, config);
	} else {
		config->gpc_mask = BIT32(config->gpc_count) - 1;
	}

	config->pe_count_per_gpc = nvgpu_get_litter_value(g,
		GPU_LIT_NUM_PES_PER_GPC);
	if (config->pe_count_per_gpc > GK20A_GR_MAX_PES_PER_GPC) {
		nvgpu_err(g, "too many pes per gpc");
		goto clean_up;
	}

	config->max_zcull_per_gpc_count = nvgpu_get_litter_value(g,
		GPU_LIT_NUM_ZCULL_BANKS);

	config->gpc_tpc_count = nvgpu_kzalloc(g, config->gpc_count *
					sizeof(u32));
	config->gpc_tpc_mask = nvgpu_kzalloc(g, config->max_gpc_count *
					sizeof(u32));
	config->gpc_zcb_count = nvgpu_kzalloc(g, config->gpc_count *
					sizeof(u32));
	config->gpc_ppc_count = nvgpu_kzalloc(g, config->gpc_count *
					sizeof(u32));
	config->gpc_skip_mask = nvgpu_kzalloc(g,
			(size_t)g->ops.gr.config.get_pd_dist_skip_table_size() *
			(size_t)4 * sizeof(u32));

	if ((config->gpc_tpc_count == NULL) || (config->gpc_tpc_mask == NULL) ||
	    (config->gpc_zcb_count == NULL) || (config->gpc_ppc_count == NULL) ||
	    (config->gpc_skip_mask == NULL)) {
		goto clean_up;
	}

	for (gpc_index = 0; gpc_index < config->max_gpc_count; gpc_index++) {
		if (g->ops.gr.config.get_gpc_tpc_mask != NULL) {
			config->gpc_tpc_mask[gpc_index] =
				g->ops.gr.config.get_gpc_tpc_mask(g, config, gpc_index);
		}
	}

	for (pes_index = 0; pes_index < config->pe_count_per_gpc; pes_index++) {
		config->pes_tpc_count[pes_index] = nvgpu_kzalloc(g,
			config->gpc_count * sizeof(u32));
		config->pes_tpc_mask[pes_index] = nvgpu_kzalloc(g,
			config->gpc_count * sizeof(u32));
		if ((config->pes_tpc_count[pes_index] == NULL) ||
		    (config->pes_tpc_mask[pes_index] == NULL)) {
			goto clean_up;
		}
	}

	config->ppc_count = 0;
	config->tpc_count = 0;
	config->zcb_count = 0;
	for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
		config->gpc_tpc_count[gpc_index] =
			g->ops.gr.config.get_tpc_count_in_gpc(g, config,
				gpc_index);
		config->tpc_count += config->gpc_tpc_count[gpc_index];

		config->gpc_zcb_count[gpc_index] =
			g->ops.gr.config.get_zcull_count_in_gpc(g, config,
				gpc_index);
		config->zcb_count += config->gpc_zcb_count[gpc_index];

		for (pes_index = 0; pes_index < config->pe_count_per_gpc;
				    pes_index++) {
			pes_tpc_mask = g->ops.gr.config.get_pes_tpc_mask(g,
						config, gpc_index, pes_index);
			pes_tpc_count = hweight32(pes_tpc_mask);

			/* detect PES presence by seeing if there are
			 * TPCs connected to it.
			 */
			if (pes_tpc_count != 0U) {
				config->gpc_ppc_count[gpc_index]++;
			}

			config->pes_tpc_count[pes_index][gpc_index] = pes_tpc_count;
			config->pes_tpc_mask[pes_index][gpc_index] = pes_tpc_mask;
		}

		config->ppc_count += config->gpc_ppc_count[gpc_index];

		gpc_new_skip_mask = 0;
		if (config->pe_count_per_gpc > 1U &&
		    config->pes_tpc_count[0][gpc_index] +
		    config->pes_tpc_count[1][gpc_index] == 5U) {
			pes_heavy_index =
				config->pes_tpc_count[0][gpc_index] >
				config->pes_tpc_count[1][gpc_index] ? 0U : 1U;

			gpc_new_skip_mask =
				config->pes_tpc_mask[pes_heavy_index][gpc_index] ^
				   (config->pes_tpc_mask[pes_heavy_index][gpc_index] &
				   (config->pes_tpc_mask[pes_heavy_index][gpc_index] - 1U));

		} else if (config->pe_count_per_gpc > 1U &&
			   (config->pes_tpc_count[0][gpc_index] +
			    config->pes_tpc_count[1][gpc_index] == 4U) &&
			   (config->pes_tpc_count[0][gpc_index] !=
			    config->pes_tpc_count[1][gpc_index])) {
				pes_heavy_index =
				    config->pes_tpc_count[0][gpc_index] >
				    config->pes_tpc_count[1][gpc_index] ? 0U : 1U;

			gpc_new_skip_mask =
				config->pes_tpc_mask[pes_heavy_index][gpc_index] ^
				   (config->pes_tpc_mask[pes_heavy_index][gpc_index] &
				   (config->pes_tpc_mask[pes_heavy_index][gpc_index] - 1U));
		}
		config->gpc_skip_mask[gpc_index] = gpc_new_skip_mask;
	}

	nvgpu_log_info(g, "max_gpc_count: %d", config->max_gpc_count);
	nvgpu_log_info(g, "max_tpc_per_gpc_count: %d", config->max_tpc_per_gpc_count);
	nvgpu_log_info(g, "max_zcull_per_gpc_count: %d", config->max_zcull_per_gpc_count);
	nvgpu_log_info(g, "max_tpc_count: %d", config->max_tpc_count);
	nvgpu_log_info(g, "gpc_count: %d", config->gpc_count);
	nvgpu_log_info(g, "pe_count_per_gpc: %d", config->pe_count_per_gpc);
	nvgpu_log_info(g, "tpc_count: %d", config->tpc_count);
	nvgpu_log_info(g, "ppc_count: %d", config->ppc_count);

	for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
		nvgpu_log_info(g, "gpc_tpc_count[%d] : %d",
			   gpc_index, config->gpc_tpc_count[gpc_index]);
	}
	for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
		nvgpu_log_info(g, "gpc_zcb_count[%d] : %d",
			   gpc_index, config->gpc_zcb_count[gpc_index]);
	}
	for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
		nvgpu_log_info(g, "gpc_ppc_count[%d] : %d",
			   gpc_index, config->gpc_ppc_count[gpc_index]);
	}
	for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
		nvgpu_log_info(g, "gpc_skip_mask[%d] : %d",
			   gpc_index, config->gpc_skip_mask[gpc_index]);
	}
	for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
		for (pes_index = 0;
		     pes_index < config->pe_count_per_gpc;
		     pes_index++) {
			nvgpu_log_info(g, "pes_tpc_count[%d][%d] : %d",
				   pes_index, gpc_index,
				   config->pes_tpc_count[pes_index][gpc_index]);
		}
	}

	for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
		for (pes_index = 0;
		     pes_index < config->pe_count_per_gpc;
		     pes_index++) {
			nvgpu_log_info(g, "pes_tpc_mask[%d][%d] : %d",
				   pes_index, gpc_index,
				   config->pes_tpc_mask[pes_index][gpc_index]);
		}
	}

	return config;

clean_up:
	nvgpu_kfree(g, config);
	return NULL;
}

static u32 prime_set[18] = {
	2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61 };

/*
 * Return map tiles count for given index
 * Return 0 if index is out-of-bounds
 */
u32 nvgpu_gr_config_get_map_tile_count(struct nvgpu_gr_config *config, u32 index)
{
	if (index >= config->map_tile_count) {
		return 0;
	}

	return config->map_tiles[index];
}

u32 nvgpu_gr_config_get_map_row_offset(struct nvgpu_gr_config *config)
{
	return config->map_row_offset;
}

int nvgpu_gr_config_init_map_tiles(struct gk20a *g,
	struct nvgpu_gr_config *config)
{
	s32 comm_denom;
	s32 mul_factor;
	s32 *init_frac = NULL;
	s32 *init_err = NULL;
	s32 *run_err = NULL;
	u32 *sorted_num_tpcs = NULL;
	u32 *sorted_to_unsorted_gpc_map = NULL;
	u32 gpc_index;
	u32 gpc_mark = 0;
	u32 num_tpc;
	u32 max_tpc_count = 0;
	u32 swap;
	u32 tile_count;
	u32 index;
	bool delete_map = false;
	bool gpc_sorted;
	int ret = 0;
	u32 num_gpcs = nvgpu_get_litter_value(g, GPU_LIT_NUM_GPCS);
	u32 num_tpc_per_gpc = nvgpu_get_litter_value(g, GPU_LIT_NUM_TPC_PER_GPC);
	u32 map_tile_count = num_gpcs * num_tpc_per_gpc;

	init_frac = nvgpu_kzalloc(g, num_gpcs * sizeof(s32));
	init_err = nvgpu_kzalloc(g, num_gpcs * sizeof(s32));
	run_err = nvgpu_kzalloc(g, num_gpcs * sizeof(s32));
	sorted_num_tpcs =
		nvgpu_kzalloc(g, (size_t)num_gpcs *
				 (size_t)num_tpc_per_gpc *
				 sizeof(s32));
	sorted_to_unsorted_gpc_map =
		nvgpu_kzalloc(g, (size_t)num_gpcs * sizeof(s32));

	if (!((init_frac != NULL) &&
	      (init_err != NULL) &&
	      (run_err != NULL) &&
	      (sorted_num_tpcs != NULL) &&
	      (sorted_to_unsorted_gpc_map != NULL))) {
		ret = -ENOMEM;
		goto clean_up;
	}

	config->map_row_offset = 0xFFFFFFFFU;

	if (config->tpc_count == 3U) {
		config->map_row_offset = 2;
	} else if (config->tpc_count < 3U) {
		config->map_row_offset = 1;
	} else {
		config->map_row_offset = 3;

		for (index = 1U; index < 18U; index++) {
			u32 prime = prime_set[index];
			if ((config->tpc_count % prime) != 0U) {
				config->map_row_offset = prime;
				break;
			}
		}
	}

	switch (config->tpc_count) {
	case 15:
		config->map_row_offset = 6;
		break;
	case 14:
		config->map_row_offset = 5;
		break;
	case 13:
		config->map_row_offset = 2;
		break;
	case 11:
		config->map_row_offset = 7;
		break;
	case 10:
		config->map_row_offset = 6;
		break;
	case 7:
	case 5:
		config->map_row_offset = 1;
		break;
	default:
		break;
	}

	if (config->map_tiles != NULL) {
		if (config->map_tile_count != config->tpc_count) {
			delete_map = true;
		}

		for (tile_count = 0; tile_count < config->map_tile_count; tile_count++) {
			if (nvgpu_gr_config_get_map_tile_count(config, tile_count)
					>= config->tpc_count) {
				delete_map = true;
			}
		}

		if (delete_map) {
			nvgpu_kfree(g, config->map_tiles);
			config->map_tiles = NULL;
			config->map_tile_count = 0;
		}
	}

	if (config->map_tiles == NULL) {
		config->map_tiles = nvgpu_kzalloc(g, map_tile_count * sizeof(u8));
		if (config->map_tiles == NULL) {
			ret = -ENOMEM;
			goto clean_up;
		}
		config->map_tile_count = map_tile_count;

		for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
			sorted_num_tpcs[gpc_index] = config->gpc_tpc_count[gpc_index];
			sorted_to_unsorted_gpc_map[gpc_index] = gpc_index;
		}

		gpc_sorted = false;
		while (!gpc_sorted) {
			gpc_sorted = true;
			for (gpc_index = 0U; gpc_index < config->gpc_count - 1U; gpc_index++) {
				if (sorted_num_tpcs[gpc_index + 1U] > sorted_num_tpcs[gpc_index]) {
					gpc_sorted = false;
					swap = sorted_num_tpcs[gpc_index];
					sorted_num_tpcs[gpc_index] = sorted_num_tpcs[gpc_index + 1U];
					sorted_num_tpcs[gpc_index + 1U] = swap;
					swap = sorted_to_unsorted_gpc_map[gpc_index];
					sorted_to_unsorted_gpc_map[gpc_index] =
						sorted_to_unsorted_gpc_map[gpc_index + 1U];
					sorted_to_unsorted_gpc_map[gpc_index + 1U] = swap;
				}
			}
		}

		for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
			if (config->gpc_tpc_count[gpc_index] > max_tpc_count) {
				max_tpc_count = config->gpc_tpc_count[gpc_index];
			}
		}

		mul_factor = S32(config->gpc_count) * S32(max_tpc_count);
		if ((U32(mul_factor) & 0x1U) != 0U) {
			mul_factor = 2;
		} else {
			mul_factor = 1;
		}

		comm_denom = S32(config->gpc_count) * S32(max_tpc_count) * mul_factor;

		for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
			num_tpc = sorted_num_tpcs[gpc_index];

			init_frac[gpc_index] = S32(num_tpc) * S32(config->gpc_count) * mul_factor;

			if (num_tpc != 0U) {
				init_err[gpc_index] = S32(gpc_index) * S32(max_tpc_count) * mul_factor - comm_denom/2;
			} else {
				init_err[gpc_index] = 0;
			}

			run_err[gpc_index] = init_frac[gpc_index] + init_err[gpc_index];
		}

		while (gpc_mark < config->tpc_count) {
			for (gpc_index = 0; gpc_index < config->gpc_count; gpc_index++) {
				if ((run_err[gpc_index] * 2) >= comm_denom) {
					config->map_tiles[gpc_mark++] = (u8)sorted_to_unsorted_gpc_map[gpc_index];
					run_err[gpc_index] += init_frac[gpc_index] - comm_denom;
				} else {
					run_err[gpc_index] += init_frac[gpc_index];
				}
			}
		}
	}

clean_up:
	nvgpu_kfree(g, init_frac);
	nvgpu_kfree(g, init_err);
	nvgpu_kfree(g, run_err);
	nvgpu_kfree(g, sorted_num_tpcs);
	nvgpu_kfree(g, sorted_to_unsorted_gpc_map);

	if (ret != 0) {
		nvgpu_err(g, "fail");
	} else {
		nvgpu_log_fn(g, "done");
	}

	return ret;
}

void nvgpu_gr_config_deinit(struct gk20a *g, struct nvgpu_gr_config *config)
{
	u32 index;

	nvgpu_kfree(g, config->gpc_tpc_count);
	nvgpu_kfree(g, config->gpc_zcb_count);
	nvgpu_kfree(g, config->gpc_ppc_count);
	nvgpu_kfree(g, config->gpc_skip_mask);
	nvgpu_kfree(g, config->gpc_tpc_mask);
	nvgpu_kfree(g, config->map_tiles);
	for (index = 0U; index < config->pe_count_per_gpc;
			    index++) {
		nvgpu_kfree(g, config->pes_tpc_count[index]);
		nvgpu_kfree(g, config->pes_tpc_mask[index]);
	}

}

u32 nvgpu_gr_config_get_max_gpc_count(struct nvgpu_gr_config *config)
{
	return config->max_gpc_count;
}

u32 nvgpu_gr_config_get_max_tpc_per_gpc_count(struct nvgpu_gr_config *config)
{
	return config->max_tpc_per_gpc_count;
}

u32 nvgpu_gr_config_get_max_zcull_per_gpc_count(struct nvgpu_gr_config *config)
{
	return config->max_zcull_per_gpc_count;
}

u32 nvgpu_gr_config_get_max_tpc_count(struct nvgpu_gr_config *config)
{
	return config->max_tpc_count;
}

u32 nvgpu_gr_config_get_gpc_count(struct nvgpu_gr_config *config)
{
	return config->gpc_count;
}

u32 nvgpu_gr_config_get_tpc_count(struct nvgpu_gr_config *config)
{
	return config->tpc_count;
}

u32 nvgpu_gr_config_get_ppc_count(struct nvgpu_gr_config *config)
{
	return config->ppc_count;
}

u32 nvgpu_gr_config_get_zcb_count(struct nvgpu_gr_config *config)
{
	return config->zcb_count;
}

u32 nvgpu_gr_config_get_pe_count_per_gpc(struct nvgpu_gr_config *config)
{
	return config->pe_count_per_gpc;
}

u32 nvgpu_gr_config_get_gpc_ppc_count(struct nvgpu_gr_config *config,
	u32 gpc_index)
{
	return config->gpc_ppc_count[gpc_index];
}

u32 nvgpu_gr_config_get_gpc_tpc_count(struct nvgpu_gr_config *config,
	u32 gpc_index)
{
	if (gpc_index >= config->gpc_count) {
		return 0;
	}
	return config->gpc_tpc_count[gpc_index];
}

u32 nvgpu_gr_config_get_gpc_zcb_count(struct nvgpu_gr_config *config,
	u32 gpc_index)
{
	return config->gpc_zcb_count[gpc_index];
}

u32 nvgpu_gr_config_get_pes_tpc_count(struct nvgpu_gr_config *config,
	u32 gpc_index, u32 pes_index)
{
	return config->pes_tpc_count[pes_index][gpc_index];
}

u32 nvgpu_gr_config_get_gpc_tpc_mask(struct nvgpu_gr_config *config,
	u32 gpc_index)
{
	return config->gpc_tpc_mask[gpc_index];
}

u32 nvgpu_gr_config_get_gpc_skip_mask(struct nvgpu_gr_config *config,
	u32 gpc_index)
{
	return config->gpc_skip_mask[gpc_index];
}

u32 nvgpu_gr_config_get_pes_tpc_mask(struct nvgpu_gr_config *config,
	u32 gpc_index, u32 pes_index)
{
	return config->pes_tpc_mask[pes_index][gpc_index];
}

u32 nvgpu_gr_config_get_gpc_mask(struct nvgpu_gr_config *config)
{
	return config->gpc_mask;
}
