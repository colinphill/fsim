// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_checkpoint.hpp"

#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_object.hpp"

#include <algorithm>
#include <stdexcept>

namespace fsim::tests::runtime {
namespace {

void require_checkpoint(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL
retained_callback(void *, const fsim_uvm_foreign_activity_v1 *) {
  return FSIM_UVM_FOREIGN_OK;
}

struct Fixture {
  fsim::runtime::SystemVerilogClassHeap heap;
  fsim::runtime::SystemVerilogUvmObjectService objects{
      heap, [](std::string_view, std::string_view, std::string_view) {
        return fsim::runtime::SystemVerilogClassHandle{};
      }};
  fsim::runtime::SystemVerilogUvmComponentService components{heap, objects};
  fsim::runtime::Scheduler scheduler;
  fsim::runtime::SystemVerilogUvmActivityService activity;
  fsim::runtime::SystemVerilogUvmPhaseService phases{components, scheduler};
  fsim::runtime::SystemVerilogUvmObjectionService objections{
      objects, components, phases, scheduler};
  fsim::runtime::SystemVerilogUvmTlm1Service tlm1{heap, components};
  fsim::runtime::SystemVerilogUvmTlm2Service tlm2{components, scheduler};
  fsim::runtime::SystemVerilogUvmForeignService foreign{phases, objections,
                                                        tlm1, tlm2, activity};

  Fixture() {
    activity.set_scheduler(scheduler);
    phases.set_activity_service(activity);
    objections.set_activity_service(activity);
    tlm1.set_scheduler(scheduler);
    tlm1.set_activity_service(activity);
    tlm1.set_phase_service(phases);
    tlm2.set_activity_service(activity);
    foreign.set_scheduler(scheduler);
    const std::array roots{components.create_root("left"),
                           components.create_root("right")};
    (void)phases.create_standard_schedule(roots);
  }
};

fsim::runtime::SystemVerilogUvmCheckpointProvenance provenance() {
  return {"content-160-16",
          "native-cache-160-16",
          "artifact-160-16",
          {"left", "right"},
          "1.2",
          "source-160-16"};
}

} // namespace

void test_systemverilog_uvm_checkpoint() {
  using namespace fsim::runtime;
  Fixture source;
  std::uint64_t callback{};
  require_checkpoint(
      source.foreign.add_callback(retained_callback, nullptr, callback) ==
          FSIM_UVM_FOREIGN_OK,
      "UVM checkpoint callback setup failed");
  const auto captured =
      capture_systemverilog_uvm_checkpoint(source.foreign, provenance());
  require_checkpoint(
      captured
          && captured.artifact.schema == systemverilog_uvm_checkpoint_schema &&
          captured.artifact.foreign_abi == FSIM_UVM_FOREIGN_ABI_VERSION &&
          captured.artifact.records.size() ==
              kSystemVerilogUvmStandardPhaseCount &&
          captured.artifact.external_state.phase_processes == 0 &&
          captured.artifact.external_state.callbacks == 1,
      "UVM checkpoint did not capture the portable phase state");
  require_checkpoint(
      std::ranges::all_of(captured.artifact.records,
                          [](const auto &record) {
                            return record.kind == FSIM_UVM_FOREIGN_PHASE &&
                                   record.kind !=
                                       FSIM_UVM_FOREIGN_PHASE_PROCESS &&
                                   !record.identity.empty();
                          }),
      "UVM checkpoint retained a nonportable process record");
  require_checkpoint(
      verify_systemverilog_uvm_checkpoint(captured.artifact, source.foreign,
                                          provenance()) ==
          SystemVerilogUvmCheckpointError::None,
      "UVM checkpoint did not verify against unchanged live state");

  const auto require_resource_limit =
      [&](SystemVerilogUvmCheckpointLimits limits) {
        const auto bounded = capture_systemverilog_uvm_checkpoint(
            source.foreign, provenance(), limits);
        require_checkpoint(
            !bounded &&
                bounded.error == SystemVerilogUvmCheckpointError::ResourceLimit,
            "FSIM-UVM-STATE-002 checkpoint resource limit was not atomic");
      };
  SystemVerilogUvmCheckpointLimits record_limits;
  record_limits.maximum_records = 1;
  require_resource_limit(record_limits);
  SystemVerilogUvmCheckpointLimits text_limits;
  text_limits.maximum_text_bytes = 1;
  require_resource_limit(text_limits);
  SystemVerilogUvmCheckpointLimits root_limits;
  root_limits.maximum_roots = 1;
  require_resource_limit(root_limits);
  SystemVerilogUvmCheckpointLimits identity_limits;
  identity_limits.maximum_identity_bytes = 1;
  require_resource_limit(identity_limits);
  SystemVerilogUvmCheckpointLimits callback_limits;
  callback_limits.maximum_external_callbacks = 0;
  require_resource_limit(callback_limits);
  auto payload_state = captured.artifact;
  payload_state.records.front().payload = {1};
  SystemVerilogUvmCheckpointLimits payload_limits;
  payload_limits.maximum_payload_bytes = 0;
  require_checkpoint(validate_systemverilog_uvm_checkpoint(
                         payload_state, provenance(), payload_limits) ==
                         SystemVerilogUvmCheckpointError::InvalidArtifact,
                     "checkpoint payload ceilings must reject retained state");

  auto mismatch = captured.artifact;
  mismatch.schema += 1;
  require_checkpoint(
      validate_systemverilog_uvm_checkpoint(mismatch, provenance()) ==
          SystemVerilogUvmCheckpointError::SchemaMismatch,
      "UVM checkpoint schema mismatch was accepted");
  mismatch = captured.artifact;
  mismatch.foreign_abi += 1;
  require_checkpoint(
      validate_systemverilog_uvm_checkpoint(mismatch, provenance()) ==
          SystemVerilogUvmCheckpointError::AbiMismatch,
      "UVM checkpoint ABI mismatch was accepted");
  auto expected = provenance();
  expected.content_identity = "other-content";
  require_checkpoint(
      validate_systemverilog_uvm_checkpoint(captured.artifact, expected) ==
          SystemVerilogUvmCheckpointError::ContentMismatch,
      "UVM checkpoint content mismatch was accepted");
  expected = provenance();
  expected.cache_identity = "other-cache";
  require_checkpoint(
      validate_systemverilog_uvm_checkpoint(captured.artifact, expected) ==
          SystemVerilogUvmCheckpointError::CacheMismatch,
      "UVM checkpoint cache mismatch was accepted");
  expected = provenance();
  expected.artifact_identity = "other-artifact";
  require_checkpoint(
      validate_systemverilog_uvm_checkpoint(captured.artifact, expected) ==
          SystemVerilogUvmCheckpointError::ArtifactMismatch,
      "UVM checkpoint artifact mismatch was accepted");
  expected = provenance();
  std::ranges::reverse(expected.roots);
  require_checkpoint(
      validate_systemverilog_uvm_checkpoint(captured.artifact, expected) ==
          SystemVerilogUvmCheckpointError::RootMismatch,
      "UVM checkpoint root mismatch was accepted");
  expected = provenance();
  expected.uvm_release = "2020.3.1";
  require_checkpoint(
      validate_systemverilog_uvm_checkpoint(captured.artifact, expected) ==
          SystemVerilogUvmCheckpointError::ReleaseMismatch,
      "UVM checkpoint release mismatch was accepted");
  expected = provenance();
  expected.source_identity = "other-source";
  require_checkpoint(
      validate_systemverilog_uvm_checkpoint(captured.artifact, expected) ==
          SystemVerilogUvmCheckpointError::SourceMismatch,
      "UVM checkpoint source mismatch was accepted");
  mismatch = captured.artifact;
  mismatch.records.front().kind = FSIM_UVM_FOREIGN_PHASE_PROCESS;
  require_checkpoint(
      validate_systemverilog_uvm_checkpoint(mismatch, provenance()) ==
          SystemVerilogUvmCheckpointError::InvalidArtifact,
      "UVM checkpoint accepted a nonportable process record");
  mismatch = captured.artifact;
  ++mismatch.records.front().value;
  require_checkpoint(verify_systemverilog_uvm_checkpoint(
                         mismatch, source.foreign, provenance()) ==
                         SystemVerilogUvmCheckpointError::StateMismatch,
                     "UVM checkpoint state mismatch was accepted");

  const auto bootstrap =
      make_systemverilog_uvm_bootstrap_checkpoint(provenance());
  require_checkpoint(bootstrap &&
                         bootstrap.artifact.records.size() ==
                             kSystemVerilogUvmStandardPhaseCount &&
                         bootstrap.artifact.provenance == provenance(),
                     "UVM bootstrap checkpoint was not deterministic");
  require_checkpoint(source.foreign.remove_callback(callback) ==
                         FSIM_UVM_FOREIGN_OK,
                     "UVM checkpoint callback teardown failed");
}

} // namespace fsim::tests::runtime
