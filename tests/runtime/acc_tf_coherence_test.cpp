// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/tf_call.hpp"
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;
using Kind = fsim::runtime::SystemVerilogVpiObjectKind;

struct Fixture {
  Registry registry{37};
  std::uint64_t root{};
  std::uint64_t module{};
  std::array<std::uint64_t, 3> arguments{};
};

Fixture* fixture{};
std::array<PLI_BYTE8, 1> work_area{};
std::array<PLI_BYTE8, 5> argv0{'f', 's', 'i', 'm', 0};
std::array<PLI_BYTE8, 8> argv1{'-', '-', 's', 'e', 'e', 'd', 0, 0};
std::array<PLI_BYTE8*, 2> argv{argv0.data(), argv1.data()};
constexpr std::array<std::uint32_t, 2> argv_sizes{5, 7};
std::uint32_t callback_count{};
bool callback_ok{true};

void require(const bool condition, const char* const message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const type) {
  const auto result = static_cast<Fixture*>(user_data)->registry.lookup(object);
  if (!result) return FSIM_ACC_VPI_OBJECT_INVALID;
  switch (result.value->kind) {
    case Kind::Root: *type = accTopModule; break;
    case Kind::Module: *type = accModuleInstance; break;
    case Kind::Net: *type = accNet; break;
    case Kind::Variable: *type = accReg; break;
    case Kind::Parameter: *type = accParameter; break;
    default: *type = accScope; break;
  }
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_lookup(
    void*, std::uint32_t, std::uint64_t, const PLI_BYTE8*, std::uint32_t,
    std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_relation(
    void*, std::uint32_t, std::uint64_t, std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_name(
    void*, std::uint64_t, PLI_BYTE8*, std::uint32_t, std::uint32_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_traverse(
    void*, std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t*,
    std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_object_query(
    void*, const fsim_acc_object_query_v3*, std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_read(
    void*, const fsim_acc_read_query_v3*, fsim_acc_read_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_write(
    void*, const fsim_acc_write_query_v3*, fsim_acc_write_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_iterate(
    void*, const fsim_acc_iterator_query_v3*, fsim_acc_iterator_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_timing(
    void*, const fsim_acc_timing_query_v3*, fsim_acc_timing_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_vcl(
    void*, const fsim_acc_vcl_query_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL coherence_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason != reason_calltf || fixture == nullptr) return 1;
  const auto* const call = fsim_tf_current_call_context_v3();
  const fsim_acc_tf_context_v3 tf{
      FSIM_ACC_TF_CONTEXT_ABI_VERSION,
      sizeof(fsim_acc_tf_context_v3),
      0,
      0,
      call,
      fixture->arguments.data(),
      static_cast<std::uint32_t>(fixture->arguments.size()),
      static_cast<std::uint32_t>(argv.size()),
      argv.data(),
      argv_sizes.data(),
  };
  fsim_acc_handle_context_v3 acc{
      FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
      sizeof(fsim_acc_handle_context_v3),
      fixture->registry.simulation_identity(),
      call->instance->generation,
      64,
      0,
      fixture,
      resolve_object,
      fixture->module,
      fixture->module,
      fixture->module,
      unsupported_lookup,
      unsupported_relation,
      unsupported_name,
      unsupported_traverse,
      unsupported_object_query,
      unsupported_read,
      unsupported_write,
      unsupported_iterate,
      unsupported_timing,
      unsupported_vcl,
      &tf,
  };
  callback_ok = callback_ok && call != nullptr &&
                fsim_acc_handle_context_enter_v3(&acc) == 1;
  if (!callback_ok) return 1;

  auto* const token = tf_getinstance();
  const auto instance = acc_handle_tfinst();
  const auto first = acc_handle_tfarg(1);
  const auto second = acc_handle_itfarg(2, token);
  const auto wrong = fsim_acc_handle_from_vpi_v3(fixture->root);
  callback_ok = callback_ok && acc_fetch_argc() == 2 &&
                acc_fetch_argv() == argv.data() &&
                std::string_view{acc_fetch_argv()[0]} == "fsim" &&
                std::string_view{acc_fetch_argv()[1]} == "--seed" &&
                fsim_acc_handle_to_vpi_v3(instance) == fixture->module &&
                fsim_acc_handle_to_vpi_v3(first) == fixture->arguments[0] &&
                fsim_acc_handle_to_vpi_v3(second) == fixture->arguments[1] &&
                acc_fetch_tfarg_int(1) == 11 &&
                std::string_view{acc_fetch_tfarg_str(1)} == "11" &&
                acc_fetch_tfarg(2) == 2.5 &&
                std::string_view{acc_fetch_tfarg_str(3)} == "hello" &&
                acc_fetch_itfarg_int(1, instance) == 11 &&
                acc_fetch_itfarg(2, instance) == 2.5 &&
                std::string_view{acc_fetch_itfarg_str(3, instance)} ==
                    "hello" &&
                acc_handle_itfarg(1, reinterpret_cast<void*>(1)) == nullptr &&
                acc_error_flag == 1 && acc_fetch_itfarg_int(1, wrong) == 0 &&
                acc_error_flag == 1;

  callback_ok = callback_ok &&
                tf_getworkarea() ==
                    (callback_count == 0 ? nullptr : work_area.data()) &&
                tf_setworkarea(work_area.data()) == 0 &&
                tf_getworkarea() == work_area.data() && tf_putp(1, 29) == 0 &&
                acc_fetch_tfarg_int(1) == 29 && acc_error_flag == 0;
  ++callback_count;
  fsim_acc_handle_context_leave_v3(&acc);
  auto mismatched = tf;
  --mismatched.argument_count;
  acc.tf = &mismatched;
  callback_ok = callback_ok &&
                fsim_acc_handle_context_enter_v3(&acc) == 0 &&
                acc_error_flag == 1;
  auto malformed_argv = tf;
  constexpr std::array<std::uint32_t, 2> bad_sizes{5, 0};
  malformed_argv.argv_sizes = bad_sizes.data();
  acc.tf = &malformed_argv;
  callback_ok = callback_ok &&
                fsim_acc_handle_context_enter_v3(&acc) == 0 &&
                acc_error_flag == 1 && acc_fetch_argc() == 0 &&
                acc_error_flag == 1;
  return 0;
}

}  // namespace

int main() {
  Fixture state;
  const auto root = state.registry.create(Kind::Root, 0, "top");
  const auto module = state.registry.create(Kind::Module, root.value, "u");
  const auto net = state.registry.create(Kind::Net, module.value, "data");
  const auto variable =
      state.registry.create(Kind::Variable, module.value, "gain");
  const auto parameter =
      state.registry.create(Kind::Parameter, module.value, "message");
  require(root && module && net && variable && parameter,
          "coherence fixture publishes one shared VPI hierarchy");
  state.root = root.value;
  state.module = module.value;
  state.arguments = {net.value, variable.value, parameter.value};
  fixture = &state;

  using fsim::runtime::TfArgument;
  using fsim::runtime::TfArgumentKind;
  using fsim::runtime::TfArgumentValue;
  using fsim::runtime::TfInstanceIdentity;
  using fsim::runtime::TfRegistration;
  using fsim::runtime::TfRegistrationKind;
  using fsim::runtime::TfValueKind;
  using fsim::runtime::bind_tf_call;

  const std::array arguments{
      TfArgument{.kind = TfArgumentKind::ReadWrite,
                 .width = 8,
                 .expression = "data"},
      TfArgument{.kind = TfArgumentKind::ReadOnlyReal,
                 .width = 64,
                 .expression = "gain"},
      TfArgument{.kind = TfArgumentKind::String,
                 .width = 40,
                 .expression = "message"},
  };
  const TfRegistration registration{
      .kind = TfRegistrationKind::Task,
      .calltf = coherence_callback,
      .name = "$coherence",
  };
  auto bound = bind_tf_call(
      registration, arguments,
      TfInstanceIdentity{.design_id = state.registry.simulation_identity(),
                         .hierarchy_id = state.module,
                         .generation = 9},
      {}, {.module_instance_name = "top.u", .scope_name = "top.u"});
  require(static_cast<bool>(bound),
          "TF binding accepts the shared ACC hierarchy identity");
  const std::array values{
      TfArgumentValue{.kind = TfValueKind::Integral,
                      .vector_words = {{11, 0}},
                      .real = 0.0,
                      .string = {}},
      TfArgumentValue{.kind = TfValueKind::Real,
                      .vector_words = {},
                      .real = 2.5,
                      .string = {}},
      TfArgumentValue{.kind = TfValueKind::String,
                      .vector_words = {},
                      .real = 0.0,
                      .string = "hello"},
  };
  const auto first_result = bound.value->invoke(values);
  const auto second_result = bound.value->invoke(values);
  require(first_result && second_result && callback_ok && callback_count == 2 &&
              first_result.argument_updates.size() == 1 &&
              first_result.argument_updates[0].value.vector_words[0].avalbits ==
                  29 &&
              second_result.argument_updates.size() == 1,
          "ACC and TF share argument values, instance scope, and work area");
  fixture = nullptr;
  require(fsim_tf_current_call_context_v3() == nullptr &&
              acc_handle_tfinst() == nullptr && acc_error_flag == 1,
          "shared call state cannot escape the callback lifetime");
  return 0;
}
