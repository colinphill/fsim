// SPDX-License-Identifier: Apache-2.0
#include "fsim/library/artifact.hpp"
#include "fsim/library/portable_unit.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>

namespace {

std::filesystem::path workspace() {
  const auto nonce =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  const auto path = std::filesystem::temp_directory_path()
      / ("fsim-library-artifact-test-" + std::to_string(nonce));
  std::filesystem::create_directories(path);
  return path;
}

fsim::library::Metadata example_metadata() {
  fsim::library::Metadata metadata;
  metadata.library = "vendor";
  metadata.producer = "fsim 0.2.0-dev";
  metadata.runtime_schema = 3;
  metadata.standards = {
      {"systemverilog", "2017"}, {"vhdl", "2008"}};
  metadata.dependencies = {"ieee_models", "common"};
  metadata.sources = {
      {"sources/00000000/stage.sv", "sources/00000000/stage.sv",
       std::string(64, 'c'), "systemverilog"}};
  metadata.units = {
      {"systemverilog", "module", "stage", {}, {},
       "units/00000000.fsimir", std::string(64, 'a')},
      {"vhdl", "architecture", "rtl", "counter", "rtl",
       "units/00000001.fsimir", std::string(64, 'b')}};
  metadata.native_artifacts = {{
      "llvm_object", "native/llvm/fixture.fobj", std::string(64, 'd'),
      1, 0, {}, "22.1.0", "x86_64-test", "e-m:e-p:64:64", "generic",
      "+sse2", "O2", std::string(64, 'e')}};
  return metadata;
}

}  // namespace

int main() {
  const auto expected = example_metadata();
  const auto serialized = fsim::library::serialize_metadata(expected);
  assert(serialized.starts_with(
      "format = 1\nlibrary = \"vendor\"\nproducer = \"fsim 0.2.0-dev\"\n"));
  assert(serialized.find("[[dependency]]") != std::string::npos);
  assert(serialized.find("artifact = \"units/00000001.fsimir\"")
      != std::string::npos);
  assert(serialized.find("[[native]]") != std::string::npos);

  fsim::diagnostic::Engine parse_diagnostics;
  const auto parsed = fsim::library::parse_metadata(
      serialized, "fsim-library.toml", parse_diagnostics);
  assert(parsed.has_value());
  assert(!parse_diagnostics.has_error());
  assert(*parsed == expected);
  assert(fsim::library::serialize_metadata(*parsed) == serialized);

  const auto directory = workspace() / "vendor.fsimlib";
  std::filesystem::create_directories(directory);
  {
    std::ofstream output(
        directory / fsim::library::kMetadataFilename, std::ios::binary);
    output << serialized;
    assert(output.good());
  }
  fsim::diagnostic::Engine load_diagnostics;
  const auto loaded = fsim::library::load_metadata(
      directory, "vendor", load_diagnostics);
  assert(loaded == parsed);
  assert(!load_diagnostics.has_error());
  // Metadata loading is deliberately lazy: neither indexed payload exists.
  assert(!std::filesystem::exists(directory / "units"));

  const auto published = directory.parent_path() / "published.fsimlib";
  auto published_metadata = expected;
  const std::vector<fsim::library::PortablePayload> payloads{
      {"units/00000000.fsimir", "first portable unit"},
      {"units/00000001.fsimir", "second portable unit"},
      {"sources/00000000/stage.sv", "module stage; endmodule\n"},
      {"native/llvm/fixture.fobj", "native object fixture"}};
  for (std::size_t index = 0; index < published_metadata.units.size(); ++index) {
    published_metadata.units[index].checksum = fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(payloads[index].bytes));
  }
  published_metadata.sources.front().checksum = fsim::support::Sha256::hex(
      fsim::support::Sha256::digest(payloads[2].bytes));
  published_metadata.native_artifacts.front().checksum =
      fsim::support::Sha256::hex(
          fsim::support::Sha256::digest(payloads[3].bytes));
  fsim::diagnostic::Engine publish_diagnostics;
  assert(fsim::library::publish(
      published, published_metadata, payloads, publish_diagnostics));
  assert(!publish_diagnostics.has_error());
  assert(std::filesystem::is_regular_file(
      published / fsim::library::kMetadataFilename));
  assert(std::filesystem::is_regular_file(
      published / "units" / "00000000.fsimir"));
  fsim::diagnostic::Engine published_load_diagnostics;
  assert(fsim::library::load_metadata(
      published, "vendor", published_load_diagnostics)
      == std::optional{published_metadata});
  fsim::diagnostic::Engine overwrite_diagnostics;
  assert(!fsim::library::publish(
      published, published_metadata, payloads, overwrite_diagnostics));

  auto bad_payloads = payloads;
  bad_payloads.front().bytes = "corrupt";
  const auto rejected = directory.parent_path() / "rejected.fsimlib";
  fsim::diagnostic::Engine rejected_diagnostics;
  assert(!fsim::library::publish(
      rejected, published_metadata, bad_payloads, rejected_diagnostics));
  assert(!std::filesystem::exists(rejected));

  const auto parsed_source = fsim::frontend::parse_text(
      "sources/stage.sv",
      R"sv(module stage #(parameter int WIDTH = 4) (
  input logic [WIDTH-1:0] value,
  output logic [WIDTH-1:0] result
);
  typedef struct packed { logic flag; logic [2:0] payload; } packet_t;
  packet_t packet;
  wire (weak0, strong1) strength_driver;
  wire switch_left, switch_right;
  trireg (large) #2 retained;
  assign (weak0, strong1) strength_driver = value[0];
  tran linked(switch_left, switch_right);
  function automatic logic [WIDTH-1:0] invert(
      input logic [WIDTH-1:0] operand);
    invert = ~operand;
  endfunction
  always_comb begin
    packet = '{default: '0};
    result = invert(value);
  end
endmodule
)sv",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed_source.ok());
  assert(parsed_source.design.units.size() == 1);
  auto source_unit = parsed_source.design.units.front();
  source_unit.library = "vendor";
  fsim::diagnostic::Engine unit_write_diagnostics;
  const auto unit_bytes = fsim::library::serialize_portable_unit(
      source_unit, unit_write_diagnostics);
  assert(unit_bytes.has_value());
  assert(!unit_write_diagnostics.has_error());
  fsim::diagnostic::Engine unit_read_diagnostics;
  const auto restored_unit = fsim::library::deserialize_portable_unit(
      *unit_bytes, "units/stage.fsimir", unit_read_diagnostics);
  assert(restored_unit.has_value());
  assert(!unit_read_diagnostics.has_error());
  assert(restored_unit->library == "vendor");
  assert(restored_unit->name == "stage");
  assert(restored_unit->parameters.size() == 1);
  assert(restored_unit->functions.size() == 1);
  const auto strength_driver = std::ranges::find_if(
      restored_unit->signals, [](const auto& signal) {
        return signal.name == "strength_driver";
      });
  const auto retained = std::ranges::find_if(
      restored_unit->signals, [](const auto& signal) {
        return signal.name == "retained";
      });
  assert(
      strength_driver != restored_unit->signals.end()
      && strength_driver->drive_strength
      && strength_driver->drive_strength->zero
          == fsim::frontend::VerilogStrength::Weak
      && retained != restored_unit->signals.end()
      && retained->charge_strength && retained->charge_decay
      && restored_unit->concurrent_statements.front()
             .verilog_drive_strength
      && std::ranges::any_of(
          restored_unit->concurrent_statements, [](const auto& statement) {
            return statement.verilog_switch_bidirectional
                && statement.verilog_switch_source.valid();
          }));
  fsim::diagnostic::Engine repeat_diagnostics;
  assert(fsim::library::serialize_portable_unit(
      *restored_unit, repeat_diagnostics) == unit_bytes);
  auto invalid_strength_unit = *restored_unit;
  invalid_strength_unit.signals.front().drive_strength =
      fsim::frontend::VerilogDriveStrength{
          static_cast<fsim::frontend::VerilogStrength>(255),
          fsim::frontend::VerilogStrength::Strong, {}};
  fsim::diagnostic::Engine invalid_strength_diagnostics;
  assert(!fsim::library::serialize_portable_unit(
      invalid_strength_unit, invalid_strength_diagnostics));
  auto invalid_switch_unit = *restored_unit;
  const auto switch_statement = std::ranges::find_if(
      invalid_switch_unit.concurrent_statements, [](const auto& statement) {
        return statement.verilog_switch_bidirectional;
      });
  assert(switch_statement
         != invalid_switch_unit.concurrent_statements.end());
  switch_statement->verilog_switch_source = {};
  fsim::diagnostic::Engine invalid_switch_diagnostics;
  assert(!fsim::library::serialize_portable_unit(
      invalid_switch_unit, invalid_switch_diagnostics));

  const auto parsed_udp = fsim::frontend::parse_text(
      "sources/invert.v",
      R"(
primitive invert_udp(q, d);
  output q; input d;
  table
    0 : 1;
    1 : 0;
    x : x;
  endtable
endprimitive
)",
      fsim::frontend::Language::Verilog2005);
  assert(parsed_udp.ok());
  assert(parsed_udp.design.udp_declarations.size() == 1);
  auto udp = parsed_udp.design.udp_declarations.front();
  udp.library = "vendor";
  fsim::diagnostic::Engine udp_write_diagnostics;
  const auto udp_bytes = fsim::library::serialize_portable_udp(
      udp, udp_write_diagnostics);
  assert(udp_bytes.has_value());
  assert(!udp_write_diagnostics.has_error());
  fsim::diagnostic::Engine udp_read_diagnostics;
  const auto restored_udp = fsim::library::deserialize_portable_udp(
      *udp_bytes, "units/invert.fsimudp", udp_read_diagnostics);
  assert(restored_udp.has_value());
  assert(!udp_read_diagnostics.has_error());
  assert(
      restored_udp->library == "vendor"
      && restored_udp->name == "invert_udp"
      && restored_udp->rows.size() == 3);
  fsim::diagnostic::Engine udp_repeat_diagnostics;
  assert(fsim::library::serialize_portable_udp(
      *restored_udp, udp_repeat_diagnostics) == udp_bytes);
  auto trailing_udp = *udp_bytes;
  trailing_udp.push_back('\0');
  fsim::diagnostic::Engine trailing_udp_diagnostics;
  assert(!fsim::library::deserialize_portable_udp(
      trailing_udp, "trailing.fsimudp", trailing_udp_diagnostics));
  auto malformed_udp = udp;
  malformed_udp.rows.front().inputs.clear();
  fsim::diagnostic::Engine malformed_udp_diagnostics;
  assert(!fsim::library::serialize_portable_udp(
      malformed_udp, malformed_udp_diagnostics));
  assert(malformed_udp_diagnostics.has_error());
  auto future_udp = *udp_bytes;
  future_udp[8] = '\2';
  fsim::diagnostic::Engine future_udp_diagnostics;
  assert(!fsim::library::deserialize_portable_udp(
      future_udp, "future.fsimudp", future_udp_diagnostics));

  const auto producer_source = directory.parent_path()
      / "producer" / "private" / "stage.sv";
  std::filesystem::create_directories(producer_source.parent_path());
  {
    std::ofstream source(producer_source, std::ios::binary);
    source << "module stage; endmodule\n";
    assert(source.good());
  }
  const auto producer_source_name =
      fsim::support::path_to_utf8(producer_source);
  source_unit.span.source_name = producer_source_name;
  fsim::diagnostic::Engine absolute_diagnostics;
  assert(!fsim::library::serialize_portable_unit(
      source_unit, absolute_diagnostics));
  assert(std::ranges::any_of(
      absolute_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-LIB-0006"
            && diagnostic.message.find("producer-absolute")
                != std::string::npos;
      }));
  fsim::diagnostic::Engine relocation_diagnostics;
  assert(fsim::library::relocate_unit_sources(
      source_unit,
      std::vector<fsim::library::SourceNameMapping>{
          {producer_source_name, "sources/00000000/stage.sv"}},
      relocation_diagnostics));
  assert(source_unit.span.source_name == "sources/00000000/stage.sv");
  assert(fsim::library::serialize_portable_unit(
      source_unit, relocation_diagnostics));

  const auto producer_alias = producer_source.parent_path() / "stage-alias.sv";
  std::error_code alias_error;
  std::filesystem::create_hard_link(
      producer_source, producer_alias, alias_error);
  assert(!alias_error);
  source_unit.span.source_name = fsim::support::path_to_utf8(producer_alias);
  fsim::diagnostic::Engine identity_relocation_diagnostics;
  assert(fsim::library::relocate_unit_sources(
      source_unit,
      std::vector<fsim::library::SourceNameMapping>{
          {producer_source_name, "sources/00000000/stage.sv"}},
      identity_relocation_diagnostics));
  assert(source_unit.span.source_name == "sources/00000000/stage.sv");

  const auto unmapped_source_name = fsim::support::path_to_utf8(
      std::filesystem::temp_directory_path() / "unmapped" / "include.svh");
  source_unit.span.physical_source_name = unmapped_source_name;
  fsim::diagnostic::Engine missing_relocation_diagnostics;
  assert(!fsim::library::relocate_unit_sources(
      source_unit, {}, missing_relocation_diagnostics));
  assert(std::ranges::any_of(
      missing_relocation_diagnostics.diagnostics(),
      [&](const auto& diagnostic) {
        return diagnostic.message.find(unmapped_source_name)
            != std::string::npos;
      }));

  auto trailing_unit = *unit_bytes;
  trailing_unit.push_back('\0');
  fsim::diagnostic::Engine trailing_diagnostics;
  assert(!fsim::library::deserialize_portable_unit(
      trailing_unit, "trailing.fsimir", trailing_diagnostics));
  auto future_unit = *unit_bytes;
  future_unit[8] = '\4';
  fsim::diagnostic::Engine future_diagnostics;
  assert(!fsim::library::deserialize_portable_unit(
      future_unit, "future.fsimir", future_diagnostics));

  fsim::diagnostic::Engine wrong_name_diagnostics;
  assert(!fsim::library::load_metadata(
      directory, "other", wrong_name_diagnostics));
  assert(std::ranges::any_of(
      wrong_name_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-LIB-0003"
            && diagnostic.message.find("metadata for 'vendor'")
                != std::string::npos;
      }));

  auto invalid_text = serialized;
  const auto safe_path = invalid_text.find("units/00000000.fsimir");
  assert(safe_path != std::string::npos);
  invalid_text.replace(
      safe_path, std::string{"units/00000000.fsimir"}.size(),
      "../escape.fsimir");
  fsim::diagnostic::Engine path_diagnostics;
  assert(!fsim::library::parse_metadata(
      invalid_text, "unsafe.toml", path_diagnostics));
  assert(std::ranges::any_of(
      path_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-LIB-0003"
            && diagnostic.message.find("contained relative")
                != std::string::npos;
      }));

  auto duplicate_path_metadata = expected;
  duplicate_path_metadata.sources.front().artifact =
      duplicate_path_metadata.units.front().artifact;
  fsim::diagnostic::Engine duplicate_path_diagnostics;
  assert(!fsim::library::parse_metadata(
      fsim::library::serialize_metadata(duplicate_path_metadata),
      "duplicate-path.toml", duplicate_path_diagnostics));
  assert(std::ranges::any_of(
      duplicate_path_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.message.find("payload paths must be unique")
            != std::string::npos;
      }));

  auto incompatible_text = serialized;
  incompatible_text.replace(
      incompatible_text.find("format = 1"),
      std::string{"format = 1"}.size(), "format = 99");
  fsim::diagnostic::Engine schema_diagnostics;
  assert(!fsim::library::parse_metadata(
      incompatible_text, "future.toml", schema_diagnostics));
  assert(std::ranges::any_of(
      schema_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-LIB-0002";
      }));

  std::error_code ignored;
  std::filesystem::remove_all(directory.parent_path(), ignored);
  std::cout << "library_artifact_test: all tests passed\n";
  return 0;
}
