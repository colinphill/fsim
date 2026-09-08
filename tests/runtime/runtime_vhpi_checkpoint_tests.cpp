// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_checkpoint.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {

namespace {

using namespace fsim::runtime;

void require_checkpoint(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

VhdlVhpiCheckpointCompatibility compatibility(const std::string& root) {
  return {
      "vhdl-design-content-158",
      "vhdl-cache-llvm22-debug",
      {{
          "work.library",
          root + "/libraries/work.fsimlib",
          "work-library-content-158",
      }},
      {{
          "vhpi-checkpoint-plugin",
          root + "/plugins/checkpoint.vhpi",
          "work.library",
          "vhpi-plugin-content-158",
          "vhpi-host-content-158",
      }},
  };
}

VhdlVhpiNativeStateSummary native_state() {
  return {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
}

struct Hierarchy {
  VhdlVhpiObjectRegistry objects;
  fsim_vhpi_handle_v1 root{};
  fsim_vhpi_handle_v1 entity{};
  fsim_vhpi_handle_v1 view{};
  fsim_vhpi_handle_v1 view_element{};
  fsim_vhpi_handle_v1 signal{};

  explicit Hierarchy(const std::uint64_t identity) : objects(identity) {
    const auto made_root = objects.create_object({
        VhdlVhpiObjectKind::Root, 0, "WORK", {},
        VhdlVhpiSourceLocation{"design/top.vhd", 1, 1}});
    const auto made_entity = objects.create_object({
        VhdlVhpiObjectKind::Entity, made_root.value, "Top", {},
        VhdlVhpiSourceLocation{"design/top.vhd", 3, 1}});
    const auto made_view = objects.create_object({
        VhdlVhpiObjectKind::InterfaceView, made_entity.value, "Bus_View", {},
        VhdlVhpiSourceLocation{"design/top.vhd", 5, 3}});
    const auto made_view_element = objects.create_object({
        VhdlVhpiObjectKind::ViewElement, made_view.value, "Ready", {},
        VhdlVhpiSourceLocation{"design/top.vhd", 6, 5}});
    const std::array<std::int64_t, 1> index{7};
    const auto made_signal = objects.create_object({
        VhdlVhpiObjectKind::Signal, made_entity.value, "Data", index,
        VhdlVhpiSourceLocation{"design/top.vhd", 7, 3}});
    require_checkpoint(
        made_root && made_entity && made_view && made_view_element
            && made_signal,
        "VHPI checkpoint hierarchy creation failed");
    root = made_root.value;
    entity = made_entity.value;
    view = made_view.value;
    view_element = made_view_element.value;
    signal = made_signal.value;
  }
};

VhdlVhpiCheckpointError portable_error(
    const VhdlVhpiCheckpointArtifact& artifact,
    const Hierarchy& target,
    const VhdlVhpiCheckpointCompatibility& expected) {
  return restore_vhdl_vhpi_checkpoint(
      artifact,
      VhdlVhpiCheckpointFlow::PortableArtifact,
      target.objects,
      expected)
      .error;
}

}  // namespace

void test_vhdl_vhpi_checkpoint_restart_and_artifact() {
  Hierarchy source{1'580};
  Hierarchy target{1'581};
  const std::array exported{
      source.root, source.entity, source.view, source.view_element,
      source.signal};
  const auto saved_compatibility = compatibility("/build/original");
  const auto saved_native = native_state();
  const auto captured = capture_vhdl_vhpi_checkpoint(
      source.objects, exported, saved_compatibility, saved_native);
  require_checkpoint(
      captured
          && captured.artifact.source_simulation_identity == 1'580
          && captured.artifact.host_size == sizeof(fsim_vhpi_host_v1)
          && captured.artifact.plugin_size == sizeof(fsim_vhpi_plugin_v1)
          && captured.artifact.objects.size() == 5
          && captured.artifact.objects.back().full_name
              == "work.top.data(7)"
          && captured.artifact.native_state.plugin_contexts == 1,
      "VHPI checkpoint lost ABI, object, or native provenance");

  const auto restarted = restore_vhdl_vhpi_checkpoint(
      captured.artifact,
      VhdlVhpiCheckpointFlow::InProcessRestart,
      source.objects,
      saved_compatibility,
      saved_native);
  require_checkpoint(
      restarted && restarted.handles.size() == exported.size()
          && restarted.invalidations.empty()
          && restarted.relocations.empty()
          && std::ranges::all_of(restarted.handles, [](const auto& remap) {
               return remap.source == remap.target;
             }),
      "VHPI same-process restart did not preserve exact handles");
  require_checkpoint(
      restore_vhdl_vhpi_checkpoint(
          captured.artifact,
          VhdlVhpiCheckpointFlow::InProcessRestart,
          target.objects,
          saved_compatibility,
          saved_native)
              .error == VhdlVhpiCheckpointError::InvalidSimulation,
      "VHPI same-process restart crossed simulation ownership");
  auto changed_native = saved_native;
  ++changed_native.active_foreign_calls;
  require_checkpoint(
      restore_vhdl_vhpi_checkpoint(
          captured.artifact,
          VhdlVhpiCheckpointFlow::InProcessRestart,
          source.objects,
          saved_compatibility,
          changed_native)
              .error == VhdlVhpiCheckpointError::NativeStateMismatch,
      "VHPI same-process restart accepted changed native owners");

  auto mismatch = captured.artifact;
  mismatch.schema += 1;
  require_checkpoint(
      portable_error(mismatch, target, saved_compatibility)
          == VhdlVhpiCheckpointError::SchemaMismatch,
      "VHPI checkpoint schema mismatch was accepted");
  mismatch = captured.artifact;
  mismatch.host_size += 1;
  require_checkpoint(
      portable_error(mismatch, target, saved_compatibility)
          == VhdlVhpiCheckpointError::HostAbiMismatch,
      "VHPI checkpoint host ABI size mismatch was accepted");
  mismatch = captured.artifact;
  mismatch.plugin_abi += 1;
  require_checkpoint(
      portable_error(mismatch, target, saved_compatibility)
          == VhdlVhpiCheckpointError::PluginAbiMismatch,
      "VHPI checkpoint plug-in ABI mismatch was accepted");
  mismatch = captured.artifact;
  mismatch.pointer_bits += 1;
  require_checkpoint(
      portable_error(mismatch, target, saved_compatibility)
          == VhdlVhpiCheckpointError::PointerWidthMismatch,
      "VHPI checkpoint pointer-width mismatch was accepted");

  auto expected = saved_compatibility;
  expected.content_fingerprint = "different-content";
  require_checkpoint(
      portable_error(captured.artifact, target, expected)
          == VhdlVhpiCheckpointError::ContentMismatch,
      "VHPI checkpoint content mismatch was accepted");
  expected = saved_compatibility;
  expected.cache_fingerprint = "different-cache";
  require_checkpoint(
      portable_error(captured.artifact, target, expected)
          == VhdlVhpiCheckpointError::CacheMismatch,
      "VHPI checkpoint cache mismatch was accepted");
  expected = saved_compatibility;
  expected.mapped_libraries.front().content_fingerprint =
      "different-library";
  require_checkpoint(
      portable_error(captured.artifact, target, expected)
          == VhdlVhpiCheckpointError::MappedLibraryMismatch,
      "VHPI checkpoint mapped-library mismatch was accepted");
  expected = saved_compatibility;
  expected.plugins.front().host_fingerprint = "different-host";
  require_checkpoint(
      portable_error(captured.artifact, target, expected)
          == VhdlVhpiCheckpointError::PluginMismatch,
      "VHPI checkpoint plug-in provenance mismatch was accepted");

  mismatch = captured.artifact;
  mismatch.objects.back().full_name = "work.top.missing";
  const auto missing = restore_vhdl_vhpi_checkpoint(
      mismatch,
      VhdlVhpiCheckpointFlow::PortableArtifact,
      target.objects,
      saved_compatibility);
  require_checkpoint(
      missing.error == VhdlVhpiCheckpointError::ObjectMismatch
          && missing.handles.empty() && missing.invalidations.empty()
          && missing.relocations.empty(),
      "VHPI object mismatch returned partial restore state");
  mismatch = captured.artifact;
  mismatch.objects.back().kind = VhdlVhpiObjectKind::Variable;
  require_checkpoint(
      portable_error(mismatch, target, saved_compatibility)
          == VhdlVhpiCheckpointError::ObjectMismatch,
      "VHPI checkpoint object-kind mismatch was accepted");

  expected = compatibility("/relocated/cache");
  const auto restored = restore_vhdl_vhpi_checkpoint(
      captured.artifact,
      VhdlVhpiCheckpointFlow::PortableArtifact,
      target.objects,
      expected);
  require_checkpoint(
      restored && restored.handles.size() == exported.size()
          && restored.invalidations.size() == 11
          && restored.relocations.size() == 2
          && restored.relocations.front().identity
              == "library:work.library"
          && restored.relocations.back().identity
              == "plugin:vhpi-checkpoint-plugin",
      "VHPI portable restore did not remap, invalidate, and relocate");
  const auto signal_remap = std::ranges::find(
      restored.handles, source.signal, &VhdlVhpiHandleRemap::source);
  require_checkpoint(
      signal_remap != restored.handles.end()
          && signal_remap->target == target.signal
          && signal_remap->source != signal_remap->target
          && signal_remap->full_name == "work.top.data(7)",
      "VHPI portable restore lost canonical signal handle remapping");
  require_checkpoint(
      restore_vhdl_vhpi_checkpoint(
          captured.artifact,
          VhdlVhpiCheckpointFlow::PortableArtifact,
          target.objects,
          expected,
          saved_native)
              .error == VhdlVhpiCheckpointError::NativeStateMismatch,
      "VHPI portable restore accepted pre-existing native owners");

  const std::array duplicate{source.signal, source.signal};
  require_checkpoint(
      capture_vhdl_vhpi_checkpoint(
          source.objects, duplicate, saved_compatibility, saved_native)
              .error == VhdlVhpiCheckpointError::InvalidArtifact,
      "VHPI checkpoint accepted duplicate exported handles");
  const std::array cross_simulation{source.signal, target.signal};
  require_checkpoint(
      capture_vhdl_vhpi_checkpoint(
          source.objects,
          cross_simulation,
          saved_compatibility,
          saved_native)
              .error == VhdlVhpiCheckpointError::ObjectMismatch,
      "VHPI checkpoint accepted a cross-simulation handle");
}

}  // namespace fsim::tests::runtime
