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

#ifndef NVGPU_BOARDOBJ_H
#define NVGPU_BOARDOBJ_H

#include <nvgpu/list.h>
#include <nvgpu/pmu/pmuif/nvgpu_cmdif.h>

struct boardobj;
struct nvgpu_list_node;
struct gk20a;

/*
* Fills out the appropriate the nv_pmu_xxxx_device_desc_<xyz> driver->PMU
* description structure, describing this BOARDOBJ board device to the PMU.
*
*/
int nvgpu_boardobj_pmu_data_init_super(struct gk20a *g, struct boardobj
		*pboardobj, struct nv_pmu_boardobj *pmudata);

/*
* Constructor for the base Board Object. Called by each device-specific
* implementation of the BOARDOBJ interface to initialize the board object.
*/
int nvgpu_boardobj_construct_super(struct gk20a *g, struct boardobj
		**ppboardobj, size_t size, void *args);

/*
* Base Class for all physical or logical device on the PCB.
* Contains fields common to all devices on the board. Specific types of
* devices may extend this object adding any details specific to that
* device or device-type.
*/

struct boardobj {
	struct gk20a *g;

	u8 type; /*type of the device*/
	u8 idx;  /*index of boardobj within in its group*/
	/* true if allocated in constructor. destructor should free */
	bool allocated;
	u32 type_mask; /*mask of types this boardobjimplements*/
	bool (*implements)(struct gk20a *g, struct boardobj *pboardobj,
			u8 type);
	int (*destruct)(struct boardobj *pboardobj);
	/*
	* Access interface apis which will be overridden by the devices
	* that inherit from BOARDOBJ
	*/
	int (*pmudatainit)(struct gk20a *g, struct boardobj *pboardobj,
			struct nv_pmu_boardobj *pmudata);
	struct nvgpu_list_node node;
};

struct boardobjgrp_pmucmdhandler_params {
	/* Pointer to the BOARDOBJGRP associated with this CMD */
	struct boardobjgrp *pboardobjgrp;
	/* Pointer to structure representing this NV_PMU_BOARDOBJ_CMD_GRP */
	struct boardobjgrp_pmu_cmd *pcmd;
	/* Boolean indicating whether the PMU successfully handled the CMD */
	u32 success;
};

#define BOARDOBJ_GET_TYPE(pobj) (((struct boardobj *)(pobj))->type)
#define BOARDOBJ_GET_IDX(pobj) (((struct boardobj *)(pobj))->idx)

#define HIGHESTBITIDX_32(n32)   \
{                               \
	u32 count = 0U;        \
	while (((n32) >>= 1U) != 0U) {       \
		count++;       \
	}                      \
	(n32) = count;            \
}

#define LOWESTBIT(x)            ((x) &  (((x)-1U) ^ (x)))

#define HIGHESTBIT(n32)     \
{                           \
	HIGHESTBITIDX_32(n32);  \
	n32 = NVBIT(n32);       \
}

#define ONEBITSET(x)            ((x) && (((x) & ((x)-1U)) == 0U))

#define LOWESTBITIDX_32(n32)  \
{                             \
	n32 = LOWESTBIT(n32); \
	IDX_32(n32);         \
}

#define NUMSETBITS_32(n32)                                         \
{                                                                  \
	(n32) = (n32) - (((n32) >> 1U) & 0x55555555U);                         \
	(n32) = ((n32) & 0x33333333U) + (((n32) >> 2U) & 0x33333333U);         \
	(n32) = ((((n32) + ((n32) >> 4U)) & 0x0F0F0F0FU) * 0x01010101U) >> 24U;\
}

#define IDX_32(n32)				\
{						\
	u32 idx = 0U;				\
	if (((n32) & 0xFFFF0000U) != 0U) {  	\
		idx += 16U;			\
	}					\
	if (((n32) & 0xFF00FF00U) != 0U) {	\
		idx += 8U;			\
	}					\
	if (((n32) & 0xF0F0F0F0U) != 0U) {	\
		idx += 4U;			\
	}					\
	if (((n32) & 0xCCCCCCCCU) != 0U) {	\
		idx += 2U;			\
	}					\
	if (((n32) & 0xAAAAAAAAU) != 0U) {	\
		idx += 1U;			\
	}					\
	(n32) = idx;				\
}

static inline struct boardobj *
boardobj_from_node(struct nvgpu_list_node *node)
{
	return (struct boardobj *)
		((uintptr_t)node - offsetof(struct boardobj, node));
};

#endif /* NVGPU_BOARDOBJ_H */
