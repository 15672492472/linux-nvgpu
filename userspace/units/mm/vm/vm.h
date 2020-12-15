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

#ifndef UNIT_VM_H
#define UNIT_VM_H

struct gk20a;
struct unit_module;

/** @addtogroup SWUTS-mm-vm
 *  @{
 *
 * Software Unit Test Specification for mm.vm
 */

/**
 * Test specification for: test_map_buf
 *
 * Description: The VM unit shall be able to map a buffer of memory such that
 * the GPU may access that memory.
 *
 * Test Type: Feature based
 *
 * Input: None
 *
 * Steps:
 * - Initialize a VM with the following characteristics:
 *   - 64KB large page support enabled
 *   - Low hole size = 64MB
 *   - Address space size = 128GB
 *   - Kernel reserved space size = 4GB
 * - Ensure that no buffers are already mapped.
 * - Use nvgpu_big_pages_possible() to ensure big pages are possible in the
 *   current condition, and check its error handling.
 * - Map a 4KB buffer into the VM
 *   - Check that the resulting GPU virtual address is aligned to 4KB
 *   - Unmap the buffer
 * - Map a 64KB buffer into the VM
 *   - Check that the resulting GPU virtual address is aligned to 64KB
 *   - Unmap the buffer
 * - Check a few corner cases:
 *   - If big pages explicitly disabled at gk20a level, mapping should still
 *     succeed.
 *   - If big pages explicitly disabled at the VM level, mapping should still
 *     succeed.
 *   - If VAs are not unified, mapping should still succeed.
 *   - If IOMMU is disabled, mapping should still succeed.
 *   - If the buffer to map is smaller than the big page size, mapping should
 *     still succeed.
 * - Uninitialize the VM
 *
 * Output: Returns PASS if the steps above were executed successfully. FAIL
 * otherwise.
 */
int test_map_buf(struct unit_module *m, struct gk20a *g, void *__args);

/**
 * Test specification for: test_map_buf_gpu_va
 *
 * Description: When a GPU virtual address is passed into the nvgpu_vm_map()
 * function the resulting GPU virtual address of the map does/does not match
 * the requested GPU virtual address.
 *
 * Test Type: Feature based
 *
 * Input: None
 *
 * Steps:
 * - Initialize a VM with the following characteristics:
 *   - 64KB large page support enabled
 *   - Low hole size = 64MB
 *   - Address space size = 128GB
 *   - Kernel reserved space size = 4GB
 * - Map a 4KB buffer into the VM at a specific GPU virtual address
 *   - Check that the resulting GPU virtual address is aligned to 4KB
 *   - Check that the resulting GPU VA is the same as the requested GPU VA
 *   - Unmap the buffer
 * - Ensure that requesting to map the same buffer at the same address still
 *   reports success and does not result in an actual extra mapping.
 * - Map a 64KB buffer into the VM at a specific GPU virtual address
 *   - Check that the resulting GPU virtual address is aligned to 64KB
 *   - Check that the resulting GPU VA is the same as the requested GPU VA
 *   - Unmap the buffer
 * - Check a few corner cases:
 *   - If VA is not unified, mapping should still succeed.
 *   - If VA is not unified, GPU_VA fixed below nvgpu_gmmu_va_small_page_limit,
 *     mapping should still succeed.
 *   - Do not allocate a VM area which will force an allocation with small
 *     pages.
 *   - Do not unmap the buffer so that nvgpu_vm_put can take care of the cleanup
 *     of both the mapping and the VM area.
 * - Uninitialize the VM
 *
 * Output: Returns PASS if the steps above were executed successfully. FAIL
 * otherwise.
 */
int test_map_buf_gpu_va(struct unit_module *m, struct gk20a *g, void *__args);

/**
 * Test specification for: test_batch
 *
 * Description: This test exercises the VM unit's batch mode. Batch mode is used
 * to optimize cache flushes.
 *
 * Test Type: Feature based
 *
 * Input: None
 *
 * Steps:
 * - Initialize a VM with the following characteristics:
 *   - 64KB large page support enabled
 *   - Low hole size = 64MB
 *   - Address space size = 128GB
 *   - Kernel reserved space size = 4GB
 * - Map/unmap 10 4KB buffers using batch mode
 * - Disable batch mode and verify cache flush counts
 * - Uninitialize the VM
 *
 * Output: Returns PASS if the steps above were executed successfully. FAIL
 * otherwise.
 */
int test_batch(struct unit_module *m, struct gk20a *g, void *__args);

/**
 * Test specification for: test_init_error_paths
 *
 * Description: This test exercises the VM unit initialization code and covers
 * a number of error paths.
 *
 * Test Type: Feature based, error injection
 *
 * Input: None
 *
 * Steps:
 * - Create VM parameters with the following characteristics:
 *   - 64KB large page support enabled
 *   - Low hole size = 64MB
 *   - Address space size = 128GB
 *   - Kernel reserved space size = 4GB
 * - Inject an error to make the allocation for struct vm_gk20a to fail and
 *   check that nvgpu_vm_init returns NULL.
 * - Set an invalid aperture size and ensure that nvgpu_vm_do_init asserts.
 * - Try to initialize a guest managed VM with kernel space and ensure that
 *   nvgpu_vm_do_init asserts.
 * - Set gk20a to report a virtual GPU and ensure that nvgpu_vm_do_init returns
 *   a failure when VM is guest managed.
 * - Ensure that nvgpu_vm_do_init reports a failure if the vm_as_alloc_share HAL
 *   fails.
 * - Set invalid parameters (low hole above the small page limit) and ensure
 *   that nvgpu_vm_do_init asserts.
 * - Inject an error to cause a failure within nvgpu_allocator_init for the user
 *   VMA and ensure that nvgpu_vm_do_init reports a failure.
 * - Inject an error to cause a failure within nvgpu_allocator_init for the
 *   kernel VMA and ensure that nvgpu_vm_do_init reports a failure.
 * - Set invalid parameters (low hole is 0 with a non unified VA) and ensure
 *   that nvgpu_vm_do_init reports a failure.
 * - Ensure that nvgpu_vm_do_init succeeds with big pages enabled and a non
 *   unified VA space.
 * - Ensure that nvgpu_vm_do_init succeeds with big pages disabled.
 * - Ensure that nvgpu_vm_do_init succeeds with no user VMA.
 * - Uninitialize the VM
 *
 * Output: Returns PASS if the steps above were executed successfully. FAIL
 * otherwise.
 */
int test_init_error_paths(struct unit_module *m, struct gk20a *g, void *__args);

/**
 * Test specification for: test_map_buffer_error_cases
 *
 * Description: This test targets error handling within the nvgpu_vm_map API.
 *
 * Test Type: Error injection
 *
 * Input: None
 *
 * Steps:
 * - Initialize a VM with the following characteristics:
 *   - 64KB large page support enabled
 *   - Low hole size = 64MB
 *   - Address space size = 128GB
 *   - Kernel reserved space size = 4GB
 * - Ensure that if a non-fixed offset with userspace managed VM is in use,
 *   the nvgpu_vm_map API reports a failure.
 * - Ensure that if an invalid buffer size is provided,the nvgpu_vm_map API
 *   reports a failure.
 * - Inject a memory allocation error at allocation 0 and ensure that
 *   nvgpu_vm_map reports a failure of type ENOMEM. (This makes mapped_buffer
 *   memory allocation to fail.)
 * - Try to map an oversized buffer of 1GB and ensure that nvgpu_vm_map reports
 *   a failure of type EINVAL.
 * - Inject a memory allocation error at allocation 40 and ensure that
 *   nvgpu_vm_map reports a failure of type ENOMEM. (This makes the call to
 *   g->ops.mm.gmmu.map to fail.)
 * - Uninitialize the VM
 *
 * Output: Returns PASS if the steps above were executed successfully. FAIL
 * otherwise.
 */
int test_map_buffer_error_cases(struct unit_module *m, struct gk20a *g,
	void *__args);

/**
 * Test specification for: test_nvgpu_vm_alloc_va
 *
 * Description: This test targets the nvgpu_vm_alloc_va API.
 *
 * Test Type: Feature based, Error injection
 *
 * Input: None
 *
 * Steps:
 * - Initialize a VM with the following characteristics:
 *   - 64KB large page support enabled
 *   - Low hole size = 64MB
 *   - Address space size = 128GB
 *   - Kernel reserved space size = 4GB
 * - Set the VM as guest managed and call nvgpu_vm_alloc_va and ensure that it
 *   fails (returns NULL) as a guest managed VM cannot allocate VA spaces.
 * - Call nvgpu_vm_alloc_va with an invalid page size and ensure that it fails
 *   (returns NULL).
 * - Call nvgpu_vm_alloc_va with an unsupported page size index
 *   (GMMU_PAGE_SIZE_BIG) and ensure that it fails (returns NULL).
 * - Inject a memory allocation error at allocation 0 and ensure that
 *   nvgpu_vm_alloc_va reports a failure (returns NULL). (This makes the PTE
 *   memory allocation to fail.)
 * - Call nvgpu_vm_alloc_va with valid parameters and ensure that it succeeds
 *   (returns a non-NULL address.)
 * - Uninitialize the VM
 *
 * Output: Returns PASS if the steps above were executed successfully. FAIL
 * otherwise.
 */
int test_nvgpu_vm_alloc_va(struct unit_module *m, struct gk20a *g,
	void *__args);

/**
 * Test specification for: test_vm_bind
 *
 * Description: This test targets the nvgpu_vm_bind_channel API.
 *
 * Test Type: Feature based
 *
 * Input: None
 *
 * Steps:
 * - Initialize a VM with the following characteristics:
 *   - 64KB large page support enabled
 *   - Low hole size = 64MB
 *   - Address space size = 128GB
 *   - Kernel reserved space size = 4GB
 * - Create an empty nvgpu_channel instance.
 * - Call the nvgpu_vm_bind_channel API with the empty channel instance.
 * - Ensure that after the call, the VM pointer in the nvgpu_channel structure
 *   points to the VM in use in the test.
 * - Uninitialize the VM
 *
 * Output: Returns PASS if the steps above were executed successfully. FAIL
 * otherwise.
 */
int test_vm_bind(struct unit_module *m, struct gk20a *g, void *__args);

/**
 * Test specification for: test_vm_aspace_id
 *
 * Description: This test targets the vm_aspace_id API.
 *
 * Test Type: Feature based
 *
 * Input: None
 *
 * Steps:
 * - Initialize a VM with the following characteristics:
 *   - 64KB large page support enabled
 *   - Low hole size = 64MB
 *   - Address space size = 128GB
 *   - Kernel reserved space size = 4GB
 * - Call vm_aspace_id on the test VM and ensure it reports an invalid value
 *   (-1) since the AS share is not set.
 * - Create an AS share structure and set its id to 0. Assign the AS share to
 *   the test VM.
 * - Call vm_aspace_id on the test VM and ensure it reports a value of 0.
 * - Uninitialize the VM
 *
 * Output: Returns PASS if the steps above were executed successfully. FAIL
 * otherwise.
 */
int test_vm_aspace_id(struct unit_module *m, struct gk20a *g, void *__args);


/** }@ */
#endif /* UNIT_VM_H */
