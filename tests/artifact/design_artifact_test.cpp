// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <sstream>
#include <vector>

#include "fsim/artifact/design.hpp"
#include "fsim/runtime/fst_change_encoder.hpp"
#include "fsim/runtime/fst_compression.hpp"
#include "fsim/runtime/fst_reader.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/kernel_backend_binding_inventory.hpp"
#include "fsim/systemc/kernel_backend_inventory.hpp"
#include "fsim/systemc/kernel_backend_observation.hpp"
#include "fsim/systemc/kernel_backend_tlm1.hpp"
#include "fsim/systemc/kernel_backend_tlm2.hpp"
#include "fsim/systemc/kernel_backend_value_codec.hpp"

namespace {

std::string checksum(const std::string_view bytes) {
  return fsim::support::Sha256::hex(fsim::support::Sha256::digest(bytes));
}

void store_u32(std::string& bytes, const std::size_t offset,
    const std::uint32_t value)
{
    assert(offset + 4U <= bytes.size());
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] = static_cast<char>((value >> shift) & 0xffU);
    }
}

void store_u64(std::string& bytes, const std::size_t offset,
    const std::uint64_t value)
{
    assert(offset + 8U <= bytes.size());
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes[offset + shift / 8U] = static_cast<char>((value >> shift) & 0xffU);
    }
}

void make_tree_writable(const std::filesystem::path& root) {
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
       !error && iterator != end; iterator.increment(error)) {
    std::filesystem::permissions(iterator->path(),
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add, error);
    error.clear();
  }
  std::filesystem::permissions(root, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::add, error);
}

}  // namespace

int main() {
    static_assert(fsim::artifact::kDesignFormatVersion == 12U);
    static_assert(fsim::runtime_abi_version == 1U);
    std::string trace_source(257U, '0');
    constexpr char trace_symbols[] = { '0', '1', 'X', 'Z' };
    for (std::size_t index = 0; index < trace_source.size(); ++index) {
        trace_source[index] = trace_symbols[index % 4U];
    }
  const auto trace_value = fsim::runtime::encode_fst_logic_value(
      fsim::runtime::PackedLogic4::from_msb_string(trace_source));
  std::string state_bytes{trace_value.symbols()};
  assert(state_bytes.size() == 257U);
  assert(state_bytes.front() == '0' && state_bytes.back() == '0');
  assert(state_bytes.find('x') != std::string::npos);
  assert(state_bytes.find('z') != std::string::npos);
  const auto real_value = fsim::runtime::encode_fst_systemverilog_real(
      {fsim::runtime::SystemVerilogScalarKind::Real,
       UINT64_C(0xfff8000000000042)});
  const auto string_value =
      fsim::runtime::encode_fst_string(std::string_view{"A\0B\xc3\xa9", 5U});
  fsim::runtime::FstExtendedTypeMetadata enum_metadata;
  enum_metadata.kind = fsim::runtime::FstExtendedTypeKind::Enumeration;
  enum_metadata.language = fsim::runtime::FstTypeLanguage::SystemVerilog;
  enum_metadata.nominal_name = "state_t";
  enum_metadata.width = 3U;
  enum_metadata.four_state = true;
  enum_metadata.enumeration_literals = {"IDLE", "BUSY", "ERROR"};
  const auto enum_value = fsim::runtime::encode_fst_extended_value(
      fsim::runtime::PackedLogic4::from_msb_string("1X0"),
      fsim::runtime::FstValueProfile::Enumeration, enum_metadata);
  fsim::runtime::FstExtendedTypeMetadata physical_metadata;
  physical_metadata.kind = fsim::runtime::FstExtendedTypeKind::VhdlPhysical;
  physical_metadata.language = fsim::runtime::FstTypeLanguage::Vhdl;
  physical_metadata.nominal_name = "duration";
  physical_metadata.width = 32U;
  physical_metadata.physical_units = {{"fs", 1}, {"ps", 1000}};
  const auto physical_value = fsim::runtime::encode_fst_extended_value(
      fsim::runtime::PackedLogic4::from_aval_bval(32U, 123U, 0U),
      fsim::runtime::FstValueProfile::VhdlPhysical, physical_metadata);
  fsim::runtime::FstExtendedTypeMetadata logic9_metadata;
  logic9_metadata.kind = fsim::runtime::FstExtendedTypeKind::VhdlLogic9;
  logic9_metadata.language = fsim::runtime::FstTypeLanguage::Vhdl;
  logic9_metadata.nominal_name = "ieee.std_logic_1164.std_logic_vector";
  logic9_metadata.width = 9U;
  logic9_metadata.enumeration_literals = {"'U'", "'X'", "'0'", "'1'", "'Z'",
                                          "'W'", "'L'", "'H'", "'-'"};
  const auto logic9_value = fsim::runtime::encode_fst_extended_value(
      fsim::runtime::PackedLogic4::from_logic9_msb_string("UX01ZWLH-"),
      fsim::runtime::FstValueProfile::VhdlLogic9, logic9_metadata);
  fsim::runtime::FstLeafTypeMetadata leaf_metadata;
  leaf_metadata.kind = fsim::runtime::FstLeafKind::PackedAggregate;
  leaf_metadata.owner_identity = "primary.packet@declaration-5";
  leaf_metadata.leaf_path = "header.kind";
  leaf_metadata.width = 17U;
  leaf_metadata.four_state = true;
  leaf_metadata.dimensions = {{7, 0, true}, {1, 0, true}};
  const auto leaf_value = fsim::runtime::encode_fst_leaf_value(
      fsim::runtime::PackedLogic4::from_msb_string("10XZ0011010101010"),
      leaf_metadata);
  fsim::runtime::FstChangeEncoder change_encoder;
  change_encoder.append({{1U}, 9U, 0U, fsim::runtime::TraceRegion::Active, 2U},
                        trace_value);
  change_encoder.append(
      {{1U}, 8U, 0U, fsim::runtime::TraceRegion::Snapshot, 1U}, trace_value);
  const auto ordered_changes = std::move(change_encoder).freeze();
  assert(ordered_changes.values().size() == 2U);
  assert(ordered_changes.values().front().event.time == 8U);
  assert(ordered_changes.values().back().event.time == 9U);

  fsim::runtime::TraceDeclarationBuilder trace_builder;
  fsim::runtime::TraceSourceMetadata systemc_source;
  systemc_source.kind = fsim::runtime::TraceSourceKind::SystemC;
  systemc_source.language = fsim::runtime::TraceLanguage::SystemC;
  systemc_source.root_identity = "systemc_root";
  systemc_source.library = "systemc";
  systemc_source.owner_identity = "systemc:trace-island";
  const auto trace_signal = trace_builder.add_typed_variable(
      "systemc_root.payload", fsim::runtime::TraceTypeKind::Packed,
      trace_source.size(), fsim::runtime::SystemVerilogScalarKind::None, {},
      systemc_source);
  static_cast<void>(trace_builder.add_alias("systemc_root.payload_input",
                                            trace_signal, systemc_source));
  const auto trace_model = std::move(trace_builder).freeze();
  std::ostringstream fst_output(std::ios::binary);
  fsim::runtime::FstWriter fst_writer{fst_output};
  fst_writer.declare(trace_model);
  fst_writer.begin(8U);
  fst_writer.set_initial_value(trace_signal, trace_value);
  fst_writer.change(
      {trace_signal, 9U, 0U, fsim::runtime::TraceRegion::Active, 1U},
      trace_value);
  fst_writer.change(
      {trace_signal, 9U, 1U, fsim::runtime::TraceRegion::Postponed, 2U},
      trace_value);
  fst_writer.close(9U);
  const auto decoded_fst = fsim::runtime::read_fst(fst_output.str());
  assert(decoded_fst.ok());
  assert(decoded_fst.trace->values.size() == 3U);
  assert(decoded_fst.trace->values.front().payload == trace_value.symbols());
  assert(decoded_fst.trace->values.back().time == 9U);
  assert(decoded_fst.trace->values[1].payload ==
         decoded_fst.trace->values[2].payload);

  state_bytes.append(real_value.canonical_type());
  for (unsigned shift = 0; shift < 64U; shift += 8U) {
    state_bytes.push_back(static_cast<char>(real_value.real_bits() >> shift));
  }
  state_bytes.append(string_value.canonical_type());
  state_bytes.append(string_value.string_bytes());
  state_bytes.append(enum_value.canonical_type());
  state_bytes.append(enum_value.symbols());
  state_bytes.append(physical_value.canonical_type());
  state_bytes.append(physical_value.symbols());
  state_bytes.append(logic9_value.canonical_type());
  state_bytes.append(logic9_value.symbols());
  state_bytes.append(leaf_value.canonical_type());
  state_bytes.append(leaf_value.symbols());
  for (const auto& change : ordered_changes.values()) {
    state_bytes.append(change.value.symbols());
  }
  const std::vector<std::uint8_t> compression_source(257U, 0x5aU);
  const auto compressed_state = fsim::runtime::compress_fst_block(
      compression_source,
      fsim::runtime::FstCompressionKind::InitialValueZlibFixedV1);
  assert(compressed_state.platform_byte_identical &&
         compressed_state.variability_reason.empty() &&
         compressed_state.bytes.size() < compression_source.size());
  state_bytes.append(compressed_state.profile_identity);
  state_bytes.append(compressed_state.semantic_digest);
  state_bytes.append(decoded_fst.trace->semantic_digest);
  state_bytes.append(
      reinterpret_cast<const char*>(compressed_state.bytes.data()),
      compressed_state.bytes.size());
  assert(state_bytes.find(std::string{"\x42\0\0\0\0\0\xf8\xff", 8U}) !=
         std::string::npos);
  assert(state_bytes.find(std::string{"A\0B\xc3\xa9", 5U}) !=
         std::string::npos);
  assert(state_bytes.find("1x0") != std::string::npos);
  assert(state_bytes.find("00000000000000000000000001111011") !=
         std::string::npos);
  assert(state_bytes.find("ux01zwlh-") != std::string::npos);
  assert(state_bytes.find("o=28:primary.packet@declaration-5;") !=
         std::string::npos);
  fsim::systemc::SystemCKernelValue crossing_value;
  crossing_value.kind = fsim::systemc::SystemCKernelValueKind::logic9;
  crossing_value.width = 129U;
  crossing_value.range = {
      64, -64, fsim::systemc::SystemCKernelRangeDirection::descending};
  crossing_value.type_name = "ieee.std_logic_1164.std_logic_vector";
  crossing_value.planes.assign(4U, std::vector<std::uint64_t>(3U));
  for (std::uint32_t bit = 0U; bit < crossing_value.width; ++bit) {
    const auto code = bit % 9U;
    const auto word = static_cast<std::size_t>(bit / 64U);
    const auto mask = std::uint64_t{1U} << (bit % 64U);
    for (unsigned plane = 0U; plane < 4U; ++plane) {
      if ((code & (1U << plane)) != 0U) {
        crossing_value.planes[plane][word] |= mask;
      }
    }
  }
  fsim::diagnostic::Engine crossing_diagnostics;
  const auto crossing_encoded = fsim::systemc::serialize_systemc_kernel_value(
      crossing_value, {}, crossing_diagnostics);
  assert(crossing_encoded && !crossing_diagnostics.has_error());
  const std::string crossing_bytes(
      reinterpret_cast<const char*>(crossing_encoded->data()),
      crossing_encoded->size());
  fsim::diagnostic::Engine crossing_decode_diagnostics;
  assert(fsim::systemc::deserialize_systemc_kernel_value(
             *crossing_encoded, {}, crossing_decode_diagnostics) ==
         crossing_value);
  const fsim::systemc::SystemCKernelProtocolLimits protocol_limits;
  const auto tlm_island = fsim::systemc::make_systemc_island_id(
      "artifact-tlm1-island", protocol_limits, crossing_diagnostics);
  const auto tlm_hierarchy = fsim::systemc::make_systemc_hierarchy_id(
      *tlm_island, "work.top", protocol_limits, crossing_diagnostics);
  const auto tlm_object = fsim::systemc::make_systemc_object_id(
      *tlm_hierarchy, "work.top.transport", protocol_limits,
      crossing_diagnostics);
  const auto tlm_source = fsim::systemc::make_systemc_endpoint_id(
      *tlm_object, "initiator", protocol_limits, crossing_diagnostics);
  const auto tlm_target = fsim::systemc::make_systemc_endpoint_id(
      *tlm_object, "target", protocol_limits, crossing_diagnostics);
  const auto tlm_sequence = fsim::systemc::make_systemc_sequence_id(
      *tlm_island, 11U, crossing_diagnostics);
  const auto tlm_transaction = fsim::systemc::make_systemc_transaction_id(
      *tlm_source, *tlm_sequence, crossing_diagnostics);
  assert(tlm_island && tlm_hierarchy && tlm_object && tlm_source &&
         tlm_target && tlm_sequence && tlm_transaction &&
         !crossing_diagnostics.has_error());
  fsim::systemc::SystemCKernelTlm1Transaction crossing_transaction;
  crossing_transaction.transaction = *tlm_transaction;
  crossing_transaction.endpoint = *tlm_source;
  crossing_transaction.peer = *tlm_target;
  crossing_transaction.sequence = *tlm_sequence;
  crossing_transaction.operation =
      fsim::systemc::SystemCKernelTlm1Operation::transport;
  crossing_transaction.state = fsim::systemc::SystemCKernelTlm1State::completed;
  crossing_transaction.time_fs = 5000U;
  crossing_transaction.delta = 3U;
  crossing_transaction.explicit_bridge = true;
  crossing_transaction.request = crossing_value;
  crossing_transaction.response = crossing_value;
  const auto tlm_encoded =
      fsim::systemc::serialize_systemc_kernel_tlm1_transaction(
          crossing_transaction, {}, crossing_diagnostics);
  assert(tlm_encoded && !crossing_diagnostics.has_error());
  const std::string tlm_bytes(
      reinterpret_cast<const char*>(tlm_encoded->data()), tlm_encoded->size());
  fsim::systemc::SystemCKernelTlm2Transaction tlm2_transaction;
  tlm2_transaction.transaction = *tlm_transaction;
  tlm2_transaction.endpoint = *tlm_source;
  tlm2_transaction.peer = *tlm_target;
  tlm2_transaction.sequence = *tlm_sequence;
  tlm2_transaction.operation =
      fsim::systemc::SystemCKernelTlm2Operation::direct_memory;
  tlm2_transaction.state = fsim::systemc::SystemCKernelTlm2State::completed;
  tlm2_transaction.sync = fsim::systemc::SystemCKernelTlm2Sync::completed;
  tlm2_transaction.time_fs = 6000U;
  tlm2_transaction.delta = 4U;
  tlm2_transaction.explicit_bridge = true;
  tlm2_transaction.payload.address = 0x40U;
  tlm2_transaction.payload.command =
      fsim::systemc::SystemCKernelTlm2Command::read;
  tlm2_transaction.payload.response =
      fsim::systemc::SystemCKernelTlm2Response::ok;
  tlm2_transaction.payload.dmi_allowed = true;
  tlm2_transaction.payload.streaming_width = 4U;
  tlm2_transaction.payload.data = {std::byte{1U}, std::byte{2U}, std::byte{3U},
                                   std::byte{4U}};
  tlm2_transaction.payload.extensions = {{"artifact.route", {std::byte{23U}}}};
  tlm2_transaction.dmi = {0x40U, 0x7fU,
                          fsim::systemc::SystemCKernelTlm2DmiAccess::read_write,
                          1000U, 2000U};
  const auto tlm2_encoded =
      fsim::systemc::serialize_systemc_kernel_tlm2_transaction(
          tlm2_transaction, {}, crossing_diagnostics);
  assert(tlm2_encoded && !crossing_diagnostics.has_error());
  const std::string tlm2_bytes(
      reinterpret_cast<const char*>(tlm2_encoded->data()),
      tlm2_encoded->size());
  fsim::systemc::SystemCKernelChannelInventory channel_inventory{
      *tlm_island, *tlm_hierarchy};
  assert(channel_inventory.register_channel(
      {"work.top.transport.ready", "sc_signal:bool:1",
       fsim::systemc::SystemCKernelChannelKind::signal,
       fsim::systemc::SystemCKernelChannelValueProfile{
           fsim::systemc::SystemCKernelValueKind::bit2, 1U, false},
       fsim::systemc::SystemCKernelWriterPolicy::one,
       fsim::systemc::SystemCKernelUpdateOwner::signal_kernel,
       fsim::systemc::SystemCKernelObservationMode::value_changed, true},
      crossing_diagnostics));
  assert(channel_inventory.freeze(crossing_diagnostics));
  const auto channel_inventory_encoded =
      fsim::systemc::serialize_systemc_kernel_channel_inventory(
          channel_inventory.snapshot(), {}, crossing_diagnostics);
  assert(channel_inventory_encoded && !crossing_diagnostics.has_error());
  const std::string channel_inventory_bytes(
      reinterpret_cast<const char*>(channel_inventory_encoded->data()),
      channel_inventory_encoded->size());
  fsim::systemc::SystemCKernelBindingInventory binding_inventory{
      channel_inventory.snapshot()};
  fsim::systemc::SystemCKernelBindingTarget binding_target;
  binding_target.chain = {"work.top.transport.ready_input",
                          "work.top.transport.ready"};
  binding_target.final_channel_path = "work.top.transport.ready";
  assert(binding_inventory.register_binding(
      {"work.top.transport.ready_input",
       "sc_in",
       fsim::systemc::SystemCKernelBindingKind::port,
       fsim::systemc::SystemCKernelBindingDirection::input,
       {std::move(binding_target)}},
      crossing_diagnostics));
  assert(binding_inventory.freeze(crossing_diagnostics));
  const auto binding_inventory_encoded =
      fsim::systemc::serialize_systemc_kernel_binding_inventory(
          binding_inventory.snapshot(), {}, crossing_diagnostics);
  assert(binding_inventory_encoded && !crossing_diagnostics.has_error());
  const std::string binding_inventory_bytes(
      reinterpret_cast<const char*>(binding_inventory_encoded->data()),
      binding_inventory_encoded->size());
  fsim::systemc::SystemCKernelObservationBatch systemc_observations;
  systemc_observations.island = *tlm_island;
  systemc_observations.records = {
      {fsim::systemc::SystemCKernelObservationKind::tlm1_end,
       {crossing_transaction.time_fs, crossing_transaction.delta,
        fsim::systemc::SystemCAccelleraRegion::quiescent, *tlm_island,
        *tlm_sequence},
       *tlm_source,
       *tlm_target,
       *tlm_transaction,
       std::nullopt,
       *tlm_encoded,
       "artifact correlated native TLM1 completion"},
      {fsim::systemc::SystemCKernelObservationKind::tlm2_dmi,
       {tlm2_transaction.time_fs, tlm2_transaction.delta,
        fsim::systemc::SystemCAccelleraRegion::quiescent, *tlm_island,
        *tlm_sequence},
       *tlm_source,
       *tlm_target,
       *tlm_transaction,
       std::nullopt,
       *tlm2_encoded,
       "artifact correlated native TLM2 DMI completion"}};
  const auto systemc_observation_encoded =
      fsim::systemc::serialize_systemc_kernel_observation_batch(
          systemc_observations, {}, crossing_diagnostics);
  assert(systemc_observation_encoded && !crossing_diagnostics.has_error());
  const std::string systemc_observation_bytes(
      reinterpret_cast<const char*>(systemc_observation_encoded->data()),
      systemc_observation_encoded->size());
  fsim::artifact::DesignMetadata metadata;
  const auto coverage_identity
      = fsim::artifact::make_code_coverage_artifact_identity(false).identity;
  metadata.code_coverage = coverage_identity;
  metadata.producer = "fsim test";
  metadata.time_resolution = "1ns";
  metadata.delay_mode = "typ";
  metadata.optimization = "O2";
  metadata.cache_key = checksum("cache");
  metadata.uvm_release = "2020.3.1";
  metadata.uvm_source_identity = checksum("uvm-sources");
  metadata.seed = 17;
  metadata.search_libraries = {"vendor"};
  metadata.roots.push_back({"primary", "sv:work.tb", "sv:work.tb"});
  metadata.bindings.push_back({"primary.child", std::nullopt, "std_logic"});
  metadata.objects.push_back({checksum("metadata"),
                              checksum("compilation"),
                              "systemverilog",
                              "2009",
                              "implicit-net,sizing",
                              "work",
                              coverage_identity,
                              {},
                              {checksum("unit")}});
  metadata.objects.push_back(
      {checksum("vhdl-metadata"),
       checksum("vhdl-compilation"),
       "vhdl",
       "1993",
       "fsim-synopsys-ieee-compat-v2",
       "vhdl_work",
       coverage_identity,
       {{"1993", "ieee-1076-standard:1993:fsim-v3", "ieee.std_logic_unsigned",
         "synopsys-legacy-ieee:1990-1992:fsim-synopsys-ieee-compat-v2",
         checksum("synopsys-unsigned-source")}},
       {checksum("vhdl-unit")}});
  metadata.vhdl_unit_provenance.push_back(
      {1,
       "1993",
       "ieee-1076-standard:1993:fsim-v3",
       "fsim-synopsys-ieee-compat-v2",
       {{"1993", "ieee-1076-standard:1993:fsim-v3", "ieee.std_logic_unsigned",
         "synopsys-legacy-ieee:1990-1992:fsim-synopsys-ieee-compat-v2",
         checksum("synopsys-unsigned-source")}}});
  metadata.verilog_unit_provenance.push_back(
      {0, "systemverilog", "2009", "implicit-net,sizing"});
  metadata.payloads = {
      {"runtime", "state/runtime.bin", checksum(state_bytes)},
      {"semantics", "state/semantics.bin", checksum(state_bytes)},
      {"design-ir", "state/design-ir.bin", checksum(state_bytes)},
      {"systemc-backend-values-v1", "state/systemc-values.bin",
       checksum(crossing_bytes)},
      {"systemc-native-tlm1-v1", "state/systemc-tlm1.bin", checksum(tlm_bytes)},
      {"systemc-native-tlm2-v1", "state/systemc-tlm2.bin",
       checksum(tlm2_bytes)},
      {"systemc-channel-inventory-v1", "state/systemc-channels.bin",
       checksum(channel_inventory_bytes)},
      {"systemc-binding-inventory-v1", "state/systemc-bindings.bin",
       checksum(binding_inventory_bytes)},
      {"systemc-observation-v1", "state/systemc-observations.bin",
       checksum(systemc_observation_bytes)},
      {"systemc-trace-dirty-v1", "state/systemc-trace.fst",
       checksum(fst_output.str())}};
  metadata.specialization_cache_keys = {checksum("specialization")};
  metadata.trace_archive = "00ff";
  metadata.unit_count = 2;
  metadata.semantic_source_count = 1;
  metadata.specialization_count = 1;
  metadata.signal_count = 2;
  metadata.process_count = 1;
  metadata.design_digest = fsim::artifact::compute_design_digest(metadata);

  const auto encoded = fsim::artifact::serialize_design_metadata(metadata);
  assert(encoded == fsim::artifact::serialize_design_metadata(metadata));
  fsim::diagnostic::Engine decode_diagnostics;
  assert(fsim::artifact::deserialize_design_metadata(
             encoded, "design", decode_diagnostics) == metadata);
  assert(!decode_diagnostics.has_error());
  auto enabled_coverage = metadata;
  enabled_coverage.code_coverage
      = fsim::artifact::make_code_coverage_artifact_identity(true).identity;
  for (auto& object : enabled_coverage.objects) {
    object.code_coverage = enabled_coverage.code_coverage;
  }
  enabled_coverage.design_digest
      = fsim::artifact::compute_design_digest(enabled_coverage);
  assert(enabled_coverage.design_digest != metadata.design_digest);
  auto stale_coverage = metadata;
  stale_coverage.code_coverage.schema = 2U;
  fsim::diagnostic::Engine stale_coverage_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(stale_coverage),
      "stale-coverage", stale_coverage_diagnostics));
  assert(std::ranges::any_of(
      stale_coverage_diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code
            == fsim::artifact::kCodeCoverageArtifactDiagnostic;
      }));

  auto embedded_plugin = metadata;
  embedded_plugin.systemc_plugins.push_back(
      { "native", checksum("plugin-input"), checksum("plugin-link"),
          checksum("compiler-producer"), "scv-2.0.1", "plugins/native",
          checksum("plugin-metadata"), checksum("plugin-library"),
          { "create_native" } });
  embedded_plugin.payloads.push_back(
      { "systemc-plugin-metadata:native",
          "plugins/native/fsim-systemc-plugin.bin", checksum("plugin-metadata") });
  embedded_plugin.payloads.push_back(
      { "systemc-plugin-native:native", "plugins/native/libnative.so",
          checksum("plugin-library") });
  embedded_plugin.design_digest = fsim::artifact::compute_design_digest(embedded_plugin);
  fsim::diagnostic::Engine embedded_plugin_diagnostics;
  assert(fsim::artifact::deserialize_design_metadata(
             fsim::artifact::serialize_design_metadata(embedded_plugin),
             "embedded-plugin", embedded_plugin_diagnostics)
      == embedded_plugin);
  assert(!embedded_plugin_diagnostics.has_error());

  auto missing_scv_identity = embedded_plugin;
  missing_scv_identity.systemc_plugins.front().scv_compatibility.clear();
  missing_scv_identity.design_digest = fsim::artifact::compute_design_digest(missing_scv_identity);
  fsim::diagnostic::Engine missing_scv_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(missing_scv_identity),
      "missing-scv-identity", missing_scv_diagnostics));
  assert(missing_scv_diagnostics.has_error());

  auto mismatched_plugin_payload = embedded_plugin;
  mismatched_plugin_payload.payloads[mismatched_plugin_payload.payloads.size() - 2U].checksum = checksum("different-plugin-metadata");
  mismatched_plugin_payload.design_digest = fsim::artifact::compute_design_digest(mismatched_plugin_payload);
  fsim::diagnostic::Engine mismatched_plugin_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(mismatched_plugin_payload),
      "mismatched-plugin-payload", mismatched_plugin_diagnostics));
  assert(mismatched_plugin_diagnostics.has_error());

  auto changed_sv_revision = metadata;
  changed_sv_revision.objects.front().standard = "2017";
  assert(fsim::artifact::compute_design_digest(changed_sv_revision) !=
         metadata.design_digest);
  auto changed_trace_archive = metadata;
  changed_trace_archive.trace_archive = "01ff";
  assert(fsim::artifact::compute_design_digest(changed_trace_archive) !=
         metadata.design_digest);
  auto changed_sv_compatibility = metadata;
  changed_sv_compatibility.objects.front().compatibility_profile = "none";
  assert(fsim::artifact::compute_design_digest(changed_sv_compatibility) !=
         metadata.design_digest);
  auto changed_sv_unit_provenance = metadata;
  changed_sv_unit_provenance.verilog_unit_provenance.front()
      .compatibility_profile = "sizing";
  assert(fsim::artifact::compute_design_digest(changed_sv_unit_provenance) !=
         metadata.design_digest);
  changed_sv_unit_provenance.design_digest =
      fsim::artifact::compute_design_digest(changed_sv_unit_provenance);
  fsim::diagnostic::Engine stale_sv_unit_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(changed_sv_unit_provenance),
      "stale-verilog-unit-provenance", stale_sv_unit_diagnostics));
  assert(stale_sv_unit_diagnostics.has_error());
  auto invalid_sv_unit_provenance = metadata;
  invalid_sv_unit_provenance.verilog_unit_provenance.front().standard = "2017";
  invalid_sv_unit_provenance.design_digest =
      fsim::artifact::compute_design_digest(invalid_sv_unit_provenance);
  fsim::diagnostic::Engine invalid_sv_unit_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(invalid_sv_unit_provenance),
      "invalid-verilog-unit-provenance", invalid_sv_unit_diagnostics));
  assert(invalid_sv_unit_diagnostics.has_error());
  auto partial_sv_unit_provenance = metadata;
  partial_sv_unit_provenance.verilog_unit_provenance.front()
      .compatibility_profile.clear();
  partial_sv_unit_provenance.design_digest =
      fsim::artifact::compute_design_digest(partial_sv_unit_provenance);
  fsim::diagnostic::Engine partial_sv_unit_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(partial_sv_unit_provenance),
      "partial-verilog-unit-provenance", partial_sv_unit_diagnostics));
  assert(partial_sv_unit_diagnostics.has_error());
  auto future_format = encoded;
  store_u32(
      future_format, 8U, fsim::artifact::kDesignFormatVersion + 1U);
  fsim::diagnostic::Engine future_format_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      future_format,
      "future-standard-compatibility-design", future_format_diagnostics));
  assert(future_format_diagnostics.has_error());

  auto changed_unit_provenance = metadata;
  changed_unit_provenance.vhdl_unit_provenance.front().standard = "2008";
  assert(fsim::artifact::compute_design_digest(changed_unit_provenance) !=
         metadata.design_digest);
  auto invalid_unit_provenance = metadata;
  invalid_unit_provenance.vhdl_unit_provenance.front().unit = 2;
  invalid_unit_provenance.design_digest =
      fsim::artifact::compute_design_digest(invalid_unit_provenance);
  fsim::diagnostic::Engine invalid_unit_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(invalid_unit_provenance),
      "invalid-vhdl-unit-provenance", invalid_unit_diagnostics));
  assert(invalid_unit_diagnostics.has_error());
  auto missing_unit_provenance = metadata;
  missing_unit_provenance.vhdl_unit_provenance.clear();
  missing_unit_provenance.design_digest =
      fsim::artifact::compute_design_digest(missing_unit_provenance);
  fsim::diagnostic::Engine missing_unit_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(missing_unit_provenance),
      "missing-vhdl-unit-provenance", missing_unit_diagnostics));
  assert(missing_unit_diagnostics.has_error());

  const auto has_design_identity_diagnostic = [](
                                                  const auto& diagnostics,
                                                  const std::string& found) {
      const auto expected = "unsupported .fsimdesign identity: found " + found
          + "; required format 12 and runtime ABI 1; regenerate .fsimdesign "
            "with this fsim build";
      return std::ranges::any_of(
          diagnostics.diagnostics(), [&](const auto& diagnostic) {
              return diagnostic.code == "FSIM-ART-0010"
                  && diagnostic.message == expected;
          });
  };

  for (const auto stale_format :
      { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U }) {
      auto stale_header = encoded.substr(0, 16U);
      store_u32(stale_header, 8U, stale_format);
      fsim::diagnostic::Engine stale_format_diagnostics;
      assert(!fsim::artifact::deserialize_design_metadata(
          stale_header, "stale-format-design", stale_format_diagnostics));
      assert(has_design_identity_diagnostic(
          stale_format_diagnostics,
          "format " + std::to_string(stale_format) + " and runtime ABI 1"));
  }

  auto future_header = encoded.substr(0, 16U);
  store_u32(
      future_header, 8U, fsim::artifact::kDesignFormatVersion + 1U);
  fsim::diagnostic::Engine future_header_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      future_header, "future-format-design", future_header_diagnostics));
  assert(has_design_identity_diagnostic(
      future_header_diagnostics, "format 13 and runtime ABI 1"));

  auto corrupt_magic = encoded;
  corrupt_magic[0] = 'X';
  fsim::diagnostic::Engine corrupt_magic_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      corrupt_magic, "corrupt-magic", corrupt_magic_diagnostics));
  assert(corrupt_magic_diagnostics.has_error());

  fsim::diagnostic::Engine truncated_header_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      encoded.substr(0, 12), "truncated-header",
      truncated_header_diagnostics));
  assert(truncated_header_diagnostics.has_error());

  auto unsupported_format = encoded;
  store_u32(unsupported_format, 8U, 3U);
  fsim::diagnostic::Engine unsupported_format_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      unsupported_format, "unsupported-format-3",
      unsupported_format_diagnostics));
  assert(unsupported_format_diagnostics.has_error());

  auto incompatible_runtime = encoded;
  store_u32(incompatible_runtime, 12U, fsim::runtime_abi_version + 1U);
  fsim::diagnostic::Engine incompatible_runtime_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      incompatible_runtime, "incompatible-runtime-abi",
      incompatible_runtime_diagnostics));
  assert(has_design_identity_diagnostic(
      incompatible_runtime_diagnostics, "format 12 and runtime ABI 2"));

  auto oversized_root = encoded;
  store_u64(oversized_root, 16U, std::numeric_limits<std::uint64_t>::max());
  fsim::diagnostic::Engine oversized_root_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      oversized_root, "oversized-root", oversized_root_diagnostics));
  assert(oversized_root_diagnostics.has_error());

  auto truncated = encoded;
  truncated.pop_back();
  fsim::diagnostic::Engine truncated_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(truncated, "truncated",
                                                      truncated_diagnostics));
  auto trailing = encoded;
  trailing.push_back('\0');
  fsim::diagnostic::Engine trailing_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(trailing, "trailing",
                                                      trailing_diagnostics));
  auto inconsistent = metadata;
  inconsistent.seed++;
  fsim::diagnostic::Engine inconsistent_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(inconsistent), "inconsistent",
      inconsistent_diagnostics));

  const auto directory =
      std::filesystem::temp_directory_path() /
      ("fsim-design-artifact-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto rejected_plugin_directory = std::filesystem::path { directory.string() + "-rejected-plugin" };
  fsim::diagnostic::Engine rejected_plugin_diagnostics;
  assert(!fsim::artifact::publish_design(rejected_plugin_directory,
      missing_scv_identity, { },
      rejected_plugin_diagnostics));
  assert(rejected_plugin_diagnostics.has_error());
  assert(!std::filesystem::exists(rejected_plugin_directory));
  const std::vector<fsim::library::PortablePayload> payloads{
      {metadata.payloads[0].artifact, state_bytes},
      {metadata.payloads[1].artifact, state_bytes},
      {metadata.payloads[2].artifact, state_bytes},
      {metadata.payloads[3].artifact, crossing_bytes},
      {metadata.payloads[4].artifact, tlm_bytes},
      {metadata.payloads[5].artifact, tlm2_bytes},
      {metadata.payloads[6].artifact, channel_inventory_bytes},
      {metadata.payloads[7].artifact, binding_inventory_bytes},
      {metadata.payloads[8].artifact, systemc_observation_bytes},
      {metadata.payloads[9].artifact, fst_output.str()}};
  auto stale_publication_metadata = metadata;
  stale_publication_metadata.format = fsim::artifact::kDesignFormatVersion - 1U;
  const auto stale_publication_directory = std::filesystem::path { directory.string() + "-stale-publication" };
  fsim::diagnostic::Engine stale_publication_diagnostics;
  assert(!fsim::artifact::publish_design(
      stale_publication_directory, stale_publication_metadata, payloads,
      stale_publication_diagnostics));
  assert(has_design_identity_diagnostic(
      stale_publication_diagnostics, "format 11 and runtime ABI 1"));
  assert(!std::filesystem::exists(stale_publication_directory));
  fsim::diagnostic::Engine publish_diagnostics;
  assert(fsim::artifact::publish_design(directory, metadata, payloads,
                                        publish_diagnostics));
  assert(!publish_diagnostics.has_error());
  fsim::diagnostic::Engine load_diagnostics;
  assert(fsim::artifact::load_design_metadata(directory, load_diagnostics) ==
         metadata);
  assert(!load_diagnostics.has_error());
  assert((std::filesystem::status(directory).permissions() &
          std::filesystem::perms::owner_write) == std::filesystem::perms::none);
#if !defined(_WIN32)
  // POSIX mode bits deny publication-tree mutation. Windows maps those bits
  // to file attributes, not directory ACLs, so creation remains permitted.
  std::ofstream denied(directory / "write-attempt", std::ios::binary);
  assert(!denied);
#endif

  fsim::diagnostic::Engine overwrite_diagnostics;
  assert(!fsim::artifact::publish_design(directory, metadata, payloads,
                                         overwrite_diagnostics));
  fsim::diagnostic::Engine rollback_diagnostics;
  assert(fsim::artifact::load_design_metadata(
             directory, rollback_diagnostics) == metadata);
  {
    std::ifstream input(directory / metadata.payloads.front().artifact,
                        std::ios::binary);
    assert((std::string{std::istreambuf_iterator<char>{input},
                        std::istreambuf_iterator<char>{}} == state_bytes));
  }
  {
    std::ifstream input(directory / metadata.payloads[3].artifact,
                        std::ios::binary);
    const std::string persisted{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    fsim::diagnostic::Engine persisted_diagnostics;
    assert(fsim::systemc::deserialize_systemc_kernel_value(
               std::as_bytes(std::span{persisted}), {},
               persisted_diagnostics) == crossing_value);
  }
  {
    std::ifstream input(directory / metadata.payloads[4].artifact,
                        std::ios::binary);
    const std::string persisted{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    fsim::diagnostic::Engine persisted_diagnostics;
    assert(fsim::systemc::deserialize_systemc_kernel_tlm1_transaction(
               std::as_bytes(std::span{persisted}), {},
               persisted_diagnostics) == crossing_transaction);
  }
  {
    std::ifstream input(directory / metadata.payloads[5].artifact,
                        std::ios::binary);
    const std::string persisted{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    fsim::diagnostic::Engine persisted_diagnostics;
    assert(fsim::systemc::deserialize_systemc_kernel_tlm2_transaction(
               std::as_bytes(std::span{persisted}), {},
               persisted_diagnostics) == tlm2_transaction);
  }
  {
    std::ifstream input(directory / metadata.payloads[6].artifact,
                        std::ios::binary);
    const std::string persisted{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    fsim::diagnostic::Engine persisted_diagnostics;
    assert(fsim::systemc::deserialize_systemc_kernel_channel_inventory(
               std::as_bytes(std::span{persisted}), {},
               persisted_diagnostics) == channel_inventory.snapshot());
  }
  {
    std::ifstream input(directory / metadata.payloads[7].artifact,
                        std::ios::binary);
    const std::string persisted{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    fsim::diagnostic::Engine persisted_diagnostics;
    assert(fsim::systemc::deserialize_systemc_kernel_binding_inventory(
               std::as_bytes(std::span{persisted}), {},
               persisted_diagnostics) == binding_inventory.snapshot());
  }
  {
    std::ifstream input(directory / metadata.payloads[8].artifact,
                        std::ios::binary);
    const std::string persisted{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    fsim::diagnostic::Engine persisted_diagnostics;
    assert(fsim::systemc::deserialize_systemc_kernel_observation_batch(
               std::as_bytes(std::span{persisted}), {},
               persisted_diagnostics) == systemc_observations);
  }
  {
    std::ifstream input(directory / metadata.payloads[9].artifact,
                        std::ios::binary);
    const std::string persisted{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    const auto trace = fsim::runtime::read_fst(persisted);
    assert(trace.ok());
    assert(trace.trace->values.size() == 3U);
    assert(trace.trace->values[1].payload == trace.trace->values[2].payload);
    assert(std::ranges::any_of(
        trace.trace->declarations, [](const auto& declaration) {
          return declaration.path == "systemc_root.payload_input";
        }));
  }
  auto mismatch = payloads;
  mismatch.front().bytes.push_back('x');
  const auto mismatch_directory =
      directory.parent_path() / (directory.filename().string() + "-mismatch");
  fsim::diagnostic::Engine mismatch_diagnostics;
  assert(!fsim::artifact::publish_design(mismatch_directory, metadata, mismatch,
                                         mismatch_diagnostics));
  assert(!std::filesystem::exists(mismatch_directory));

  const auto generated_directory = directory.parent_path()
      / (directory.filename().string() + "-generated");
  const std::vector<fsim::library::PortablePayload> retained_payloads {
      payloads.begin() + 1, payloads.end()
  };
  const std::vector<fsim::artifact::GeneratedDesignPayload>
      generated_payloads { {
          metadata.payloads.front().artifact,
          metadata.payloads.front().checksum,
          [&](const std::filesystem::path& path,
              fsim::diagnostic::Engine&) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(state_bytes.data(),
                static_cast<std::streamsize>(state_bytes.size()));
            return static_cast<bool>(output);
          } } };
  fsim::diagnostic::Engine generated_diagnostics;
  assert(fsim::artifact::publish_design(generated_directory, metadata,
      retained_payloads, generated_payloads, generated_diagnostics));
  assert(!generated_diagnostics.has_error());
  {
    std::ifstream input(
        generated_directory / metadata.payloads.front().artifact,
        std::ios::binary);
    assert((std::string { std::istreambuf_iterator<char> { input },
                std::istreambuf_iterator<char> { } }
        == state_bytes));
  }
  make_tree_writable(generated_directory);
  std::error_code generated_cleanup_error;
  std::filesystem::remove_all(
      generated_directory, generated_cleanup_error);
  assert(!generated_cleanup_error);

  make_tree_writable(directory);
  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  assert(!cleanup_error);
  return 0;
}
