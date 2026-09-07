// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <array>
#include <bit>
#include <cstdarg>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace {

thread_local std::string string_buffer;

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] const fsim_acc_handle_context_v3* context() noexcept {
  return fsim::runtime::acc_detail::current_context();
}

[[nodiscard]] bool checked_bytes(const void* const pointer,
                                 const std::size_t size,
                                 const fsim::runtime::TfNativePointerAccess
                                     access) noexcept {
  return pointer != nullptr && size != 0 &&
         fsim::runtime::validate_tf_native_pointer(pointer, size, access) ==
             fsim::runtime::TfContainmentError::None;
}

[[nodiscard]] bool copy_native_string(const PLI_BYTE8* const pointer,
                                      const std::uint32_t size,
                                      const std::uint32_t maximum,
                                      const bool allow_empty) noexcept {
  if (size == 0) {
    if (!allow_empty) return false;
    string_buffer.clear();
    return true;
  }
  if (size > maximum ||
      !checked_bytes(pointer, size,
                     fsim::runtime::TfNativePointerAccess::Read)) {
    return false;
  }
  try {
    string_buffer.assign(pointer, size);
  } catch (...) {
    return false;
  }
  return string_buffer.find('\0') == std::string::npos;
}

[[nodiscard]] bool copy_argument_string(const PLI_BYTE8* const pointer,
                                        std::string& output,
                                        const bool allow_empty = false) noexcept {
  if (pointer == nullptr) return false;
  const auto base = reinterpret_cast<std::uintptr_t>(pointer);
  try {
    for (std::uint32_t offset = 0;
         offset <= FSIM_ACC_NAME_MAXIMUM_BYTES; offset += 64U) {
      const auto remaining = FSIM_ACC_NAME_MAXIMUM_BYTES + 1U - offset;
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

enum class ReadStatus { Valid, NotFound, Error };

[[nodiscard]] ReadStatus read_call(
    const std::uint32_t operation, const std::uint64_t object,
    const std::string_view attribute,
    fsim_acc_read_result_v3& result) noexcept {
  const auto* const active = context();
  if (active == nullptr) return ReadStatus::Error;
  fsim_acc_read_query_v3 query{};
  query.abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  query.struct_size = sizeof(query);
  query.operation = operation;
  query.object = object;
  query.attribute_name = attribute.empty() ? nullptr : attribute.data();
  query.attribute_name_size = static_cast<std::uint32_t>(attribute.size());
  result = {};
  result.abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  result.struct_size = sizeof(result);
  std::uint32_t status{};
  try {
    status = active->read(active->user_data, &query, &result);
  } catch (...) {
    return ReadStatus::Error;
  }
  if (status == FSIM_ACC_VPI_OBJECT_NOT_FOUND) return ReadStatus::NotFound;
  if (status != FSIM_ACC_VPI_OBJECT_VALID ||
      result.abi_version != FSIM_ACC_READ_QUERY_ABI_VERSION ||
      result.struct_size < sizeof(result) || result.reserved != 0) {
    return ReadStatus::Error;
  }
  return ReadStatus::Valid;
}

[[nodiscard]] ReadStatus read_object(const handle object,
                                     fsim_acc_read_result_v3& result) noexcept {
  const auto vpi = fsim_acc_handle_to_vpi_v3(object);
  if (vpi == 0) return ReadStatus::Error;
  const auto status = read_call(FSIM_ACC_READ_OBJECT, vpi, {}, result);
  const auto* const active = context();
  PLI_INT32 resolved_type{};
  if (status != ReadStatus::Valid || result.type <= 0 ||
      result.full_type <= 0 || active == nullptr) {
    return ReadStatus::Error;
  }
  try {
    if (active->resolve(active->user_data, vpi, &resolved_type) !=
            FSIM_ACC_VPI_OBJECT_VALID ||
        resolved_type != result.full_type) {
      return ReadStatus::Error;
    }
  } catch (...) {
    return ReadStatus::Error;
  }
  return status;
}

[[nodiscard]] PLI_BYTE8* read_string_field(
    const handle object, const PLI_BYTE8* fsim_acc_read_result_v3::*pointer,
    const std::uint32_t fsim_acc_read_result_v3::*size,
    const std::uint32_t maximum = FSIM_ACC_READ_MAXIMUM_STRING_BYTES) noexcept {
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid) {
    publish_error(true);
    return nullptr;
  }
  const auto length = result.*size;
  if (length == 0) {
    publish_error(false);
    return nullptr;
  }
  if (!copy_native_string(result.*pointer, length, maximum, false)) {
    publish_error(true);
    return nullptr;
  }
  publish_error(false);
  return string_buffer.data();
}

struct TypeName {
  PLI_INT32 type;
  const PLI_BYTE8* name;
};

#define FSIM_ACC_TYPE_NAME(name) TypeName{name, #name}
constexpr std::array type_names{
    FSIM_ACC_TYPE_NAME(accModule), FSIM_ACC_TYPE_NAME(accScope),
    FSIM_ACC_TYPE_NAME(accTopModule), FSIM_ACC_TYPE_NAME(accModuleInstance),
    FSIM_ACC_TYPE_NAME(accCellInstance), FSIM_ACC_TYPE_NAME(accNet),
    FSIM_ACC_TYPE_NAME(accReg), FSIM_ACC_TYPE_NAME(accPort),
    FSIM_ACC_TYPE_NAME(accTerminal), FSIM_ACC_TYPE_NAME(accInputTerminal),
    FSIM_ACC_TYPE_NAME(accOutputTerminal),
    FSIM_ACC_TYPE_NAME(accInoutTerminal), FSIM_ACC_TYPE_NAME(accCombPrim),
    FSIM_ACC_TYPE_NAME(accSeqPrim), FSIM_ACC_TYPE_NAME(accAndGate),
    FSIM_ACC_TYPE_NAME(accNandGate), FSIM_ACC_TYPE_NAME(accNorGate),
    FSIM_ACC_TYPE_NAME(accOrGate), FSIM_ACC_TYPE_NAME(accXorGate),
    FSIM_ACC_TYPE_NAME(accXnorGate), FSIM_ACC_TYPE_NAME(accBufGate),
    FSIM_ACC_TYPE_NAME(accNotGate), FSIM_ACC_TYPE_NAME(accBufif0Gate),
    FSIM_ACC_TYPE_NAME(accBufif1Gate), FSIM_ACC_TYPE_NAME(accNotif0Gate),
    FSIM_ACC_TYPE_NAME(accNotif1Gate), FSIM_ACC_TYPE_NAME(accNmosGate),
    FSIM_ACC_TYPE_NAME(accPmosGate), FSIM_ACC_TYPE_NAME(accCmosGate),
    FSIM_ACC_TYPE_NAME(accRnmosGate), FSIM_ACC_TYPE_NAME(accRpmosGate),
    FSIM_ACC_TYPE_NAME(accRcmosGate), FSIM_ACC_TYPE_NAME(accRtranGate),
    FSIM_ACC_TYPE_NAME(accRtranif0Gate), FSIM_ACC_TYPE_NAME(accRtranif1Gate),
    FSIM_ACC_TYPE_NAME(accTranGate), FSIM_ACC_TYPE_NAME(accTranif0Gate),
    FSIM_ACC_TYPE_NAME(accTranif1Gate), FSIM_ACC_TYPE_NAME(accPullupGate),
    FSIM_ACC_TYPE_NAME(accPulldownGate), FSIM_ACC_TYPE_NAME(accIntegerParam),
    FSIM_ACC_TYPE_NAME(accRealParam), FSIM_ACC_TYPE_NAME(accStringParam),
    FSIM_ACC_TYPE_NAME(accTchk), FSIM_ACC_TYPE_NAME(accPrimitive),
    FSIM_ACC_TYPE_NAME(accParameter), FSIM_ACC_TYPE_NAME(accSpecparam),
    FSIM_ACC_TYPE_NAME(accModPath), FSIM_ACC_TYPE_NAME(accInterModPath),
    FSIM_ACC_TYPE_NAME(accScalarPort), FSIM_ACC_TYPE_NAME(accBitSelectPort),
    FSIM_ACC_TYPE_NAME(accPartSelectPort), FSIM_ACC_TYPE_NAME(accVectorPort),
    FSIM_ACC_TYPE_NAME(accConcatPort), FSIM_ACC_TYPE_NAME(accWire),
    FSIM_ACC_TYPE_NAME(accWand), FSIM_ACC_TYPE_NAME(accWor),
    FSIM_ACC_TYPE_NAME(accTri), FSIM_ACC_TYPE_NAME(accTriand),
    FSIM_ACC_TYPE_NAME(accTrior), FSIM_ACC_TYPE_NAME(accTri0),
    FSIM_ACC_TYPE_NAME(accTri1), FSIM_ACC_TYPE_NAME(accTrireg),
    FSIM_ACC_TYPE_NAME(accSupply0), FSIM_ACC_TYPE_NAME(accSupply1),
    FSIM_ACC_TYPE_NAME(accNamedEvent), FSIM_ACC_TYPE_NAME(accIntegerVar),
    FSIM_ACC_TYPE_NAME(accRealVar), FSIM_ACC_TYPE_NAME(accTimeVar),
    FSIM_ACC_TYPE_NAME(accScalar), FSIM_ACC_TYPE_NAME(accVector),
    FSIM_ACC_TYPE_NAME(accExpandedVector),
    FSIM_ACC_TYPE_NAME(accUnExpandedVector), FSIM_ACC_TYPE_NAME(accProtected),
    FSIM_ACC_TYPE_NAME(accBit), FSIM_ACC_TYPE_NAME(accPortBit),
    FSIM_ACC_TYPE_NAME(accNetBit), FSIM_ACC_TYPE_NAME(accRegBit),
    FSIM_ACC_TYPE_NAME(accBitSelect), FSIM_ACC_TYPE_NAME(accPartSelect),
    FSIM_ACC_TYPE_NAME(accSetup), FSIM_ACC_TYPE_NAME(accHold),
    FSIM_ACC_TYPE_NAME(accWidth), FSIM_ACC_TYPE_NAME(accPeriod),
    FSIM_ACC_TYPE_NAME(accRecovery), FSIM_ACC_TYPE_NAME(accSkew),
    FSIM_ACC_TYPE_NAME(accNochange), FSIM_ACC_TYPE_NAME(accSetuphold),
    FSIM_ACC_TYPE_NAME(accInput), FSIM_ACC_TYPE_NAME(accOutput),
    FSIM_ACC_TYPE_NAME(accInout), FSIM_ACC_TYPE_NAME(accMixedIo),
    FSIM_ACC_TYPE_NAME(accPositive), FSIM_ACC_TYPE_NAME(accNegative),
    FSIM_ACC_TYPE_NAME(accUnknown), FSIM_ACC_TYPE_NAME(accPathTerminal),
    FSIM_ACC_TYPE_NAME(accPathInput), FSIM_ACC_TYPE_NAME(accPathOutput),
    FSIM_ACC_TYPE_NAME(accDataPath), FSIM_ACC_TYPE_NAME(accTchkTerminal),
    FSIM_ACC_TYPE_NAME(accTask), FSIM_ACC_TYPE_NAME(accFunction),
    FSIM_ACC_TYPE_NAME(accStatement), FSIM_ACC_TYPE_NAME(accTaskCall),
    FSIM_ACC_TYPE_NAME(accFunctionCall), FSIM_ACC_TYPE_NAME(accSystemTask),
    FSIM_ACC_TYPE_NAME(accSystemFunction),
    FSIM_ACC_TYPE_NAME(accSystemRealFunction), FSIM_ACC_TYPE_NAME(accUserTask),
    FSIM_ACC_TYPE_NAME(accUserFunction), FSIM_ACC_TYPE_NAME(accUserRealFunction),
    FSIM_ACC_TYPE_NAME(accConstant), FSIM_ACC_TYPE_NAME(accConcat),
    FSIM_ACC_TYPE_NAME(accOperator), FSIM_ACC_TYPE_NAME(accMinTypMax)};
#undef FSIM_ACC_TYPE_NAME

[[nodiscard]] PLI_INT32 scalar_value(
    const fsim_acc_logic_word_v3 word) noexcept {
  const auto aval = word.aval & 1U;
  const auto bval = word.bval & 1U;
  if (bval == 0) return aval == 0 ? acc0 : acc1;
  return aval == 0 ? accZ : accX;
}

[[nodiscard]] bool format_string(const PLI_BYTE8* const format,
                                 std::array<PLI_BYTE8, 3>& copied) noexcept {
  if (!checked_bytes(format, copied.size(),
                     fsim::runtime::TfNativePointerAccess::Read)) {
    return false;
  }
  copied = {format[0], format[1], format[2]};
  return copied[0] == '%' && copied[2] == '\0' &&
         (copied[1] == 'b' || copied[1] == 'd' || copied[1] == 'h' ||
          copied[1] == 'o' || copied[1] == 'v' || copied[1] == '%');
}

}  // namespace

namespace fsim::runtime::acc_detail {

void reset_read_borrowed_storage() noexcept {
  if (!string_buffer.empty()) string_buffer.front() = '\0';
  string_buffer.clear();
}

}  // namespace fsim::runtime::acc_detail

extern "C" {

double acc_fetch_attribute(const handle object, ...) {
  std::va_list arguments;
  va_start(arguments, object);
  auto* const name = va_arg(arguments, PLI_BYTE8*);
  const bool zero_default = fsim::runtime::acc_detail::configuration_enabled(
      accDefaultAttr0, "true");
  const auto fallback = zero_default ? 0.0 : va_arg(arguments, double);
  va_end(arguments);
  std::string attribute;
  const auto vpi = fsim_acc_handle_to_vpi_v3(object);
  if (vpi == 0 || !copy_argument_string(name, attribute)) {
    publish_error(true);
    return 0.0;
  }
  fsim_acc_read_result_v3 result{};
  const auto status = read_call(FSIM_ACC_READ_ATTRIBUTE, vpi, attribute, result);
  if (status == ReadStatus::NotFound) {
    publish_error(false);
    return fallback;
  }
  if (status != ReadStatus::Valid) {
    publish_error(true);
    return 0.0;
  }
  publish_error(false);
  return result.attribute_real;
}

PLI_INT32 acc_fetch_attribute_int(const handle object, ...) {
  std::va_list arguments;
  va_start(arguments, object);
  auto* const name = va_arg(arguments, PLI_BYTE8*);
  const bool zero_default = fsim::runtime::acc_detail::configuration_enabled(
      accDefaultAttr0, "true");
  const auto fallback = zero_default ? 0 : va_arg(arguments, PLI_INT32);
  va_end(arguments);
  std::string attribute;
  const auto vpi = fsim_acc_handle_to_vpi_v3(object);
  if (vpi == 0 || !copy_argument_string(name, attribute)) {
    publish_error(true);
    return 0;
  }
  fsim_acc_read_result_v3 result{};
  const auto status = read_call(FSIM_ACC_READ_ATTRIBUTE, vpi, attribute, result);
  if (status == ReadStatus::NotFound) {
    publish_error(false);
    return fallback;
  }
  if (status != ReadStatus::Valid) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return result.attribute_integer;
}

PLI_BYTE8* acc_fetch_attribute_str(const handle object, ...) {
  std::va_list arguments;
  va_start(arguments, object);
  auto* const name = va_arg(arguments, PLI_BYTE8*);
  const bool zero_default = fsim::runtime::acc_detail::configuration_enabled(
      accDefaultAttr0, "true");
  auto* const fallback = zero_default ? nullptr : va_arg(arguments, PLI_BYTE8*);
  va_end(arguments);
  std::string attribute;
  const auto vpi = fsim_acc_handle_to_vpi_v3(object);
  if (vpi == 0 || !copy_argument_string(name, attribute)) {
    publish_error(true);
    return nullptr;
  }
  fsim_acc_read_result_v3 result{};
  const auto status = read_call(FSIM_ACC_READ_ATTRIBUTE, vpi, attribute, result);
  if (status == ReadStatus::NotFound) {
    if (zero_default) {
      string_buffer = "0";
    } else {
      string_buffer.clear();
      if (!copy_argument_string(fallback, string_buffer, true)) {
        publish_error(true);
        return nullptr;
      }
    }
    publish_error(false);
    return string_buffer.data();
  }
  if (status != ReadStatus::Valid ||
      !copy_native_string(result.attribute_string,
                          result.attribute_string_size,
                          FSIM_ACC_READ_MAXIMUM_STRING_BYTES, true)) {
    publish_error(true);
    return nullptr;
  }
  publish_error(false);
  return string_buffer.data();
}

PLI_BYTE8* acc_fetch_defname(const handle object) {
  return read_string_field(object, &fsim_acc_read_result_v3::definition_name,
                           &fsim_acc_read_result_v3::definition_name_size,
                           FSIM_ACC_NAME_MAXIMUM_BYTES);
}

PLI_INT32 acc_fetch_direction(const handle object) {
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid ||
      (result.direction != accInput && result.direction != accOutput &&
       result.direction != accInout && result.direction != accMixedIo)) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return result.direction;
}

PLI_INT32 acc_fetch_edge(const handle object) {
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid || result.edge < 0 ||
      (result.edge & ~PLI_INT32{63}) != 0) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return result.edge;
}

PLI_BYTE8* acc_fetch_fullname(const handle object) {
  return read_string_field(object, &fsim_acc_read_result_v3::full_name,
                           &fsim_acc_read_result_v3::full_name_size);
}

PLI_INT32 acc_fetch_fulltype(const handle object) {
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return result.full_type;
}

PLI_INT32 acc_fetch_index(const handle object) {
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid || result.index < 0) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return result.index;
}

PLI_INT32 acc_fetch_location(p_location const location, const handle object) {
  if (!checked_bytes(location, sizeof(*location),
                     fsim::runtime::TfNativePointerAccess::Write)) {
    publish_error(true);
    return 0;
  }
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid ||
      result.source_line <= 0 ||
      !copy_native_string(result.source_file, result.source_file_size,
                          FSIM_ACC_NAME_MAXIMUM_BYTES, false)) {
    publish_error(true);
    return 0;
  }
  location->line_no = result.source_line;
  location->filename = string_buffer.data();
  publish_error(false);
  return 1;
}

PLI_BYTE8* acc_fetch_name(const handle object) {
  return read_string_field(object, &fsim_acc_read_result_v3::name,
                           &fsim_acc_read_result_v3::name_size,
                           FSIM_ACC_NAME_MAXIMUM_BYTES);
}

PLI_INT32 acc_fetch_paramtype(const handle parameter) {
  fsim_acc_read_result_v3 result{};
  if (read_object(parameter, result) != ReadStatus::Valid ||
      (result.parameter_type != accIntegerParam &&
       result.parameter_type != accRealParam &&
       result.parameter_type != accStringParam)) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return result.parameter_type;
}

double acc_fetch_paramval(const handle parameter) {
  fsim_acc_read_result_v3 result{};
  if (read_object(parameter, result) != ReadStatus::Valid ||
      (result.parameter_type != accIntegerParam &&
       result.parameter_type != accRealParam &&
       result.parameter_type != accStringParam)) {
    publish_error(true);
    return 0.0;
  }
  publish_error(false);
  return result.parameter_value;
}

PLI_INT32 acc_fetch_precision(void) {
  fsim_acc_read_result_v3 result{};
  if (read_call(FSIM_ACC_READ_DESIGN, 0, {}, result) != ReadStatus::Valid ||
      result.design_precision < -15 || result.design_precision > 2) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return result.design_precision;
}

PLI_INT32 acc_fetch_range(const handle object, PLI_INT32* const msb,
                          PLI_INT32* const lsb) {
  if (!checked_bytes(msb, sizeof(*msb),
                     fsim::runtime::TfNativePointerAccess::Write) ||
      !checked_bytes(lsb, sizeof(*lsb),
                     fsim::runtime::TfNativePointerAccess::Write)) {
    publish_error(true);
    return 0;
  }
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid ||
      result.has_range != 1) {
    publish_error(true);
    return 0;
  }
  *msb = result.msb;
  *lsb = result.lsb;
  publish_error(false);
  return 1;
}

PLI_INT32 acc_fetch_size(const handle object) {
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid || result.width <= 0 ||
      result.width > static_cast<PLI_INT32>(FSIM_ACC_READ_MAXIMUM_BITS)) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return result.width;
}

void acc_fetch_timescale_info(const handle object, p_timescale_info const info) {
  if (!checked_bytes(info, sizeof(*info),
                     fsim::runtime::TfNativePointerAccess::Write)) {
    publish_error(true);
    return;
  }
  fsim_acc_read_result_v3 result{};
  const auto status = object == nullptr
                          ? read_call(FSIM_ACC_READ_DESIGN, 0, {}, result)
                          : read_object(object, result);
  if (status != ReadStatus::Valid || result.timescale_unit < -15 ||
      result.timescale_unit > 2 || result.timescale_precision < -15 ||
      result.timescale_precision > 2) {
    publish_error(true);
    return;
  }
  info->unit = result.timescale_unit;
  info->precision = result.timescale_precision;
  publish_error(false);
}

PLI_INT32 acc_fetch_type(const handle object) {
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return result.type;
}

PLI_BYTE8* acc_fetch_type_str(const PLI_INT32 type) {
  for (const auto& entry : type_names) {
    if (entry.type == type) {
      publish_error(false);
      return const_cast<PLI_BYTE8*>(entry.name);
    }
  }
  publish_error(true);
  return nullptr;
}

PLI_BYTE8* acc_fetch_value(const handle object, PLI_BYTE8* const format,
                           p_acc_value const value) {
  std::array<PLI_BYTE8, 3> copied_format{};
  if (!format_string(format, copied_format)) {
    publish_error(true);
    return nullptr;
  }
  fsim_acc_read_result_v3 result{};
  if (read_object(object, result) != ReadStatus::Valid || result.width <= 0 ||
      result.width > static_cast<PLI_INT32>(FSIM_ACC_READ_MAXIMUM_BITS)) {
    publish_error(true);
    return nullptr;
  }
  const auto copy_result_string = [&](const PLI_BYTE8* pointer,
                                      const std::uint32_t size,
                                      const bool allow_empty = false) {
    if (!copy_native_string(pointer, size,
                            FSIM_ACC_READ_MAXIMUM_STRING_BYTES, allow_empty)) {
      return false;
    }
    return true;
  };
  const auto return_string = [&](const PLI_BYTE8* pointer,
                                 const std::uint32_t size) -> PLI_BYTE8* {
    if (!copy_result_string(pointer, size)) {
      publish_error(true);
      return nullptr;
    }
    publish_error(false);
    return string_buffer.data();
  };
  switch (copied_format[1]) {
    case 'b': return return_string(result.binary_value, result.binary_value_size);
    case 'd': return return_string(result.decimal_value, result.decimal_value_size);
    case 'h':
      return return_string(result.hexadecimal_value,
                           result.hexadecimal_value_size);
    case 'o': return return_string(result.octal_value, result.octal_value_size);
    case 'v':
      return return_string(result.strength_value, result.strength_value_size);
    default: break;
  }
  if (!checked_bytes(value, sizeof(*value),
                     fsim::runtime::TfNativePointerAccess::Read) ||
      !checked_bytes(value, sizeof(*value),
                     fsim::runtime::TfNativePointerAccess::Write)) {
    publish_error(true);
    return nullptr;
  }
  const auto words = static_cast<std::uint32_t>((result.width + 31) / 32);
  switch (value->format) {
    case accBinStrVal:
      if (!copy_result_string(result.binary_value, result.binary_value_size)) {
        publish_error(true);
        return nullptr;
      }
      value->value.str = string_buffer.data();
      break;
    case accOctStrVal:
      if (!copy_result_string(result.octal_value, result.octal_value_size)) {
        publish_error(true);
        return nullptr;
      }
      value->value.str = string_buffer.data();
      break;
    case accDecStrVal:
      if (!copy_result_string(result.decimal_value, result.decimal_value_size)) {
        publish_error(true);
        return nullptr;
      }
      value->value.str = string_buffer.data();
      break;
    case accHexStrVal:
      if (!copy_result_string(result.hexadecimal_value,
                              result.hexadecimal_value_size)) {
        publish_error(true);
        return nullptr;
      }
      value->value.str = string_buffer.data();
      break;
    case accStringVal:
      if (!copy_result_string(result.string_value, result.string_value_size,
                              true)) {
        publish_error(true);
        return nullptr;
      }
      value->value.str = string_buffer.data();
      break;
    case accScalarVal:
      if (result.value_kind != FSIM_ACC_READ_VALUE_LOGIC4 ||
          result.width != 1 || result.logic_word_count != 1 ||
          !checked_bytes(result.logic_words, sizeof(*result.logic_words),
                         fsim::runtime::TfNativePointerAccess::Read)) {
        publish_error(true);
        return nullptr;
      }
      value->value.scalar = scalar_value(result.logic_words[0]);
      break;
    case accIntVal:
      if (result.value_kind != FSIM_ACC_READ_VALUE_LOGIC4 ||
          result.width > 32 || result.logic_word_count != 1 ||
          !checked_bytes(result.logic_words, sizeof(*result.logic_words),
                         fsim::runtime::TfNativePointerAccess::Read)) {
        publish_error(true);
        return nullptr;
      }
      value->value.integer = std::bit_cast<PLI_INT32>(result.logic_words[0].aval);
      break;
    case accRealVal:
      if (result.value_kind != FSIM_ACC_READ_VALUE_REAL) {
        publish_error(true);
        return nullptr;
      }
      value->value.real = result.real_value;
      break;
    case accVectorVal:
      if (result.value_kind != FSIM_ACC_READ_VALUE_LOGIC4 ||
          result.logic_word_count != words ||
          !checked_bytes(result.logic_words,
                         static_cast<std::size_t>(words) *
                             sizeof(*result.logic_words),
                         fsim::runtime::TfNativePointerAccess::Read) ||
          !checked_bytes(value->value.vector,
                         static_cast<std::size_t>(words) *
                             sizeof(*value->value.vector),
                         fsim::runtime::TfNativePointerAccess::Write)) {
        publish_error(true);
        return nullptr;
      }
      for (std::uint32_t index = 0; index < words; ++index) {
        value->value.vector[index].aval =
            std::bit_cast<PLI_INT32>(result.logic_words[index].aval);
        value->value.vector[index].bval =
            std::bit_cast<PLI_INT32>(result.logic_words[index].bval);
      }
      break;
    default:
      publish_error(true);
      return nullptr;
  }
  publish_error(false);
  return nullptr;
}

}  // extern "C"
