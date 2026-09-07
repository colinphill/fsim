// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

thread_local std::string write_string_buffer;

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] bool checked_bytes(
    const void* const pointer, const std::size_t size,
    const fsim::runtime::TfNativePointerAccess access) noexcept {
  return pointer != nullptr && size != 0 &&
         fsim::runtime::validate_tf_native_pointer(pointer, size, access) ==
             fsim::runtime::TfContainmentError::None;
}

[[nodiscard]] bool copy_argument_string(const PLI_BYTE8* const pointer,
                                        std::string& output,
                                        const bool allow_empty) noexcept {
  if (pointer == nullptr) return false;
  const auto base = reinterpret_cast<std::uintptr_t>(pointer);
  try {
    output.clear();
    for (std::uint32_t offset = 0;
         offset <= FSIM_ACC_READ_MAXIMUM_STRING_BYTES; offset += 64U) {
      const auto remaining =
          FSIM_ACC_READ_MAXIMUM_STRING_BYTES + 1U - offset;
      const auto step = remaining < 64U ? remaining : 64U;
      if (offset > std::numeric_limits<std::uintptr_t>::max() - base) {
        return false;
      }
      const auto* const window = reinterpret_cast<const PLI_BYTE8*>(
          base + static_cast<std::uintptr_t>(offset));
      if (!checked_bytes(window, step,
                         fsim::runtime::TfNativePointerAccess::Read)) {
        return false;
      }
      for (std::uint32_t index = 0; index < step; ++index) {
        const auto byte = static_cast<unsigned char>(window[index]);
        if (byte == 0) {
          if (!allow_empty && output.empty() && index == 0) return false;
          output.append(window, index);
          return true;
        }
        if (byte < 0x20U || byte == 0x7fU) return false;
      }
      output.append(window, step);
    }
  } catch (...) {
  }
  return false;
}

[[nodiscard]] bool copy_result_string(const PLI_BYTE8* const pointer,
                                      const std::uint32_t size,
                                      const bool allow_empty) noexcept {
  if (size == 0) {
    if (!allow_empty) return false;
    write_string_buffer.clear();
    return true;
  }
  if (size > FSIM_ACC_READ_MAXIMUM_STRING_BYTES ||
      !checked_bytes(pointer, size,
                     fsim::runtime::TfNativePointerAccess::Read)) {
    return false;
  }
  try {
    write_string_buffer.assign(pointer, size);
  } catch (...) {
    return false;
  }
  return write_string_buffer.find('\0') == std::string::npos;
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
           metadata.width > 0 &&
           metadata.width <=
               static_cast<PLI_INT32>(FSIM_ACC_READ_MAXIMUM_BITS) &&
           (metadata.write_capabilities & ~FSIM_ACC_WRITE_CAP_ALL) == 0 &&
           active->resolve(active->user_data, vpi, &current_type) ==
               FSIM_ACC_VPI_OBJECT_VALID &&
           current_type == metadata.full_type;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool is_net(const PLI_INT32 type) noexcept {
  return type == accNet || type == accNetBit || type == accWire ||
         (type >= accWand && type <= accSupply1);
}

[[nodiscard]] bool is_register(const PLI_INT32 type) noexcept {
  return type == accReg || type == accRegBit || type == accIntegerVar ||
         type == accRealVar || type == accTimeVar;
}

[[nodiscard]] bool is_select(const PLI_INT32 type) noexcept {
  return type == accBit || type == accPortBit || type == accBitSelect ||
         type == accPartSelect;
}

[[nodiscard]] bool valid_target(const PLI_INT32 model,
                                const PLI_INT32 type,
                                const PLI_INT32 full_type,
                                const std::uint32_t capabilities) noexcept {
  const bool net = is_net(type) || is_net(full_type);
  const bool reg = is_register(type) || is_register(full_type) ||
                   is_select(type) || is_select(full_type);
  const bool writable = reg || net || type == accSeqPrim ||
                        full_type == accSeqPrim;
  switch (model) {
    case accNoDelay:
    case accInertialDelay:
    case accTransportDelay:
    case accPureTransportDelay:
      return writable && (capabilities & FSIM_ACC_WRITE_CAP_DEPOSIT) != 0;
    case accForceFlag:
      return writable && (capabilities & FSIM_ACC_WRITE_CAP_FORCE) != 0;
    case accReleaseFlag:
      return writable && (capabilities & FSIM_ACC_WRITE_CAP_RELEASE) != 0;
    case accAssignFlag:
      return reg && (capabilities & FSIM_ACC_WRITE_CAP_ASSIGN) != 0;
    case accDeassignFlag:
      return reg && (capabilities & FSIM_ACC_WRITE_CAP_DEASSIGN) != 0;
    default: return false;
  }
}

[[nodiscard]] bool returns_value(const PLI_INT32 model) noexcept {
  return model == accReleaseFlag || model == accDeassignFlag;
}

[[nodiscard]] bool prepare_time(const s_setval_delay& delay,
                                fsim_acc_write_query_v3& query) noexcept {
  if (delay.model == accNoDelay || delay.model == accForceFlag ||
      delay.model == accReleaseFlag || delay.model == accAssignFlag ||
      delay.model == accDeassignFlag) {
    query.time_type = FSIM_ACC_WRITE_TIME_NONE;
    return true;
  }
  if (delay.model != accInertialDelay && delay.model != accTransportDelay &&
      delay.model != accPureTransportDelay) {
    return false;
  }
  query.time_type = delay.time.type;
  if (delay.time.type == accTime || delay.time.type == accSimTime) {
    query.time_ticks =
        (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(
             delay.time.high))
         << 32U) |
        std::bit_cast<std::uint32_t>(delay.time.low);
    return true;
  }
  if (delay.time.type == accRealTime && std::isfinite(delay.time.real) &&
      delay.time.real >= 0.0) {
    query.time_real = delay.time.real;
    return true;
  }
  return false;
}

struct PreparedValue {
  std::vector<fsim_acc_logic_word_v3> words;
  std::string text;
};

[[nodiscard]] bool prepare_input(const s_setval_value& value,
                                 const PLI_INT32 width,
                                 fsim_acc_write_query_v3& query,
                                 PreparedValue& storage) noexcept {
  query.value.format = value.format;
  query.value.width = width;
  const auto word_count = static_cast<std::uint32_t>((width + 31) / 32);
  try {
    switch (value.format) {
      case accScalarVal: {
        if (width != 1 || value.value.scalar < acc0 ||
            value.value.scalar > accZ) {
          return false;
        }
        const auto scalar = static_cast<std::uint32_t>(value.value.scalar);
        storage.words.push_back(
            {(scalar == acc1 || scalar == accX) ? 1U : 0U,
             (scalar == accX || scalar == accZ) ? 1U : 0U});
        break;
      }
      case accIntVal:
        storage.words.push_back(
            {std::bit_cast<std::uint32_t>(value.value.integer), 0});
        break;
      case accVectorVal:
        if (!checked_bytes(value.value.vector,
                           static_cast<std::size_t>(word_count) *
                               sizeof(*value.value.vector),
                           fsim::runtime::TfNativePointerAccess::Read)) {
          return false;
        }
        storage.words.reserve(word_count);
        for (std::uint32_t index = 0; index < word_count; ++index) {
          storage.words.push_back(
              {std::bit_cast<std::uint32_t>(value.value.vector[index].aval),
               std::bit_cast<std::uint32_t>(value.value.vector[index].bval)});
        }
        break;
      case accRealVal:
        if (!std::isfinite(value.value.real)) return false;
        query.value.real_value = value.value.real;
        break;
      case accStringVal:
      case accBinStrVal:
      case accOctStrVal:
      case accDecStrVal:
      case accHexStrVal:
        if (!copy_argument_string(value.value.str, storage.text,
                                  value.format == accStringVal)) {
          return false;
        }
        break;
      default: return false;
    }
  } catch (...) {
    return false;
  }
  query.value.logic_word_count =
      static_cast<std::uint32_t>(storage.words.size());
  query.value.logic_words = storage.words.empty() ? nullptr : storage.words.data();
  query.value.string_value = storage.text.empty() ? nullptr : storage.text.data();
  query.value.string_value_size =
      static_cast<std::uint32_t>(storage.text.size());
  return true;
}

[[nodiscard]] bool valid_return_destination(const s_setval_value& value,
                                            const PLI_INT32 width) noexcept {
  switch (value.format) {
    case accBinStrVal:
    case accOctStrVal:
    case accDecStrVal:
    case accHexStrVal:
    case accStringVal:
    case accRealVal: return true;
    case accIntVal: return width <= 32;
    case accScalarVal: return width == 1;
    case accVectorVal: {
      const auto words = static_cast<std::size_t>((width + 31) / 32);
      return checked_bytes(value.value.vector,
                           words * sizeof(*value.value.vector),
                           fsim::runtime::TfNativePointerAccess::Write);
    }
    default: return false;
  }
}

[[nodiscard]] PLI_INT32 scalar_value(
    const fsim_acc_logic_word_v3 word) noexcept {
  const auto aval = word.aval & 1U;
  const auto bval = word.bval & 1U;
  if (bval == 0) return aval == 0 ? acc0 : acc1;
  return aval == 0 ? accZ : accX;
}

[[nodiscard]] bool valid_current(const fsim_acc_read_result_v3& current,
                                 const fsim_acc_read_result_v3& metadata) noexcept {
  return current.abi_version == FSIM_ACC_READ_QUERY_ABI_VERSION &&
         current.struct_size >= sizeof(current) && current.reserved == 0 &&
         current.type == metadata.type &&
         current.full_type == metadata.full_type &&
         current.width == metadata.width;
}

[[nodiscard]] bool publish_current(
    s_setval_value& destination, const fsim_acc_read_result_v3& current,
    const fsim_acc_read_result_v3& metadata) noexcept {
  if (!valid_current(current, metadata)) return false;
  const auto words = static_cast<std::uint32_t>((current.width + 31) / 32);
  const auto publish_string = [&](const PLI_BYTE8* pointer,
                                  const std::uint32_t size,
                                  const bool allow_empty = false) {
    if (!copy_result_string(pointer, size, allow_empty)) return false;
    destination.value.str = write_string_buffer.data();
    return true;
  };
  switch (destination.format) {
    case accBinStrVal:
      return publish_string(current.binary_value, current.binary_value_size);
    case accOctStrVal:
      return publish_string(current.octal_value, current.octal_value_size);
    case accDecStrVal:
      return publish_string(current.decimal_value, current.decimal_value_size);
    case accHexStrVal:
      return publish_string(current.hexadecimal_value,
                            current.hexadecimal_value_size);
    case accStringVal:
      return publish_string(current.string_value, current.string_value_size,
                            true);
    case accScalarVal:
      if (current.value_kind != FSIM_ACC_READ_VALUE_LOGIC4 ||
          current.width != 1 || current.logic_word_count != 1 ||
          !checked_bytes(current.logic_words, sizeof(*current.logic_words),
                         fsim::runtime::TfNativePointerAccess::Read)) {
        return false;
      }
      destination.value.scalar = scalar_value(current.logic_words[0]);
      return true;
    case accIntVal:
      if (current.value_kind != FSIM_ACC_READ_VALUE_LOGIC4 ||
          current.width > 32 || current.logic_word_count != 1 ||
          !checked_bytes(current.logic_words, sizeof(*current.logic_words),
                         fsim::runtime::TfNativePointerAccess::Read)) {
        return false;
      }
      destination.value.integer =
          std::bit_cast<PLI_INT32>(current.logic_words[0].aval);
      return true;
    case accRealVal:
      if (current.value_kind != FSIM_ACC_READ_VALUE_REAL) return false;
      destination.value.real = current.real_value;
      return true;
    case accVectorVal:
      if (current.value_kind != FSIM_ACC_READ_VALUE_LOGIC4 ||
          current.logic_word_count != words ||
          !checked_bytes(current.logic_words,
                         static_cast<std::size_t>(words) *
                             sizeof(*current.logic_words),
                         fsim::runtime::TfNativePointerAccess::Read)) {
        return false;
      }
      for (std::uint32_t index = 0; index < words; ++index) {
        destination.value.vector[index].aval =
            std::bit_cast<PLI_INT32>(current.logic_words[index].aval);
        destination.value.vector[index].bval =
            std::bit_cast<PLI_INT32>(current.logic_words[index].bval);
      }
      return true;
    default: return false;
  }
}

}  // namespace

namespace fsim::runtime::acc_detail {

void reset_write_borrowed_storage() noexcept {
  if (!write_string_buffer.empty()) write_string_buffer.front() = '\0';
  write_string_buffer.clear();
}

}  // namespace fsim::runtime::acc_detail

extern "C" PLI_INT32 acc_set_value(const handle object,
                                   p_setval_value const value,
                                   p_setval_delay const delay) {
  if (!checked_bytes(value, sizeof(*value),
                     fsim::runtime::TfNativePointerAccess::Read) ||
      !checked_bytes(delay, sizeof(*delay),
                     fsim::runtime::TfNativePointerAccess::Read)) {
    publish_error(true);
    return 1;
  }
  std::uint64_t vpi{};
  fsim_acc_read_result_v3 metadata{};
  if (!resolve_metadata(object, vpi, metadata) ||
      !valid_target(delay->model, metadata.type, metadata.full_type,
                    metadata.write_capabilities)) {
    publish_error(true);
    return 1;
  }
  const bool output = returns_value(delay->model);
  if (output &&
      (!checked_bytes(value, sizeof(*value),
                      fsim::runtime::TfNativePointerAccess::Write) ||
       !valid_return_destination(*value, metadata.width))) {
    publish_error(true);
    return 1;
  }
  fsim_acc_write_query_v3 query{};
  query.abi_version = FSIM_ACC_WRITE_QUERY_ABI_VERSION;
  query.struct_size = sizeof(query);
  query.operation = FSIM_ACC_WRITE_SET_VALUE;
  query.object = vpi;
  query.object_type = metadata.type;
  query.object_full_type = metadata.full_type;
  query.model = delay->model;
  if (!prepare_time(*delay, query)) {
    publish_error(true);
    return 1;
  }
  PreparedValue storage;
  if (!output && !prepare_input(*value, metadata.width, query, storage)) {
    publish_error(true);
    return 1;
  }
  fsim_acc_write_result_v3 result{};
  result.abi_version = FSIM_ACC_WRITE_QUERY_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.current_value.abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  result.current_value.struct_size = sizeof(result.current_value);
  const auto* const active = fsim::runtime::acc_detail::current_context();
  std::uint32_t status{};
  try {
    if (active == nullptr) {
      publish_error(true);
      return 1;
    }
    status = active->write(active->user_data, &query, &result);
  } catch (...) {
    publish_error(true);
    return 1;
  }
  if (status != FSIM_ACC_VPI_OBJECT_VALID ||
      result.abi_version != FSIM_ACC_WRITE_QUERY_ABI_VERSION ||
      result.struct_size < sizeof(result) || result.reserved != 0 ||
      result.reserved2 != 0 ||
      (output && !publish_current(*value, result.current_value, metadata))) {
    publish_error(true);
    return 1;
  }
  publish_error(false);
  return 0;
}
