/*
 *
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "gtest/gtest.h"

#include "utils/utils.hpp"
#include "test_harness/test_harness.hpp"
#include "logging/logging.hpp"

namespace lzt = level_zero_tests;

#include <level_zero/ze_api.h>

namespace {

class CooperativeKernelTests
    : public ::testing::Test,
      public ::testing::WithParamInterface<std::tuple<int, bool>> {};

TEST_P(
    CooperativeKernelTests,
    GivenCooperativeKernelWhenAppendingLaunchCooperativeKernelThenSuccessIsReturnedAndOutputIsCorrect) {
  uint32_t max_group_count = 0;
  ze_module_handle_t module = nullptr;
  ze_kernel_handle_t kernel = nullptr;
  auto driver = lzt::get_default_driver();
  auto device = lzt::get_default_device(driver);
  auto context = lzt::create_context(driver);
  ze_command_queue_flags_t flags = 0;
  auto mode = ZE_COMMAND_QUEUE_MODE_DEFAULT;
  auto priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
  int ordinal = -1;

  auto command_queue_group_properties =
      lzt::get_command_queue_group_properties(device);
  for (int i = 0; i < command_queue_group_properties.size(); i++) {
    if (command_queue_group_properties[i].flags &
        ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COOPERATIVE_KERNELS) {
      ordinal = i;
      break;
    }
  }

  if (ordinal < 0) {
    LOG_WARNING << "No command queues that support cooperative kernels";
    GTEST_SKIP();
  }
  auto is_immediate = std::get<1>(GetParam());
  auto cmd_bundle = lzt::create_command_bundle(
      context, device, flags, mode, priority, 0, ordinal, 0, is_immediate);

  // Set up input vector data
  const size_t data_size = 4096;
  uint64_t kernel_data[data_size] = {0};
  auto input_data = lzt::allocate_shared_memory(sizeof(uint64_t) * data_size, 1,
                                                0, 0, device, context);
  memcpy(input_data, kernel_data, data_size * sizeof(uint64_t));

  auto row_num = std::get<0>(GetParam());
  uint32_t groups_x = 1;

  module = lzt::create_module(context, device, "cooperative_kernel.spv",
                              ZE_MODULE_FORMAT_IL_SPIRV, "", nullptr);
  kernel = lzt::create_function(module, "cooperative_kernel");

  // Use a small group size in order to use more groups
  // in order to stress cooperation
  zeKernelSetGroupSize(kernel, 1, 1, 1);
  ASSERT_EQ(ZE_RESULT_SUCCESS,
            zeKernelSuggestMaxCooperativeGroupCount(kernel, &groups_x));
  ASSERT_GT(groups_x, 0);
  // We've set the number of workgroups to the max
  // so check that it is sufficient for the input,
  // otherwise just clamp the test
  if (groups_x < row_num) {
    row_num = groups_x;
  }
  ASSERT_EQ(ZE_RESULT_SUCCESS, zeKernelSetArgumentValue(
                                   kernel, 0, sizeof(input_data), &input_data));
  ASSERT_EQ(ZE_RESULT_SUCCESS,
            zeKernelSetArgumentValue(kernel, 1, sizeof(row_num), &row_num));

  ze_group_count_t args = {groups_x, 1, 1};
  ASSERT_EQ(ZE_RESULT_SUCCESS,
            zeCommandListAppendLaunchCooperativeKernel(
                cmd_bundle.list, kernel, &args, nullptr, 0, nullptr));

  if (is_immediate) {
    lzt::synchronize_command_list_host(cmd_bundle.list, UINT64_MAX);
  } else {
    lzt::close_command_list(cmd_bundle.list);
    lzt::execute_command_lists(cmd_bundle.queue, 1, &cmd_bundle.list, nullptr);
    lzt::synchronize(cmd_bundle.queue, UINT64_MAX);
  }

  // Validate the kernel completed successfully and correctly
  uint64_t val = 0;
  for (int i = 0; i <= row_num; i++) {
    val = i + row_num;
    ASSERT_EQ(static_cast<uint64_t *>(input_data)[i], val);
  }

  lzt::free_memory(context, input_data);
  lzt::destroy_command_bundle(cmd_bundle);
  lzt::destroy_context(context);
}

INSTANTIATE_TEST_SUITE_P(
    // 62 is the max row such that no calculation will overflow max uint64 value
    GroupNumbers, CooperativeKernelTests,
    ::testing::Combine(::testing::Values(0, 1, 5, 10, 50, 62),
                       ::testing::Bool()));

} // namespace
