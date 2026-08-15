// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "fsim/artifact/design.hpp"
#include "fsim/runtime/fst_change_encoder.hpp"
#include "fsim/runtime/fst_compression.hpp"
#include "fsim/runtime/fst_reader.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/support/sha256.hpp"

namespace {

std::string checksum(const std::string_view bytes) {
  return fsim::support::Sha256::hex(fsim::support::Sha256::digest(bytes));
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
  std::string trace_source(257U, '0');
  constexpr char trace_symbols[] = {'0', '1', 'X', 'Z'};
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
  change_encoder.append(
      {{1U}, 9U, 0U, fsim::runtime::TraceRegion::Active, 2U}, trace_value);
  change_encoder.append(
      {{1U}, 8U, 0U, fsim::runtime::TraceRegion::Snapshot, 1U}, trace_value);
  const auto ordered_changes = std::move(change_encoder).freeze();
  assert(ordered_changes.values().size() == 2U);
  assert(ordered_changes.values().front().event.time == 8U);
  assert(ordered_changes.values().back().event.time == 9U);

  fsim::runtime::TraceDeclarationBuilder trace_builder;
  const auto trace_signal
      = trace_builder.add_variable("primary.payload", trace_source.size());
  const auto trace_model = std::move(trace_builder).freeze();
  std::ostringstream fst_output(std::ios::binary);
  fsim::runtime::FstWriter fst_writer{fst_output};
  fst_writer.declare(trace_model);
  fst_writer.begin(8U);
  fst_writer.set_initial_value(trace_signal, trace_value);
  fst_writer.change({trace_signal, 9U, 0U,
                        fsim::runtime::TraceRegion::Active, 1U},
                    trace_value);
  fst_writer.close(9U);
  const auto decoded_fst = fsim::runtime::read_fst(fst_output.str());
  assert(decoded_fst.ok());
  assert(decoded_fst.trace->values.size() == 2U);
  assert(decoded_fst.trace->values.front().payload == trace_value.symbols());
  assert(decoded_fst.trace->values.back().time == 9U);

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
  state_bytes.append(reinterpret_cast<const char*>(compressed_state.bytes.data()),
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
  fsim::artifact::DesignMetadata metadata;
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
                              {},
                              {checksum("unit")}});
  metadata.objects.push_back(
      {checksum("vhdl-metadata"),
       checksum("vhdl-compilation"),
       "vhdl",
       "1993",
       "fsim-synopsys-ieee-compat-v2",
       "vhdl_work",
       {{"1993", "ieee-1076-standard:1993:fsim-v1", "ieee.std_logic_unsigned",
         "synopsys-legacy-ieee:1990-1992:fsim-synopsys-ieee-compat-v2",
         checksum("synopsys-unsigned-source")}},
       {checksum("vhdl-unit")}});
  metadata.vhdl_unit_provenance.push_back(
      {1,
       "1993",
       "ieee-1076-standard:1993:fsim-v1",
       "fsim-synopsys-ieee-compat-v2",
       {{"1993", "ieee-1076-standard:1993:fsim-v1", "ieee.std_logic_unsigned",
         "synopsys-legacy-ieee:1990-1992:fsim-synopsys-ieee-compat-v2",
         checksum("synopsys-unsigned-source")}}});
  metadata.verilog_unit_provenance.push_back(
      {0, "systemverilog", "2009", "implicit-net,sizing"});
  metadata.payloads = {
      {"runtime", "state/runtime.bin", checksum(state_bytes)},
      {"semantics", "state/semantics.bin", checksum(state_bytes)},
      {"design-ir", "state/design-ir.bin", checksum(state_bytes)}};
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
  auto future_format = metadata;
  ++future_format.format;
  fsim::diagnostic::Engine future_format_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(future_format),
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

  auto format_one = metadata;
  format_one.format = 1;
  format_one.objects.erase(format_one.objects.begin() + 1);
  format_one.objects.front().compatibility_profile = "none";
  format_one.vhdl_unit_provenance.clear();
  format_one.verilog_unit_provenance.clear();
  format_one.trace_archive.clear();
  format_one.unit_count = 1;
  format_one.uvm_release = "none";
  format_one.uvm_source_identity.clear();
  format_one.design_digest = fsim::artifact::compute_design_digest(format_one);
  const auto format_one_encoded =
      fsim::artifact::serialize_design_metadata(format_one);
  fsim::diagnostic::Engine format_one_diagnostics;
  assert(fsim::artifact::deserialize_design_metadata(
             format_one_encoded, "format-one-design", format_one_diagnostics) ==
         format_one);
  assert(!format_one_diagnostics.has_error());

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
  const std::vector<fsim::library::PortablePayload> payloads{
      {metadata.payloads[0].artifact, state_bytes},
      {metadata.payloads[1].artifact, state_bytes},
      {metadata.payloads[2].artifact, state_bytes}};
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
  auto mismatch = payloads;
  mismatch.front().bytes.push_back('x');
  const auto mismatch_directory =
      directory.parent_path() / (directory.filename().string() + "-mismatch");
  fsim::diagnostic::Engine mismatch_diagnostics;
  assert(!fsim::artifact::publish_design(mismatch_directory, metadata, mismatch,
                                         mismatch_diagnostics));
  assert(!std::filesystem::exists(mismatch_directory));

  make_tree_writable(directory);
  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  assert(!cleanup_error);
  return 0;
}
