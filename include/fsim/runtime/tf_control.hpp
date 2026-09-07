// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/tf_call_bridge.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fsim::runtime {

inline constexpr std::uint32_t kMaxTfControlEffects = 256;
inline constexpr std::uint32_t kMaxTfControlTextSize = 65536;
inline constexpr std::uint32_t kMaxTfControlMetadataSize = 255;
inline constexpr std::uint32_t kMaxTfControlBytes = 1U << 20U;

enum class TfControlEffectKind : std::uint32_t {
  Output = FSIM_TF_CONTROL_OUTPUT,
  Warning = FSIM_TF_CONTROL_WARNING,
  Error = FSIM_TF_CONTROL_ERROR,
  Message = FSIM_TF_CONTROL_MESSAGE,
  Finish = FSIM_TF_CONTROL_FINISH,
  Stop = FSIM_TF_CONTROL_STOP,
};

struct TfControlEffect {
  TfControlEffectKind kind{TfControlEffectKind::Output};
  PLI_INT32 channel{};
  PLI_INT32 level{};
  std::string facility;
  std::string message_number;
  std::string text;
};

enum class TfControlError {
  None,
  InvalidKind,
  InvalidLevel,
  InvalidText,
  EffectLimit,
  ByteLimit,
  Allocation,
};

struct TfControlCapture {
  std::vector<TfControlEffect> effects;
  std::uint32_t bytes{};
  TfControlError error{TfControlError::None};
};

[[nodiscard]] PLI_INT32 FSIM_NATIVE_PLUGIN_CALL capture_tf_control_effect_v3(
    void* user_data, std::uint32_t kind, PLI_INT32 channel, PLI_INT32 level,
    const PLI_BYTE8* facility, std::uint32_t facility_size,
    const PLI_BYTE8* message_number, std::uint32_t message_number_size,
    const PLI_BYTE8* text, std::uint32_t text_size) noexcept;

}  // namespace fsim::runtime
