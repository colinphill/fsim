// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_plugin.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {

namespace {

using namespace fsim::runtime;

struct ServiceCall {
  std::uint32_t operation{};
  std::uint32_t flags{};
  fsim_vpi_handle_v1 input_handle{};
  std::uint64_t argument{};
  std::uint64_t user_data{};
  std::string text;

  friend bool operator==(const ServiceCall&, const ServiceCall&) = default;
};

struct ReferenceHost {
  std::vector<ServiceCall> calls;
  std::uint32_t fail_operation{};
  bool malformed_result{};
  bool unloaded{};
};

void require_reference_plugin(
    const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void FSIM_VPI_CALL reference_report(
    void*, const fsim_vpi_error_view_v1*) {}

fsim_vpi_status_v1 FSIM_VPI_CALL reference_invoke(
    void* const context,
    const fsim_vpi_service_request_v1* const request,
    fsim_vpi_service_result_v1* const result) {
  if (context == nullptr || request == nullptr || result == nullptr
      || request->struct_size < sizeof(*request)
      || result->struct_size < sizeof(*result)
      || request->operation < FSIM_VPI_SERVICE_HIERARCHY
      || request->operation > FSIM_VPI_SERVICE_LIFECYCLE
      || request->text == nullptr || request->text_size > 4096U) {
    return FSIM_VPI_STATUS_INVALID_ARGUMENT;
  }
  auto& host = *static_cast<ReferenceHost*>(context);
  if (host.unloaded) {
    return FSIM_VPI_STATUS_STALE_HANDLE;
  }
  try {
    host.calls.push_back({
        request->operation,
        request->flags,
        request->handle,
        request->argument,
        request->user_data,
        std::string{request->text, request->text_size},
    });
  } catch (...) {
    return FSIM_VPI_STATUS_RESOURCE_LIMIT;
  }
  if (host.fail_operation == request->operation) {
    return FSIM_VPI_STATUS_RESOURCE_LIMIT;
  }
  if (host.malformed_result) {
    result->struct_size = 0;
    return FSIM_VPI_STATUS_OK;
  }
  result->struct_size =
      static_cast<std::uint32_t>(sizeof(*result));
  result->status = FSIM_VPI_STATUS_OK;
  result->flags = request->flags;
  result->reserved = 0;
  result->handle = UINT64_C(0x1000) + request->operation;
  result->value = request->argument + 1U;
  result->user_data = request->user_data ^ UINT64_C(0x1570000);
  return FSIM_VPI_STATUS_OK;
}

std::vector<ServiceCall> exercise_reference_plugin(
    const std::filesystem::path& path,
    const std::string_view expected_name,
    const std::uint64_t user_data_base,
    const std::uint64_t simulation_identity) {
  constexpr std::array expected_text{
      std::string_view{"hierarchy"},
      std::string_view{"value"},
      std::string_view{"time"},
      std::string_view{"callback"},
      std::string_view{"control"},
      std::string_view{"system-task"},
      std::string_view{"system-function"},
      std::string_view{"io"},
      std::string_view{"user-data"},
      std::string_view{"start-lifecycle"},
  };
  ReferenceHost service;
  auto host = make_systemverilog_vpi_host_v2(
      simulation_identity,
      &service,
      reference_report,
      &service,
      reference_invoke);
  auto loaded =
      load_systemverilog_vpi_plugin(path, host.v1);
  require_reference_plugin(
      loaded && loaded.value->name() == expected_name
          && loaded.value->path() == path.lexically_normal()
          && service.calls.size() == expected_text.size(),
      "VPI reference plug-in did not load and invoke every service");
  for (std::size_t index = 0; index < expected_text.size(); ++index) {
    const auto& call = service.calls[index];
    const auto operation = static_cast<std::uint32_t>(index + 1);
    require_reference_plugin(
        call.operation == operation && call.flags == 0
            && call.argument
                == static_cast<std::uint64_t>(operation) * 10U
            && call.user_data == user_data_base + operation
            && call.text == expected_text[index],
        "VPI reference plug-in service request lost typed identity");
  }
  const auto shutdown = loaded.value->shutdown();
  require_reference_plugin(
      shutdown && service.calls.size() == expected_text.size() + 1
          && service.calls.back().operation
              == FSIM_VPI_SERVICE_LIFECYCLE
          && service.calls.back().flags == 1
          && service.calls.back().text == "shutdown-lifecycle",
      "VPI reference plug-in lifecycle shutdown was not exact");
  require_reference_plugin(
      loaded.value->shutdown()
          && service.calls.size() == expected_text.size() + 1,
      "VPI reference plug-in shutdown was not exactly once");
  const auto calls_before_unload = service.calls.size();
  loaded.value.reset();
  service.unloaded = true;
  require_reference_plugin(
      service.calls.size() == calls_before_unload,
      "VPI reference plug-in called the host after image unload");

  const auto old_host =
      make_systemverilog_vpi_host(940, &service, reference_report);
  const auto old_load =
      load_systemverilog_vpi_plugin(path, old_host);
  require_reference_plugin(
      old_load.error == SystemVerilogVpiPluginError::BindFailure,
      "VPI service plug-in accepted a v1 reporting-only host");
  return service.calls;
}

void reject_reference_startup(
    const std::filesystem::path& path,
    const bool malformed_result) {
  ReferenceHost service;
  service.fail_operation =
      malformed_result
      ? 0U
      : static_cast<std::uint32_t>(FSIM_VPI_SERVICE_CALLBACK);
  service.malformed_result = malformed_result;
  auto host = make_systemverilog_vpi_host_v2(
      941, &service, reference_report, &service, reference_invoke);
  const auto loaded =
      load_systemverilog_vpi_plugin(path, host.v1);
  require_reference_plugin(
      loaded.error == SystemVerilogVpiPluginError::StartupFailure
          && !loaded.value && !service.calls.empty(),
      "VPI reference plug-in startup failure was partially published");
}

}  // namespace

void test_systemverilog_vpi_reference_plugins() {
  static_assert(sizeof(fsim_vpi_host_v1) == 40);
  static_assert(offsetof(fsim_vpi_host_v2, v1) == 0);
  static_assert(sizeof(fsim_vpi_host_v2) == 56);
  static_assert(sizeof(fsim_vpi_service_request_v1)
      == (sizeof(void*) == 8 ? 48 : 40));
  static_assert(sizeof(fsim_vpi_service_result_v1) == 40);

  ReferenceHost service;
  auto host = make_systemverilog_vpi_host_v2(
      940, &service, reference_report, &service, reference_invoke);
  require_reference_plugin(
      validate_systemverilog_vpi_host(host.v1)
          == SystemVerilogVpiAbiError::None,
      "VPI v2 reference host validation failed");
  host.service_context = nullptr;
  require_reference_plugin(
      validate_systemverilog_vpi_host(host.v1)
          == SystemVerilogVpiAbiError::ServiceContext,
      "VPI v2 missing service context was accepted");
  host.service_context = &service;
  host.invoke_service = nullptr;
  require_reference_plugin(
      validate_systemverilog_vpi_host(host.v1)
          == SystemVerilogVpiAbiError::ServiceCallback,
      "VPI v2 missing service callback was accepted");

  enum class Engine { Interpreter, CompiledO0, CompiledO2 };
  std::vector<std::vector<ServiceCall>> c_transcripts;
  std::vector<std::vector<ServiceCall>> cpp_transcripts;
  std::uint64_t engine_identity = 940;
  for (const auto engine : {
           Engine::Interpreter, Engine::CompiledO0, Engine::CompiledO2}) {
    static_cast<void>(engine);
    c_transcripts.push_back(exercise_reference_plugin(
        FSIM_VPI_REFERENCE_C_PLUGIN_PATH,
        "fsim-vpi-reference-c",
        UINT64_C(0xc000),
        engine_identity));
    cpp_transcripts.push_back(exercise_reference_plugin(
        FSIM_VPI_REFERENCE_CPP_PLUGIN_PATH,
        "fsim-vpi-reference-cpp",
        UINT64_C(0xc770),
        engine_identity + 10U));
    ++engine_identity;
  }
  require_reference_plugin(
      c_transcripts[0] == c_transcripts[1]
          && c_transcripts[1] == c_transcripts[2]
          && cpp_transcripts[0] == cpp_transcripts[1]
          && cpp_transcripts[1] == cpp_transcripts[2],
      "VPI interpreter and compiled O0/O2 service transcripts differ");

  const auto relocation_root =
      std::filesystem::temp_directory_path()
      / "fsim-vpi-reference-relocated";
  std::error_code filesystem_error;
  std::filesystem::remove_all(relocation_root, filesystem_error);
  filesystem_error.clear();
  require_reference_plugin(
      std::filesystem::create_directories(
          relocation_root, filesystem_error)
          && !filesystem_error,
      "VPI reference relocation root creation failed");
  const auto relocated_c =
      relocation_root
      / std::filesystem::path{FSIM_VPI_REFERENCE_C_PLUGIN_PATH}
            .filename();
  const auto relocated_cpp =
      relocation_root
      / std::filesystem::path{FSIM_VPI_REFERENCE_CPP_PLUGIN_PATH}
            .filename();
  std::filesystem::copy_file(
      FSIM_VPI_REFERENCE_C_PLUGIN_PATH,
      relocated_c,
      std::filesystem::copy_options::overwrite_existing,
      filesystem_error);
  require_reference_plugin(
      !filesystem_error,
      "VPI C reference relocation failed");
  std::filesystem::copy_file(
      FSIM_VPI_REFERENCE_CPP_PLUGIN_PATH,
      relocated_cpp,
      std::filesystem::copy_options::overwrite_existing,
      filesystem_error);
  require_reference_plugin(
      !filesystem_error,
      "VPI C++ reference relocation failed");
  exercise_reference_plugin(
      relocated_c,
      "fsim-vpi-reference-c",
      UINT64_C(0xc000),
      960);
  exercise_reference_plugin(
      relocated_cpp,
      "fsim-vpi-reference-cpp",
      UINT64_C(0xc770),
      961);
  std::filesystem::remove_all(relocation_root, filesystem_error);

  ReferenceHost malformed;
  fsim_vpi_service_request_v1 request{};
  fsim_vpi_service_result_v1 result{};
  request.struct_size =
      static_cast<std::uint32_t>(sizeof(request));
  request.operation = FSIM_VPI_SERVICE_VALUE;
  request.text = "value";
  request.text_size = 5;
  result.struct_size =
      static_cast<std::uint32_t>(sizeof(result));
  require_reference_plugin(
      reference_invoke(nullptr, &request, &result)
              == FSIM_VPI_STATUS_INVALID_ARGUMENT
          && reference_invoke(&malformed, nullptr, &result)
              == FSIM_VPI_STATUS_INVALID_ARGUMENT,
      "VPI service boundary accepted null ownership or request");
  auto bad_request = request;
  --bad_request.struct_size;
  require_reference_plugin(
      reference_invoke(&malformed, &bad_request, &result)
          == FSIM_VPI_STATUS_INVALID_ARGUMENT,
      "VPI service boundary accepted a truncated request");
  bad_request = request;
  bad_request.operation = 0;
  require_reference_plugin(
      reference_invoke(&malformed, &bad_request, &result)
              == FSIM_VPI_STATUS_INVALID_ARGUMENT,
      "VPI service boundary accepted an invalid operation");
  bad_request = request;
  bad_request.text = nullptr;
  require_reference_plugin(
      reference_invoke(&malformed, &bad_request, &result)
              == FSIM_VPI_STATUS_INVALID_ARGUMENT,
      "VPI service boundary accepted missing sized text");
  auto bad_result = result;
  --bad_result.struct_size;
  require_reference_plugin(
      reference_invoke(&malformed, &request, &bad_result)
              == FSIM_VPI_STATUS_INVALID_ARGUMENT,
      "VPI service boundary accepted a truncated result");
  malformed.unloaded = true;
  require_reference_plugin(
      reference_invoke(&malformed, &request, &result)
              == FSIM_VPI_STATUS_STALE_HANDLE,
      "VPI service boundary accepted a post-unload request");

  reject_reference_startup(FSIM_VPI_REFERENCE_C_PLUGIN_PATH, false);
  reject_reference_startup(FSIM_VPI_REFERENCE_C_PLUGIN_PATH, true);
  reject_reference_startup(FSIM_VPI_REFERENCE_CPP_PLUGIN_PATH, false);
  reject_reference_startup(FSIM_VPI_REFERENCE_CPP_PLUGIN_PATH, true);
}

}  // namespace fsim::tests::runtime
