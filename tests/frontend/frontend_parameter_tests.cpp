// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

} // namespace

void test_systemverilog_parameters() {
  const auto result = parse_text(
      "parameters.sv",
      R"(
module parameterized #(
  parameter int WIDTH = 8,
  parameter logic [1:0] MODE = 2'b01,
  localparam int LAST = WIDTH - 1
) (
  input logic [WIDTH - 1:0] data,
  output logic [WIDTH - 1:0] result
);
  localparam int DOUBLE_WIDTH = WIDTH * 2;
  localparam int CLOG_WIDTH = $clog2(WIDTH);
  assign result = data;
endmodule

module parameter_top;
  logic [3:0] input_value;
  logic [3:0] named_value;
  logic [1:0] positional_value;
  parameterized #(.WIDTH(4), .MODE(2'b10)) named_instance(
    .data(input_value),
    .result(named_value)
  );
  parameterized #(2, 2'b11) positional_instance(
    .data(input_value[1:0]),
    .result(positional_value)
  );
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "module parameters and overrides must parse");
  require(result.design.units.size() == 2, "parameterized unit count");
  const auto* parameterized =
      result.design.find(UnitKind::VerilogModule, "parameterized");
  require(
      parameterized != nullptr && parameterized->parameters.size() == 5,
      "parameter and localparam declarations are retained in source order");
  require(
      parameterized->parameters[0].name == "WIDTH"
          && parameterized->parameters[0].type.spelling == "int"
          && !parameterized->parameters[0].local
          && parameterized->parameters[1].name == "MODE"
          && parameterized->parameters[1].type.packed_range
          && parameterized->parameters[1].type.packed_range->width() == 2
          && parameterized->parameters[2].name == "LAST"
          && parameterized->parameters[2].local
          && parameterized->parameters[3].name == "DOUBLE_WIDTH"
          && parameterized->parameters[3].local
          && parameterized->parameters[4].name == "CLOG_WIDTH"
          && parameterized->parameters[4].local
          && parameterized->parameters[4].default_value.kind
              == ExpressionKind::Call
          && parameterized->parameters[4].default_value.text
              == "$clog2"
          && parameterized->parameters[4]
                 .default_value.operands.size()
              == 1,
      "typed parameter metadata");
  require(
      parameterized->ports.size() == 2
          && !parameterized->ports.front().type.packed_range
          && parameterized->ports.front().type.packed_range_expression
          && parameterized->ports.front()
                 .type.packed_range_expression->left.kind
              == ExpressionKind::Binary,
      "symbolic packed range remains in typed HIR");
  const auto* top =
      result.design.find(UnitKind::VerilogModule, "parameter_top");
  require(
      top != nullptr && top->instances.size() == 2
          && top->instances[0].parameter_overrides.size() == 2
          && top->instances[0].parameter_overrides[0].name
              == std::optional<std::string>{"WIDTH"}
          && top->instances[1].parameter_overrides.size() == 2
          && !top->instances[1].parameter_overrides[0].name,
      "named and positional parameter overrides are represented");

  const auto sized = parse_text(
      "sized-parameters.sv",
      R"(
module sized_parameters #(
  parameter byte SIGNED_BYTE = 8'hff,
  parameter byte unsigned UNSIGNED_BYTE = 8'hff,
  parameter shortint SHORT_VALUE = 16'h8000,
  parameter longint LONG_VALUE = 1,
  parameter longint unsigned UNSIGNED_LONG_VALUE = 1,
  parameter time TIME_VALUE = 2,
  parameter logic signed [7:0] SIGNED_VECTOR = 8'h80,
  parameter int unsigned UNSIGNED_INT = 3
) ();
endmodule
)",
      Language::SystemVerilog2017);
  require(sized.ok(), "SystemVerilog integral parameter types must parse");
  const auto* sized_unit =
      sized.design.find(UnitKind::VerilogModule, "sized_parameters");
  require(
      sized_unit != nullptr && sized_unit->parameters.size() == 8,
      "integral parameter declarations retain source order");
  require(
      sized_unit->parameters[0].type.spelling == "byte"
          && sized_unit->parameters[0].type.domain
              == ValueDomain::Bit2
          && sized_unit->parameters[0].type.is_signed
          && sized_unit->parameters[0].type.width() == 8
          && sized_unit->parameters[1].type.spelling == "byte"
          && !sized_unit->parameters[1].type.is_signed
          && sized_unit->parameters[2].type.spelling == "shortint"
          && sized_unit->parameters[2].type.width() == 16
          && sized_unit->parameters[3].type.spelling == "longint"
          && sized_unit->parameters[3].type.width() == 64
          && sized_unit->parameters[3].type.is_signed
          && sized_unit->parameters[4].type.spelling == "longint"
          && !sized_unit->parameters[4].type.is_signed
          && sized_unit->parameters[4].type.width() == 64
          && sized_unit->parameters[5].type.spelling == "time"
          && sized_unit->parameters[5].type.domain
              == ValueDomain::Logic4
          && !sized_unit->parameters[5].type.is_signed
          && sized_unit->parameters[5].type.width() == 64
          && sized_unit->parameters[6].type.is_signed
          && sized_unit->parameters[6].type.width() == 8
          && !sized_unit->parameters[7].type.is_signed
          && sized_unit->parameters[7].type.width() == 32,
      "integral parameter widths, domains, and signedness are typed");

  struct IntegralExpectation {
    std::string_view spelling;
    ValueDomain domain;
    std::uint8_t width;
    bool fixed_width;
    bool is_signed;
    SystemVerilogScalarKind scalar;
  };
  constexpr std::array integral_expectations{
      IntegralExpectation{"bit", ValueDomain::Bit2, 1U, false, false,
                          SystemVerilogScalarKind::None},
      IntegralExpectation{"logic", ValueDomain::Logic4, 1U, false, false,
                          SystemVerilogScalarKind::None},
      IntegralExpectation{"reg", ValueDomain::Logic4, 1U, false, false,
                          SystemVerilogScalarKind::None},
      IntegralExpectation{"byte", ValueDomain::Bit2, 8U, true, true,
                          SystemVerilogScalarKind::None},
      IntegralExpectation{"shortint", ValueDomain::Bit2, 16U, true, true,
                          SystemVerilogScalarKind::None},
      IntegralExpectation{"int", ValueDomain::Bit2, 32U, true, true,
                          SystemVerilogScalarKind::None},
      IntegralExpectation{"longint", ValueDomain::Bit2, 64U, true, true,
                          SystemVerilogScalarKind::None},
      IntegralExpectation{"integer", ValueDomain::Logic4, 32U, true, true,
                          SystemVerilogScalarKind::None},
      IntegralExpectation{"time", ValueDomain::Logic4, 64U, true, false,
                          SystemVerilogScalarKind::Time}};
  for (const auto& expected : integral_expectations) {
    const auto descriptor =
        systemverilog_integral_type_descriptor(expected.spelling);
    require(
        descriptor && descriptor->domain == expected.domain
            && descriptor->default_width == expected.width
            && descriptor->fixed_width == expected.fixed_width
            && descriptor->default_signed == expected.is_signed
            && descriptor->scalar_kind == expected.scalar,
        "built-in SystemVerilog integral descriptors are canonical");
  }
  Type event_type{ValueDomain::Logic4, "event", std::nullopt, false};
  Type logic_type;
  require(
      apply_systemverilog_integral_type(logic_type, "logic")
          && is_systemverilog_simple_integral_type(logic_type)
          && !systemverilog_integral_type_descriptor("event")
          && !is_systemverilog_simple_integral_type(event_type),
      "event handles remain outside simple integral type equivalence");

  const auto revised_integrals = parse_verilog(
      SourceText{"revised-integrals-2023.sv", R"(
module revised_integrals_2023 #(
  parameter bit BIT_VALUE = 1'bx,
  parameter logic LOGIC_VALUE = 1'bx,
  parameter reg REG_VALUE = 1'bz,
  parameter byte BYTE_VALUE = 8'hff,
  parameter shortint SHORT_VALUE = 16'h8000,
  parameter int INT_VALUE = 32'h8000_0000,
  parameter longint LONG_VALUE = 64'h8000_0000_0000_0000,
  parameter integer INTEGER_VALUE = 32'hxxxx_xxxx,
  parameter time TIME_VALUE = 64'hxxxx_xxxx_xxxx_xxxx
) ();
endmodule
)"},
      StandardRevision::SystemVerilog2023);
  require(
      revised_integrals.ok(),
      "SystemVerilog-2023 built-in integral declarations must parse");
  const auto* revised_integral_unit = revised_integrals.design.find(
      UnitKind::VerilogModule, "revised_integrals_2023");
  require(
      revised_integral_unit != nullptr
          && revised_integral_unit->parameters.size()
              == integral_expectations.size(),
      "SystemVerilog-2023 integral declarations retain source order");
  for (std::size_t index = 0; index < integral_expectations.size(); ++index) {
    const auto& actual = revised_integral_unit->parameters[index].type;
    const auto& expected = integral_expectations[index];
    require(
        actual.spelling == expected.spelling
            && actual.domain == expected.domain
            && actual.width()
                == std::optional<std::uint64_t>{expected.width}
            && actual.is_signed == expected.is_signed
            && actual.systemverilog_scalar == expected.scalar,
        "SystemVerilog-2023 integral declarations retain exact shape");
  }

  const auto revised_aggregates = parse_verilog(
      SourceText{"revised-aggregates-2023.sv", R"(
module revised_aggregates_2023;
  struct packed { logic [3:0] code; logic valid; } packed_left;
  struct packed { logic [3:0] code; logic valid; } packed_right;
  struct packed { logic [3:0] other; logic valid; } packed_member_mismatch;
  struct { logic [7:0] code; int count; } rows_left[0:1];
  struct { logic [7:0] code; int count; } rows_right[0:1];
  struct { logic [7:0] code; int count; } rows_bound_mismatch[1:2];
endmodule
)"},
      StandardRevision::SystemVerilog2023);
  require(
      revised_aggregates.ok(),
      "SystemVerilog-2023 aggregate declarations must parse");
  const auto* revised_aggregate_unit = revised_aggregates.design.find(
      UnitKind::VerilogModule, "revised_aggregates_2023");
  require(
      revised_aggregate_unit != nullptr
          && revised_aggregate_unit->signals.size() == 3
          && revised_aggregate_unit->variables.size() == 3,
      "SystemVerilog-2023 aggregate declarations retain source order");
  require(
      systemverilog_types_equivalent(
          revised_aggregate_unit->signals[0].type,
          revised_aggregate_unit->signals[1].type)
          && !systemverilog_types_equivalent(
              revised_aggregate_unit->signals[0].type,
              revised_aggregate_unit->signals[2].type)
          && systemverilog_types_equivalent(
              revised_aggregate_unit->variables[0].type,
              revised_aggregate_unit->variables[1].type)
          && !systemverilog_types_equivalent(
              revised_aggregate_unit->variables[0].type,
              revised_aggregate_unit->variables[2].type),
      "anonymous aggregate equivalence is recursive and bound-aware");

  auto canonical_interface_specializations = parse_verilog(
      SourceText{"parameterized-interface-diamond-2023.sv", R"(
interface class ParameterizedBase #(type T = logic [3:0]);
endclass
interface class LogicBranch
    extends ParameterizedBase #(logic [3:0]);
endclass
interface class RegBranch
    extends ParameterizedBase #(reg [3:0]);
endclass
interface class CanonicalDiamond extends LogicBranch, RegBranch;
endclass
class TypeIdentityWitness;
  ParameterizedBase #(logic [3:0]) logic_handle;
  ParameterizedBase #(reg [3:0]) reg_handle;
  ParameterizedBase #(bit [3:0]) bit_handle;
endclass
)"},
      StandardRevision::SystemVerilog2023);
  require(
      canonical_interface_specializations.ok(),
      "equivalent parameterized interface-class diamond must parse");
  std::vector<Diagnostic> canonical_interface_resolution;
  require(
      resolve_systemverilog_classes(
          canonical_interface_specializations.design,
          canonical_interface_resolution),
      "parameterized interface-class relations must resolve");
  std::vector<Diagnostic> canonical_interface_inheritance;
  require(
      validate_systemverilog_class_inheritance(
          canonical_interface_specializations.design,
          canonical_interface_inheritance),
      "equivalent interface-class specializations form one diamond");
  const auto canonical_specializations =
      specialize_systemverilog_classes(
          canonical_interface_specializations.design);
  require(
      canonical_specializations.ok(),
      "equivalent integral type actuals must share specialization identity");
  const auto find_specialization =
      [&](const std::string_view suffix) {
        return std::ranges::find_if(
            canonical_specializations.specializations,
            [&](const SystemVerilogClassSpecialization& specialization) {
              return specialization.declaration_identity.ends_with(suffix);
            });
      };
  const auto logic_branch = find_specialization("::LogicBranch");
  const auto reg_branch = find_specialization("::RegBranch");
  const auto diamond = find_specialization("::CanonicalDiamond");
  require(
      logic_branch != canonical_specializations.specializations.end()
          && reg_branch != canonical_specializations.specializations.end()
          && diamond != canonical_specializations.specializations.end()
          && logic_branch->base_specialization_identity
              == reg_branch->base_specialization_identity
          && !diamond->base_specialization_identity.empty()
          && diamond->interface_specialization_identities.size() == 1,
      "canonical interface specializations and direct relations are explicit");
  const auto& type_identity_properties =
      canonical_interface_specializations.design
          .systemverilog_classes.back().properties;
  require(
      type_identity_properties.size() == 3
          && systemverilog_types_equivalent(
              type_identity_properties[0].declaration.type,
              type_identity_properties[1].declaration.type)
          && !systemverilog_types_equivalent(
              type_identity_properties[0].declaration.type,
              type_identity_properties[2].declaration.type),
      "parameterized class-handle identity includes canonical type actuals");

  auto conflicting_interface_specializations = parse_verilog(
      SourceText{"conflicting-interface-specializations-2023.sv", R"(
interface class ConflictBase #(type T = logic);
endclass
interface class FourStateBranch extends ConflictBase #(logic);
endclass
interface class TwoStateBranch extends ConflictBase #(bit);
endclass
interface class ConflictingDiamond
    extends FourStateBranch, TwoStateBranch;
endclass
)"},
      StandardRevision::SystemVerilog2023);
  require(
      conflicting_interface_specializations.ok(),
      "distinct parameterized interface-class diamond must parse");
  std::vector<Diagnostic> conflicting_interface_resolution;
  require(
      resolve_systemverilog_classes(
          conflicting_interface_specializations.design,
          conflicting_interface_resolution),
      "distinct parameterized interface-class relations must resolve");
  const auto conflicting_specializations =
      specialize_systemverilog_classes(
          conflicting_interface_specializations.design);
  require(
      !conflicting_specializations.ok()
          && std::ranges::any_of(
              conflicting_specializations.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-CLASS-SPEC-012";
              }),
      "distinct specializations of one inherited interface must reject");

  auto class_callable_lifetimes = parse_verilog(
      SourceText{"class-callable-lifetimes-2023.sv", R"(
class CallableBase;
  function new(int value = 0);
  endfunction
  virtual function int evaluate(input int value);
    return value;
  endfunction
endclass
class CallableDerived extends CallableBase;
  function new(int value = 0);
    super.new(value);
  endfunction
  function int evaluate(input int value);
    int temporary;
    temporary = value + 1;
    return temporary;
  endfunction
  static function int utility(input int value);
    return value;
  endfunction
  function automatic int explicit_automatic(input int value);
    return value;
  endfunction
endclass
)"},
      StandardRevision::SystemVerilog2023);
  require(
      class_callable_lifetimes.ok(),
      "2023 constructors and class method lifetime forms must parse");
  std::vector<Diagnostic> class_callable_resolution;
  require(
      resolve_systemverilog_classes(
          class_callable_lifetimes.design,
          class_callable_resolution),
      "2023 constructor and method selections must resolve");
  std::vector<Diagnostic> class_callable_inheritance;
  require(
      validate_systemverilog_class_inheritance(
          class_callable_lifetimes.design,
          class_callable_inheritance),
      "single first-statement base construction must be legal");
  const auto class_callable_specializations =
      specialize_systemverilog_classes(class_callable_lifetimes.design);
  require(
      class_callable_specializations.ok()
          && std::ranges::all_of(
              class_callable_specializations.specializations,
              [](const SystemVerilogClassSpecialization& specialization) {
                return std::ranges::all_of(
                    specialization.methods,
                    [](const SystemVerilogClassMethodProfile& method) {
                      return method.lifetime
                          == SystemVerilogClassLifetime::Automatic;
                    });
              }),
      "instance and static class methods must materialize automatic frames");

  auto invalid_class_callables = parse_verilog(
      SourceText{"invalid-class-callables-2023.sv", R"(
class ConstructorBase;
  function new();
  endfunction
endclass
class DuplicateConstructor extends ConstructorBase;
  function new();
    super.new();
  endfunction
  function new(int value);
  endfunction
endclass
class QualifiedConstructor extends ConstructorBase;
  static function new();
    super.new();
  endfunction
endclass
class StaticLifetime;
  function static int retain();
    return 1;
  endfunction
endclass
class LateBaseCall extends ConstructorBase;
  int value;
  function new();
    value = 1;
    super.new();
  endfunction
endclass
class NestedBaseCall extends ConstructorBase;
  function new();
    begin
      super.new();
    end
  endfunction
endclass
class RepeatedBaseCall extends ConstructorBase;
  function new();
    super.new();
    super.new();
  endfunction
endclass
class OutsideConstructor extends ConstructorBase;
  function void initialize();
    super.new();
  endfunction
endclass
)"},
      StandardRevision::SystemVerilog2023);
  require(
      invalid_class_callables.ok(),
      "constructor and method lifetime negatives must parse before legality");
  std::vector<Diagnostic> invalid_class_callable_resolution;
  require(
      resolve_systemverilog_classes(
          invalid_class_callables.design,
          invalid_class_callable_resolution),
      "constructor and method lifetime negatives must resolve names");
  std::vector<Diagnostic> invalid_class_callable_diagnostics;
  require(
      !validate_systemverilog_class_inheritance(
          invalid_class_callables.design,
          invalid_class_callable_diagnostics),
      "invalid constructors and static class-method storage must reject");
  for (const auto code : {
           "FSIM-SV-CLASS-INHERIT-014",
           "FSIM-SV-CLASS-INHERIT-015",
           "FSIM-SV-CLASS-INHERIT-016"}) {
    require(
        std::ranges::any_of(
            invalid_class_callable_diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "constructor and lifetime diagnostics must remain stable");
  }

  const auto type_parameters = parse_text(
      "type-parameters.sv",
      R"(
package types_pkg;
  typedef logic [11:0] word_t;
endpackage
module typed_child #(
  parameter type ELEMENT = logic,
  parameter type WORD = types_pkg::word_t,
  parameter WIDTH = 3
) (
  input ELEMENT element,
  input WORD word
);
endmodule
module typed_parent;
  typedef bit [7:0] byte_t;
  typed_child #(
    .ELEMENT(bit),
    .WORD(byte_t),
    .WIDTH(5)
  ) child();
endmodule
)",
      Language::SystemVerilog2017);
  require(
      type_parameters.ok(),
      "bounded SystemVerilog type parameters must parse");
  const auto* typed_child =
      type_parameters.design.find(
          UnitKind::VerilogModule, "typed_child");
  const auto* typed_parent =
      type_parameters.design.find(
          UnitKind::VerilogModule, "typed_parent");
  require(
      typed_child != nullptr
          && typed_child->parameters.size() == 3
          && typed_child->parameters[0].kind
              == ParameterKind::Type
          && typed_child->parameters[0].default_type
          && typed_child->parameters[0].default_type->spelling
              == "logic"
          && typed_child->parameters[1].kind
              == ParameterKind::Type
          && typed_child->parameters[1].default_type
          && typed_child->parameters[1].default_type->named_type
              == "types_pkg::word_t"
          && typed_child->parameters[2].kind
              == ParameterKind::Value,
      "type and value formals retain ordered distinct HIR");
  require(
      typed_parent != nullptr
          && typed_parent->instances.size() == 1
          && typed_parent->instances[0]
                 .parameter_overrides.size()
              == 3
          && typed_parent->instances[0]
                 .parameter_overrides[0].type_value
          && typed_parent->instances[0]
                 .parameter_overrides[0].type_value->spelling
              == "bit"
          && !typed_parent->instances[0]
                  .parameter_overrides[1].type_value
          && typed_parent->instances[0]
                 .parameter_overrides[1].value.text
              == "byte_t",
      "unambiguous and identifier type actuals retain tentative HIR");

  const auto string_parameters = parse_text(
      "string-parameters.sv",
      R"(
package string_pkg;
  localparam string PACKAGE_LABEL = "package";
endpackage
module string_parameters #(
  parameter string LABEL = "fsim\n\v\f\a\x41"
) ();
  localparam string DECORATED = {LABEL, "!"};
endmodule
)",
      Language::SystemVerilog2017);
  require(
      string_parameters.ok(),
      "string parameters and localparams must parse");
  const auto* string_unit =
      string_parameters.design.find(
          UnitKind::VerilogModule, "string_parameters");
  require(
      string_unit != nullptr
          && string_unit->parameters.size() == 2
          && string_unit->parameters[0].type.spelling == "string"
          && string_unit->parameters[0].default_value.kind
              == ExpressionKind::StringLiteral
          && string_unit->parameters[0].default_value.decoded_string
              == std::optional<std::string> { "fsim\n\v\f\aA" }
          && string_unit->parameters[1].local
          && string_unit->parameters[1].type.spelling == "string"
          && string_unit->parameters[1].default_value.kind
              == ExpressionKind::Concatenation,
      "string HIR retains type, locality, expression, and decoded bytes");

  const auto invalid_systemverilog_hex_escape = parse_text(
      "invalid-systemverilog-hex-escape.sv",
      R"(module bad #(parameter string VALUE = "bad\x4") (); endmodule)",
      Language::SystemVerilog2017);
  require(
      !invalid_systemverilog_hex_escape.ok()
          && std::ranges::any_of(
              invalid_systemverilog_hex_escape.diagnostics,
              [](const auto& diagnostic) {
                  return diagnostic.code == "FSIM-SV-SEM-040";
              }),
      "SystemVerilog hexadecimal string escapes require two digits");

  const auto verilog_systemverilog_escape = parse_text(
      "verilog-systemverilog-escape.v",
      R"(module bad #(parameter VALUE = "bad\v") (); endmodule)",
      Language::Verilog2005);
  require(
      !verilog_systemverilog_escape.ok()
          && std::ranges::any_of(
              verilog_systemverilog_escape.diagnostics,
              [](const auto& diagnostic) {
                  return diagnostic.code == "FSIM-SV-SEM-040";
              }),
      "SystemVerilog-only string escapes remain illegal in Verilog-2005");

  const auto mutable_strings = parse_text(
      "mutable-strings.sv",
      R"(
module mutable_strings;
  string title = "fsim";
  string empty;

  function automatic string decorate(input string value);
    string suffix = "!";
    return {value, suffix};
  endfunction

  task automatic remember(input string value, output string copied);
    string temporary;
    temporary = value;
    copied = temporary;
  endtask

  initial begin : worker
    string scratch = {title, "-local"};
    title = decorate(scratch);
    if (title != "")
      title[0] = "F";
    if (title.len() == 0)
      title = title.tolower().substr(0, 2);
    $sformat(title, "%s", scratch);
    title = $sformatf("%s", title);
  end
endmodule
module string_ports(
    input string incoming,
    output string outgoing,
    inout string shared);
endmodule
module classic_string_ports(incoming, outgoing, shared);
  input string incoming;
  output string outgoing;
  inout string shared;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      mutable_strings.ok(),
      "bounded mutable string syntax must parse without integral-type "
      "fallback diagnostics");
  const auto* mutable_unit =
      mutable_strings.design.find(
          UnitKind::VerilogModule, "mutable_strings");
  require(
      mutable_unit != nullptr
          && mutable_unit->variables.size() == 2
          && mutable_unit->variables[0].type.domain
              == ValueDomain::String
          && mutable_unit->variables[0].initializer
          && mutable_unit->variables[0].initializer->decoded_string
              == std::optional<std::string>{"fsim"}
          && !mutable_unit->variables[1].initializer,
      "module string variables retain distinct type and initialization");
  const auto* string_port_unit =
      mutable_strings.design.find(
          UnitKind::VerilogModule, "string_ports");
  const auto* classic_string_port_unit =
      mutable_strings.design.find(
          UnitKind::VerilogModule, "classic_string_ports");
  require(
      string_port_unit != nullptr
          && string_port_unit->ports.size() == 3
          && std::ranges::all_of(
              string_port_unit->ports,
              [](const auto& port) {
                return port.type.domain == ValueDomain::String;
              })
          && string_port_unit->ports[0].direction
              == PortDirection::Input
          && string_port_unit->ports[1].direction
              == PortDirection::Output
          && string_port_unit->ports[2].direction
              == PortDirection::Inout
          && classic_string_port_unit != nullptr
          && classic_string_port_unit->ports.size() == 3
          && std::ranges::all_of(
              classic_string_port_unit->ports,
              [](const auto& port) {
                return port.type.domain == ValueDomain::String;
              }),
      "ANSI and classic mutable string ports retain type and direction");
  require(
      mutable_unit != nullptr
          && mutable_unit->functions.size() == 1
          && mutable_unit->functions[0].return_type.domain
              == ValueDomain::String
          && mutable_unit->functions[0].arguments.size() == 1
          && mutable_unit->functions[0].arguments[0].type.domain
              == ValueDomain::String
          && mutable_unit->functions[0].variables.size() == 1
          && mutable_unit->functions[0].variables[0].type.domain
              == ValueDomain::String,
      "string function result, formal, and automatic local retain typing");
  require(
      mutable_unit != nullptr
          && mutable_unit->tasks.size() == 1
          && mutable_unit->tasks[0].arguments.size() == 2
          && mutable_unit->tasks[0].arguments[0].type.domain
              == ValueDomain::String
          && mutable_unit->tasks[0].arguments[1].type.domain
              == ValueDomain::String
          && mutable_unit->tasks[0].variables.size() == 1
          && mutable_unit->tasks[0].variables[0].type.domain
              == ValueDomain::String,
      "string task formals and automatic local retain typing");
  require(
      mutable_unit != nullptr
          && mutable_unit->processes.size() == 1
          && mutable_unit->processes[0].statements.size() == 1
          && mutable_unit->processes[0].statements[0]
                 .declarations.size() == 1
          && mutable_unit->processes[0].statements[0]
                 .declarations[0].type.domain
              == ValueDomain::String
          && mutable_unit->processes[0].statements[0]
                 .statements.size() == 5
          && mutable_unit->processes[0].statements[0]
                 .statements[2].condition.operands[0].kind
              == ExpressionKind::Call
          && mutable_unit->processes[0].statements[0]
                 .statements[2].condition.operands[0].text
              == ".len",
      "block string declarations and len method retain executable HIR");
  require(
      mutable_unit != nullptr
          && mutable_unit->processes[0].statements[0]
                 .statements[3].kind == StatementKind::TaskCall
          && mutable_unit->processes[0].statements[0]
                 .statements[3].task_name == "$sformat"
          && mutable_unit->processes[0].statements[0]
                 .statements[4].value.kind == ExpressionKind::Call
          && mutable_unit->processes[0].statements[0]
                 .statements[4].value.text == "$sformatf",
      "string formatting tasks and functions retain ordinary compact HIR");

  const auto invalid_string_method = parse_text(
      "invalid-string-method.sv",
      "module bad; string value; initial value = value.substr(0); endmodule",
      Language::SystemVerilog2017);
  require(
      !invalid_string_method.ok()
          && std::ranges::any_of(
              invalid_string_method.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-127";
              }),
      "string method arity has a targeted diagnostic");

  const auto invalid_string_escape = parse_text(
      "invalid-string-escape.sv",
      R"(
module invalid_string_escape #(
  parameter string LABEL = "bad\q"
) ();
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_string_escape.ok()
          && std::ranges::any_of(
              invalid_string_escape.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-040";
              }),
      "invalid string parameter escapes retain the shared diagnostic");

  const auto type_namespace_conflict = parse_text(
      "type-parameter-conflict.sv",
      R"(
module type_parameter_conflict #(
  parameter type element_t = logic
) ();
  typedef logic element_t;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !type_namespace_conflict.ok()
          && std::ranges::any_of(
              type_namespace_conflict.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-055";
              }),
      "type parameters share the bounded typedef namespace");

  const auto verilog = parse_text(
      "clog2.v",
      R"(
module clog2 #(
  parameter WIDTH = 9
) ();
  localparam CLOG_WIDTH = $clog2(WIDTH);
endmodule
)",
      Language::Verilog2005);
  require(verilog.ok(), "Verilog-2005 $clog2 call must parse");
  const auto* clog2 =
      verilog.design.find(UnitKind::VerilogModule, "clog2");
  require(
      clog2 != nullptr && clog2->parameters.size() == 2
          && clog2->parameters[1].default_value.kind
              == ExpressionKind::Call
          && clog2->parameters[1].default_value.text == "$clog2"
          && clog2->parameters[1].default_value.operands.size() == 1,
      "Verilog-2005 $clog2 parameter-call HIR");

  const auto verilog_declarations = parse_text(
      "verilog-declarations.v",
      R"(
(* module_attr, tool_note = "verilog-2005" *)
module declaration_matrix #(
  (* param_attr = (1 + 2) * 3 *)
  parameter [256:0] WIDE = 257'h1,
  (* count_attr *) parameter integer COUNT = 4
) (
  (* port_attr = 2 *) input wire signed [7:0] data,
  output reg [7:0] result,
  inout tri shared
);
  (* net_attr = WIDE[0] *) wire [7:0] driven = data;
  (* reg_attr *) reg signed [257'h7:257'h0] scratch;
  integer index;
  time stamp;
  real gain;
  realtime interval;
  (* memory_attr *) reg [7:0] memory [257'h0:257'h3];
  (* event_attr *) event changed;
  (* genvar_attr *) genvar g;
  localparam [256:0] COPY = WIDE;
  (* function_attr *) function automatic integer identity;
    input value;
    begin
      identity = value;
    end
  endfunction
  (* task_attr *) task automatic capture;
    input value;
    begin
      index = value;
    end
  endtask
endmodule
)",
      Language::Verilog2005);
  if (!verilog_declarations.ok()) {
      for (const auto& diagnostic : verilog_declarations.diagnostics) {
          std::cerr << diagnostic.code << ": "
                    << diagnostic.message << '\n';
      }
  }
  require(
      verilog_declarations.ok(),
      "Verilog-2005 declarations, memories, and attributes must parse");
  const auto* declaration_matrix = verilog_declarations.design.find(
      UnitKind::VerilogModule, "declaration_matrix");
  require(
      declaration_matrix != nullptr
          && declaration_matrix->ports.size() == 3
          && declaration_matrix->ports[0].direction
              == PortDirection::Input
          && declaration_matrix->ports[0].type.is_signed
          && declaration_matrix->ports[0].type.width() == 8
          && declaration_matrix->ports[1].direction
              == PortDirection::Output
          && declaration_matrix->ports[2].direction
              == PortDirection::Inout,
      "Verilog-2005 ANSI ports retain direction, signedness, and range");
  require(
      declaration_matrix != nullptr
          && declaration_matrix->parameters.size() == 3
          && declaration_matrix->parameters[0].name == "WIDE"
          && declaration_matrix->parameters[0].type.width() == 257
          && declaration_matrix->parameters[0].default_value.text
              == "257'h1"
          && declaration_matrix->parameters[1].name == "COUNT"
          && declaration_matrix->parameters[2].name == "COPY"
          && declaration_matrix->parameters[2].default_value.text
              == "WIDE",
      "Verilog-2005 parameters retain wide types, defaults, and locality");
  const auto memory = std::ranges::find_if(
      declaration_matrix->variables,
      [](const auto& variable) {
          return variable.name == "memory";
      });
  require(
      memory != declaration_matrix->variables.end(),
      "one-dimensional Verilog memory remains a variable");
  require(
      memory->type.width() == 8,
      "one-dimensional Verilog memory retains its element width");
  require(
      memory->type.systemverilog_container.has_value(),
      "one-dimensional Verilog memory retains its container HIR");
  const auto& memory_container = *memory->type.systemverilog_container;
  require(
      memory_container.kind == SystemVerilogContainerKind::StaticArray,
      "one-dimensional Verilog memory remains a static array");
  require(
      memory_container.static_range.has_value(),
      "one-dimensional Verilog memory retains a concrete bound");
  require(
      memory_container.static_range->left == 0
          && memory_container.static_range->right == 3,
      "one-dimensional Verilog memory retains its evaluated bound");
  require(
      memory_container.static_range_expressions.size() == 1,
      "one-dimensional Verilog memory retains one bound expression");
  require(
      memory_container.static_range_expressions[0].left.text == "257'h0"
          && memory_container.static_range_expressions[0].right.text
              == "257'h3",
      "one-dimensional Verilog memory retains its wide bound HIR");
  const auto scratch = std::ranges::find_if(
      declaration_matrix->signals,
      [](const auto& signal) {
          return signal.name == "scratch";
      });
  require(
      scratch != declaration_matrix->signals.end()
          && scratch->type.is_signed && scratch->type.width() == 8
          && declaration_matrix->signals.size() == 7,
      "Verilog net, reg, integer, time, real, realtime, and event objects "
      "remain distinct after ignored attributes");
  require(
      declaration_matrix->functions.size() == 1
          && declaration_matrix->functions[0].automatic
          && declaration_matrix->functions[0].lifetime_explicit
          && declaration_matrix->functions[0].language
              == Language::Verilog2005
          && declaration_matrix->tasks.size() == 1
          && declaration_matrix->tasks[0].automatic
          && declaration_matrix->tasks[0].lifetime_explicit,
      "Verilog-2005 callable lifetime remains explicit and source-owned");

  const auto malformed_attribute_name = parse_text(
      "verilog-attribute-name.v",
      "(* = 1 *) module bad_name; endmodule",
      Language::Verilog2005);
  const auto malformed_attribute_value = parse_text(
      "verilog-attribute-value.v",
      "(* keep = *) module bad_value; endmodule",
      Language::Verilog2005);
  const auto malformed_attribute_close = parse_text(
      "verilog-attribute-close.v",
      "(* keep = 1",
      Language::Verilog2005);
  const auto has_diagnostic = [](
                                  const ParseResult& parsed_result, const std::string_view code) {
      return std::ranges::any_of(
          parsed_result.diagnostics,
          [&](const auto& diagnostic) {
              return diagnostic.code == code;
          });
  };
  require(
      !malformed_attribute_name.ok()
          && has_diagnostic(
              malformed_attribute_name, "FSIM-SV-PARSE-336")
          && !malformed_attribute_value.ok()
          && has_diagnostic(
              malformed_attribute_value, "FSIM-SV-PARSE-338")
          && !malformed_attribute_close.ok()
          && has_diagnostic(
              malformed_attribute_close, "FSIM-SV-PARSE-337"),
      "malformed Verilog attributes have stable bounded diagnostics");

  const auto invalid_verilog_memories = parse_text(
      "verilog-invalid-memories.v",
      "module bad; reg [7:0] matrix [0:3][0:1]; "
      "wire [7:0] wire_memory [0:3]; endmodule",
      Language::Verilog2005);
  require(
      !invalid_verilog_memories.ok()
          && has_diagnostic(
              invalid_verilog_memories, "FSIM-VERILOG-SEM-012")
          && has_diagnostic(
              invalid_verilog_memories, "FSIM-SV-SEM-079"),
      "multidimensional and net Verilog memories diagnose precisely");

  const auto systemverilog_net_array = parse_text(
      "systemverilog-net-array.sv",
      "module net_array; wire [7:0] values [0:3]; endmodule",
      Language::SystemVerilog2017);
  require(
      systemverilog_net_array.ok()
          && systemverilog_net_array.design.units.size() == 1
          && systemverilog_net_array.design.units.front()
                 .variables.size()
              == 1
          && systemverilog_net_array.design.units.front()
                 .variables.front().name
              == "values"
          && systemverilog_net_array.design.units.front()
                 .variables.front().type.width()
              == 8
          && systemverilog_net_array.design.units.front()
                 .variables.front().type.systemverilog_container
                 .has_value(),
      "SystemVerilog unpacked net arrays retain their container HIR");

  const auto nettypes = parse_text(
      "nettypes.sv",
      R"(
package resolution_pkg;
  function automatic logic [7:0] resolve(
      input logic [7:0] drivers[]);
    return drivers[0];
  endfunction
endpackage
package nettype_pkg;
  nettype logic [7:0] byte_net with resolution_pkg::resolve;
endpackage
module nettype_user;
  import nettype_pkg::*;
  byte_net value;
endmodule
)",
      Language::SystemVerilog2017);
  require(nettypes.ok(), "user-defined nettypes must parse");
  const auto* nettype_package = nettypes.design.find(
      UnitKind::SystemVerilogPackage, "nettype_pkg");
  const auto* nettype_user = nettypes.design.find(
      UnitKind::VerilogModule, "nettype_user");
  require(
      nettype_package != nullptr
          && nettype_package->type_aliases.size() == 1
          && nettype_package->type_aliases.front().name == "byte_net"
          && nettype_package->type_aliases.front().declaration_kind
              == TypeDeclarationKind::SystemVerilogNettype
          && nettype_package->type_aliases.front()
                  .type.systemverilog_net_type
              == "byte_net"
          && nettype_package->type_aliases.front().type.width()
              == std::optional<std::uint64_t> { 8 }
          && nettype_package->type_aliases.front()
                  .systemverilog_resolution_function
              == "resolution_pkg::resolve"
          && nettype_user != nullptr
          && nettype_user->signals.size() == 1
          && nettype_user->signals.front().type.named_type == "byte_net",
      "nettype base, resolver, declaration identity, and imported use");

  const auto duplicate_nettype = parse_text(
      "duplicate-nettype.sv",
      "package p; nettype logic n; nettype logic n; endpackage",
      Language::SystemVerilog2017);
  require(
      !duplicate_nettype.ok()
          && has_diagnostic(duplicate_nettype, "FSIM-SV-SEM-237"),
      "duplicate nettypes diagnose deterministically");

  const auto alias_and_let = parse_text(
      "alias-let.sv",
      R"(
package let_pkg;
  let passthrough(value) = value;
endpackage
module alias_let;
  logic [7:0] left;
  logic [7:0] right;
  alias left = right;
  let merge(input logic [7:0] value, fallback = 8'h01) =
      value | fallback;
  initial right = merge(left);
endmodule
)",
      Language::SystemVerilog2017);
  require(alias_and_let.ok(), "alias and let declarations must parse");
  const auto* let_package = alias_and_let.design.find(
      UnitKind::SystemVerilogPackage, "let_pkg");
  const auto* alias_let_unit = alias_and_let.design.find(
      UnitKind::VerilogModule, "alias_let");
  require(
      let_package != nullptr
          && let_package->systemverilog_lets.size() == 1
          && let_package->systemverilog_lets.front().name == "passthrough"
          && alias_let_unit != nullptr
          && alias_let_unit->systemverilog_aliases.size() == 1
          && alias_let_unit->systemverilog_aliases.front().terminals.size()
              == 2
          && alias_let_unit->systemverilog_aliases.front().terminals[0].text
              == "left"
          && alias_let_unit->systemverilog_aliases.front().terminals[1].text
              == "right"
          && alias_let_unit->systemverilog_lets.size() == 1
          && alias_let_unit->systemverilog_lets.front().ports.size() == 2
          && alias_let_unit->systemverilog_lets.front().ports[0].type
          && alias_let_unit->systemverilog_lets.front().ports[0].type->width()
              == std::optional<std::uint64_t> { 8 }
          && alias_let_unit->systemverilog_lets.front().ports[1].default_value,
      "alias terminals and typed/defaulted let formals remain exact");

  const auto invalid = parse_text(
      "invalid-parameters.sv",
      R"(
module invalid_parameters #(
  parameter WIDTH,
  parameter WIDTH = 2
);
endmodule
module invalid_parameter_top;
  invalid_parameters #(.WIDTH(1), .WIDTH(2)) duplicate();
  invalid_parameters #(.WIDTH(1), 2) mixed();
endmodule
module parameter_object_conflict #(
  parameter CLASH = 1
) (
  input logic CLASH
);
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::any_of(
        invalid.diagnostics.begin(),
        invalid.diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !invalid.ok() && has_code("FSIM-SV-PARSE-050")
          && has_code("FSIM-SV-SEM-017")
          && has_code("FSIM-SV-SEM-018")
          && has_code("FSIM-SV-SEM-019")
          && has_code("FSIM-SV-SEM-020"),
      "parameter declaration and override diagnostics are targeted");

  const std::string fuzz_oom_source{
      "module m(inpsedge ic6\0\0\0automatic\0a+ begin\n", 43U};
  const auto fuzz_oom = parse_text(
      "fuzz-oom-08279901975c1c5c906abd0a1609b94359e048a9.sv",
      fuzz_oom_source,
      Language::SystemVerilog2017);
  require(
      !fuzz_oom.ok() && fuzz_oom.design.units.size() == 1
          && fuzz_oom.diagnostics.size() < 32,
      "malformed lifetime declaration recovery must make bounded progress");
}

} // namespace fsim::tests::frontend
