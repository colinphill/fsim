// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_control.hpp"

#include "fsim/runtime/tf_containment.hpp"

#include <limits>
#include <new>
#include <string_view>

namespace fsim::runtime {

namespace {

[[nodiscard]] bool valid_kind(const std::uint32_t kind) noexcept {
  return kind >= FSIM_TF_CONTROL_OUTPUT && kind <= FSIM_TF_CONTROL_STOP;
}

[[nodiscard]] bool valid_text(const PLI_BYTE8* const value,
                              const std::uint32_t size,
                              const std::uint32_t maximum) noexcept {
  return size <= maximum &&
         (size == 0 ||
          (value != nullptr &&
           validate_tf_native_pointer(
               value, size, TfNativePointerAccess::Read) ==
               TfContainmentError::None));
}

}  // namespace

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL capture_tf_control_effect_v3(
    void* const user_data, const std::uint32_t kind,
    const PLI_INT32 channel, const PLI_INT32 level,
    const PLI_BYTE8* const facility, const std::uint32_t facility_size,
    const PLI_BYTE8* const message_number,
    const std::uint32_t message_number_size, const PLI_BYTE8* const text,
    const std::uint32_t text_size) noexcept {
  auto* const capture = static_cast<TfControlCapture*>(user_data);
  if (validate_tf_native_pointer(
          capture, sizeof(*capture), TfNativePointerAccess::Write) !=
          TfContainmentError::None ||
      capture->error != TfControlError::None) {
    return 1;
  }
  if (!valid_kind(kind)) {
    capture->error = TfControlError::InvalidKind;
    return 1;
  }
  if (kind == FSIM_TF_CONTROL_MESSAGE &&
      (level < ERR_MESSAGE || level > ERR_SYSTEM)) {
    capture->error = TfControlError::InvalidLevel;
    return 1;
  }
  if (!valid_text(facility, facility_size, kMaxTfControlMetadataSize) ||
      !valid_text(message_number, message_number_size,
                  kMaxTfControlMetadataSize) ||
      !valid_text(text, text_size, kMaxTfControlTextSize)) {
    capture->error = TfControlError::InvalidText;
    return 1;
  }
  if (capture->effects.size() >= kMaxTfControlEffects) {
    capture->error = TfControlError::EffectLimit;
    return 1;
  }
  const auto bytes = static_cast<std::uint64_t>(facility_size) +
                     message_number_size + text_size;
  if (bytes > kMaxTfControlBytes ||
      capture->bytes > kMaxTfControlBytes - bytes) {
    capture->error = TfControlError::ByteLimit;
    return 1;
  }
  try {
    capture->effects.push_back(TfControlEffect{
        .kind = static_cast<TfControlEffectKind>(kind),
        .channel = channel,
        .level = level,
        .facility = std::string{facility == nullptr ? "" : facility,
                                facility_size},
        .message_number =
            std::string{message_number == nullptr ? "" : message_number,
                        message_number_size},
        .text = std::string{text == nullptr ? "" : text, text_size},
    });
    capture->bytes += static_cast<std::uint32_t>(bytes);
    return 0;
  } catch (const std::bad_alloc&) {
    capture->error = TfControlError::Allocation;
    return 1;
  } catch (...) {
    capture->error = TfControlError::Allocation;
    return 1;
  }
}

}  // namespace fsim::runtime
