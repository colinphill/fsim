// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_call.hpp"
#include "fsim/runtime/tf_control.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

PLI_BYTE8* text(const char* const value) {
  return const_cast<PLI_BYTE8*>(value);
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL control_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_checktf) {
    return tf_warning(text("check %d"), 12);
  }
  io_printf(text("value=%d"), 7);
  io_mcdprintf(5, text("mcd %s"), text("output"));
  if (tf_text(text("text %x"), 42) != 0 ||
      tf_warning(text("warning %s"), text("value")) != 0 ||
      tf_error(text("error %.1f"), 2.5) != 0 ||
      tf_message(ERR_SYSTEM, text("FAC"), text("E123"),
                 text("message %u"), 9U) != 0 ||
      tf_dostop() != 0 || tf_dofinish() != 0) {
    return 1;
  }
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL throwing_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_calltf) {
    (void)tf_error(text("discarded"));
    throw std::runtime_error{"contained control callback"};
  }
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL unassigned_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_sizetf) return 8;
  if (reason == reason_calltf) (void)tf_dofinish();
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL effect_limit_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason != reason_calltf) return 0;
  for (std::uint32_t index = 0;
       index < fsim::runtime::kMaxTfControlEffects; ++index) {
    if (tf_text(text("x")) != 0) return 1;
  }
  return tf_text(text("overflow"));
}

std::string oversized_text(
    fsim::runtime::kMaxTfControlTextSize + 1U, 'x');
std::string oversized_metadata(
    fsim::runtime::kMaxTfControlMetadataSize + 1U, 'm');

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL text_limit_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  return reason == reason_calltf
             ? tf_text(text("%s"), oversized_text.data())
             : 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL metadata_limit_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  return reason == reason_calltf
             ? tf_message(ERR_ERROR, oversized_metadata.data(), text("E"),
                          text("message"))
             : 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL level_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  return reason == reason_calltf
             ? tf_message(0, text("FAC"), text("E"), text("message"))
             : 0;
}

fsim::runtime::TfRegistration registration(
    std::string name, const fsim::runtime::TfRegistrationKind kind,
    const fsim_tf_routine_v3 callback,
    const fsim_tf_routine_v3 check = nullptr,
    const fsim_tf_routine_v3 size = nullptr) {
  return {.kind = kind,
          .user_data = 0,
          .checktf = check,
          .sizetf = size,
          .calltf = callback,
          .misctf = nullptr,
          .name = std::move(name)};
}

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error{message};
}

}  // namespace

int main() {
  using fsim::runtime::TfCallError;
  using fsim::runtime::TfControlCapture;
  using fsim::runtime::TfControlEffectKind;
  using fsim::runtime::TfControlError;
  using fsim::runtime::TfRegistrationKind;
  using fsim::runtime::bind_tf_call;
  using fsim::runtime::capture_tf_control_effect_v3;

  require(tf_text(text("outside")) != 0 &&
              tf_warning(text("outside")) != 0 &&
              tf_error(text("outside")) != 0 && tf_dostop() != 0 &&
              tf_dofinish() != 0,
          "TF control APIs are neutral outside a callback");

  auto bound = bind_tf_call(
      registration("$control", TfRegistrationKind::Task, control_callback,
                   control_callback));
  require(static_cast<bool>(bound) && bound.control_effects.size() == 1 &&
              bound.control_effects[0].kind ==
                  TfControlEffectKind::Warning &&
              bound.control_effects[0].level == ERR_WARNING &&
              bound.control_effects[0].text == "check 12",
          "TF check-time output is captured by the bind transaction");
  const auto invoked = bound.value->invoke();
  require(invoked && invoked.callback_value == 0 &&
              invoked.control_effects.size() == 8,
          "TF output and controls publish one ordered call transaction");
  const auto& effects = invoked.control_effects;
  require(effects[0].kind == TfControlEffectKind::Output &&
              effects[0].channel == 1 && effects[0].text == "value=7" &&
              effects[1].kind == TfControlEffectKind::Output &&
              effects[1].channel == 5 && effects[1].text == "mcd output" &&
              effects[2].kind == TfControlEffectKind::Output &&
              effects[2].text == "text 2a" &&
              effects[3].kind == TfControlEffectKind::Warning &&
              effects[3].level == ERR_WARNING &&
              effects[3].text == "warning value" &&
              effects[4].kind == TfControlEffectKind::Error &&
              effects[4].level == ERR_ERROR && effects[4].text == "error 2.5",
          "TF formatted output preserves channel, severity, text, and order");
  require(effects[5].kind == TfControlEffectKind::Message &&
              effects[5].level == ERR_SYSTEM && effects[5].facility == "FAC" &&
              effects[5].message_number == "E123" &&
              effects[5].text == "message 9" &&
              effects[6].kind == TfControlEffectKind::Stop &&
              effects[7].kind == TfControlEffectKind::Finish,
          "TF structured messages and stop/finish remain distinct effects");

  auto throwing = bind_tf_call(registration(
      "$throwing", TfRegistrationKind::Task, throwing_callback));
  const auto thrown = throwing.value->invoke();
  require(thrown.error == TfCallError::CallbackException &&
              thrown.control_effects.empty(),
          "callback exceptions discard output and control effects");
  auto unassigned = bind_tf_call(
      registration("$unassigned", TfRegistrationKind::Function,
                   unassigned_callback, nullptr, unassigned_callback));
  const auto missing_result = unassigned.value->invoke();
  require(missing_result.error == TfCallError::UnassignedResult &&
              missing_result.control_effects.empty(),
          "unassigned function results discard control effects");

  for (const auto& limit : {
           registration("$effect_limit", TfRegistrationKind::Task,
                        effect_limit_callback),
           registration("$text_limit", TfRegistrationKind::Task,
                        text_limit_callback),
           registration("$metadata_limit", TfRegistrationKind::Task,
                        metadata_limit_callback),
           registration("$level", TfRegistrationKind::Task, level_callback)}) {
    auto limited = bind_tf_call(limit);
    const auto result = limited.value->invoke();
    require(result.error == TfCallError::ControlLimit &&
                result.control_effects.empty(),
            "TF control resource or validation failure is transactional");
  }

  TfControlCapture malformed;
  require(capture_tf_control_effect_v3(
              &malformed, 0, 0, 0, nullptr, 0, nullptr, 0, nullptr, 0) != 0 &&
              malformed.error == TfControlError::InvalidKind &&
              malformed.effects.empty(),
          "TF host capture rejects malformed effect kinds without residue");
  return 0;
}
