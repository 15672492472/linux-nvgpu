/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NVGPU_LINUX_SOC_FUSE_H
#define NVGPU_LINUX_SOC_FUSE_H

/*
 * Note: Following defines should be ideally in tegra fuse driver. They are
 * defined here since nvgpu uses the tegra_fuse_readl API directly to read
 * those fuses. Once nvgpu starts using nvmem API to read these fuses,
 * these offsets can be defined in tegra fuse driver.
 * See Bug 200633045.
 */

#ifndef FUSE_GCPLEX_CONFIG_FUSE_0
#define FUSE_GCPLEX_CONFIG_FUSE_0       0x1c8
#endif

#ifndef FUSE_RESERVED_CALIB0_0
#define FUSE_RESERVED_CALIB0_0          0x204
#endif

/* T186+ */
#if !defined(FUSE_PDI0) && !defined(FUSE_PDI1)
#define FUSE_PDI0			0x300
#define FUSE_PDI1			0x304
#endif

#endif /* NVGPU_LINUX_SOC_FUSE_H */
