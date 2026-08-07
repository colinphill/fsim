// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_plugin.hpp"

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
  fsim_vhpi_handle_v1 input_handle{};
  std::uint64_t argument{};
  std::uint64_t user_data{};
  std::string text;

  friend bool operator==(const ServiceCall&, const ServiceCall&) = default;
};

struct ReferenceHost {
  std::vector<ServiceCall> calls;
  std::string report_code;
  std::string report_message;
  std::uint32_t reports{};
  bool unloaded{};
};

void require_reference(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void FSIM_VHPI_CALL reference_report(
    void* const context,
    const fsim_vhpi_error_view_v1* const report) {
  auto& host = *static_cast<ReferenceHost*>(context);
  host.report_code.assign(report->code, report->code_size);
  host.report_message.assign(report->message, report->message_size);
  ++host.reports;
}

fsim_vhpi_status_v1 FSIM_VHPI_CALL reference_invoke(
    void* const context,
    const fsim_vhpi_service_request_v1* const request,
    fsim_vhpi_service_result_v1* const result) {
  if (context == nullptr || request == nullptr || result == nullptr
      || request->struct_size < sizeof(*request)
      || result->struct_size < sizeof(*result)
      || request->operation < FSIM_VHPI_SERVICE_HIERARCHY
      || request->operation > FSIM_VHPI_SERVICE_LIFECYCLE
      || request->text == nullptr || request->text_size > 4096U) {
    return FSIM_VHPI_STATUS_INVALID_ARGUMENT;
  }
  auto& host = *static_cast<ReferenceHost*>(context);
  if (host.unloaded) {
    return FSIM_VHPI_STATUS_STALE_HANDLE;
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
    return FSIM_VHPI_STATUS_RESOURCE_LIMIT;
  }
  result->struct_size =
      static_cast<std::uint32_t>(sizeof(*result));
  result->status = FSIM_VHPI_STATUS_OK;
  result->flags = request->flags;
  result->reserved = 0;
  result->handle = UINT64_C(0x5000) + request->operation;
  result->value = request->argument + 1U;
  result->user_data = request->user_data ^ UINT64_C(0x1580000);
  return FSIM_VHPI_STATUS_OK;
}

std::vector<ServiceCall> exercise_reference(
    const std::filesystem::path& path,
    const std::string_view expected_name,
    const std::string_view expected_report,
    const std::uint64_t user_data_base,
    const std::uint64_t simulation_identity) {
  constexpr std::array expected_text{
      std::string_view{"hierarchy"},
      std::string_view{"type"},
      std::string_view{"value"},
      std::string_view{"driver"},
      std::string_view{"control"},
      std::string_view{"time"},
      std::string_view{"callback"},
      std::string_view{"foreign"},
      std::string_view{"association"},
      std::string_view{"io"},
      std::string_view{"user-data"},
      std::string_view{"checkpoint"},
      std::string_view{"start-lifecycle"},
  };
  ReferenceHost service;
  auto host = make_vhdl_vhpi_host_v2(
      simulation_identity,
      &service,
      reference_report,
      &service,
      reference_invoke);
  auto loaded = load_vhdl_vhpi_plugin(path, host.v1);
  require_reference(
      loaded && loaded.value->name() == expected_name
          && loaded.value->path() == path.lexically_normal()
          && service.calls.size() == expected_text.size()
          && service.reports == 1
          && service.report_code == expected_report,
      "VHPI reference image did not report, load, and invoke every service");
  for (std::size_t index = 0; index < expected_text.size(); ++index) {
    const auto operation = static_cast<std::uint32_t>(index + 1);
    const auto& call = service.calls[index];
    require_reference(
        call.operation == operation && call.flags == 0
            && (index == 0 ? call.input_handle == 0
                           : call.input_handle != 0)
            && call.argument
                == static_cast<std::uint64_t>(operation) * 10U
            && call.user_data == user_data_base + operation
            && call.text == expected_text[index],
        "VHPI reference image service request lost public ABI fields");
  }
  require_reference(
      loaded.value->shutdown()
          && service.calls.size() == expected_text.size() + 1
          && service.calls.back().operation
              == FSIM_VHPI_SERVICE_LIFECYCLE
          && service.calls.back().flags == 1
          && service.calls.back().text == "shutdown-lifecycle",
      "VHPI reference image shutdown lifecycle was not exact");
  require_reference(
      loaded.value->shutdown()
          && service.calls.size() == expected_text.size() + 1,
      "VHPI reference image shutdown was not exactly once");
  const auto count_before_unload = service.calls.size();
  loaded.value.reset();
  service.unloaded = true;
  require_reference(
      service.calls.size() == count_before_unload,
      "VHPI reference image invoked the host after unload");

  const auto v1 = make_vhdl_vhpi_host(
      simulation_identity + 100U, &service, reference_report);
  require_reference(
      load_vhdl_vhpi_plugin(path, v1).error
          == VhdlVhpiPluginError::BindFailure,
      "VHPI service image accepted a reporting-only v1 host");
  return service.calls;
}

}  // namespace

void test_vhdl_vhpi_reference_plugins() {
  static_assert(sizeof(fsim_vhpi_host_v1) == 40);
  static_assert(offsetof(fsim_vhpi_host_v2, v1) == 0);
  static_assert(sizeof(fsim_vhpi_host_v2) == 56);
  static_assert(sizeof(fsim_vhpi_service_request_v1)
      == (sizeof(void*) == 8 ? 48 : 40));
  static_assert(sizeof(fsim_vhpi_service_result_v1) == 40);

  ReferenceHost service;
  auto host = make_vhdl_vhpi_host_v2(
      1'582, &service, reference_report, &service, reference_invoke);
  require_reference(
      validate_vhdl_vhpi_host(host.v1) == VhdlVhpiAbiError::None,
      "VHPI v2 reference host validation failed");
  host.service_context = nullptr;
  require_reference(
      validate_vhdl_vhpi_host(host.v1)
          == VhdlVhpiAbiError::ServiceContext,
      "VHPI v2 missing service context was accepted");
  host.service_context = &service;
  host.invoke_service = nullptr;
  require_reference(
      validate_vhdl_vhpi_host(host.v1)
          == VhdlVhpiAbiError::ServiceCallback,
      "VHPI v2 missing service callback was accepted");

  enum class Engine { Interpreter, CompiledO0, CompiledO2 };
  std::vector<std::vector<ServiceCall>> c_transcripts;
  std::vector<std::vector<ServiceCall>> cpp_transcripts;
  std::uint64_t engine_identity = 1'582;
  for (const auto engine : {
           Engine::Interpreter, Engine::CompiledO0, Engine::CompiledO2}) {
    static_cast<void>(engine);
    c_transcripts.push_back(exercise_reference(
        FSIM_VHPI_REFERENCE_C_PLUGIN_PATH,
        "fsim-vhpi-reference-c",
        "FSIM-VHPI-REFERENCE-C",
        UINT64_C(0xc000),
        engine_identity));
    cpp_transcripts.push_back(exercise_reference(
        FSIM_VHPI_REFERENCE_CPP_PLUGIN_PATH,
        "fsim-vhpi-reference-cpp",
        "FSIM-VHPI-REFERENCE-CPP",
        UINT64_C(0xc770),
        engine_identity + 10U));
    ++engine_identity;
  }
  require_reference(
      c_transcripts[0] == c_transcripts[1]
          && c_transcripts[1] == c_transcripts[2]
          && cpp_transcripts[0] == cpp_transcripts[1]
          && cpp_transcripts[1] == cpp_transcripts[2],
      "VHPI interpreter and compiled O0/O2 transcripts differ");

  const auto relocation_root =
      std::filesystem::temp_directory_path()
      / "fsim-vhpi-reference-relocated";
  std::error_code error;
  std::filesystem::remove_all(relocation_root, error);
  error.clear();
  require_reference(
      std::filesystem::create_directories(relocation_root, error)
          && !error,
      "VHPI relocation root creation failed");
  const auto relocated_c = relocation_root
      / std::filesystem::path{FSIM_VHPI_REFERENCE_C_PLUGIN_PATH}
            .filename();
  const auto relocated_cpp = relocation_root
      / std::filesystem::path{FSIM_VHPI_REFERENCE_CPP_PLUGIN_PATH}
            .filename();
  std::filesystem::copy_file(
      FSIM_VHPI_REFERENCE_C_PLUGIN_PATH,
      relocated_c,
      std::filesystem::copy_options::overwrite_existing,
      error);
  require_reference(!error, "VHPI C image relocation failed");
  std::filesystem::copy_file(
      FSIM_VHPI_REFERENCE_CPP_PLUGIN_PATH,
      relocated_cpp,
      std::filesystem::copy_options::overwrite_existing,
      error);
  require_reference(!error, "VHPI C++ image relocation failed");
  require_reference(
      exercise_reference(
          relocated_c,
          "fsim-vhpi-reference-c",
          "FSIM-VHPI-REFERENCE-C",
          UINT64_C(0xc000),
          1'586)
              == c_transcripts.front()
          && exercise_reference(
                 relocated_cpp,
                 "fsim-vhpi-reference-cpp",
                 "FSIM-VHPI-REFERENCE-CPP",
                 UINT64_C(0xc770),
                 1'587)
              == cpp_transcripts.front(),
      "VHPI relocated image transcripts differ");
  std::filesystem::remove_all(relocation_root, error);

  ReferenceHost malformed;
  fsim_vhpi_service_request_v1 request{};
  fsim_vhpi_service_result_v1 result{};
  request.struct_size = static_cast<std::uint32_t>(sizeof(request));
  request.operation = FSIM_VHPI_SERVICE_VALUE;
  request.text = "value";
  request.text_size = 5;
  result.struct_size = static_cast<std::uint32_t>(sizeof(result));
  require_reference(
      reference_invoke(nullptr, &request, &result)
              == FSIM_VHPI_STATUS_INVALID_ARGUMENT
          && reference_invoke(&malformed, nullptr, &result)
              == FSIM_VHPI_STATUS_INVALID_ARGUMENT,
      "VHPI service boundary accepted null ownership or request");
  auto bad_request = request;
  --bad_request.struct_size;
  require_reference(
      reference_invoke(&malformed, &bad_request, &result)
          == FSIM_VHPI_STATUS_INVALID_ARGUMENT,
      "VHPI service boundary accepted a truncated request");
  bad_request = request;
  bad_request.operation = 0;
  require_reference(
      reference_invoke(&malformed, &bad_request, &result)
          == FSIM_VHPI_STATUS_INVALID_ARGUMENT,
      "VHPI service boundary accepted an unknown family");
  bad_request = request;
  bad_request.text = nullptr;
  require_reference(
      reference_invoke(&malformed, &bad_request, &result)
          == FSIM_VHPI_STATUS_INVALID_ARGUMENT,
      "VHPI service boundary accepted missing bounded text");
  auto bad_result = result;
  --bad_result.struct_size;
  require_reference(
      reference_invoke(&malformed, &request, &bad_result)
          == FSIM_VHPI_STATUS_INVALID_ARGUMENT,
      "VHPI service boundary accepted a truncated result");
  malformed.unloaded = true;
  require_reference(
      reference_invoke(&malformed, &request, &result)
          == FSIM_VHPI_STATUS_STALE_HANDLE,
      "VHPI service boundary accepted a post-unload request");
}

}  // namespace fsim::tests::runtime
