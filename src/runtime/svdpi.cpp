// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/svdpi_bridge.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

constexpr int word_bits = 32;
constexpr std::size_t maximum_context_depth = 64U;

thread_local std::array<fsim_svdpi_call_context_v3*, maximum_context_depth>
    context_stack{};
thread_local std::size_t context_depth{};
std::atomic<fsim_svdpi_export_dispatcher_v3> export_dispatcher{};
std::atomic<void*> export_user_data{};

[[nodiscard]] constexpr bool valid_index(const int index) noexcept {
  return index >= 0;
}

[[nodiscard]] constexpr bool valid_width(const int width) noexcept {
  return width >= 0 && width <= word_bits;
}

[[nodiscard]] constexpr std::uint32_t bit_mask(const int index) noexcept {
  return UINT32_C(1) << (index % word_bits);
}

[[nodiscard]] fsim_svdpi_call_context_v3* current_context() noexcept {
  return context_depth == 0U ? nullptr : context_stack[context_depth - 1U];
}

[[nodiscard]] bool valid_context(
    const fsim_svdpi_call_context_v3* const context) noexcept {
  return context != nullptr
      && context->abi_version == FSIM_SVDPI_CONTEXT_ABI_VERSION
      && context->struct_size >= sizeof(fsim_svdpi_call_context_v3)
      && context->user_data != nullptr
      && context->get_scope != nullptr
      && context->set_scope != nullptr
      && context->get_name_from_scope != nullptr
      && context->get_scope_from_name != nullptr
      && context->put_user_data != nullptr
      && context->get_user_data != nullptr
      && context->get_caller_info != nullptr
      && context->is_disabled_state != nullptr
      && context->acknowledge_disabled_state != nullptr
      && context->get_time != nullptr
      && context->get_time_unit != nullptr
      && context->get_time_precision != nullptr;
}

// An open-array handle (IEEE 1800-2023 H.12); nullptr unless it is one the
// simulator made.
[[nodiscard]] const fsim_svdpi_open_array_v3* open_array(
    const svOpenArrayHandle handle) noexcept {
  const auto* const array
      = static_cast<const fsim_svdpi_open_array_v3*>(handle);
  return array != nullptr && array->magic == FSIM_SVDPI_OPEN_ARRAY_MAGIC
          && array->struct_size >= sizeof(fsim_svdpi_open_array_v3)
          && array->dimensions >= 0
          && (array->dimensions == 0 || array->bounds != nullptr)
      ? array : nullptr;
}

// The left and right bound of `dimension`: 0 is the packed part, 1 the
// leftmost unpacked dimension (H.12.2).
[[nodiscard]] bool open_range(const svOpenArrayHandle handle,
    const int dimension, int& left, int& right) noexcept {
  const auto* const array = open_array(handle);
  if (array == nullptr || dimension < 0 || dimension > array->dimensions) {
    return false;
  }
  if (dimension == 0) {
    left = array->packed_left;
    right = array->packed_right;
  } else {
    left = array->bounds[2 * (dimension - 1)];
    right = array->bounds[2 * (dimension - 1) + 1];
  }
  return true;
}

[[nodiscard]] std::int64_t range_size(const int left, const int right) noexcept {
  return left >= right ? std::int64_t { left } - right + 1
                       : std::int64_t { right } - left + 1;
}

// The element at SV indices `indices` (one per unpacked dimension), or
// nullptr when an index is outside its range.
[[nodiscard]] unsigned char* open_element(
    const svOpenArrayHandle handle, const int* const indices,
    const int count) noexcept {
  const auto* const array = open_array(handle);
  if (array == nullptr || array->data == nullptr
      || count != array->dimensions) {
    return nullptr;
  }
  std::int64_t linear = 0;
  for (int dimension = 0; dimension < array->dimensions; ++dimension) {
    const auto left = array->bounds[2 * dimension];
    const auto right = array->bounds[2 * dimension + 1];
    const auto size = range_size(left, right);
    const auto ordinal = left >= right
        ? std::int64_t { left } - indices[dimension]
        : std::int64_t { indices[dimension] } - left;
    if (ordinal < 0 || ordinal >= size) {
      return nullptr;
    }
    linear = linear * size + ordinal;
  }
  return static_cast<unsigned char*>(array->data)
      + linear * static_cast<std::int64_t>(array->element_bytes);
}

constexpr int maximum_open_dimensions = 32;

// The indices of a variadic accessor: `first` and then one int per further
// unpacked dimension.
[[nodiscard]] int open_indices(const svOpenArrayHandle handle, const int first,
    va_list arguments, std::array<int, maximum_open_dimensions>& indices)
    noexcept {
  const auto* const array = open_array(handle);
  if (array == nullptr || array->dimensions < 1
      || array->dimensions > maximum_open_dimensions) {
    return -1;
  }
  indices[0] = first;
  for (int dimension = 1; dimension < array->dimensions; ++dimension) {
    indices[static_cast<std::size_t>(dimension)] = va_arg(arguments, int);
  }
  return array->dimensions;
}

[[nodiscard]] std::size_t vector_words(const std::uint32_t width) noexcept {
  return (static_cast<std::size_t>(width) + 31U) / 32U;
}

// An element's value as two-state words or as aval/bval words.
void load_element(const fsim_svdpi_open_array_v3& array,
    const unsigned char* const element, svLogicVecVal* const destination)
    noexcept {
  const auto words = vector_words(array.element_width);
  for (std::size_t word = 0U; word < words; ++word) {
    destination[word] = {};
  }
  switch (array.element_code) {
  case 'b':
    destination[0].aval = element[0] & 1U;
    break;
  case 'g':
    destination[0].aval = element[0] & 1U;
    destination[0].bval = (element[0] >> 1U) & 1U;
    break;
  case 'B':
    for (std::size_t word = 0U; word < words; ++word) {
      std::memcpy(&destination[word].aval,
          element + word * sizeof(std::uint32_t), sizeof(std::uint32_t));
    }
    break;
  case 'V':
    std::memcpy(destination, element, words * sizeof(svLogicVecVal));
    break;
  default: {
    std::uint64_t value {};
    std::memcpy(&value, element,
        std::min<std::size_t>(array.element_bytes, sizeof(value)));
    destination[0].aval = static_cast<std::uint32_t>(value);
    if (words > 1U) {
      destination[1].aval = static_cast<std::uint32_t>(value >> 32U);
    }
    break;
  }
  }
  if (array.element_width % 32U != 0U) {
    const auto mask = SV_MASK(array.element_width % 32U);
    destination[words - 1U].aval &= mask;
    destination[words - 1U].bval &= mask;
  }
}

void store_element(const fsim_svdpi_open_array_v3& array,
    unsigned char* const element, const svLogicVecVal* const source) noexcept {
  const auto words = vector_words(array.element_width);
  switch (array.element_code) {
  case 'b':
    element[0] = static_cast<unsigned char>(source[0].aval & 1U);
    break;
  case 'g':
    element[0] = static_cast<unsigned char>(
        (source[0].aval & 1U) | ((source[0].bval & 1U) << 1U));
    break;
  case 'B':
    for (std::size_t word = 0U; word < words; ++word) {
      std::memcpy(element + word * sizeof(std::uint32_t),
          &source[word].aval, sizeof(std::uint32_t));
    }
    break;
  case 'V':
    std::memcpy(element, source, words * sizeof(svLogicVecVal));
    break;
  default: {
    std::uint64_t value = source[0].aval;
    if (words > 1U) {
      value |= std::uint64_t { source[1].aval } << 32U;
    }
    std::memcpy(element, &value,
        std::min<std::size_t>(array.element_bytes, sizeof(value)));
    break;
  }
  }
}

constexpr std::size_t maximum_element_words = 4096U;

void get_bit_vector(svBitVecVal* const destination,
    const svOpenArrayHandle source, const int* const indices,
    const int count) noexcept {
  const auto* const array = open_array(source);
  const auto* const element = open_element(source, indices, count);
  if (destination == nullptr || array == nullptr || element == nullptr
      || vector_words(array->element_width) > maximum_element_words) {
    return;
  }
  std::array<svLogicVecVal, maximum_element_words> value;
  load_element(*array, element, value.data());
  for (std::size_t word = 0U; word < vector_words(array->element_width);
       ++word) {
    destination[word] = value[word].aval & ~value[word].bval;
  }
}

void get_logic_vector(svLogicVecVal* const destination,
    const svOpenArrayHandle source, const int* const indices,
    const int count) noexcept {
  const auto* const array = open_array(source);
  const auto* const element = open_element(source, indices, count);
  if (destination == nullptr || array == nullptr || element == nullptr) {
    return;
  }
  load_element(*array, element, destination);
}

void put_bit_vector(const svOpenArrayHandle destination,
    const svBitVecVal* const source, const int* const indices,
    const int count) noexcept {
  const auto* const array = open_array(destination);
  auto* const element = open_element(destination, indices, count);
  if (source == nullptr || array == nullptr || element == nullptr
      || vector_words(array->element_width) > maximum_element_words) {
    return;
  }
  std::array<svLogicVecVal, maximum_element_words> value {};
  for (std::size_t word = 0U; word < vector_words(array->element_width);
       ++word) {
    value[word].aval = source[word];
  }
  store_element(*array, element, value.data());
}

void put_logic_vector(const svOpenArrayHandle destination,
    const svLogicVecVal* const source, const int* const indices,
    const int count) noexcept {
  const auto* const array = open_array(destination);
  auto* const element = open_element(destination, indices, count);
  if (source == nullptr || array == nullptr || element == nullptr) {
    return;
  }
  store_element(*array, element, source);
}

[[nodiscard]] svLogic get_logic(const svOpenArrayHandle source,
    const int* const indices, const int count) noexcept {
  const auto* const array = open_array(source);
  const auto* const element = open_element(source, indices, count);
  if (array == nullptr || element == nullptr
      || vector_words(array->element_width) > maximum_element_words) {
    return sv_x;
  }
  std::array<svLogicVecVal, maximum_element_words> value;
  load_element(*array, element, value.data());
  return static_cast<svLogic>((value[0].aval & 1U)
      | ((value[0].bval & 1U) << 1U));
}

void put_logic(const svOpenArrayHandle destination, const svLogic value,
    const int* const indices, const int count) noexcept {
  const auto* const array = open_array(destination);
  auto* const element = open_element(destination, indices, count);
  if (array == nullptr || element == nullptr
      || vector_words(array->element_width) > maximum_element_words) {
    return;
  }
  std::array<svLogicVecVal, maximum_element_words> word;
  load_element(*array, element, word.data());
  word[0].aval = (word[0].aval & ~UINT32_C(1)) | (value & 1U);
  word[0].bval = (word[0].bval & ~UINT32_C(1)) | ((value >> 1U) & 1U);
  store_element(*array, element, word.data());
}

}  // namespace

extern "C" {

DPI_DLLESPEC int svLeft(const svOpenArrayHandle handle, const int dimension) {
  int left {};
  int right {};
  return open_range(handle, dimension, left, right) ? left : 0;
}

DPI_DLLESPEC int svRight(const svOpenArrayHandle handle, const int dimension) {
  int left {};
  int right {};
  return open_range(handle, dimension, left, right) ? right : 0;
}

DPI_DLLESPEC int svLow(const svOpenArrayHandle handle, const int dimension) {
  int left {};
  int right {};
  return open_range(handle, dimension, left, right)
      ? (left < right ? left : right) : 0;
}

DPI_DLLESPEC int svHigh(const svOpenArrayHandle handle, const int dimension) {
  int left {};
  int right {};
  return open_range(handle, dimension, left, right)
      ? (left > right ? left : right) : 0;
}

DPI_DLLESPEC int svIncrement(
    const svOpenArrayHandle handle, const int dimension) {
  int left {};
  int right {};
  return open_range(handle, dimension, left, right)
      ? (left >= right ? 1 : -1) : 0;
}

DPI_DLLESPEC int svSize(const svOpenArrayHandle handle, const int dimension) {
  int left {};
  int right {};
  return open_range(handle, dimension, left, right)
      ? static_cast<int>(range_size(left, right)) : 0;
}

DPI_DLLESPEC int svDimensions(const svOpenArrayHandle handle) {
  const auto* const array = open_array(handle);
  return array == nullptr ? 0 : array->dimensions;
}

DPI_DLLESPEC void* svGetArrayPtr(const svOpenArrayHandle handle) {
  const auto* const array = open_array(handle);
  return array == nullptr ? nullptr : array->data;
}

DPI_DLLESPEC int svSizeOfArray(const svOpenArrayHandle handle) {
  const auto* const array = open_array(handle);
  if (array == nullptr) return 0;
  std::int64_t elements = 1;
  for (int dimension = 1; dimension <= array->dimensions; ++dimension) {
    elements *= svSize(handle, dimension);
  }
  return static_cast<int>(elements * array->element_bytes);
}

DPI_DLLESPEC void* svGetArrElemPtr(
    const svOpenArrayHandle handle, const int index1, ...) {
  std::array<int, maximum_open_dimensions> indices {};
  va_list arguments;
  va_start(arguments, index1);
  const auto count = open_indices(handle, index1, arguments, indices);
  va_end(arguments);
  return count < 0 ? nullptr : open_element(handle, indices.data(), count);
}

DPI_DLLESPEC void* svGetArrElemPtr1(
    const svOpenArrayHandle handle, const int index1) {
  const int indices[] { index1 };
  return open_element(handle, indices, 1);
}

DPI_DLLESPEC void* svGetArrElemPtr2(
    const svOpenArrayHandle handle, const int index1, const int index2) {
  const int indices[] { index1, index2 };
  return open_element(handle, indices, 2);
}

DPI_DLLESPEC void* svGetArrElemPtr3(const svOpenArrayHandle handle,
    const int index1, const int index2, const int index3) {
  const int indices[] { index1, index2, index3 };
  return open_element(handle, indices, 3);
}

#define FSIM_SVDPI_VARIADIC(call)                                        \
  std::array<int, maximum_open_dimensions> indices {};                  \
  va_list arguments;                                                    \
  va_start(arguments, index1);                                          \
  const auto count = open_indices(handle, index1, arguments, indices);  \
  va_end(arguments);                                                    \
  call

DPI_DLLESPEC void svPutBitArrElemVecVal(const svOpenArrayHandle destination,
    const svBitVecVal* const source, const int index1, ...) {
  const auto handle = destination;
  FSIM_SVDPI_VARIADIC(if (count >= 0) put_bit_vector(
      destination, source, indices.data(), count);)
}

DPI_DLLESPEC void svPutBitArrElem1VecVal(const svOpenArrayHandle destination,
    const svBitVecVal* const source, const int index1) {
  const int indices[] { index1 };
  put_bit_vector(destination, source, indices, 1);
}

DPI_DLLESPEC void svPutBitArrElem2VecVal(const svOpenArrayHandle destination,
    const svBitVecVal* const source, const int index1, const int index2) {
  const int indices[] { index1, index2 };
  put_bit_vector(destination, source, indices, 2);
}

DPI_DLLESPEC void svPutBitArrElem3VecVal(const svOpenArrayHandle destination,
    const svBitVecVal* const source, const int index1, const int index2,
    const int index3) {
  const int indices[] { index1, index2, index3 };
  put_bit_vector(destination, source, indices, 3);
}

DPI_DLLESPEC void svPutLogicArrElemVecVal(const svOpenArrayHandle destination,
    const svLogicVecVal* const source, const int index1, ...) {
  const auto handle = destination;
  FSIM_SVDPI_VARIADIC(if (count >= 0) put_logic_vector(
      destination, source, indices.data(), count);)
}

DPI_DLLESPEC void svPutLogicArrElem1VecVal(const svOpenArrayHandle destination,
    const svLogicVecVal* const source, const int index1) {
  const int indices[] { index1 };
  put_logic_vector(destination, source, indices, 1);
}

DPI_DLLESPEC void svPutLogicArrElem2VecVal(const svOpenArrayHandle destination,
    const svLogicVecVal* const source, const int index1, const int index2) {
  const int indices[] { index1, index2 };
  put_logic_vector(destination, source, indices, 2);
}

DPI_DLLESPEC void svPutLogicArrElem3VecVal(const svOpenArrayHandle destination,
    const svLogicVecVal* const source, const int index1, const int index2,
    const int index3) {
  const int indices[] { index1, index2, index3 };
  put_logic_vector(destination, source, indices, 3);
}

DPI_DLLESPEC void svGetBitArrElemVecVal(svBitVecVal* const destination,
    const svOpenArrayHandle source, const int index1, ...) {
  const auto handle = source;
  FSIM_SVDPI_VARIADIC(if (count >= 0) get_bit_vector(
      destination, source, indices.data(), count);)
}

DPI_DLLESPEC void svGetBitArrElem1VecVal(svBitVecVal* const destination,
    const svOpenArrayHandle source, const int index1) {
  const int indices[] { index1 };
  get_bit_vector(destination, source, indices, 1);
}

DPI_DLLESPEC void svGetBitArrElem2VecVal(svBitVecVal* const destination,
    const svOpenArrayHandle source, const int index1, const int index2) {
  const int indices[] { index1, index2 };
  get_bit_vector(destination, source, indices, 2);
}

DPI_DLLESPEC void svGetBitArrElem3VecVal(svBitVecVal* const destination,
    const svOpenArrayHandle source, const int index1, const int index2,
    const int index3) {
  const int indices[] { index1, index2, index3 };
  get_bit_vector(destination, source, indices, 3);
}

DPI_DLLESPEC void svGetLogicArrElemVecVal(svLogicVecVal* const destination,
    const svOpenArrayHandle source, const int index1, ...) {
  const auto handle = source;
  FSIM_SVDPI_VARIADIC(if (count >= 0) get_logic_vector(
      destination, source, indices.data(), count);)
}

DPI_DLLESPEC void svGetLogicArrElem1VecVal(svLogicVecVal* const destination,
    const svOpenArrayHandle source, const int index1) {
  const int indices[] { index1 };
  get_logic_vector(destination, source, indices, 1);
}

DPI_DLLESPEC void svGetLogicArrElem2VecVal(svLogicVecVal* const destination,
    const svOpenArrayHandle source, const int index1, const int index2) {
  const int indices[] { index1, index2 };
  get_logic_vector(destination, source, indices, 2);
}

DPI_DLLESPEC void svGetLogicArrElem3VecVal(svLogicVecVal* const destination,
    const svOpenArrayHandle source, const int index1, const int index2,
    const int index3) {
  const int indices[] { index1, index2, index3 };
  get_logic_vector(destination, source, indices, 3);
}

DPI_DLLESPEC svBit svGetBitArrElem(
    const svOpenArrayHandle source, const int index1, ...) {
  const auto handle = source;
  FSIM_SVDPI_VARIADIC(return count < 0 ? sv_0 : static_cast<svBit>(
      get_logic(source, indices.data(), count) == sv_1 ? sv_1 : sv_0);)
}

DPI_DLLESPEC svBit svGetBitArrElem1(
    const svOpenArrayHandle source, const int index1) {
  const int indices[] { index1 };
  return get_logic(source, indices, 1) == sv_1 ? sv_1 : sv_0;
}

DPI_DLLESPEC svBit svGetBitArrElem2(
    const svOpenArrayHandle source, const int index1, const int index2) {
  const int indices[] { index1, index2 };
  return get_logic(source, indices, 2) == sv_1 ? sv_1 : sv_0;
}

DPI_DLLESPEC svBit svGetBitArrElem3(const svOpenArrayHandle source,
    const int index1, const int index2, const int index3) {
  const int indices[] { index1, index2, index3 };
  return get_logic(source, indices, 3) == sv_1 ? sv_1 : sv_0;
}

DPI_DLLESPEC svLogic svGetLogicArrElem(
    const svOpenArrayHandle source, const int index1, ...) {
  const auto handle = source;
  FSIM_SVDPI_VARIADIC(return count < 0 ? static_cast<svLogic>(sv_x)
      : get_logic(source, indices.data(), count);)
}

DPI_DLLESPEC svLogic svGetLogicArrElem1(
    const svOpenArrayHandle source, const int index1) {
  const int indices[] { index1 };
  return get_logic(source, indices, 1);
}

DPI_DLLESPEC svLogic svGetLogicArrElem2(
    const svOpenArrayHandle source, const int index1, const int index2) {
  const int indices[] { index1, index2 };
  return get_logic(source, indices, 2);
}

DPI_DLLESPEC svLogic svGetLogicArrElem3(const svOpenArrayHandle source,
    const int index1, const int index2, const int index3) {
  const int indices[] { index1, index2, index3 };
  return get_logic(source, indices, 3);
}

DPI_DLLESPEC void svPutLogicArrElem(const svOpenArrayHandle destination,
    const svLogic value, const int index1, ...) {
  const auto handle = destination;
  FSIM_SVDPI_VARIADIC(if (count >= 0) put_logic(
      destination, value, indices.data(), count);)
}

DPI_DLLESPEC void svPutLogicArrElem1(const svOpenArrayHandle destination,
    const svLogic value, const int index1) {
  const int indices[] { index1 };
  put_logic(destination, value, indices, 1);
}

DPI_DLLESPEC void svPutLogicArrElem2(const svOpenArrayHandle destination,
    const svLogic value, const int index1, const int index2) {
  const int indices[] { index1, index2 };
  put_logic(destination, value, indices, 2);
}

DPI_DLLESPEC void svPutLogicArrElem3(const svOpenArrayHandle destination,
    const svLogic value, const int index1, const int index2,
    const int index3) {
  const int indices[] { index1, index2, index3 };
  put_logic(destination, value, indices, 3);
}

DPI_DLLESPEC void svPutBitArrElem(const svOpenArrayHandle destination,
    const svBit value, const int index1, ...) {
  const auto handle = destination;
  FSIM_SVDPI_VARIADIC(if (count >= 0) put_logic(
      destination, static_cast<svLogic>(value & 1U), indices.data(), count);)
}

DPI_DLLESPEC void svPutBitArrElem1(const svOpenArrayHandle destination,
    const svBit value, const int index1) {
  const int indices[] { index1 };
  put_logic(destination, static_cast<svLogic>(value & 1U), indices, 1);
}

DPI_DLLESPEC void svPutBitArrElem2(const svOpenArrayHandle destination,
    const svBit value, const int index1, const int index2) {
  const int indices[] { index1, index2 };
  put_logic(destination, static_cast<svLogic>(value & 1U), indices, 2);
}

DPI_DLLESPEC void svPutBitArrElem3(const svOpenArrayHandle destination,
    const svBit value, const int index1, const int index2, const int index3) {
  const int indices[] { index1, index2, index3 };
  put_logic(destination, static_cast<svLogic>(value & 1U), indices, 3);
}

#undef FSIM_SVDPI_VARIADIC


DPI_DLLESPEC const char* svDpiVersion(void) {
  return "1800-2023";
}

DPI_DLLESPEC svBit svGetBitselBit(
    const svBitVecVal* const source, const int index) {
  if (source == nullptr || !valid_index(index)) return sv_0;
  return (source[index / word_bits] & bit_mask(index)) == 0U ? sv_0 : sv_1;
}

DPI_DLLESPEC svLogic svGetBitselLogic(
    const svLogicVecVal* const source, const int index) {
  if (source == nullptr || !valid_index(index)) return sv_x;
  const auto mask = bit_mask(index);
  const auto& word = source[index / word_bits];
  const auto aval = (word.aval & mask) == 0U ? 0U : 1U;
  const auto bval = (word.bval & mask) == 0U ? 0U : 2U;
  return static_cast<svLogic>(aval | bval);
}

DPI_DLLESPEC void svPutBitselBit(
    svBitVecVal* const destination, const int index, const svBit value) {
  if (destination == nullptr || !valid_index(index)) return;
  const auto mask = bit_mask(index);
  auto& word = destination[index / word_bits];
  word = value == sv_0 ? word & ~mask : word | mask;
}

DPI_DLLESPEC void svPutBitselLogic(
    svLogicVecVal* const destination, const int index,
    const svLogic value) {
  if (destination == nullptr || !valid_index(index)) return;
  const auto mask = bit_mask(index);
  auto& word = destination[index / word_bits];
  word.aval = (value & 1U) == 0U ? word.aval & ~mask : word.aval | mask;
  word.bval = (value & 2U) == 0U ? word.bval & ~mask : word.bval | mask;
}

DPI_DLLESPEC void svGetPartselBit(
    svBitVecVal* const destination, const svBitVecVal* const source,
    const int index, const int width) {
  if (destination == nullptr || source == nullptr || !valid_index(index)
      || !valid_width(width)) {
    return;
  }
  *destination = 0U;
  for (int bit = 0; bit < width; ++bit) {
    if (svGetBitselBit(source, index + bit) == sv_1) {
      *destination |= UINT32_C(1) << bit;
    }
  }
}

DPI_DLLESPEC void svGetPartselLogic(
    svLogicVecVal* const destination, const svLogicVecVal* const source,
    const int index, const int width) {
  if (destination == nullptr || source == nullptr || !valid_index(index)
      || !valid_width(width)) {
    return;
  }
  *destination = {};
  for (int bit = 0; bit < width; ++bit) {
    const auto value = svGetBitselLogic(source, index + bit);
    destination->aval |= static_cast<std::uint32_t>(value & 1U) << bit;
    destination->bval |= static_cast<std::uint32_t>((value >> 1U) & 1U) << bit;
  }
}

DPI_DLLESPEC void svPutPartselBit(
    svBitVecVal* const destination, const svBitVecVal source,
    const int index, const int width) {
  if (destination == nullptr || !valid_index(index) || !valid_width(width)) {
    return;
  }
  for (int bit = 0; bit < width; ++bit) {
    svPutBitselBit(
        destination, index + bit,
        static_cast<svBit>((source >> bit) & UINT32_C(1)));
  }
}

DPI_DLLESPEC void svPutPartselLogic(
    svLogicVecVal* const destination, const svLogicVecVal source,
    const int index, const int width) {
  if (destination == nullptr || !valid_index(index) || !valid_width(width)) {
    return;
  }
  for (int bit = 0; bit < width; ++bit) {
    const auto aval = (source.aval >> bit) & UINT32_C(1);
    const auto bval = (source.bval >> bit) & UINT32_C(1);
    svPutBitselLogic(
        destination, index + bit,
        static_cast<svLogic>(aval | (bval << 1U)));
  }
}

DPI_DLLESPEC svScope svGetScope(void) {
  const auto* const context = current_context();
  return context == nullptr ? nullptr : context->get_scope(context->user_data);
}

DPI_DLLESPEC svScope svSetScope(const svScope scope) {
  const auto* const context = current_context();
  return context == nullptr
      ? nullptr : context->set_scope(context->user_data, scope);
}

DPI_DLLESPEC const char* svGetNameFromScope(const svScope scope) {
  const auto* const context = current_context();
  return context == nullptr ? nullptr
      : context->get_name_from_scope(context->user_data, scope);
}

DPI_DLLESPEC svScope svGetScopeFromName(const char* const scope_name) {
  const auto* const context = current_context();
  return context == nullptr ? nullptr
      : context->get_scope_from_name(context->user_data, scope_name);
}

DPI_DLLESPEC int svPutUserData(
    const svScope scope, void* const user_key, void* const user_data) {
  const auto* const context = current_context();
  return context == nullptr ? -1
      : context->put_user_data(
          context->user_data, scope, user_key, user_data);
}

DPI_DLLESPEC void* svGetUserData(
    const svScope scope, void* const user_key) {
  const auto* const context = current_context();
  return context == nullptr ? nullptr
      : context->get_user_data(context->user_data, scope, user_key);
}

DPI_DLLESPEC int svGetCallerInfo(
    const char** const file_name, int* const line_number) {
  const auto* const context = current_context();
  return context == nullptr ? 0
      : context->get_caller_info(
          context->user_data, file_name, line_number);
}

DPI_DLLESPEC int svIsDisabledState(void) {
  const auto* const context = current_context();
  return context == nullptr ? 0
      : context->is_disabled_state(context->user_data);
}

DPI_DLLESPEC void svAckDisabledState(void) {
  const auto* const context = current_context();
  if (context != nullptr) {
    context->acknowledge_disabled_state(context->user_data);
  }
}

DPI_DLLESPEC int svGetTime(
    const svScope scope, svTimeVal* const time_value) {
  const auto* const context = current_context();
  return context == nullptr ? -1
      : context->get_time(context->user_data, scope, time_value);
}

DPI_DLLESPEC int svGetTimeUnit(
    const svScope scope, int32_t* const time_unit) {
  const auto* const context = current_context();
  return context == nullptr ? -1
      : context->get_time_unit(context->user_data, scope, time_unit);
}

DPI_DLLESPEC int svGetTimePrecision(
    const svScope scope, int32_t* const time_precision) {
  const auto* const context = current_context();
  return context == nullptr ? -1
      : context->get_time_precision(
          context->user_data, scope, time_precision);
}

FSIM_SVDPI_BRIDGE_API int FSIM_SVDPI_CALL
fsim_svdpi_call_context_enter_v3(
    fsim_svdpi_call_context_v3* const context) {
  if (!valid_context(context) || context_depth >= context_stack.size()) {
    return -1;
  }
  context_stack[context_depth++] = context;
  return 0;
}

FSIM_SVDPI_BRIDGE_API int FSIM_SVDPI_CALL
fsim_svdpi_call_context_leave_v3(
    fsim_svdpi_call_context_v3* const context) {
  if (context_depth == 0U || context_stack[context_depth - 1U] != context) {
    return -1;
  }
  context_stack[--context_depth] = nullptr;
  return 0;
}

FSIM_SVDPI_BRIDGE_API const fsim_svdpi_call_context_v3* FSIM_SVDPI_CALL
fsim_svdpi_current_call_context_v3(void) {
  return current_context();
}

FSIM_SVDPI_BRIDGE_API void FSIM_SVDPI_CALL
fsim_svdpi_set_export_dispatcher_v3(
    const fsim_svdpi_export_dispatcher_v3 dispatcher, void* const user_data) {
  export_user_data.store(user_data);
  export_dispatcher.store(dispatcher);
}

FSIM_SVDPI_BRIDGE_API int FSIM_SVDPI_CALL
fsim_svdpi_export_call_v3(
    const char* const name, void** const arguments, void* const result) {
  const auto dispatcher = export_dispatcher.load();
  if (dispatcher == nullptr || name == nullptr) {
    return -1;
  }
  return dispatcher(export_user_data.load(), name, arguments, result);
}

}  // extern "C"
