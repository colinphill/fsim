// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/parser.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/library/portable_unit.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <string>

namespace {

std::filesystem::path workspace()
{
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("fsim-library-artifact-test-" + std::to_string(nonce));
    std::filesystem::create_directories(path);
    return path;
}

fsim::library::Metadata example_metadata()
{
    fsim::library::Metadata metadata;
    metadata.library = "vendor";
    metadata.producer = "fsim 0.2.0-dev";
    metadata.runtime_schema = 3;
    metadata.standards = {
        { "systemverilog", "2017" }, { "vhdl", "2008" }
    };
    metadata.dependencies = { "ieee_models", "common" };
    metadata.sources = {
        { "sources/00000000/stage.sv", "sources/00000000/stage.sv",
            std::string(64, 'c'), "systemverilog" }
    };
    metadata.units = {
        { "systemverilog", "module", "stage", { }, { },
            "units/00000000.fsimir", std::string(64, 'a') },
        { "vhdl", "architecture", "rtl", "counter", "rtl",
            "units/00000001.fsimir", std::string(64, 'b') }
    };
    metadata.native_artifacts = { { "llvm_object", "native/llvm/fixture.fobj", std::string(64, 'd'),
        1, 0, { }, "22.1.0", "x86_64-test", "e-m:e-p:64:64", "generic",
        "+sse2", "O2", std::string(64, 'e') } };
    return metadata;
}

} // namespace

int main()
{
    static_assert(fsim::library::kOwningUnitSchemaVersion == 18);
    static_assert(fsim::library::kPortableSchemaVersion == 9);
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
    const std::vector<fsim::library::PortablePayload> payloads {
        { "units/00000000.fsimir", "first portable unit" },
        { "units/00000001.fsimir", "second portable unit" },
        { "sources/00000000/stage.sv", "module stage; endmodule\n" },
        { "native/llvm/fixture.fobj", "native object fixture" }
    };
    for (std::size_t index = 0; index < published_metadata.units.size(); ++index) {
        published_metadata.units[index].checksum = fsim::support::Sha256::hex(
            fsim::support::Sha256::digest(payloads[index].bytes));
    }
    published_metadata.sources.front().checksum = fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(payloads[2].bytes));
    published_metadata.native_artifacts.front().checksum = fsim::support::Sha256::hex(
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
        == std::optional { published_metadata });
    fsim::diagnostic::Engine overwrite_diagnostics;
    assert(!fsim::library::publish(
        published, published_metadata, payloads, overwrite_diagnostics));
    fsim::diagnostic::Engine rollback_diagnostics;
    assert(fsim::library::load_metadata(
               published, "vendor", rollback_diagnostics)
        == std::optional { published_metadata });
    {
        std::ifstream input(published / payloads.front().path, std::ios::binary);
        assert((std::string {
                    std::istreambuf_iterator<char> { input },
                    std::istreambuf_iterator<char> { } }
            == payloads.front().bytes));
    }

    auto bad_payloads = payloads;
    bad_payloads.front().bytes = "corrupt";
    const auto rejected = directory.parent_path() / "rejected.fsimlib";
    fsim::diagnostic::Engine rejected_diagnostics;
    assert(!fsim::library::publish(
        rejected, published_metadata, bad_payloads, rejected_diagnostics));
    assert(!std::filesystem::exists(rejected));

    const auto parsed_source = fsim::frontend::parse_text(
        "sources/stage.sv",
        R"sv(interface unit_if #(parameter int WIDTH = 4);
  logic clock;
  logic [WIDTH-1:0] value;
  clocking cb @(posedge clock);
    input #0 value;
  endclocking
  modport view(input value, clocking cb);
endinterface

module stage #(parameter int WIDTH = 4) (
  input logic clock,
  input logic [WIDTH-1:0] value,
  output logic [WIDTH-1:0] result
);
  typedef struct packed { logic flag; logic [2:0] payload; } packet_t;
  typedef enum logic [136:0] {
    WIDE_ZERO = 137'b0,
    WIDE_MARK = {1'b1, 62'b0, 1'b1, 69'b0, 4'b1000}
  } wide_enum_t;
  typedef struct packed {
    logic [72:0] high = {1'b1, 71'b0, 1'b1};
    logic [63:0] low = 64'h8;
  } initialized_wide_t;
  typedef union tagged packed {
    logic [135:0] wide;
    logic [7:0] narrow;
  } tagged_wide_t;
  packet_t packet;
  wire (weak0, strong1) strength_driver;
  wire switch_left, switch_right;
  trireg (large) #2 retained;
  reg notifier;
  real real_value;
  shortreal short_value;
  realtime realtime_value;
  time tick_value;
  chandle handle_value;
  virtual unit_if #(.WIDTH(4)).view interface_view;
  covergroup portable_coverage with function sample(input logic value);
    option.goal = 80;
    point: coverpoint value {
      bins zero = {0};
      bins one = {1};
    }
  endgroup : portable_coverage
  specify
    (value[0] => result[0]) = (1:2:3);
    $setup(posedge value[0], posedge clock, 2, notifier);
  endspecify
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
  initial begin
    real_value = 1.25;
    short_value = -2.5;
    realtime_value = 3.75;
    tick_value = 64'd9007199254740993;
    handle_value = null;
  end
endmodule
)sv",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_source.ok());
    assert(parsed_source.design.units.size() == 2);
    const auto stage_unit = std::ranges::find(
        parsed_source.design.units,
        std::string { "stage" },
        &fsim::frontend::DesignUnit::name);
    const auto interface_unit = std::ranges::find(
        parsed_source.design.units,
        std::string { "unit_if" },
        &fsim::frontend::DesignUnit::name);
    assert(stage_unit != parsed_source.design.units.end());
    assert(interface_unit != parsed_source.design.units.end());
    auto source_unit = *stage_unit;
    source_unit.library = "vendor";
    source_unit.compilation_unit_identity = "fixture-compilation-unit";
    fsim::frontend::Type portable_const_type;
    portable_const_type.domain = fsim::frontend::ValueDomain::Bit2;
    portable_const_type.spelling = "bit";
    portable_const_type.packed_range = fsim::frontend::PackedRange { 31, 0, true };
    portable_const_type.systemverilog_packed_dimensions = {
        { fsim::frontend::Expression {
              fsim::frontend::ExpressionKind::IntegerLiteral,
              "7", { }, { } },
            fsim::frontend::Expression {
                fsim::frontend::ExpressionKind::IntegerLiteral,
                "0", { }, { } },
            { }, std::nullopt },
        { fsim::frontend::Expression {
              fsim::frontend::ExpressionKind::IntegerLiteral,
              "3", { }, { } },
            fsim::frontend::Expression {
                fsim::frontend::ExpressionKind::IntegerLiteral,
                "0", { }, { } },
            { }, std::nullopt }
    };
    fsim::frontend::VariableDeclaration portable_const {
        "portable_const",
        std::move(portable_const_type),
        fsim::frontend::Expression {
            fsim::frontend::ExpressionKind::IntegerLiteral,
            "0", { }, { } },
        { }
    };
    portable_const.systemverilog_const = true;
    source_unit.variables.push_back(std::move(portable_const));
    const auto portable_wide_digits = "1" + std::string(127, '0') + "x"
        + std::string(127, '0') + "z";
    const auto portable_wide_literal
        = "257'sb" + portable_wide_digits;
    fsim::frontend::Type portable_wide_type;
    portable_wide_type.domain = fsim::frontend::ValueDomain::Logic4;
    portable_wide_type.spelling = "logic";
    portable_wide_type.packed_range
        = fsim::frontend::PackedRange { 256, 0, true };
    portable_wide_type.is_signed = true;
    fsim::frontend::VariableDeclaration portable_wide_const {
        "portable_wide_const",
        std::move(portable_wide_type),
        fsim::frontend::Expression {
            fsim::frontend::ExpressionKind::LogicLiteral,
            portable_wide_literal, { }, { } },
        { }
    };
    portable_wide_const.systemverilog_const = true;
    source_unit.variables.push_back(std::move(portable_wide_const));
    source_unit.parameters.front().vhdl_deferred = true;
    source_unit.parameters.front().vhdl_completion_span = source_unit.parameters.front().span;
    fsim::frontend::VerilogDefparamDeclaration portable_defparam;
    portable_defparam.path = {
        { "lanes",
            { fsim::frontend::Expression {
                fsim::frontend::ExpressionKind::IntegerLiteral,
                "1", { }, { } } },
            { } },
        { "VALUE", { }, { } }
    };
    portable_defparam.value = fsim::frontend::Expression {
        fsim::frontend::ExpressionKind::LogicLiteral,
        portable_wide_literal, { }, { }
    };
    source_unit.verilog_defparams.push_back(
        std::move(portable_defparam));
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
    assert(restored_unit->compilation_unit_identity
        == "fixture-compilation-unit");
    assert(restored_unit->name == "stage");
    assert(restored_unit->parameters.size() == 3);
    assert(restored_unit->parameters.front().vhdl_deferred);
    assert(restored_unit->parameters.front().vhdl_completion_span);
    assert(restored_unit->functions.size() == 1);
    assert(restored_unit->systemverilog_covergroups.size() == 1);
    assert(
        restored_unit->verilog_defparams.size() == 1U
        && restored_unit->verilog_defparams.front().path.size() == 2U
        && restored_unit->verilog_defparams.front()
                .path.front()
                .indices.size()
            == 1U
        && restored_unit->verilog_defparams.front().value.text
            == portable_wide_literal);
    const auto restored_const = std::ranges::find(
        restored_unit->variables,
        std::string { "portable_const" },
        &fsim::frontend::VariableDeclaration::name);
    assert(
        restored_const != restored_unit->variables.end()
        && restored_const->systemverilog_const
        && restored_const->type.systemverilog_packed_dimensions.size()
            == 2
        && restored_const->type.width() == 32);
    const auto restored_wide_const = std::ranges::find(
        restored_unit->variables,
        std::string { "portable_wide_const" },
        &fsim::frontend::VariableDeclaration::name);
    assert(
        restored_wide_const != restored_unit->variables.end()
        && restored_wide_const->systemverilog_const
        && restored_wide_const->type.domain
            == fsim::frontend::ValueDomain::Logic4
        && restored_wide_const->type.is_signed
        && restored_wide_const->type.width() == 257
        && restored_wide_const->initializer
        && restored_wide_const->initializer->text == portable_wide_literal);
    const auto& restored_coverage = restored_unit->systemverilog_covergroups.front();
    assert(restored_coverage.name == "portable_coverage");
    assert(restored_coverage.effective_instance_goal == 80);
    assert(restored_coverage.coverage_declarations.size() == 1);
    assert(restored_coverage.coverage_declarations.front().bins.size() == 2);

    const auto parsed_vhdl_psl = fsim::frontend::parse_text(
        "sources/portable_psl.vhd",
        R"vhdl(library ieee;
use ieee.std_logic_1164.all;
entity portable_psl is
  port (clk, request, acknowledge : in std_logic);
end entity;
architecture rtl of portable_psl is
  -- psl default clock is rising_edge(clk);
  -- psl sequence response(delay : natural := 1) is
  -- psl   {request = '1'; acknowledge = '1'};
  -- psl property completes is response(1);
begin
  -- psl CHECK_RESPONSE: assert completes;
end architecture;
)vhdl",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed_vhdl_psl.ok());
    const auto psl_architecture = std::ranges::find(
        parsed_vhdl_psl.design.units,
        fsim::frontend::UnitKind::VhdlArchitecture,
        &fsim::frontend::DesignUnit::kind);
    assert(psl_architecture != parsed_vhdl_psl.design.units.end());
    fsim::diagnostic::Engine psl_write_diagnostics;
    const auto psl_bytes = fsim::library::serialize_portable_unit(
        *psl_architecture, psl_write_diagnostics);
    assert(psl_bytes && !psl_write_diagnostics.has_error());
    fsim::diagnostic::Engine psl_read_diagnostics;
    const auto restored_psl = fsim::library::deserialize_portable_unit(
        *psl_bytes, "units/portable-psl.fsimir", psl_read_diagnostics);
    assert(restored_psl && !psl_read_diagnostics.has_error());
    assert(restored_psl->vhdl_psl_declarations.size() == 3U);
    assert(restored_psl->vhdl_psl_declarations[1].name == "response");
    assert(restored_psl->vhdl_psl_declarations[1].formals.size() == 1U);
    assert(restored_psl->vhdl_psl_declarations[1].formals.front().name
        == "delay");
    assert(restored_psl->vhdl_psl_directives.size() == 1U);
    assert(restored_psl->vhdl_psl_directives.front().label
        == "check_response");
    assert(restored_psl->vhdl_psl_directives.front().property_tokens.size()
        == 1U);
    assert(restored_psl->vhdl_psl_directives.front().property_tokens.front().text
        == "completes");

    auto portable_interface = *interface_unit;
    portable_interface.library = "vendor";
    portable_interface.compilation_unit_identity = "fixture-compilation-unit";
    fsim::diagnostic::Engine interface_write_diagnostics;
    const auto interface_bytes = fsim::library::serialize_portable_unit(
        portable_interface, interface_write_diagnostics);
    assert(interface_bytes && !interface_write_diagnostics.has_error());
    fsim::diagnostic::Engine interface_read_diagnostics;
    const auto restored_interface_unit = fsim::library::deserialize_portable_unit(
        *interface_bytes,
        "units/unit-if.fsimir",
        interface_read_diagnostics);
    assert(restored_interface_unit && !interface_read_diagnostics.has_error());
    assert(
        restored_interface_unit->systemverilog_modports.size() == 1
        && restored_interface_unit->systemverilog_modports.front()
                .members.back()
                .kind
            == fsim::frontend::SystemVerilogModportMemberKind::Clocking);
    auto invalid_modport_unit = *restored_interface_unit;
    invalid_modport_unit.systemverilog_modports.front().members.back().kind = static_cast<fsim::frontend::SystemVerilogModportMemberKind>(255);
    fsim::diagnostic::Engine invalid_modport_diagnostics;
    assert(!fsim::library::serialize_portable_unit(
        invalid_modport_unit, invalid_modport_diagnostics));
    const auto type_alias = [&](const std::string_view name)
        -> const fsim::frontend::Type* {
        const auto alias = std::ranges::find_if(
            restored_unit->type_aliases,
            [&](const auto& candidate) { return candidate.name == name; });
        return alias == restored_unit->type_aliases.end()
            ? nullptr
            : &alias->type;
    };
    const auto wide_enum = type_alias("wide_enum_t");
    const auto initialized_wide = type_alias("initialized_wide_t");
    const auto tagged_wide = type_alias("tagged_wide_t");
    assert(
        wide_enum && wide_enum->width() == 137
        && wide_enum->enumeration_literals.size() == 2
        && wide_enum->systemverilog_enumeration_values.size() == 2
        && wide_enum->systemverilog_enumeration_values[1].kind
            == fsim::frontend::ExpressionKind::Concatenation);
    assert(
        initialized_wide && initialized_wide->width() == 137
        && initialized_wide->packed_members.size() == 2
        && initialized_wide->packed_members[0].initializer
        && initialized_wide->packed_members[1].initializer);
    assert(
        tagged_wide && tagged_wide->width() == 137
        && tagged_wide->packed_aggregate
            == fsim::frontend::PackedAggregateKind::TaggedUnion);
    auto malformed_type_unit = *restored_unit;
    const auto malformed_alias = std::ranges::find_if(
        malformed_type_unit.type_aliases, [](const auto& alias) {
            return alias.name == "tagged_wide_t";
        });
    assert(malformed_alias != malformed_type_unit.type_aliases.end());
    malformed_alias->type.packed_aggregate = static_cast<fsim::frontend::PackedAggregateKind>(255);
    fsim::diagnostic::Engine malformed_type_diagnostics;
    assert(!fsim::library::serialize_portable_unit(
        malformed_type_unit, malformed_type_diagnostics));
    assert(std::ranges::any_of(
        malformed_type_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-LIB-0006"
                && diagnostic.message.find("invalid scalar enumeration")
                != std::string::npos;
        }));
    const auto scalar_type = [&](const std::string_view name)
        -> const fsim::frontend::Type* {
        const auto variable = std::ranges::find_if(
            restored_unit->variables,
            [&](const auto& candidate) { return candidate.name == name; });
        if (variable != restored_unit->variables.end())
            return &variable->type;
        const auto signal = std::ranges::find_if(
            restored_unit->signals,
            [&](const auto& candidate) { return candidate.name == name; });
        return signal == restored_unit->signals.end() ? nullptr : &signal->type;
    };
    assert(
        scalar_type("real_value") != nullptr
        && scalar_type("real_value")->systemverilog_scalar
            == fsim::frontend::SystemVerilogScalarKind::Real);
    assert(
        scalar_type("short_value") != nullptr
        && scalar_type("short_value")->systemverilog_scalar
            == fsim::frontend::SystemVerilogScalarKind::ShortReal);
    assert(
        scalar_type("realtime_value") != nullptr
        && scalar_type("realtime_value")->systemverilog_scalar
            == fsim::frontend::SystemVerilogScalarKind::Realtime);
    assert(
        scalar_type("tick_value") != nullptr
        && scalar_type("tick_value")->systemverilog_scalar
            == fsim::frontend::SystemVerilogScalarKind::Time);
    assert(
        scalar_type("handle_value") != nullptr
        && scalar_type("handle_value")->systemverilog_scalar
            == fsim::frontend::SystemVerilogScalarKind::Chandle);
    assert(
        scalar_type("interface_view") != nullptr
        && scalar_type("interface_view")->systemverilog_virtual_interface
        && scalar_type("interface_view")->systemverilog_interface_type
            == "unit_if"
        && scalar_type("interface_view")->systemverilog_interface_modport
            == "view"
        && scalar_type("interface_view")
                ->systemverilog_class_parameter_actuals.size()
            == 1
        && scalar_type("interface_view")
                ->systemverilog_class_parameter_actuals.front()
                .name
            == std::optional<std::string> { "WIDTH" }
        && scalar_type("interface_view")
                ->systemverilog_class_parameter_actuals.front()
                .value.text
            == "4");
    const auto scalar_process = std::ranges::find_if(
        restored_unit->processes, [](const auto& process) {
            return std::ranges::any_of(
                process.statements, [](const auto& statement) {
                    return statement.target.text == "short_value";
                });
        });
    assert(scalar_process != restored_unit->processes.end());
    const auto negative_shortreal = std::ranges::find_if(
        scalar_process->statements, [](const auto& statement) {
            return statement.target.text == "short_value";
        });
    assert(
        negative_shortreal != scalar_process->statements.end()
        && negative_shortreal->value.operands.size() == 1
        && negative_shortreal->value.operands.front()
                .systemverilog_scalar_kind
            == fsim::frontend::SystemVerilogScalarKind::Real
        && negative_shortreal->value.operands.front()
            .systemverilog_decimal_literal.has_value()
        && negative_shortreal->value.operands.front()
                .systemverilog_decimal_literal->digits
            == "25"
        && negative_shortreal->value.operands.front()
                .systemverilog_decimal_literal->decimal_exponent
            == -1);
    assert(restored_unit->verilog_specify_blocks.size() == 1);
    assert(restored_unit->verilog_specify_blocks.front().module_paths.size()
        == 1);
    assert(restored_unit->verilog_specify_blocks.front().timing_checks.size()
        == 1);
    assert(restored_unit->verilog_specify_blocks.front().module_paths.front().delays.front().minimum.has_value());
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
               *restored_unit, repeat_diagnostics)
        == unit_bytes);
    auto invalid_scalar_unit = *restored_unit;
    fsim::frontend::Type* invalid_scalar_type = nullptr;
    const auto invalid_scalar_variable = std::ranges::find_if(
        invalid_scalar_unit.variables, [](const auto& variable) {
            return variable.name == "real_value";
        });
    if (invalid_scalar_variable != invalid_scalar_unit.variables.end()) {
        invalid_scalar_type = &invalid_scalar_variable->type;
    } else {
        const auto invalid_scalar_signal = std::ranges::find_if(
            invalid_scalar_unit.signals, [](const auto& signal) {
                return signal.name == "real_value";
            });
        assert(invalid_scalar_signal != invalid_scalar_unit.signals.end());
        invalid_scalar_type = &invalid_scalar_signal->type;
    }
    invalid_scalar_type->systemverilog_scalar = static_cast<fsim::frontend::SystemVerilogScalarKind>(255);
    fsim::diagnostic::Engine invalid_scalar_diagnostics;
    assert(!fsim::library::serialize_portable_unit(
        invalid_scalar_unit, invalid_scalar_diagnostics));
    assert(std::ranges::any_of(
        invalid_scalar_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-LIB-0006"
                && diagnostic.message.find("invalid scalar enumeration")
                != std::string::npos;
        }));
    auto invalid_specify_unit = *restored_unit;
    invalid_specify_unit.verilog_specify_blocks.front()
        .module_paths.front()
        .kind = static_cast<fsim::frontend::VerilogModulePathKind>(255);
    fsim::diagnostic::Engine invalid_specify_diagnostics;
    assert(!fsim::library::serialize_portable_unit(
        invalid_specify_unit, invalid_specify_diagnostics));
    auto invalid_strength_unit = *restored_unit;
    invalid_strength_unit.signals.front().drive_strength = fsim::frontend::VerilogDriveStrength {
        static_cast<fsim::frontend::VerilogStrength>(255),
        fsim::frontend::VerilogStrength::Strong, { }
    };
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
    switch_statement->verilog_switch_source = { };
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
               *restored_udp, udp_repeat_diagnostics)
        == udp_bytes);
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
    const auto producer_source_name = fsim::support::path_to_utf8(producer_source);
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
        std::vector<fsim::library::SourceNameMapping> {
            { producer_source_name, "sources/00000000/stage.sv" } },
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
        std::vector<fsim::library::SourceNameMapping> {
            { producer_source_name, "sources/00000000/stage.sv" } },
        identity_relocation_diagnostics));
    assert(source_unit.span.source_name == "sources/00000000/stage.sv");

    const auto unmapped_source_name = fsim::support::path_to_utf8(
        std::filesystem::temp_directory_path() / "unmapped" / "include.svh");
    source_unit.span.physical_source_name = unmapped_source_name;
    fsim::diagnostic::Engine missing_relocation_diagnostics;
    assert(!fsim::library::relocate_unit_sources(
        source_unit, { }, missing_relocation_diagnostics));
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
    future_unit[8] = static_cast<char>(
        fsim::library::kOwningUnitSchemaVersion + 1U);
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
        safe_path, std::string { "units/00000000.fsimir" }.size(),
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
    duplicate_path_metadata.sources.front().artifact = duplicate_path_metadata.units.front().artifact;
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
        std::string { "format = 1" }.size(), "format = 99");
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
