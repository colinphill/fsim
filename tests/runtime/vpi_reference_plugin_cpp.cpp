// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_abi.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace {

const fsim_vpi_host_v2* host;
std::uint32_t startup_calls;
std::uint32_t shutdown_calls;
std::uint32_t service_calls;
std::uint64_t checksum;
std::uint64_t last_user_data;

fsim_vpi_status_v1 call_service(
    const std::uint32_t operation,
    const std::uint32_t flags,
    const std::string_view text) {
  fsim_vpi_service_request_v1 request{};
  request.struct_size = static_cast<std::uint32_t>(sizeof(request));
  request.operation = operation;
  request.flags = flags;
  request.text_size = static_cast<std::uint32_t>(text.size());
  request.handle = service_calls == 0 ? 0 : checksum;
  request.argument = static_cast<std::uint64_t>(operation) * 10U;
  request.user_data = UINT64_C(0xc770) + operation;
  request.text = text.data();
  fsim_vpi_service_result_v1 result{};
  result.struct_size = static_cast<std::uint32_t>(sizeof(result));
  if (host->invoke_service(
          host->service_context, &request, &result)
          != FSIM_VPI_STATUS_OK
      || result.status != FSIM_VPI_STATUS_OK
      || result.struct_size < sizeof(result)) {
    return FSIM_VPI_STATUS_INTERNAL_ERROR;
  }
  ++service_calls;
  checksum ^= result.handle;
  checksum ^= result.value;
  checksum ^= result.user_data;
  last_user_data = result.user_data;
  return FSIM_VPI_STATUS_OK;
}

fsim_vpi_status_v1 FSIM_VPI_CALL startup(void* context) {
  constexpr std::array names{
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
  host = static_cast<const fsim_vpi_host_v2*>(context);
  ++startup_calls;
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto operation = static_cast<std::uint32_t>(index + 1);
    const auto status = call_service(operation, 0, names[index]);
    if (status != FSIM_VPI_STATUS_OK) {
      return status;
    }
  }
  return FSIM_VPI_STATUS_OK;
}

fsim_vpi_status_v1 FSIM_VPI_CALL shutdown(void*) {
  ++shutdown_calls;
  return call_service(
      FSIM_VPI_SERVICE_LIFECYCLE, 1, "shutdown-lifecycle");
}

}  // namespace

extern "C" FSIM_VPI_EXPORT std::uint32_t FSIM_VPI_CALL
fsim_vpi_reference_startup_calls() {
  return startup_calls;
}

extern "C" FSIM_VPI_EXPORT std::uint32_t FSIM_VPI_CALL
fsim_vpi_reference_shutdown_calls() {
  return shutdown_calls;
}

extern "C" FSIM_VPI_EXPORT std::uint32_t FSIM_VPI_CALL
fsim_vpi_reference_service_calls() {
  return service_calls;
}

extern "C" FSIM_VPI_EXPORT std::uint64_t FSIM_VPI_CALL
fsim_vpi_reference_checksum() {
  return checksum;
}

extern "C" FSIM_VPI_EXPORT std::uint64_t FSIM_VPI_CALL
fsim_vpi_reference_last_user_data() {
  return last_user_data;
}

extern "C" FSIM_VPI_EXPORT fsim_vpi_status_v1 FSIM_VPI_CALL
fsim_vpi_plugin_bind_v1(
    const fsim_vpi_host_v1* const host_v1,
    fsim_vpi_plugin_v1* const plugin) {
  static constexpr char name[] = "fsim-vpi-reference-cpp";
  if (host_v1 == nullptr || plugin == nullptr
      || host_v1->abi_version != FSIM_VPI_HOST_ABI_VERSION_V2
      || host_v1->struct_size < sizeof(fsim_vpi_host_v2)) {
    return FSIM_VPI_STATUS_UNSUPPORTED;
  }
  host = reinterpret_cast<const fsim_vpi_host_v2*>(host_v1);
  *plugin = {
      FSIM_VPI_PLUGIN_ABI_VERSION,
      static_cast<std::uint32_t>(sizeof(fsim_vpi_plugin_v1)),
      0,
      static_cast<std::uint32_t>(sizeof(name) - 1),
      name,
      const_cast<fsim_vpi_host_v2*>(host),
      startup,
      shutdown,
  };
  return FSIM_VPI_STATUS_OK;
}
