// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <array>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace {

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] bool checked_doubles(
    const double* const pointer, const std::size_t count,
    const fsim::runtime::TfNativePointerAccess access) noexcept {
  return pointer != nullptr && count != 0 &&
         fsim::runtime::validate_tf_native_pointer(
             pointer, count * sizeof(double), access) ==
             fsim::runtime::TfContainmentError::None;
}

[[nodiscard]] bool resolve_metadata(
    const handle object, std::uint64_t& vpi,
    fsim_acc_read_result_v3& metadata) noexcept {
  vpi = fsim_acc_handle_to_vpi_v3(object);
  const auto* const active = fsim::runtime::acc_detail::current_context();
  if (vpi == 0 || active == nullptr) return false;
  fsim_acc_read_query_v3 query{};
  query.abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  query.struct_size = sizeof(query);
  query.operation = FSIM_ACC_READ_OBJECT;
  query.object = vpi;
  metadata = {};
  metadata.abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  metadata.struct_size = sizeof(metadata);
  try {
    PLI_INT32 current_type{};
    return active->read(active->user_data, &query, &metadata) ==
               FSIM_ACC_VPI_OBJECT_VALID &&
           metadata.abi_version == FSIM_ACC_READ_QUERY_ABI_VERSION &&
           metadata.struct_size >= sizeof(metadata) && metadata.reserved == 0 &&
           metadata.type > 0 && metadata.full_type > 0 &&
           (metadata.timing_capabilities & ~FSIM_ACC_TIMING_CAP_ALL) == 0 &&
           metadata.timing_delay_count <= FSIM_ACC_TIMING_MAXIMUM_DELAYS &&
           active->resolve(active->user_data, vpi, &current_type) ==
               FSIM_ACC_VPI_OBJECT_VALID &&
           current_type == metadata.full_type;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool is_path_type(const PLI_INT32 type) noexcept {
  return type == accModPath || type == accInterModPath ||
         type == accDataPath;
}

[[nodiscard]] bool is_timing_check_type(const PLI_INT32 type) noexcept {
  return fsim::runtime::acc_detail::type_matches(type, accTchk);
}

[[nodiscard]] bool is_primitive_type(const PLI_INT32 type) noexcept {
  return fsim::runtime::acc_detail::type_matches(type, accPrimitive);
}

[[nodiscard]] bool delay_target(
    const fsim_acc_read_result_v3& metadata) noexcept {
  const bool input_port =
      (fsim::runtime::acc_detail::type_matches(metadata.type, accPort) ||
       fsim::runtime::acc_detail::type_matches(metadata.full_type, accPort)) &&
      metadata.direction == accInput;
  return (metadata.timing_capabilities & FSIM_ACC_TIMING_CAP_DELAYS) != 0 &&
         (is_path_type(metadata.type) || is_path_type(metadata.full_type) ||
          is_timing_check_type(metadata.type) ||
          is_timing_check_type(metadata.full_type) ||
          is_primitive_type(metadata.type) ||
          is_primitive_type(metadata.full_type) || input_port);
}

[[nodiscard]] bool path_target(
    const fsim_acc_read_result_v3& metadata,
    const std::uint32_t capability) noexcept {
  return (metadata.timing_capabilities & capability) != 0 &&
         (is_path_type(metadata.type) || is_path_type(metadata.full_type));
}

[[nodiscard]] std::optional<std::uint32_t> path_delay_count() noexcept {
  const auto configured =
      fsim::runtime::acc_detail::configuration_value(accPathDelayCount);
  if (!configured.has_value()) return std::nullopt;
  if (configured->empty() || *configured == "6") return 6U;
  if (*configured == "1") return 1U;
  if (*configured == "2") return 2U;
  if (*configured == "3") return 3U;
  if (*configured == "12") return 12U;
  return std::nullopt;
}

[[nodiscard]] std::optional<bool> min_typ_max() noexcept {
  const auto configured =
      fsim::runtime::acc_detail::configuration_value(accMinTypMaxDelays);
  if (!configured.has_value()) return std::nullopt;
  if (configured->empty() || *configured == "false") return false;
  if (*configured == "true") return true;
  return std::nullopt;
}

[[nodiscard]] std::optional<PLI_INT32> to_hiz_policy() noexcept {
  const auto configured =
      fsim::runtime::acc_detail::configuration_value(accToHiZDelay);
  if (!configured.has_value()) return std::nullopt;
  if (configured->empty() || *configured == "from_user") {
    return static_cast<PLI_INT32>(FSIM_ACC_TIMING_TO_HIZ_FROM_USER);
  }
  if (*configured == "average") {
    return static_cast<PLI_INT32>(FSIM_ACC_TIMING_TO_HIZ_AVERAGE);
  }
  if (*configured == "maximum") {
    return static_cast<PLI_INT32>(FSIM_ACC_TIMING_TO_HIZ_MAXIMUM);
  }
  if (*configured == "minimum") {
    return static_cast<PLI_INT32>(FSIM_ACC_TIMING_TO_HIZ_MINIMUM);
  }
  return std::nullopt;
}

struct PreparedTiming {
  std::uint64_t vpi{};
  fsim_acc_read_result_v3 metadata{};
  std::uint32_t delay_count{};
  bool min_typ_max{};
  PLI_INT32 to_hiz{};
};

[[nodiscard]] bool prepare_timing(const handle object,
                                  PreparedTiming& prepared) noexcept {
  if (!resolve_metadata(object, prepared.vpi, prepared.metadata)) return false;
  const auto configured_mtm = min_typ_max();
  const auto configured_hiz = to_hiz_policy();
  if (!configured_mtm.has_value() || !configured_hiz.has_value()) return false;
  prepared.min_typ_max = *configured_mtm;
  prepared.to_hiz = *configured_hiz;
  if (is_path_type(prepared.metadata.type) ||
      is_path_type(prepared.metadata.full_type)) {
    const auto configured_count = path_delay_count();
    if (!configured_count.has_value() ||
        *configured_count > prepared.metadata.timing_delay_count) {
      return false;
    }
    prepared.delay_count = *configured_count;
  } else {
    prepared.delay_count = prepared.metadata.timing_delay_count;
  }
  return prepared.delay_count != 0 &&
         prepared.delay_count <= FSIM_ACC_TIMING_MAXIMUM_DELAYS;
}

[[nodiscard]] bool finite_delays(const std::vector<double>& values) noexcept {
  for (const auto value : values) {
    if (!std::isfinite(value) || value < 0.0) return false;
  }
  return true;
}

[[nodiscard]] bool collect_delay_values(va_list arguments,
                                        const PreparedTiming& prepared,
                                        std::vector<double>& values) noexcept {
  try {
    const auto count = prepared.delay_count *
                       (prepared.min_typ_max ? UINT32_C(3) : UINT32_C(1));
    values.reserve(count);
    if (prepared.min_typ_max) {
      const auto* const source = va_arg(arguments, const double*);
      if (!checked_doubles(source, count,
                           fsim::runtime::TfNativePointerAccess::Read)) {
        return false;
      }
      values.assign(source, source + count);
    } else {
      for (std::uint32_t index = 0; index < count; ++index) {
        values.push_back(va_arg(arguments, double));
      }
    }
    return finite_delays(values);
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool collect_delay_destinations(
    va_list arguments, const PreparedTiming& prepared,
    std::vector<double*>& destinations) noexcept {
  try {
    const auto count = prepared.delay_count *
                       (prepared.min_typ_max ? UINT32_C(3) : UINT32_C(1));
    if (prepared.min_typ_max) {
      auto* const destination = va_arg(arguments, double*);
      if (!checked_doubles(destination, count,
                           fsim::runtime::TfNativePointerAccess::Write)) {
        return false;
      }
      destinations.push_back(destination);
    } else {
      destinations.reserve(count);
      for (std::uint32_t index = 0; index < count; ++index) {
        auto* const destination = va_arg(arguments, double*);
        if (!checked_doubles(destination, 1,
                             fsim::runtime::TfNativePointerAccess::Write)) {
          return false;
        }
        destinations.push_back(destination);
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool call_timing(
    const PreparedTiming& prepared, const std::uint32_t operation,
    const std::vector<double>& input,
    fsim_acc_timing_result_v3& result) noexcept {
  const auto* const active = fsim::runtime::acc_detail::current_context();
  if (active == nullptr) return false;
  fsim_acc_timing_query_v3 query{};
  query.abi_version = FSIM_ACC_TIMING_QUERY_ABI_VERSION;
  query.struct_size = sizeof(query);
  query.operation = operation;
  query.object = prepared.vpi;
  query.object_type = prepared.metadata.type;
  query.object_full_type = prepared.metadata.full_type;
  query.delay_count = prepared.delay_count;
  query.min_typ_max = prepared.min_typ_max ? 1U : 0U;
  query.to_hiz_policy = prepared.to_hiz;
  query.value_count = static_cast<std::uint32_t>(input.size());
  query.values = input.empty() ? nullptr : input.data();
  result = {};
  result.abi_version = FSIM_ACC_TIMING_QUERY_ABI_VERSION;
  result.struct_size = sizeof(result);
  try {
    return active->timing(active->user_data, &query, &result) ==
               FSIM_ACC_VPI_OBJECT_VALID &&
           result.abi_version == FSIM_ACC_TIMING_QUERY_ABI_VERSION &&
           result.struct_size >= sizeof(result) && result.reserved == 0 &&
           result.reserved2 == 0 && result.reserved3 == 0;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] PLI_INT32 mutate_delays(const handle object,
                                      const std::uint32_t operation,
                                      va_list arguments) noexcept {
  PreparedTiming prepared;
  std::vector<double> values;
  fsim_acc_timing_result_v3 result{};
  const bool valid = prepare_timing(object, prepared) &&
                     delay_target(prepared.metadata) &&
                     collect_delay_values(arguments, prepared, values) &&
                     call_timing(prepared, operation, values, result) &&
                     result.value_count == 0 && result.values == nullptr;
  publish_error(!valid);
  return valid ? 1 : 0;
}

[[nodiscard]] PLI_INT32 mutate_pulse(
    const handle object, const std::uint32_t operation,
    const double first_reject, const double first_error,
    va_list arguments) noexcept {
  PreparedTiming prepared;
  std::vector<double> values;
  fsim_acc_timing_result_v3 result{};
  if (!prepare_timing(object, prepared) ||
      !path_target(prepared.metadata, FSIM_ACC_TIMING_CAP_PULSE)) {
    publish_error(true);
    return 0;
  }
  try {
    values.reserve(prepared.delay_count * 2U);
    values.push_back(first_reject);
    values.push_back(first_error);
    for (std::uint32_t index = 1; index < prepared.delay_count; ++index) {
      values.push_back(va_arg(arguments, double));
      values.push_back(va_arg(arguments, double));
    }
  } catch (...) {
    publish_error(true);
    return 0;
  }
  bool ordered = finite_delays(values);
  for (std::size_t index = 0; ordered && index < values.size(); index += 2U) {
    ordered = values[index] <= values[index + 1U];
  }
  const bool valid = ordered && call_timing(prepared, operation, values, result) &&
                     result.value_count == 0 && result.values == nullptr;
  publish_error(!valid);
  return valid ? 1 : 0;
}

}  // namespace

extern "C" {

PLI_INT32 acc_append_delays(const handle object, ...) {
  va_list arguments;
  va_start(arguments, object);
  const auto result =
      mutate_delays(object, FSIM_ACC_TIMING_APPEND_DELAYS, arguments);
  va_end(arguments);
  return result;
}

PLI_INT32 acc_replace_delays(const handle object, ...) {
  va_list arguments;
  va_start(arguments, object);
  const auto result =
      mutate_delays(object, FSIM_ACC_TIMING_REPLACE_DELAYS, arguments);
  va_end(arguments);
  return result;
}

PLI_INT32 acc_fetch_delays(const handle object, ...) {
  PreparedTiming prepared;
  if (!prepare_timing(object, prepared) || !delay_target(prepared.metadata)) {
    publish_error(true);
    return 0;
  }
  va_list arguments;
  va_start(arguments, object);
  std::vector<double*> destinations;
  const bool destination_ok =
      collect_delay_destinations(arguments, prepared, destinations);
  va_end(arguments);
  fsim_acc_timing_result_v3 result{};
  const auto expected = prepared.delay_count *
                        (prepared.min_typ_max ? UINT32_C(3) : UINT32_C(1));
  if (!destination_ok ||
      !call_timing(prepared, FSIM_ACC_TIMING_FETCH_DELAYS, {}, result) ||
      result.value_count != expected ||
      !checked_doubles(result.values, expected,
                       fsim::runtime::TfNativePointerAccess::Read)) {
    publish_error(true);
    return 0;
  }
  std::vector<double> values;
  try {
    values.assign(result.values, result.values + expected);
  } catch (...) {
    publish_error(true);
    return 0;
  }
  if (!finite_delays(values)) {
    publish_error(true);
    return 0;
  }
  if (prepared.min_typ_max) {
    for (std::uint32_t index = 0; index < expected; ++index) {
      destinations.front()[index] = values[index];
    }
  } else {
    for (std::uint32_t index = 0; index < expected; ++index) {
      *destinations[index] = values[index];
    }
  }
  publish_error(false);
  return 1;
}

PLI_INT32 acc_append_pulsere(const handle object, const double first_reject,
                             const double first_error, ...) {
  va_list arguments;
  va_start(arguments, first_error);
  const auto result = mutate_pulse(object, FSIM_ACC_TIMING_APPEND_PULSE,
                                   first_reject, first_error, arguments);
  va_end(arguments);
  return result;
}

PLI_INT32 acc_replace_pulsere(const handle object, const double first_reject,
                              const double first_error, ...) {
  va_list arguments;
  va_start(arguments, first_error);
  const auto result = mutate_pulse(object, FSIM_ACC_TIMING_REPLACE_PULSE,
                                   first_reject, first_error, arguments);
  va_end(arguments);
  return result;
}

PLI_INT32 acc_fetch_pulsere(const handle object, double* const first_reject,
                            double* const first_error, ...) {
  PreparedTiming prepared;
  if (!prepare_timing(object, prepared) ||
      !path_target(prepared.metadata, FSIM_ACC_TIMING_CAP_PULSE)) {
    publish_error(true);
    return 0;
  }
  std::vector<double*> destinations;
  try {
    destinations.reserve(prepared.delay_count * 2U);
    destinations.push_back(first_reject);
    destinations.push_back(first_error);
    va_list arguments;
    va_start(arguments, first_error);
    for (std::uint32_t index = 1; index < prepared.delay_count; ++index) {
      destinations.push_back(va_arg(arguments, double*));
      destinations.push_back(va_arg(arguments, double*));
    }
    va_end(arguments);
  } catch (...) {
    publish_error(true);
    return 0;
  }
  for (auto* const destination : destinations) {
    if (!checked_doubles(destination, 1,
                         fsim::runtime::TfNativePointerAccess::Write)) {
      publish_error(true);
      return 0;
    }
  }
  fsim_acc_timing_result_v3 result{};
  const auto expected = prepared.delay_count * 2U;
  if (!call_timing(prepared, FSIM_ACC_TIMING_FETCH_PULSE, {}, result) ||
      result.value_count != expected ||
      !checked_doubles(result.values, expected,
                       fsim::runtime::TfNativePointerAccess::Read)) {
    publish_error(true);
    return 0;
  }
  std::vector<double> values;
  try {
    values.assign(result.values, result.values + expected);
  } catch (...) {
    publish_error(true);
    return 0;
  }
  bool ordered = finite_delays(values);
  for (std::size_t index = 0; ordered && index < values.size(); index += 2U) {
    ordered = values[index] <= values[index + 1U];
  }
  if (!ordered) {
    publish_error(true);
    return 0;
  }
  for (std::uint32_t index = 0; index < expected; ++index) {
    *destinations[index] = values[index];
  }
  publish_error(false);
  return 1;
}

PLI_INT32 acc_set_pulsere(const handle object, const double reject,
                          const double error) {
  PreparedTiming prepared;
  fsim_acc_timing_result_v3 result{};
  const std::vector<double> values{reject, error};
  const bool valid = prepare_timing(object, prepared) &&
                     path_target(prepared.metadata,
                                 FSIM_ACC_TIMING_CAP_PULSE) &&
                     finite_delays(values) && reject <= error && error <= 100.0 &&
                     call_timing(prepared,
                                 FSIM_ACC_TIMING_SET_PULSE_PERCENT, values,
                                 result) &&
                     result.value_count == 0 && result.values == nullptr;
  publish_error(!valid);
  return valid ? 1 : 0;
}

PLI_INT32 acc_fetch_delay_mode(const handle object) {
  PreparedTiming prepared;
  fsim_acc_timing_result_v3 result{};
  const bool valid = prepare_timing(object, prepared) &&
                     delay_target(prepared.metadata) &&
                     call_timing(prepared,
                                 FSIM_ACC_TIMING_FETCH_DELAY_MODE, {}, result) &&
                     result.value_count == 0 && result.values == nullptr &&
                     result.delay_mode >= accDelayModeNone &&
                     result.delay_mode <= accDelayModeVeritime;
  publish_error(!valid);
  return valid ? result.delay_mode : accDelayModeNone;
}

PLI_INT32 acc_fetch_polarity(const handle object) {
  PreparedTiming prepared;
  fsim_acc_timing_result_v3 result{};
  const bool valid = prepare_timing(object, prepared) &&
                     path_target(prepared.metadata,
                                 FSIM_ACC_TIMING_CAP_POLARITY) &&
                     call_timing(prepared, FSIM_ACC_TIMING_FETCH_POLARITY, {},
                                 result) &&
                     result.value_count == 0 && result.values == nullptr &&
                     (result.polarity == accPositive ||
                      result.polarity == accNegative ||
                      result.polarity == accUnknown);
  publish_error(!valid);
  return valid ? result.polarity : accUnknown;
}

}  // extern "C"
