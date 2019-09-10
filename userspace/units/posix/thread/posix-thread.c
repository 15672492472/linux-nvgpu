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

#include <stdlib.h>
#include <unistd.h>

#include <unit/io.h>
#include <unit/unit.h>
#include "posix-thread.h"

static int test_thread_fn(void *args)
{
	int policy;
	struct sched_param param;
	struct unit_test_thread_data *data;

	(void) memset(&param, 0, sizeof(struct sched_param));

	data = (struct unit_test_thread_data *)args;

	if (data->check_priority) {
		pthread_getschedparam(pthread_self(), &policy, &param);
		data->thread_priority = param.sched_priority;
	}

	data->thread_created = 1;

	if (data->check_stop) {
		while (!nvgpu_thread_should_stop(&test_thread)) {
			usleep(2);
		}
	}

	return 0;
}

static void test_thread_stop_graceful_callback(void *args)
{
	struct unit_test_thread_data *data;

	data = (struct unit_test_thread_data *)args;

	data->callback_invoked = 1;
}

int test_thread_cycle(struct unit_module *m, struct gk20a *g, void *args)
{
	int ret;
	struct test_thread_args *test_args = (struct test_thread_args *)args;

	memset(&test_thread, 0, sizeof(struct nvgpu_thread));
	memset(&test_data, 0, sizeof(struct unit_test_thread_data));

	if (test_args->check_stop == true) {
		test_data.check_stop = 1;
	}

	if (test_args->use_priority == false) {
		ret = nvgpu_thread_create(&test_thread, &test_data,
				test_thread_fn,
				"test_thread");
	} else {
		test_data.check_priority = 1;

		ret = nvgpu_thread_create_priority(&test_thread,
				&test_data, test_thread_fn,
				UNIT_TEST_THREAD_PRIORITY,
				"test_thread_priority");
	}

	if (ret != 0) {
		if (test_args->use_priority == true && ret == 1) {
			unit_info(m, "No permission to set thread priority\n");
			unit_info(m, "Return PASS\n");
			return UNIT_SUCCESS;
		}
		unit_return_fail(m, "Thread creation failed %d\n", ret);
	}

	while (!test_data.thread_created) {
		unit_info(m, "Waiting for thread creation\n");
		usleep(10);
	}

	if (test_args->use_priority == true) {
		if (test_data.thread_priority != UNIT_TEST_THREAD_PRIORITY) {
			unit_return_fail(m, "Thread priority %d mismatch\n",
					test_data.thread_priority);
		}
	}

	if (test_args->check_stop == true) {
		if (!nvgpu_thread_is_running(&test_thread)) {
			unit_return_fail(m, "Thread running status is wrong\n");
		}

		if (test_args->stop_graceful == false) {
			nvgpu_thread_stop(&test_thread);
		} else {
			nvgpu_thread_stop_graceful(&test_thread,
				test_thread_stop_graceful_callback,
				&test_data);
			if (!test_data.callback_invoked) {
				unit_return_fail(m, "Callback not invoked\n");
			}
		}
	}

	return UNIT_SUCCESS;
}

struct unit_module_test posix_thread_tests[] = {
	UNIT_TEST(create,            test_thread_cycle, &create_normal, 0),
	UNIT_TEST(create_priority,   test_thread_cycle, &create_priority, 0),
	UNIT_TEST(cycle,             test_thread_cycle, &check_stop, 0),
	UNIT_TEST(stop_graceful,     test_thread_cycle, &stop_graceful, 0),
};

UNIT_MODULE(posix_thread, posix_thread_tests, UNIT_PRIO_POSIX_TEST);