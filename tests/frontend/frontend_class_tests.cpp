// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

}  // namespace

void test_systemverilog_class_declarations() {
  auto parsed = parse_text(
      "classes.sv",
      R"(
package base_pkg;
  class Base #(parameter int WIDTH = 1);
  endclass : Base
endpackage : base_pkg

class Box #(
    parameter type T = int,
    parameter int COUNT = 2);
  T value;
endclass : Box

class DispatchBase;
  virtual function int value(input int argument);
    return argument;
  endfunction
endclass : DispatchBase

class DispatchDerived extends DispatchBase;
  function int value(input int argument);
    return argument + 1;
  endfunction
  virtual function int extra();
    return 1;
  endfunction
endclass : DispatchDerived

typedef class Worker;
virtual class automatic Worker #(parameter int WIDTH = 8)
    extends base_pkg::Base #(WIDTH);
  typedef class ForwardNested;
  typedef struct packed {
    logic [WIDTH-1:0] payload;
    bit valid;
  } packet_t;
  local static int count = 1;
  protected const logic [WIDTH-1:0] payload;
  rand bit enabled;
  randc logic [1:0] choice;
  string label;
  logic lanes [0:1][2:3];
  packet_t packet;
  ForwardNested child;
  Worker #(.WIDTH(4)) narrowed;
  Box #(.T(logic [3:0]), .COUNT(3)) boxes;
  function new(int seed = 0);
    this.count = seed;
    super.new();
  endfunction : new
  virtual function int get_count();
    return this.count;
  endfunction : get_count
  static function int current_count();
    return count;
  endfunction : current_count
  pure virtual function void apply(input int value);
  extern protected task run(input int cycles = 1);
  extern function int declared(input int delta);
  constraint valid_count {
    count >= 0;
    enabled inside {0, 1};
  }
  pure constraint abstract_rule;
  class ForwardNested;
  endclass : ForwardNested
  virtual class Nested;
  endclass : Nested
endclass : Worker

function int Worker::declared(input int delta);
  return delta;
endfunction

package class_package;
  virtual class Abstract;
  endclass : Abstract
  interface class Contract;
  endclass : Contract
endpackage : class_package

module class_owner;
  import base_pkg::Base;
  class Local extends Base;
  endclass : Local
endmodule : class_owner
)",
      Language::SystemVerilog2017);
  if (!parsed.ok()) {
    throw std::runtime_error(
        parsed.diagnostics.front().code + ": "
        + parsed.diagnostics.front().message);
  }
  std::vector<Diagnostic> resolution_diagnostics;
  require(
      resolve_systemverilog_classes(
          parsed.design, resolution_diagnostics),
      "visible class names and extern definitions must resolve");
  std::vector<Diagnostic> inheritance_diagnostics;
  require(
      validate_systemverilog_class_inheritance(
          parsed.design, inheritance_diagnostics),
      "the resolved class inheritance graph must be legal");
  require(
      parsed.design.systemverilog_classes.size() == 4,
      "a forward declaration and definition must merge");
  const auto worker_declaration = std::ranges::find(
      parsed.design.systemverilog_classes,
      std::string{"Worker"},
      &SystemVerilogClassDeclaration::name);
  require(
      worker_declaration != parsed.design.systemverilog_classes.end(),
      "the Worker class definition must remain present");
  const auto& worker = *worker_declaration;
  require(
      worker.name == "Worker"
          && worker.canonical_identity == "work::$unit::Worker"
          && !worker.is_forward_declaration,
      "a compilation-unit class must retain its canonical identity");
  require(
      worker.lifetime == SystemVerilogClassLifetime::Automatic
          && worker.parameters.size() == 1,
      "class lifetime and parameters must remain source owned");
  require(
      worker.base && worker.base->name == "base_pkg::Base"
          && worker.base->parameter_actuals.size() == 1,
      "a parameterized base selection must remain source owned");
  require(
      worker.nested_classes.size() == 2
          && worker.nested_classes.front().canonical_identity
              == "work::$unit::Worker::ForwardNested"
          && worker.nested_classes.back().is_virtual,
      "nested classes must merge forwards and use lexical identities");
  require(
      worker.type_aliases.size() == 1
          && worker.properties.size() == 10,
      "class typedefs and declaration-ordered properties must be owned");
  require(
      worker.properties.front().visibility
              == SystemVerilogClassVisibility::Local
          && worker.properties.front().is_static
          && worker.properties[1].visibility
              == SystemVerilogClassVisibility::Protected
          && worker.properties[1].is_const
          && worker.properties[2].is_rand
          && worker.properties[3].is_randc,
      "class property qualifiers must remain explicit");
  require(
      worker.properties[4].declaration.type.domain == ValueDomain::String
          && worker.properties[5].declaration.type.systemverilog_container
          && worker.properties[6].declaration.type.named_type == "packet_t"
          && worker.properties[7].declaration.type.named_type
              == "ForwardNested",
      "string, container, aggregate, and class-handle spellings must survive");
  require(
      worker.methods.size() == 6
          && worker.methods.front().kind
              == SystemVerilogClassMethodKind::Constructor
          && worker.methods[1].is_virtual
          && worker.methods[2].is_static
          && worker.methods[3].is_pure
          && !worker.methods[3].defined
          && worker.methods[4].is_extern
          && worker.methods[4].visibility
              == SystemVerilogClassVisibility::Protected
          && worker.methods[4].arguments.front().default_value,
      "constructors, methods, prototypes, qualifiers, and defaults must survive");
  require(
      parsed.design.systemverilog_class_method_definitions.size() == 1
          && parsed.design.systemverilog_class_method_definitions.front()
                 .canonical_identity == "Worker::declared"
          && parsed.design.systemverilog_class_method_definitions.front()
                 .out_of_block_definition,
      "qualified out-of-block class method definitions must remain distinct");
  require(
      worker.methods.back().canonical_identity
              == "work::$unit::Worker::declared"
          && worker.methods.back().defined
          && !worker.methods.back().is_extern
          && worker.methods.back().out_of_block_definition,
      "a qualified definition must transactionally complete its extern prototype");
  require(
      worker.constraints.size() == 2
          && worker.constraints.front().expressions.size() == 2
          && worker.constraints.back().is_pure
          && !worker.constraints.back().defined,
      "constraint bodies and prototypes must remain source owned");
  const auto specialized = specialize_systemverilog_classes(parsed.design);
  if (!specialized.ok()) {
    throw std::runtime_error(
        specialized.diagnostics.front().code + ": "
        + specialized.diagnostics.front().message);
  }
  const auto default_worker = std::ranges::find_if(
      specialized.specializations,
      [&](const SystemVerilogClassSpecialization& specialization) {
        return specialization.declaration_identity
                   == "work::$unit::Worker"
            && std::ranges::any_of(
                specialization.parameter_values,
                [](const auto& value) {
                  return value.first == "WIDTH" && value.second == "8";
                });
      });
  require(
      default_worker != specialized.specializations.end()
          && default_worker->static_property_count == 1
          && !default_worker->base_specialization_identity.empty()
          && default_worker->properties[1].bit_width == 8
          && default_worker->methods.size() == 6,
      "default class specialization must own layout, base, and method profiles");
  require(
      std::ranges::any_of(
          specialized.specializations,
          [](const SystemVerilogClassSpecialization& specialization) {
            return specialization.declaration_identity
                       == "work::$unit::Worker"
                && std::ranges::any_of(
                    specialization.parameter_values,
                    [](const auto& value) {
                      return value.first == "WIDTH" && value.second == "4";
                    });
          })
          && std::ranges::any_of(
              specialized.specializations,
              [](const SystemVerilogClassSpecialization& specialization) {
                return specialization.declaration_identity
                           == "work::$unit::Box"
                    && std::ranges::any_of(
                        specialization.parameter_values,
                        [](const auto& value) {
                          return value.first == "COUNT"
                              && value.second == "3";
                        });
              }),
      "explicit value and type class actuals must create distinct specializations");
  const auto dispatch_base = std::ranges::find_if(
      specialized.specializations,
      [](const SystemVerilogClassSpecialization& specialization) {
        return specialization.declaration_identity
            == "work::$unit::DispatchBase";
      });
  const auto dispatch_derived = std::ranges::find_if(
      specialized.specializations,
      [](const SystemVerilogClassSpecialization& specialization) {
        return specialization.declaration_identity
            == "work::$unit::DispatchDerived";
      });
  require(
      dispatch_base != specialized.specializations.end()
          && dispatch_derived != specialized.specializations.end(),
      "virtual dispatch classes must materialize default specializations");
  require(
      dispatch_derived->instance_bit_width
              == dispatch_base->instance_bit_width
          && dispatch_derived->properties.size()
              == dispatch_base->properties.size(),
      "derived instance layouts must include inherited object properties");
  const auto base_value = std::ranges::find(
      dispatch_base->methods,
      std::string{"value"},
      &SystemVerilogClassMethodProfile::name);
  const auto derived_value = std::ranges::find(
      dispatch_derived->methods,
      std::string{"value"},
      &SystemVerilogClassMethodProfile::name);
  const auto derived_extra = std::ranges::find(
      dispatch_derived->methods,
      std::string{"extra"},
      &SystemVerilogClassMethodProfile::name);
  require(
      base_value != dispatch_base->methods.end()
          && derived_value != dispatch_derived->methods.end()
          && derived_extra != dispatch_derived->methods.end()
          && base_value->virtual_slot
          && derived_value->virtual_slot == base_value->virtual_slot
          && derived_extra->virtual_slot
          && derived_extra->virtual_slot != base_value->virtual_slot,
      "overrides must retain stable virtual slots while new methods append");
  require(
      worker.methods.front().statements.size() == 2
          && worker.methods.front().statements.front().target.text
              == "this.count"
          && worker.methods.front().statements.back().task_name
              == "super.new",
      "this and super selected-name expressions must remain source owned");
  require(
      parsed.design.units.size() == 3,
      "classes must not become top-selectable design units");
  const auto package = std::ranges::find(
      parsed.design.units,
      std::string{"class_package"},
      &DesignUnit::name);
  require(
      package != parsed.design.units.end()
          && package->systemverilog_classes.size() == 2
          && package->systemverilog_classes.front().is_virtual
          && package->systemverilog_classes.back().is_interface,
      "package virtual and interface classes must retain their kind");
  const auto module = std::ranges::find(
      parsed.design.units,
      std::string{"class_owner"},
      &DesignUnit::name);
  require(
      module != parsed.design.units.end()
          && module->systemverilog_classes.size() == 1
          && module->systemverilog_classes.front().canonical_identity
              == "work::class_owner::Local"
          && module->systemverilog_classes.front().base
          && module->systemverilog_classes.front().base
                 ->declaration_identity == "work::base_pkg::Base",
      "module classes must remain declarations on their owning unit");

  const auto invalid = parse_text(
      "invalid_classes.sv",
      R"(
class Duplicate;
endclass
class Duplicate;
endclass
class Mismatch;
endclass : Other
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      has_code("FSIM-SV-SEM-167")
          && has_code("FSIM-SV-SEM-168"),
      "duplicate and closing-name class diagnostics must be stable");

  const auto invalid_members = parse_text(
      "invalid_class_members.sv",
      R"(
class InvalidMembers;
  rand randc int conflicted;
  int duplicate, duplicate;
  pure function int not_virtual();
  constraint repeated { 1; }
  constraint repeated { 1; }
endclass
function int unqualified();
  return 0;
endfunction
)",
      Language::SystemVerilog2017);
  const auto has_member_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid_members.diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      has_member_code("FSIM-SV-SEM-169")
          && has_member_code("FSIM-SV-SEM-170")
          && has_member_code("FSIM-SV-SEM-171")
          && has_member_code("FSIM-SV-SEM-172")
          && has_member_code("FSIM-SV-SEM-173"),
      "class qualifier, member, method, and constraint diagnostics must be stable");

  const auto invalid_syntax = parse_text(
      "invalid_class_syntax.sv",
      "class MissingHeaderTerminator endclass\n",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          invalid_syntax.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-PARSE-258";
          }),
      "malformed class headers must retain a stable syntax diagnostic");

  auto unresolved = parse_text(
      "unresolved_classes.sv",
      R"(
typedef class MissingDefinition;
package left;
  class Shared;
  endclass
endpackage
package right;
  class Shared;
  endclass
endpackage
module owner;
  import left::*;
  import right::*;
  class MissingBase extends NotVisible;
  endclass
  class AmbiguousBase extends Shared;
  endclass
endmodule
)",
      Language::SystemVerilog2017);
  require(unresolved.ok(), "resolution negatives must be syntactically valid");
  std::vector<Diagnostic> unresolved_diagnostics;
  require(
      !resolve_systemverilog_classes(
          unresolved.design, unresolved_diagnostics),
      "unresolved class declarations must fail transactionally");
  const auto has_resolution_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        unresolved_diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      has_resolution_code("FSIM-SV-CLASS-001")
          && has_resolution_code("FSIM-SV-CLASS-002")
          && has_resolution_code("FSIM-SV-CLASS-003"),
      "forward, missing-base, and ambiguous-base diagnostics must be stable");

  auto invalid_inheritance = parse_text(
      "invalid_inheritance.sv",
      R"(
virtual class Parent;
  final virtual function int locked(input int value);
    return value;
  endfunction
  pure virtual function int required(input int value);
endclass
class Child extends Parent;
  virtual function int locked(input int value);
    return value;
  endfunction
endclass
class CycleA extends CycleB;
endclass
class CycleB extends CycleA;
endclass
class BadInterface implements Parent;
endclass
)",
      Language::SystemVerilog2017);
  require(invalid_inheritance.ok(), "inheritance negatives must parse");
  std::vector<Diagnostic> invalid_resolution_diagnostics;
  require(
      resolve_systemverilog_classes(
          invalid_inheritance.design,
          invalid_resolution_diagnostics),
      "inheritance negatives must resolve before legality checks");
  std::vector<Diagnostic> invalid_inheritance_diagnostics;
  require(
      !validate_systemverilog_class_inheritance(
          invalid_inheritance.design,
          invalid_inheritance_diagnostics),
      "illegal inheritance must fail transactionally");
  const auto has_inheritance_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid_inheritance_diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      has_inheritance_code("FSIM-SV-CLASS-INHERIT-001")
          && has_inheritance_code("FSIM-SV-CLASS-INHERIT-005")
          && has_inheritance_code("FSIM-SV-CLASS-INHERIT-007")
          && has_inheritance_code("FSIM-SV-CLASS-INHERIT-009"),
      "cycle, interface, final override, and pure obligations must be stable");

  auto covariant = parse_text(
      "covariant_classes.sv",
      R"(
class ResultBase;
endclass
class ResultDerived extends ResultBase;
endclass
class FactoryBase;
  virtual function ResultBase make();
    return null;
  endfunction
endclass
class FactoryDerived extends FactoryBase;
  function ResultDerived make();
    return null;
  endfunction
endclass
class Unrelated;
endclass
class InvalidFactory extends FactoryBase;
  function Unrelated make();
    return null;
  endfunction
endclass
)",
      Language::SystemVerilog2017);
  if (!covariant.ok()) {
    throw std::runtime_error(
        covariant.diagnostics.front().code + ": "
        + covariant.diagnostics.front().message);
  }
  std::vector<Diagnostic> covariant_resolution;
  require(
      resolve_systemverilog_classes(
          covariant.design, covariant_resolution),
      "covariant return class handles must resolve");
  std::vector<Diagnostic> covariant_diagnostics;
  require(
      !validate_systemverilog_class_inheritance(
          covariant.design, covariant_diagnostics),
      "an unrelated class-handle return must still reject");
  require(
      std::ranges::count(
          covariant_diagnostics,
          std::string{"FSIM-SV-CLASS-INHERIT-006"},
          &Diagnostic::code) == 1,
      "a derived class-handle return must be covariant while an unrelated return rejects");

  auto invalid_actuals = parse_text(
      "invalid_class_actuals.sv",
      R"(
class Parameterized #(parameter type T, parameter int COUNT = 1);
endclass
class UsesParameters;
  Parameterized #(.T(4), .COUNT(not_constant)) member;
endclass
)",
      Language::SystemVerilog2017);
  require(invalid_actuals.ok(), "invalid class actuals must parse");
  std::vector<Diagnostic> invalid_actual_resolution;
  require(
      resolve_systemverilog_classes(
          invalid_actuals.design, invalid_actual_resolution),
      "invalid class actuals must resolve before specialization");
  const auto invalid_actual_specialization =
      specialize_systemverilog_classes(invalid_actuals.design);
  require(
      !invalid_actual_specialization.ok()
          && std::ranges::any_of(
              invalid_actual_specialization.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-CLASS-SPEC-003";
              })
          && std::ranges::any_of(
              invalid_actual_specialization.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-CLASS-SPEC-004";
              }),
      "class type and nonconstant value actuals must reject transactionally");

  auto excessive_layout = parse_text(
      "excessive_class_layout.sv",
      R"(
class ExcessiveLayout;
  logic [9223372036854775807:0] first;
  logic [9223372036854775807:0] second;
endclass
)",
      Language::SystemVerilog2017);
  require(excessive_layout.ok(), "excessive class layouts must parse");
  std::vector<Diagnostic> excessive_resolution;
  require(
      resolve_systemverilog_classes(
          excessive_layout.design, excessive_resolution),
      "excessive class layouts must resolve before materialization");
  const auto excessive_specialization =
      specialize_systemverilog_classes(excessive_layout.design);
  require(
      !excessive_specialization.ok()
          && std::ranges::any_of(
              excessive_specialization.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-CLASS-SPEC-010";
              }),
      "host-addressable class layout overflow must reject transactionally");
}

}  // namespace fsim::tests::frontend
