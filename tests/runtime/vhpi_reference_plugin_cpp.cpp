// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_abi.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace {

const fsim_vhpi_host_v2* host;
std::uint32_t service_calls;
std::uint64_t checksum;

fsim_vhpi_status_v1 call_service(
    const std::uint32_t operation,
    const std::uint32_t flags,
    const std::string_view text) {
  fsim_vhpi_service_request_v1 request{};
  request.struct_size = static_cast<std::uint32_t>(sizeof(request));
  request.operation = operation;
  request.flags = flags;
  request.text_size = static_cast<std::uint32_t>(text.size());
  request.handle = service_calls == 0 ? 0 : checksum;
  request.argument = static_cast<std::uint64_t>(operation) * 10U;
  request.user_data = UINT64_C(0xc770) + operation;
  request.text = text.data();
  fsim_vhpi_service_result_v1 result{};
  result.struct_size = static_cast<std::uint32_t>(sizeof(result));
  if (host->invoke_service(
          host->service_context, &request, &result)
          != FSIM_VHPI_STATUS_OK
      || result.status != FSIM_VHPI_STATUS_OK
      || result.struct_size < sizeof(result)
      || result.reserved != 0U) {
    return FSIM_VHPI_STATUS_INTERNAL_ERROR;
  }
  ++service_calls;
  checksum ^= result.handle;
  checksum ^= result.value;
  checksum ^= result.user_data;
  return FSIM_VHPI_STATUS_OK;
}

void report_startup() {
  constexpr std::string_view code{"FSIM-VHPI-REFERENCE-CPP"};
  constexpr std::string_view message{"independent C++ image startup"};
  const fsim_vhpi_error_view_v1 report{
      FSIM_VHPI_ERROR_NOTE,
      static_cast<std::uint32_t>(code.size()),
      code.data(),
      static_cast<std::uint32_t>(message.size()),
      message.data(),
  };
  host->v1.report(host->v1.context, &report);
}

fsim_vhpi_status_v1 FSIM_VHPI_CALL startup(void* context) {
  constexpr std::array names{
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
  host = static_cast<const fsim_vhpi_host_v2*>(context);
  report_startup();
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto operation = static_cast<std::uint32_t>(index + 1);
    const auto status = call_service(operation, 0, names[index]);
    if (status != FSIM_VHPI_STATUS_OK) {
      return status;
    }
  }
  return FSIM_VHPI_STATUS_OK;
}

fsim_vhpi_status_v1 FSIM_VHPI_CALL shutdown(void*) {
  return call_service(
      FSIM_VHPI_SERVICE_LIFECYCLE, 1, "shutdown-lifecycle");
}

}  // namespace

extern "C" FSIM_VHPI_EXPORT fsim_vhpi_status_v1 FSIM_VHPI_CALL
fsim_vhpi_plugin_bind_v1(
    const fsim_vhpi_host_v1* const host_v1,
    fsim_vhpi_plugin_v1* const plugin) {
  static constexpr char name[] = "fsim-vhpi-reference-cpp";
  if (host_v1 == nullptr || plugin == nullptr
      || host_v1->abi_version != FSIM_VHPI_HOST_ABI_VERSION_V2
      || host_v1->struct_size < sizeof(fsim_vhpi_host_v2)) {
    return FSIM_VHPI_STATUS_UNSUPPORTED;
  }
  host = reinterpret_cast<const fsim_vhpi_host_v2*>(host_v1);
  service_calls = 0;
  checksum = 0;
  *plugin = {
      FSIM_VHPI_PLUGIN_ABI_VERSION,
      static_cast<std::uint32_t>(sizeof(fsim_vhpi_plugin_v1)),
      0,
      static_cast<std::uint32_t>(sizeof(name) - 1),
      name,
      const_cast<fsim_vhpi_host_v2*>(host),
      startup,
      shutdown,
  };
  return FSIM_VHPI_STATUS_OK;
}
