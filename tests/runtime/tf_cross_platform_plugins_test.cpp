// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_plugin.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace {

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error{message};
}

fsim::runtime::TfInstanceIdentity instance() {
  return {.design_id = 181, .hierarchy_id = 19, .generation = 3};
}

}  // namespace

int main() {
  using fsim::runtime::TfArgument;
  using fsim::runtime::TfArgumentKind;
  using fsim::runtime::TfArgumentValue;
  using fsim::runtime::TfFunctionResultKind;
  using fsim::runtime::TfValueKind;
  using fsim::runtime::load_tf_plugin;
  using fsim::runtime::tf_plugin_artifact_loaded;

  const std::filesystem::path c_path{FSIM_TF_C_PLUGIN_PATH};
  const std::filesystem::path cpp_path{FSIM_TF_CPP_PLUGIN_PATH};
  require(!tf_plugin_artifact_loaded(c_path) &&
              !tf_plugin_artifact_loaded(cpp_path),
          "TF probe images start unloaded");

  auto c_plugin = load_tf_plugin(c_path);
  auto cpp_plugin = load_tf_plugin(cpp_path);
  require(c_plugin && cpp_plugin &&
              c_plugin.value->metadata().name == "fsim-tf-link-probe" &&
              cpp_plugin.value->metadata().name == "fsim-tf-cpp-probe" &&
              c_plugin.value->registrations().size() == 2U &&
              cpp_plugin.value->registrations().size() == 2U &&
              tf_plugin_artifact_loaded(c_path) &&
              tf_plugin_artifact_loaded(cpp_path),
          "independent C and C++ TF images load through one direct-v3 ABI");

  auto c_task = c_plugin.value->bind(0, {}, instance());
  auto c_function = c_plugin.value->bind(1, {}, instance());
  const std::array task_arguments{TfArgument{
      .kind = TfArgumentKind::ReadWrite,
      .width = 8,
      .is_signed = false,
      .lhs_select = -1,
      .rhs_select = -1,
      .expression = "state"}};
  const std::array function_arguments{TfArgument{
      .kind = TfArgumentKind::ReadOnly,
      .width = 8,
      .is_signed = false,
      .lhs_select = -1,
      .rhs_select = -1,
      .expression = "input"}};
  auto cpp_task = cpp_plugin.value->bind(0, task_arguments, instance());
  auto cpp_function =
      cpp_plugin.value->bind(1, function_arguments, instance());
  require(c_task && c_function && cpp_task && cpp_function &&
              c_function.value->result_width() == 17U &&
              cpp_function.value->result_width() == 12U,
          "C and C++ TF descriptors bind tasks and sized functions");

  c_plugin.value.reset();
  cpp_plugin.value.reset();
  require(tf_plugin_artifact_loaded(c_path) &&
              tf_plugin_artifact_loaded(cpp_path),
          "bound calls retain both native images after loader release");

  const std::array task_values{TfArgumentValue{
      .kind = TfValueKind::Integral,
      .vector_words = {{.avalbits = 4, .bvalbits = 0}},
      .real = 0.0,
      .string = {}}};
  const std::array function_values{TfArgumentValue{
      .kind = TfValueKind::Integral,
      .vector_words = {{.avalbits = 9, .bvalbits = 0}},
      .real = 0.0,
      .string = {}}};
  const auto c_task_result = c_task.value->invoke();
  const auto c_function_result = c_function.value->invoke();
  const auto cpp_task_result = cpp_task.value->invoke(task_values);
  const auto cpp_function_result = cpp_function.value->invoke(function_values);
  require(c_task_result && c_task_result.callback_value == 0 &&
              c_function_result && c_function_result.function_result &&
              c_function_result.function_result->kind ==
                  TfFunctionResultKind::Integral &&
              c_function_result.function_result->aval_words[0] == 23U &&
              cpp_task_result && cpp_task_result.argument_updates.size() == 1U &&
              cpp_task_result.argument_updates[0].value.vector_words[0]
                      .avalbits == 7 &&
              cpp_function_result && cpp_function_result.function_result &&
              cpp_function_result.function_result->kind ==
                  TfFunctionResultKind::Integral &&
              cpp_function_result.function_result->aval_words[0] == 19U,
          "C and C++ callbacks execute with equivalent value/result ownership");

  c_task.value.reset();
  c_function.value.reset();
  cpp_task.value.reset();
  cpp_function.value.reset();
  require(!tf_plugin_artifact_loaded(c_path) &&
              !tf_plugin_artifact_loaded(cpp_path),
          "both native images unload after their final bound call");
  return 0;
}
