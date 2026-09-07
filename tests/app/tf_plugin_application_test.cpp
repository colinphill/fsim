// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application_tf.hpp"

#include <array>
#include <filesystem>
#include <stdexcept>

namespace {

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error{message};
}

}  // namespace

int main() {
  using fsim::app::TfApplicationError;
  using fsim::app::TfApplicationRegistry;
  using fsim::app::tf_application_profile_supported;
  using fsim::frontend::StandardRevision;
  using fsim::runtime::TfRegistrationKind;

  const std::array retained_profiles{
      StandardRevision::Verilog1995,
      StandardRevision::Verilog2001,
      StandardRevision::Verilog2001NoConfig,
      StandardRevision::Verilog2005,
      StandardRevision::SystemVerilog2005,
      StandardRevision::SystemVerilog2009,
      StandardRevision::SystemVerilog2012,
      StandardRevision::SystemVerilog2017,
  };
  const std::array vhdl_profiles{
      StandardRevision::Vhdl1987,
      StandardRevision::Vhdl1993,
      StandardRevision::Vhdl2000,
      StandardRevision::Vhdl2002,
      StandardRevision::Vhdl2008,
  };
  for (const auto profile : retained_profiles) {
    require(tf_application_profile_supported(profile),
            "every retained Verilog/SystemVerilog profile supports TF");
  }
  for (const auto profile : vhdl_profiles) {
    require(!tf_application_profile_supported(profile),
            "TF registration does not leak into VHDL profiles");
  }

  TfApplicationRegistry registry;
  const auto loaded = registry.load(FSIM_TF_LINK_PROBE_PLUGIN_PATH);
  require(loaded && loaded.plugin_index == 0 &&
              loaded.registrations.size() == 2 &&
              registry.plugin_count() == 1 &&
              registry.registration_count() == 2,
          "application registry transactionally publishes one TF plug-in");
  require(loaded.registrations[0].name == "$fsim_tf_link_probe" &&
              loaded.registrations[0].kind == TfRegistrationKind::Task &&
              loaded.registrations[1].name == "$fsim_tf_function_probe" &&
              loaded.registrations[1].kind == TfRegistrationKind::Function,
          "application registry preserves task and function identities");

  for (const auto profile : retained_profiles) {
    const auto task = registry.resolve(
        profile, "$fsim_tf_link_probe", TfRegistrationKind::Task);
    const auto function = registry.resolve(
        profile, "$fsim_tf_function_probe", TfRegistrationKind::Function);
    require(task && function && task.registration.plugin_index == 0 &&
                task.registration.registration_index == 0 &&
                function.registration.plugin_index == 0 &&
                function.registration.registration_index == 1,
            "TF tasks and functions resolve identically in every HDL profile");
  }

  for (const auto profile : vhdl_profiles) {
    require(registry.resolve(profile, "$fsim_tf_link_probe").error ==
                TfApplicationError::UnsupportedProfile,
            "VHDL lookup rejects the TF namespace deterministically");
  }
  require(registry
                  .resolve(StandardRevision::Verilog2005,
                           "$fsim_tf_link_probe",
                           TfRegistrationKind::Function)
                  .error == TfApplicationError::KindMismatch &&
              registry
                      .resolve(StandardRevision::SystemVerilog2017,
                               "$FSIM_TF_LINK_PROBE")
                      .error == TfApplicationError::MissingRegistration,
          "TF lookup preserves callable kind and exact source spelling");

  auto task = registry.bind(StandardRevision::Verilog1995,
                            "$fsim_tf_link_probe");
  require(task && task.binding.value->invoke(),
          "registered TF task binds and executes in a Verilog profile");
  auto function = registry.bind(StandardRevision::SystemVerilog2017,
                                "$fsim_tf_function_probe");
  require(static_cast<bool>(function),
          "registered TF function binds in a SystemVerilog profile");
  const auto function_result = function.binding.value->invoke();
  require(function.binding.value->result_width() == 17 &&
              function_result && function_result.function_result.has_value() &&
              function_result.function_result->width == 17 &&
              function_result.function_result->aval_words[0] == 23,
          "registered TF function preserves sizing and result callbacks");

  const auto duplicate = registry.load(FSIM_TF_LINK_PROBE_PLUGIN_PATH);
  require(duplicate.error == TfApplicationError::DuplicateRegistration &&
              registry.plugin_count() == 1 &&
              registry.registration_count() == 2,
          "duplicate plug-in names publish no partial application state");
  const auto missing = registry.load(
      std::filesystem::path{FSIM_TF_LINK_PROBE_PLUGIN_PATH}.parent_path() /
      "missing-tf-plugin");
  require(missing.error == TfApplicationError::Plugin &&
              missing.plugin_error ==
                  fsim::runtime::TfPluginError::ArtifactOpen &&
              registry.plugin_count() == 1 &&
              registry.registration_count() == 2,
          "plug-in load failures preserve the existing TF registry");
  return 0;
}
