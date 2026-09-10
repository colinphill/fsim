// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/parser.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/library/portable_unit.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

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
    metadata.vhdl_package_dependencies = {{
        "2008", "ieee-1076-standard:2008:fsim-v3",
        "ieee.std_logic_unsigned",
        "synopsys-legacy-ieee:1990-1992:fsim-synopsys-ieee-compat-v2",
        std::string(64, 'f') }};
    metadata.dependencies = { "ieee_models", "common" };
    metadata.sources = {
        { "sources/00000000/stage.sv", "sources/00000000/stage.sv",
            std::string(64, 'c'), "systemverilog", "2017", "none" }
    };
    metadata.units = {
        { "systemverilog", "module", "stage", { }, { },
            "units/00000000.fsimir", std::string(64, 'a'), "2017", "none" },
        { "vhdl", "architecture", "rtl", "counter", "rtl",
            "units/00000001.fsimir", std::string(64, 'b'), "2008",
            "fsim-synopsys-ieee-compat-v2" }
    };
    metadata.native_artifacts = { { "llvm_object", "native/llvm/fixture.fobj", std::string(64, 'd'),
        1, 0, { }, std::string(64, 'f'), "22.1.0", "x86_64-test",
        "e-m:e-p:64:64", "generic",
        "+sse2", "O2", std::string(64, 'e') } };
    return metadata;
}

bool has_identity_diagnostic(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view family,
    const std::string_view found,
    const std::string_view required,
    const std::string_view artifact)
{
    const auto expected = "unsupported " + std::string { family }
        + " identity: found " + std::string { found } + "; required "
        + std::string { required } + "; regenerate "
        + std::string { artifact } + " with this fsim build";
    return std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
            return diagnostic.message == expected;
        });
}

} // namespace

int main()
{
    static_assert(fsim::library::kFormatVersion == 5);
    static_assert(fsim::library::kOwningUnitSchemaVersion == 31);
    static_assert(fsim::library::kPortableSchemaVersion == 14);
    const auto expected = example_metadata();
    const auto serialized = fsim::library::serialize_metadata(expected);
    assert(serialized.starts_with(
        "format = 5\nlibrary = \"vendor\"\nproducer = \"fsim 0.2.0-dev\"\n"));
    assert(serialized.find("trace_archive = \"\"") != std::string::npos);
    assert(serialized.find("[[dependency]]") != std::string::npos);
    assert(serialized.find("[[vhdl_package_dependency]]")
        != std::string::npos);
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
    auto stale_publication_metadata = published_metadata;
    stale_publication_metadata.format = fsim::library::kFormatVersion - 1U;
    const auto stale_publication = directory.parent_path() / "stale-publication.fsimlib";
    fsim::diagnostic::Engine stale_publication_diagnostics;
    assert(!fsim::library::publish(
        stale_publication, stale_publication_metadata, payloads,
        stale_publication_diagnostics));
    assert(has_identity_diagnostic(
        stale_publication_diagnostics, ".fsimlib publication",
        "format 4 and portable-unit schema 14",
        "format 5 and portable-unit schema 14", ".fsimlib"));
    assert(!std::filesystem::exists(stale_publication));
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

    auto source_hidden_metadata = published_metadata;
    source_hidden_metadata.sources.front().artifact.clear();
    source_hidden_metadata.sources.front().checksum.clear();
    auto source_hidden_payloads = payloads;
    source_hidden_payloads.erase(source_hidden_payloads.begin() + 2);
    const auto source_hidden = directory.parent_path() / "source-hidden.fsimlib";
    fsim::diagnostic::Engine source_hidden_diagnostics;
    assert(fsim::library::publish(
        source_hidden, source_hidden_metadata, source_hidden_payloads,
        source_hidden_diagnostics));
    assert(!source_hidden_diagnostics.has_error());
    assert(!std::filesystem::exists(source_hidden / "sources"));
    assert(std::filesystem::is_regular_file(
        source_hidden / source_hidden_metadata.units.front().artifact));
    assert(std::filesystem::is_regular_file(
        source_hidden / source_hidden_metadata.native_artifacts.front().artifact));
    fsim::diagnostic::Engine source_hidden_load_diagnostics;
    assert(fsim::library::load_metadata(
               source_hidden, "vendor", source_hidden_load_diagnostics)
        == std::optional { source_hidden_metadata });
    assert(!source_hidden_load_diagnostics.has_error());
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
  covergroup portable_coverage with function sample(
      input logic [136:0] sampled);
    option.goal = 80;
    point: coverpoint sampled {
      bins zero = {0} with (item == 0);
      bins one = {1};
      bins exact_x = {137'bx};
      bins exact_z = {137'bz};
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
  function automatic logic [7:0] resolve_byte(
      input logic [7:0] drivers[]);
    return drivers[0];
  endfunction
  nettype logic [7:0] byte_net with resolve_byte;
  logic [7:0] alias_left;
  logic [7:0] alias_right;
  alias alias_left = alias_right;
  let add_mask(logic [7:0] value,
               logic [7:0] mask = 8'h0f) = value | mask;
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
    source_unit.standard_revision
        = fsim::frontend::StandardRevision::SystemVerilog2023;
    source_unit.functions.front().arguments.front().reference = true;
    source_unit.functions.front().arguments.front().const_reference = true;
    source_unit.functions.front().arguments.front().static_reference = true;
    source_unit.functions.front().arguments.front().direction
        = fsim::frontend::PortDirection::Input;
    fsim::frontend::TaskDeclaration portable_task;
    portable_task.name = "portable_ref_task";
    portable_task.automatic = true;
    portable_task.arguments.emplace_back(
        "target",
        source_unit.functions.front().arguments.front().type,
        fsim::frontend::PortDirection::Inout,
        fsim::frontend::SourceSpan { },
        true,
        std::nullopt,
        false,
        true);
    source_unit.tasks.push_back(std::move(portable_task));
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
    assert(restored_unit->standard_revision
        == fsim::frontend::StandardRevision::SystemVerilog2023);
    assert(
        restored_unit->functions.front().arguments.front().reference
        && restored_unit->functions.front().arguments.front().const_reference
        && restored_unit->functions.front().arguments.front().static_reference
        && restored_unit->tasks.back().arguments.front().reference
        && !restored_unit->tasks.back().arguments.front().const_reference
        && restored_unit->tasks.back().arguments.front().static_reference);
    assert(restored_unit->verilog_compatibility_profile == "none");
    assert(restored_unit->name == "stage");
    assert(restored_unit->parameters.size() == 3);
    assert(restored_unit->parameters.front().vhdl_deferred);
    assert(restored_unit->parameters.front().vhdl_completion_span);
    assert(restored_unit->functions.size() == 2);
    assert(std::ranges::any_of(
        restored_unit->functions, [](const auto& function) {
            return function.name == "resolve_byte";
        }));
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

    fsim::frontend::DesignUnit vhdl_2019_unit;
    vhdl_2019_unit.kind = fsim::frontend::UnitKind::VhdlPackage;
    vhdl_2019_unit.language = fsim::frontend::Language::Vhdl2008;
    vhdl_2019_unit.vhdl_standard = fsim::frontend::VhdlStandard::Vhdl2019;
    vhdl_2019_unit.standard_revision
        = fsim::frontend::StandardRevision::Vhdl2019;
    vhdl_2019_unit.library = "work";
    vhdl_2019_unit.compilation_unit_identity = "vhdl-2019-round-trip";
    vhdl_2019_unit.name = "vhdl_2019_round_trip";

    auto vhdl_integer = fsim::frontend::vhdl_predefined_integer_type(
        fsim::frontend::VhdlStandard::Vhdl2019, "integer");
    fsim::frontend::Type index_subtype = vhdl_integer;
    index_subtype.named_type = "matrix_t";
    index_subtype.vhdl_predefined_subtype_attribute
        = fsim::frontend::VhdlPredefinedSubtypeAttribute::Index;
    index_subtype.vhdl_predefined_subtype_attribute_dimension
        = fsim::frontend::Expression {
            fsim::frontend::ExpressionKind::IntegerLiteral, "2", { }, { } };
    vhdl_2019_unit.type_aliases.push_back({
        "matrix_index_t", std::move(index_subtype), { }, { },
        fsim::frontend::TypeDeclarationKind::VhdlSubtype,
        { }, { }, { }, false });

    fsim::frontend::Type unspecified = vhdl_integer;
    unspecified.vhdl_unspecified
        = std::make_shared<fsim::frontend::VhdlUnspecifiedTypeInfo>();
    unspecified.vhdl_unspecified->type_class
        = fsim::frontend::VhdlUnspecifiedTypeClass::Array;
    unspecified.vhdl_unspecified->component_types = { vhdl_integer };
    unspecified.vhdl_unspecified->array_index_count = 1U;
    unspecified.vhdl_unspecified->inference_identity
        = "vhdl-2019-round-trip:generic:T";
    vhdl_2019_unit.type_aliases.push_back({
        "unspecified_t", std::move(unspecified), { }, { },
        fsim::frontend::TypeDeclarationKind::VhdlIncomplete,
        { }, { }, { }, false });

    fsim::frontend::Type access_type;
    access_type.domain = fsim::frontend::ValueDomain::Integer;
    access_type.spelling = "integer_access";
    access_type.vhdl_access = fsim::frontend::VhdlAccessInfo { };
    access_type.vhdl_access->deallocate_releases_storage = false;
    access_type.vhdl_access->reclaim_when_unreachable = true;
    access_type.vhdl_access->simulation_lifetime = false;
    vhdl_2019_unit.type_aliases.push_back({
        "integer_access", std::move(access_type), { }, { },
        fsim::frontend::TypeDeclarationKind::VhdlAccess,
        { }, { }, { }, false });

    fsim::frontend::TypeAliasDeclaration view;
    view.name = "bus_view";
    view.type.named_type = "bus_t";
    view.declaration_kind
        = fsim::frontend::TypeDeclarationKind::VhdlModeView;
    view.vhdl_mode_view_elements.push_back({
        "request", fsim::frontend::VhdlModeViewElementKind::direction,
        fsim::frontend::PortDirection::Input, { }, { } });
    view.vhdl_mode_view_elements.push_back({
        "payload", fsim::frontend::VhdlModeViewElementKind::array_view,
        fsim::frontend::PortDirection::Unknown, "payload_view", { } });
    vhdl_2019_unit.type_aliases.push_back(std::move(view));
    fsim::frontend::TypeAliasDeclaration converse;
    converse.name = "reverse_bus_view";
    converse.declaration_kind
        = fsim::frontend::TypeDeclarationKind::VhdlModeView;
    converse.vhdl_mode_view_converse_of = "bus_view";
    converse.vhdl_mode_view_converse_parity = true;
    vhdl_2019_unit.type_aliases.push_back(std::move(converse));

    fsim::frontend::SignalDeclaration viewed_port;
    viewed_port.name = "bus";
    viewed_port.type.named_type = "bus_t";
    viewed_port.is_port = true;
    viewed_port.vhdl_mode_view = fsim::frontend::VhdlModeViewIndication { };
    viewed_port.vhdl_mode_view->kind
        = fsim::frontend::VhdlModeViewIndicationKind::record;
    viewed_port.vhdl_mode_view->view = "bus_view";
    viewed_port.vhdl_mode_view->explicit_subtype = true;
    viewed_port.vhdl_mode_view->elements.push_back({
        "request", fsim::frontend::PortDirection::Input, { } });
    vhdl_2019_unit.ports.push_back(std::move(viewed_port));

    fsim::frontend::Type protected_type;
    protected_type.spelling = "protected_box";
    protected_type.vhdl_protected
        = std::make_shared<fsim::frontend::VhdlProtectedInfo>();
    fsim::frontend::ParameterDeclaration generic;
    generic.name = "limit";
    generic.type = vhdl_integer;
    protected_type.vhdl_protected->generic_parameters.push_back(
        std::move(generic));
    protected_type.vhdl_protected->variables.push_back({
        "value", vhdl_integer, std::nullopt, { }, false, false,
        std::nullopt, true });
    protected_type.vhdl_protected->method_aliases.push_back({
        "observe", "read_value", { },
        fsim::frontend::PortDirection::Unknown, { } });
    vhdl_2019_unit.type_aliases.push_back({
        "protected_box", std::move(protected_type), { }, { },
        fsim::frontend::TypeDeclarationKind::VhdlProtected,
        { }, { }, { }, false });

    fsim::frontend::FunctionDeclaration constrained_function;
    constrained_function.name = "select_value";
    constrained_function.return_type.named_type = "vector_t";
    constrained_function.vhdl_return_identifier = "result";
    constrained_function.language = fsim::frontend::Language::Vhdl2008;
    fsim::frontend::Statement return_statement;
    return_statement.kind = fsim::frontend::StatementKind::Return;
    return_statement.value.kind
        = fsim::frontend::ExpressionKind::Conditional;
    return_statement.value.operands = {
        { fsim::frontend::ExpressionKind::BooleanLiteral, "true", { }, { } },
        { fsim::frontend::ExpressionKind::Identifier, "left", { }, { } },
        { fsim::frontend::ExpressionKind::Identifier, "right", { }, { } }
    };
    constrained_function.statements.push_back(std::move(return_statement));
    vhdl_2019_unit.functions.push_back(std::move(constrained_function));

    fsim::frontend::Process sequential_process;
    fsim::frontend::Statement sequential_block;
    sequential_block.kind = fsim::frontend::StatementKind::Block;
    sequential_block.label = "nested_region";
    sequential_block.constants.push_back({ });
    sequential_block.constants.back().name = "local_limit";
    sequential_block.constants.back().type = vhdl_integer;
    sequential_block.type_aliases.push_back({
        "local_integer", vhdl_integer, { }, { },
        fsim::frontend::TypeDeclarationKind::VhdlSubtype,
        { }, { }, { }, false });
    sequential_block.declarations.push_back({
        "local_value", vhdl_integer, std::nullopt, { } });
    sequential_process.statements.push_back(std::move(sequential_block));
    vhdl_2019_unit.processes.push_back(std::move(sequential_process));

    fsim::diagnostic::Engine vhdl_2019_write_diagnostics;
    const auto vhdl_2019_bytes = fsim::library::serialize_portable_unit(
        vhdl_2019_unit, vhdl_2019_write_diagnostics);
    assert(vhdl_2019_bytes && !vhdl_2019_write_diagnostics.has_error());
    fsim::diagnostic::Engine vhdl_2019_read_diagnostics;
    const auto restored_vhdl_2019
        = fsim::library::deserialize_portable_unit(
            *vhdl_2019_bytes, "units/vhdl-2019.fsimir",
            vhdl_2019_read_diagnostics);
    assert(restored_vhdl_2019 && !vhdl_2019_read_diagnostics.has_error());
    assert(
        restored_vhdl_2019->standard_revision
            == fsim::frontend::StandardRevision::Vhdl2019
        && restored_vhdl_2019->type_aliases[0]
                .type.vhdl_predefined_subtype_attribute
            == fsim::frontend::VhdlPredefinedSubtypeAttribute::Index
        && restored_vhdl_2019->type_aliases[0]
                .type.vhdl_predefined_subtype_attribute_dimension
        && restored_vhdl_2019->type_aliases[1].type.vhdl_unspecified
        && restored_vhdl_2019->type_aliases[2]
                .type.vhdl_access->reclaim_when_unreachable
        && restored_vhdl_2019->type_aliases[3]
                .vhdl_mode_view_elements.size()
            == 2U
        && restored_vhdl_2019->type_aliases[4]
                .vhdl_mode_view_converse_parity
        && restored_vhdl_2019->ports.front().vhdl_mode_view
        && restored_vhdl_2019->ports.front()
                .vhdl_mode_view->elements.front().path
            == "request"
        && restored_vhdl_2019->type_aliases[5]
                .type.vhdl_protected->generic_parameters.size()
            == 1U
        && restored_vhdl_2019->type_aliases[5]
                .type.vhdl_protected->variables.front().vhdl_private
        && restored_vhdl_2019->functions.front().vhdl_return_identifier
            == "result"
        && restored_vhdl_2019->functions.front()
                .statements.front().value.kind
            == fsim::frontend::ExpressionKind::Conditional
        && restored_vhdl_2019->processes.front()
                .statements.front().constants.front().name
            == "local_limit");
    fsim::diagnostic::Engine vhdl_2019_repeat_diagnostics;
    assert(fsim::library::serialize_portable_unit(
               *restored_vhdl_2019, vhdl_2019_repeat_diagnostics)
        == vhdl_2019_bytes);

    auto invalid_vhdl_2019 = *restored_vhdl_2019;
    invalid_vhdl_2019.ports.front().vhdl_mode_view->kind
        = static_cast<fsim::frontend::VhdlModeViewIndicationKind>(255);
    fsim::diagnostic::Engine invalid_vhdl_2019_diagnostics;
    assert(!fsim::library::serialize_portable_unit(
        invalid_vhdl_2019, invalid_vhdl_2019_diagnostics));
    const auto& restored_coverage = restored_unit->systemverilog_covergroups.front();
    assert(restored_coverage.name == "portable_coverage");
    assert(restored_coverage.effective_instance_goal == 80);
    assert(restored_coverage.coverage_declarations.size() == 1);
    const auto& restored_coverage_bins
        = restored_coverage.coverage_declarations.front().bins;
    assert(restored_coverage_bins.size() == 4);
    assert(
        restored_coverage_bins[0].with_tokens.size() == 3U
        && restored_coverage_bins[0].with_tokens[0].text == "item"
        && restored_coverage_bins[0].with_tokens[1].text == "=="
        && restored_coverage_bins[0].with_tokens[2].text == "0"
        && restored_coverage_bins[2].values.front().exact_bits
            == std::string(137U, '1')
        && restored_coverage_bins[2].values.front().exact_unknown_bits
            == std::string(137U, '1')
        && restored_coverage_bins[3].values.front().exact_bits
            == std::string(137U, '0')
        && restored_coverage_bins[3].values.front().exact_unknown_bits
            == std::string(137U, '1'));

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
    const auto byte_net = std::ranges::find_if(
        restored_unit->type_aliases, [](const auto& alias) {
            return alias.name == "byte_net";
        });
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
    assert(
        byte_net != restored_unit->type_aliases.end()
        && byte_net->declaration_kind
            == fsim::frontend::TypeDeclarationKind::SystemVerilogNettype
        && byte_net->type.width() == 8
        && byte_net->type.systemverilog_resolution_function
            == "resolve_byte"
        && byte_net->systemverilog_resolution_function == "resolve_byte");
    assert(restored_unit->systemverilog_aliases.size() == 1);
    assert(restored_unit->systemverilog_aliases.front().terminals.size() == 2);
    assert(restored_unit->systemverilog_aliases.front().terminals[0].text
        == "alias_left");
    assert(restored_unit->systemverilog_aliases.front().terminals[1].text
        == "alias_right");
    assert(restored_unit->systemverilog_lets.size() == 1);
    assert(restored_unit->systemverilog_lets.front().name == "add_mask");
    assert(restored_unit->systemverilog_lets.front().ports.size() == 2);
    assert(restored_unit->systemverilog_lets.front().ports[0].type);
    assert(restored_unit->systemverilog_lets.front().ports[1].default_value);
    assert(restored_unit->systemverilog_lets.front().expression.text == "|");
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

    const auto hierarchy_source = fsim::frontend::parse_text(
        "sources/portable-hierarchy.sv",
        R"(
extern module portable_monitor #(
  parameter logic [136:0] MAGIC = 137'h1
) (
  input logic [136:0] value
);

bind portable_target portable_monitor #(
  .MAGIC(137'h1_0000_0000_0000_0000_0000_0000_0000_0001)
) portable_bound(.value(value));

config portable_configuration;
  design vendor.portable_top;
  default liblist fast slow;
  instance portable_top.lanes[1].target use fast.portable_target;
  cell portable_target liblist slow;
endconfig : portable_configuration
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(hierarchy_source.ok());
    assert(hierarchy_source.design.units.size() == 3U);
    std::vector<fsim::frontend::DesignUnit> restored_hierarchy_units;
    for (auto unit : hierarchy_source.design.units) {
        unit.library = "vendor";
        fsim::diagnostic::Engine write_diagnostics;
        const auto bytes = fsim::library::serialize_portable_unit(
            unit, write_diagnostics);
        assert(bytes && !write_diagnostics.has_error());
        fsim::diagnostic::Engine read_diagnostics;
        auto restored = fsim::library::deserialize_portable_unit(
            *bytes, "units/portable-hierarchy.fsimir", read_diagnostics);
        assert(restored && !read_diagnostics.has_error());
        fsim::diagnostic::Engine deterministic_diagnostics;
        assert(
            fsim::library::serialize_portable_unit(
                *restored, deterministic_diagnostics)
            == bytes);
        restored_hierarchy_units.push_back(std::move(*restored));
    }
    const auto restored_extern = std::ranges::find_if(
        restored_hierarchy_units,
        [](const auto& unit) { return unit.systemverilog_extern; });
    const auto restored_bind = std::ranges::find(
        restored_hierarchy_units,
        fsim::frontend::UnitKind::SystemVerilogBind,
        &fsim::frontend::DesignUnit::kind);
    const auto restored_configuration = std::ranges::find(
        restored_hierarchy_units,
        fsim::frontend::UnitKind::SystemVerilogConfiguration,
        &fsim::frontend::DesignUnit::kind);
    assert(
        restored_extern != restored_hierarchy_units.end()
        && restored_extern->ports.front().type.width() == 137U);
    assert(
        restored_bind != restored_hierarchy_units.end()
        && restored_bind->systemverilog_binds.size() == 1U
        && restored_bind->systemverilog_binds.front().target
            == "portable_target"
        && restored_bind->systemverilog_binds.front()
                .instances.front()
                .parameter_overrides.front()
                .value.text
            == "137'h1_0000_0000_0000_0000_0000_0000_0000_0001");
    assert((
        restored_configuration != restored_hierarchy_units.end()
        && restored_configuration->systemverilog_configuration
        && restored_configuration->systemverilog_configuration
                ->default_liblist
            == std::vector<std::string> { "fast", "slow" }
        && restored_configuration->systemverilog_configuration->rules.size()
            == 2U
        && restored_configuration->systemverilog_configuration
                ->rules.front()
                .selector
            == "portable_top.lanes[1].target"));
    auto invalid_configuration = *restored_configuration;
    invalid_configuration.systemverilog_configuration->rules.front().kind
        = static_cast<
            fsim::frontend::SystemVerilogConfigurationRuleKind>(255);
    fsim::diagnostic::Engine invalid_configuration_diagnostics;
    assert(!fsim::library::serialize_portable_unit(
        invalid_configuration, invalid_configuration_diagnostics));

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
        && restored_udp->rows.size() == 3
        && restored_udp->standard_revision
            == fsim::frontend::StandardRevision::Verilog2005
        && restored_udp->verilog_compatibility_profile == "none");
    fsim::diagnostic::Engine udp_repeat_diagnostics;
    assert(fsim::library::serialize_portable_udp(
               *restored_udp, udp_repeat_diagnostics)
        == udp_bytes);
    fsim::library::PortableSystemVerilogClassUnit class_unit;
    class_unit.library = "vendor";
    class_unit.compilation_unit_identity = "class-fixture";
    fsim::frontend::SystemVerilogClassDeclaration class_declaration;
    class_declaration.name = "portable_class";
    class_declaration.canonical_identity = "vendor::portable_class";
    class_declaration.library = "vendor";
    class_declaration.compilation_unit_identity = "class-fixture";
    class_declaration.standard_revision
        = fsim::frontend::StandardRevision::SystemVerilog2009;
    class_declaration.verilog_compatibility_profile = "implicit-net";
    fsim::frontend::SystemVerilogClassMethod class_method;
    class_method.name = "sample";
    class_method.canonical_identity = "vendor::portable_class::sample";
    class_method.library = "vendor";
    class_method.compilation_unit_identity = "class-fixture";
    class_method.standard_revision
        = fsim::frontend::StandardRevision::SystemVerilog2009;
    class_method.verilog_compatibility_profile = "implicit-net";
    class_declaration.methods.push_back(class_method);
    class_unit.declarations.push_back(class_declaration);
    class_unit.method_definitions.push_back(class_method);
    fsim::diagnostic::Engine class_write_diagnostics;
    const auto class_bytes = fsim::library::serialize_portable_class_unit(
        class_unit, class_write_diagnostics);
    assert(class_bytes && !class_write_diagnostics.has_error());
    fsim::diagnostic::Engine class_read_diagnostics;
    const auto restored_class = fsim::library::deserialize_portable_class_unit(
        *class_bytes, "units/portable-class.fsimclass",
        class_read_diagnostics);
    assert(restored_class && !class_read_diagnostics.has_error());
    assert(
        restored_class->declarations.front().standard_revision
            == fsim::frontend::StandardRevision::SystemVerilog2009
        && restored_class->declarations.front()
                .verilog_compatibility_profile
            == "implicit-net"
        && restored_class->declarations.front().methods.front()
                .standard_revision
            == fsim::frontend::StandardRevision::SystemVerilog2009
        && restored_class->declarations.front().methods.front()
                .verilog_compatibility_profile
            == "implicit-net"
        && restored_class->method_definitions.front().standard_revision
            == fsim::frontend::StandardRevision::SystemVerilog2009
        && restored_class->method_definitions.front()
                .verilog_compatibility_profile
            == "implicit-net");
    auto stale_class = *class_bytes;
    stale_class[8] = static_cast<char>(
        fsim::library::kOwningUnitSchemaVersion - 1U);
    fsim::diagnostic::Engine stale_class_diagnostics;
    assert(!fsim::library::deserialize_portable_class_unit(
        stale_class, "stale.fsimclass", stale_class_diagnostics));
    assert(has_identity_diagnostic(
        stale_class_diagnostics, "portable class unit", "schema 30",
        "schema 31", ".fsimobj"));
    auto future_class = *class_bytes;
    future_class[8] = static_cast<char>(
        fsim::library::kOwningUnitSchemaVersion + 1U);
    fsim::diagnostic::Engine future_class_diagnostics;
    assert(!fsim::library::deserialize_portable_class_unit(
        future_class, "future.fsimclass", future_class_diagnostics));
    assert(has_identity_diagnostic(
        future_class_diagnostics, "portable class unit", "schema 32",
        "schema 31", ".fsimobj"));
    fsim::diagnostic::Engine truncated_class_diagnostics;
    assert(!fsim::library::deserialize_portable_class_unit(
        class_bytes->substr(0, 15), "truncated.fsimclass",
        truncated_class_diagnostics));
    auto trailing_class = *class_bytes;
    trailing_class.push_back('\0');
    fsim::diagnostic::Engine trailing_class_diagnostics;
    assert(!fsim::library::deserialize_portable_class_unit(
        trailing_class, "trailing.fsimclass", trailing_class_diagnostics));
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
    assert(has_identity_diagnostic(
        future_udp_diagnostics, "portable UDP declaration", "schema 2",
        "schema 1", ".fsimobj"));
    auto stale_udp = *udp_bytes;
    stale_udp[8] = '\0';
    fsim::diagnostic::Engine stale_udp_diagnostics;
    assert(!fsim::library::deserialize_portable_udp(
        stale_udp, "stale.fsimudp", stale_udp_diagnostics));
    assert(has_identity_diagnostic(
        stale_udp_diagnostics, "portable UDP declaration", "schema 0",
        "schema 1", ".fsimobj"));
    fsim::diagnostic::Engine truncated_udp_diagnostics;
    assert(!fsim::library::deserialize_portable_udp(
        udp_bytes->substr(0, 15), "truncated.fsimudp",
        truncated_udp_diagnostics));

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
    assert(has_identity_diagnostic(
        future_diagnostics, "portable owning unit", "schema 32",
        "schema 31", ".fsimobj"));
    auto stale_unit = *unit_bytes;
    stale_unit[8] = static_cast<char>(
        fsim::library::kOwningUnitSchemaVersion - 1U);
    fsim::diagnostic::Engine stale_diagnostics;
    assert(!fsim::library::deserialize_portable_unit(
        stale_unit, "stale.fsimir", stale_diagnostics));
    assert(has_identity_diagnostic(
        stale_diagnostics, "portable owning unit", "schema 30",
        "schema 31", ".fsimobj"));
    fsim::diagnostic::Engine truncated_unit_diagnostics;
    assert(!fsim::library::deserialize_portable_unit(
        unit_bytes->substr(0, 15), "truncated.fsimir",
        truncated_unit_diagnostics));
    auto oversized_unit = *unit_bytes;
    assert(oversized_unit.size() > 56U);
    std::fill(
        oversized_unit.begin() + 48, oversized_unit.begin() + 56,
        static_cast<char>(0xff));
    fsim::diagnostic::Engine oversized_unit_diagnostics;
    assert(!fsim::library::deserialize_portable_unit(
        oversized_unit, "oversized.fsimir", oversized_unit_diagnostics));
    auto corrupt_unit = *unit_bytes;
    corrupt_unit[0] = 'X';
    fsim::diagnostic::Engine corrupt_unit_diagnostics;
    assert(!fsim::library::deserialize_portable_unit(
        corrupt_unit, "corrupt.fsimir", corrupt_unit_diagnostics));

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
        incompatible_text.find("format = 5"),
        std::string { "format = 5" }.size(), "format = 99");
    fsim::diagnostic::Engine schema_diagnostics;
    assert(!fsim::library::parse_metadata(
        incompatible_text, "future.toml", schema_diagnostics));
    assert(has_identity_diagnostic(
        schema_diagnostics, ".fsimlib", "format 99", "format 5",
        ".fsimlib"));

    for (std::uint32_t format = 0;
        format < fsim::library::kFormatVersion; ++format) {
        auto stale_text = serialized;
        stale_text.replace(
            stale_text.find("format = 5"),
            std::string { "format = 5" }.size(),
            "format = " + std::to_string(format));
        fsim::diagnostic::Engine stale_schema_diagnostics;
        assert(!fsim::library::parse_metadata(
            stale_text, "stale.toml", stale_schema_diagnostics));
        assert(has_identity_diagnostic(
            stale_schema_diagnostics, ".fsimlib",
            "format " + std::to_string(format), "format 5", ".fsimlib"));
    }

    for (std::uint32_t schema = 0;
        schema < fsim::library::kPortableSchemaVersion; ++schema) {
        auto stale_portable_text = serialized;
        stale_portable_text.replace(
            stale_portable_text.find("portable_schema = 14"),
            std::string { "portable_schema = 14" }.size(),
            "portable_schema = " + std::to_string(schema));
        fsim::diagnostic::Engine stale_portable_diagnostics;
        assert(!fsim::library::parse_metadata(
            stale_portable_text, "stale-portable.toml",
            stale_portable_diagnostics));
        assert(has_identity_diagnostic(
            stale_portable_diagnostics, ".fsimlib",
            "portable-unit schema " + std::to_string(schema),
            "portable-unit schema 14", ".fsimlib"));
    }

    auto future_portable_text = serialized;
    future_portable_text.replace(
        future_portable_text.find("portable_schema = 14"),
        std::string { "portable_schema = 14" }.size(),
        "portable_schema = 15");
    fsim::diagnostic::Engine future_portable_diagnostics;
    assert(!fsim::library::parse_metadata(
        future_portable_text, "future-portable.toml",
        future_portable_diagnostics));
    assert(has_identity_diagnostic(
        future_portable_diagnostics, ".fsimlib",
        "portable-unit schema 15", "portable-unit schema 14", ".fsimlib"));

    auto systemc_metadata = expected;
    systemc_metadata.native_artifacts = { { "systemc_plugin", "native/systemc/libfixture.so",
        std::string(64, 'd'), 1, 4, "scv-2.0.1",
        "compiler-producer-fingerprint", { }, "x86_64-test", { },
        "generic", "+sse2", { }, { } } };
    fsim::diagnostic::Engine systemc_metadata_diagnostics;
    assert(fsim::library::parse_metadata(
               fsim::library::serialize_metadata(systemc_metadata),
               "systemc-native.toml", systemc_metadata_diagnostics)
        == std::optional { systemc_metadata });
    assert(!systemc_metadata_diagnostics.has_error());

    auto missing_scv_metadata = systemc_metadata;
    missing_scv_metadata.native_artifacts.front().scv_compatibility.clear();
    fsim::diagnostic::Engine missing_scv_metadata_diagnostics;
    assert(!fsim::library::parse_metadata(
        fsim::library::serialize_metadata(missing_scv_metadata),
        "missing-scv-native.toml", missing_scv_metadata_diagnostics));
    assert(missing_scv_metadata_diagnostics.has_error());
    const auto rejected_native = directory.parent_path() / "rejected-native.fsimlib";
    fsim::diagnostic::Engine rejected_native_diagnostics;
    assert(!fsim::library::publish(
        rejected_native, missing_scv_metadata, { },
        rejected_native_diagnostics));
    assert(rejected_native_diagnostics.has_error());
    assert(!std::filesystem::exists(rejected_native));

    std::error_code ignored;
    std::filesystem::remove_all(directory.parent_path(), ignored);
    std::cout << "library_artifact_test: all tests passed\n";
    return 0;
}
