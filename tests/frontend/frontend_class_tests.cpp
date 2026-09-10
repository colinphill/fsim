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

class Chained #(
    parameter type A = int,
    parameter type B = A);
  B value;
  A values[$];
endclass : Chained

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
  Box #(.T(DispatchBase)) dispatch_box;
  function new(int seed = 0);
    super.new();
    this.count = seed;
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
  Local module_handle;
  function automatic Local echo(input Local value);
    Local function_local;
    return value;
  endfunction : echo
  task automatic accept(input Local value);
    Local task_local;
  endtask : accept
  initial begin : object_scope
    Local block_local;
  end
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
      parsed.design.systemverilog_classes.size() == 5,
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
          && worker.properties.size() == 11,
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
          && worker.constraints.front().canonical_identity
              == "work::$unit::Worker::valid_count"
          && worker.constraints.back().is_pure
          && !worker.constraints.back().defined,
      "constraint bodies, identities, and prototypes must remain source owned");
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
          && worker.methods.front().statements.back().target.text
              == "@sv-property:work::$unit::Worker::count"
          && worker.methods.front().statements.back().target.operands.front()
                 .text == "this"
          && worker.methods.front().statements.front().task_name
              == "@sv-base-constructor:work::base_pkg::Base::new"
          && worker.methods.front().statements.front().task_arguments.front()
                 .text == "super",
      "this and super selections must bind to canonical class members");

  auto local_typedefs = parse_text(
      "class_method_local_typedefs.sv",
      R"(
class LocalTypedefs #(type T = int);
  function void automate();
    typedef T local_t;
    local_t value;
    begin
      T local_data;
      typedef T nested_t;
      nested_t nested_value;
    end
  endfunction
  task service();
    typedef int item_t;
    item_t item;
  endtask
endclass
class LocalTypedefUser;
  LocalTypedefs #(bit [3:0]) explicit_value;
endclass
)",
      Language::SystemVerilog2017);
  if (!local_typedefs.ok()) {
    throw std::runtime_error(
        local_typedefs.diagnostics.front().code + ": "
        + local_typedefs.diagnostics.front().message);
  }
  std::vector<Diagnostic> local_typedef_diagnostics;
  require(
      resolve_systemverilog_classes(
          local_typedefs.design, local_typedef_diagnostics),
      "class method-local typedefs must shadow class names during type "
      "resolution");
  const auto& local_typedef_class =
      local_typedefs.design.systemverilog_classes.front();
  const auto& local_function = local_typedef_class.methods.front();
  const auto& local_task = local_typedef_class.methods.back();
  require(
      local_function.type_aliases.size() == 1
          && local_function.type_aliases.front().name == "local_t"
          && local_function.type_aliases.front().type.named_type == "T"
          && local_function.variables.size() == 1
          && local_function.variables.front().type.named_type == "local_t"
          && local_function.statements.size() == 1
          && local_function.statements.front().type_aliases.size() == 1
          && local_function.statements.front().type_aliases.front().name
              == "nested_t"
          && local_function.statements.front().declarations.size() == 2
          && local_function.statements.front().declarations.back().type
                 .named_type == "nested_t"
          && local_task.type_aliases.size() == 1
          && local_task.type_aliases.front().name == "item_t"
          && local_task.variables.size() == 1
          && local_task.variables.front().type.named_type == "item_t",
      "function and task local aliases must remain source-owned in class "
      "method HIR");
  const auto local_specializations =
      specialize_systemverilog_classes(local_typedefs.design);
  const auto explicit_local_specialization = std::ranges::find_if(
      local_specializations.specializations,
      [](const SystemVerilogClassSpecialization& specialization) {
        return specialization.declaration_identity
                   == "work::$unit::LocalTypedefs"
            && !specialization.methods.empty()
            && !specialization.methods.front().variables.empty()
            && specialization.methods.front().variables.front().type.width()
                == 4;
      });
  require(
      local_specializations.ok()
          && explicit_local_specialization
              != local_specializations.specializations.end()
          && explicit_local_specialization->methods.size() == 2
          && explicit_local_specialization->methods.front()
                 .type_aliases.size() == 1
          && explicit_local_specialization->methods.front()
                 .type_aliases.front().type.width() == 4
          && explicit_local_specialization->methods.front()
                 .statements.size() == 1
          && explicit_local_specialization->methods.front()
                 .statements.front().type_aliases.size() == 1
          && explicit_local_specialization->methods.front()
                 .statements.front().type_aliases.front().type.width() == 4
          && explicit_local_specialization->methods.front()
                 .statements.front().declarations.size() == 2
          && std::ranges::all_of(
              explicit_local_specialization->methods.front()
                  .statements.front().declarations,
              [](const VariableDeclaration& declaration) {
                return declaration.type.width() == 4;
              }),
      "explicit class type actuals must specialize method-local aliases, "
      "variables, and nested statement declarations");

  auto deferred_class_types = parse_text(
      "deferred_class_types.sv",
      R"(
package deferred_class_types;
  class Base;
    function int get();
      return 1;
    endfunction
  endclass
  class Wrapper #(type BASE = Base) extends BASE;
  endclass
  class InterfaceActual;
    function void put(input int value);
    endfunction
  endclass
  class GenericPort #(type IF = Base) extends IF;
    typedef GenericPort #(IF) this_type;
    this_type implementation;
  endclass
  class ConcretePort extends GenericPort #(InterfaceActual);
    function void send();
      implementation.put(1);
    endfunction
  endclass
  typedef class AliasForward;
  typedef Wrapper #(Base) AliasForward;
  AliasForward alias_value;
  function int use_alias();
    return alias_value.get();
  endfunction
  typedef class PoolForward;
  class PoolTarget;
    function new(string name = "");
    endfunction
    function int get();
      return 1;
    endfunction
  endclass
  typedef PoolTarget PoolForward;
  typedef PoolTarget PoolList[string];
  PoolForward pool;
  PoolList pool_list;
  function int use_pool();
    pool = new("pool");
    pool_list.delete();
    return pool.get();
  endfunction
endpackage
)",
      Language::SystemVerilog2017);
  require(
      deferred_class_types.ok(),
      "deferred class-type bases and forward aliases must parse");
  std::vector<Diagnostic> deferred_class_diagnostics;
  require(
      resolve_systemverilog_classes(
          deferred_class_types.design,
          deferred_class_diagnostics),
      "a type-parameter base must defer until specialization and a "
      "forward class typedef may complete as a class-type alias");
  const auto& deferred_package =
      deferred_class_types.design.units.front();
  const auto deferred_wrapper = std::ranges::find(
      deferred_package.systemverilog_classes,
      std::string{"Wrapper"},
      &SystemVerilogClassDeclaration::name);
  require(
      deferred_wrapper
              != deferred_package.systemverilog_classes.end()
          && deferred_wrapper->base
          && deferred_wrapper->base->name == "BASE"
          && deferred_wrapper->base->declaration_identity.empty(),
      "a type-parameter base must retain its symbolic selection");
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
  require(
      module->variables.size() == 1
          && module->variables.front().name == "module_handle"
          && module->variables.front().type.systemverilog_class_declaration
              == "work::class_owner::Local"
          && std::ranges::none_of(
              module->signals,
              [](const SignalDeclaration& signal) {
                return signal.name == "module_handle";
              }),
      "resolved module class handles must be typed objects, not packed signals");
  require(
      module->functions.size() == 1
          && module->functions.front().return_type
                 .systemverilog_class_declaration
              == "work::class_owner::Local"
          && module->functions.front().arguments.front().type
                 .systemverilog_class_declaration
              == "work::class_owner::Local"
          && module->functions.front().variables.front().type
                 .systemverilog_class_declaration
              == "work::class_owner::Local"
          && module->tasks.size() == 1
          && module->tasks.front().arguments.front().type
                 .systemverilog_class_declaration
              == "work::class_owner::Local"
          && module->tasks.front().variables.front().type
                 .systemverilog_class_declaration
              == "work::class_owner::Local",
      "function/task return, argument, and local class handles must resolve canonically");
  require(
      module->processes.size() == 1
          && module->processes.front().statements.size() == 1
          && module->processes.front().statements.front().declarations.size()
              == 1
          && module->processes.front().statements.front()
                 .declarations.front().type
                 .systemverilog_class_declaration
              == "work::class_owner::Local",
      "process and leading block class-handle declarations must resolve canonically");

  auto executable_syntax = parse_text(
      "executable_class_syntax.sv",
      R"(
class ParseObject;
  int payload;
  static int count;
  function new(int seed = 0);
    payload = seed;
  endfunction
  function int value(input int offset = 0);
    return payload + offset;
  endfunction
  static function int make();
    return count;
  endfunction
  task run(input int cycles = 1);
    payload = cycles;
  endtask
endclass
module executable_class_syntax;
  ParseObject handle;
  ParseObject other;
  int result;
  initial begin
    handle = new(.seed(3));
    other = null;
    other = handle;
    result = (handle == null);
    result = handle.value(.offset(1));
    result = ParseObject::count;
    result = ParseObject::make();
    result = $cast(other, handle);
    handle.payload = other.payload;
    handle.run(.cycles(2));
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(executable_syntax.ok(), "executable class syntax must parse");
  std::vector<Diagnostic> executable_resolution;
  require(
      resolve_systemverilog_classes(
          executable_syntax.design, executable_resolution),
      "executable class syntax must resolve before lowering");
  const auto& executable_body = executable_syntax.design.units.front()
      .processes.front().statements;
  require(
      executable_body.size() == 10,
      "executable class syntax must retain ten source statements, found "
          + std::to_string(executable_body.size()));
  require(
      executable_body[0].value.kind == ExpressionKind::Call
          && executable_body[0].value.text
              == "@sv-new:work::$unit::ParseObject"
          && executable_body[0].value.call_argument_names
              == std::vector<std::string>{"seed"},
      "new and named constructor actuals must retain a distinct expression");
  require(
      executable_body[1].value.kind == ExpressionKind::Call
          && executable_body[1].value.text == "@sv-null",
      "null must retain a distinct expression");
  require(
      executable_body[2].value.text == "handle"
          && executable_body[3].value.kind == ExpressionKind::Binary
          && executable_body[3].value.text == "=="
          && executable_body[3].value.operands[1].text == "@sv-null",
      "class-handle assignment and equality must retain opaque operands");
  require(
      executable_body[4].value.kind == ExpressionKind::Call
          && executable_body[4].value.text
              == "@sv-method:work::$unit::ParseObject::value"
          && executable_body[4].value.operands.front().text == "handle"
          && executable_body[4].value.call_argument_names
              == std::vector<std::string>({"", "offset"})
          && executable_body[5].value.text
              == "@sv-static-property:work::$unit::ParseObject::count"
          && executable_body[6].value.kind == ExpressionKind::Call
          && executable_body[6].value.text
              == "@sv-static-method:work::$unit::ParseObject::make",
      "selected methods, named actuals, and class-qualified statics must remain source owned");
  require(
      executable_body[7].value.kind == ExpressionKind::Call
          && executable_body[7].value.text
              == "@sv-dollar-cast:work::$unit::ParseObject"
          && executable_body[7].value.operands.size() == 2,
      "$cast must bind to its destination handle type");
  require(
      executable_body[8].target.text
              == "@sv-property:work::$unit::ParseObject::payload"
          && executable_body[8].value.text
              == "@sv-property:work::$unit::ParseObject::payload",
      "selected properties must bind to their declaring class");
  require(
      executable_body[9].kind == StatementKind::TaskCall
          && executable_body[9].task_name
              == "@sv-task:work::$unit::ParseObject::run"
          && executable_body[9].task_argument_names
              == std::vector<std::string>({"", "cycles"})
          && executable_body[9].task_arguments.front().text == "handle"
          && executable_body[9].statements.size() == 1,
      "named class-task actuals must retain the explicit receiver: "
          + executable_body[9].task_name + "/"
          + std::to_string(executable_body[9].task_arguments.size()) + "/"
          + std::to_string(executable_body[9].task_argument_names.size()));

  auto left_compilation_unit = parse_text(
      "left_class_unit.sv",
      R"(
class Scoped;
endclass
module left_owner;
  Scoped handle;
endmodule
)",
      Language::SystemVerilog2017);
  auto right_compilation_unit = parse_text(
      "right_class_unit.sv",
      R"(
class Scoped;
endclass
module right_owner;
  Scoped handle;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      left_compilation_unit.ok() && right_compilation_unit.ok(),
      "independent class compilation units must parse");
  left_compilation_unit.design.systemverilog_classes.front().library = "work";
  left_compilation_unit.design.systemverilog_classes.front()
      .compilation_unit_identity = "cu-left";
  left_compilation_unit.design.units.front().library = "work";
  left_compilation_unit.design.units.front().compilation_unit_identity =
      "cu-left";
  right_compilation_unit.design.systemverilog_classes.front().library =
      "work";
  right_compilation_unit.design.systemverilog_classes.front()
      .compilation_unit_identity = "cu-right";
  right_compilation_unit.design.units.front().library = "work";
  right_compilation_unit.design.units.front().compilation_unit_identity =
      "cu-right";
  left_compilation_unit.design.systemverilog_classes.push_back(
      std::move(
          right_compilation_unit.design.systemverilog_classes.front()));
  left_compilation_unit.design.units.push_back(
      std::move(right_compilation_unit.design.units.front()));
  std::vector<Diagnostic> compilation_unit_diagnostics;
  require(
      resolve_systemverilog_classes(
          left_compilation_unit.design, compilation_unit_diagnostics)
          && left_compilation_unit.design.units[0].variables.front().type
                 .systemverilog_class_declaration
              == "work::$unit@cu-left::Scoped"
          && left_compilation_unit.design.units[1].variables.front().type
                 .systemverilog_class_declaration
              == "work::$unit@cu-right::Scoped",
      "class handles must resolve within their owning compilation unit");

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

  auto uvm_surface = parse_text(
      "uvm_type_surface.sv",
      R"(
package uvm_type_surface;
  typedef int uvm_field_flag_t;
  typedef enum uvm_field_flag_t {
    UVM_NONE = 'h0000000,
    UVM_ALL = 'hf000000
  } uvm_mask_e;
  parameter UVM_MASK = 'hf000000;
  const int UVM_UNBOUNDED_CONNECTIONS = -1;
  const string s_connection_error_id = "Connection Error";
  bit uvm_enabled = 1;
  string uvm_version = "2020.3.1";
  int uvm_count = 0;
  uvm_mask_e uvm_mask = UVM_ALL;

  typedef class uvm_forward_item;
  class uvm_forward_item;
  endclass

  class uvm_resource #(type T = int);
  endclass
  class uvm_byte_rsrc #(int unsigned N = 1)
      extends uvm_resource #(bit [7:0][N-1:0]);
  endclass

  virtual class uvm_wrapper #(
      type T = int,
      type U = T,
      string FIELD = "config");
    typedef uvm_wrapper #(T, U) this_type;
    typedef T types_t[$];
    typedef enum bit [1:0] {
      UVM_IDLE = 2'b00,
      UVM_BUSY = 2'b01
    } state_e;
    localparam int unsigned MAX_VALUE = '1;
    protected static this_type singleton;
    local static state_e state = UVM_IDLE;
    extern function new(string name = "");
    extern static function this_type get();
    extern virtual function T convert(input U value);
    extern task run(const ref T values[]);
    pure virtual function void required(const ref T values[]);
  endclass

  function uvm_wrapper::new(string name = "");
  endfunction : new
  function uvm_wrapper::this_type uvm_wrapper::get();
    return singleton;
  endfunction : get
  function T uvm_wrapper::convert(input U value);
    return value;
  endfunction : convert
  task uvm_wrapper::run(const ref T values[]);
  endtask : run
endpackage
)",
      Language::SystemVerilog2017);
  if (!uvm_surface.ok()) {
    throw std::runtime_error(
        uvm_surface.diagnostics.front().code + ": "
        + uvm_surface.diagnostics.front().message);
  }
  std::vector<Diagnostic> uvm_surface_resolution;
  require(
      resolve_systemverilog_classes(
          uvm_surface.design, uvm_surface_resolution),
      "UVM-shaped forward, wrapper, and extern identities must resolve");
  const auto& uvm_package = uvm_surface.design.units.front();
  const auto uvm_wrapper = std::ranges::find(
      uvm_package.systemverilog_classes,
      std::string{"uvm_wrapper"},
      &SystemVerilogClassDeclaration::name);
  const TypeAliasDeclaration* uvm_types = nullptr;
  if (uvm_wrapper != uvm_package.systemverilog_classes.end()) {
    const auto found = std::ranges::find(
        uvm_wrapper->type_aliases,
        std::string{"types_t"},
        &TypeAliasDeclaration::name);
    if (found != uvm_wrapper->type_aliases.end()) {
      uvm_types = &*found;
    }
  }
  const SystemVerilogClassMethod* uvm_constructor = nullptr;
  const SystemVerilogClassMethod* uvm_required = nullptr;
  if (uvm_wrapper != uvm_package.systemverilog_classes.end()) {
    const auto found = std::ranges::find(
        uvm_wrapper->methods,
        std::string{"new"},
        &SystemVerilogClassMethod::name);
    if (found != uvm_wrapper->methods.end()) {
      uvm_constructor = &*found;
    }
    const auto required = std::ranges::find(
        uvm_wrapper->methods,
        std::string{"required"},
        &SystemVerilogClassMethod::name);
    if (required != uvm_wrapper->methods.end()) {
      uvm_required = &*required;
    }
  }
  require(
      uvm_package.type_aliases.size() == 2
          && uvm_package.parameters.size() == 3
          && uvm_package.variables.size() == 6
          && uvm_package.variables[0].systemverilog_const
          && uvm_package.variables[1].systemverilog_const
          && uvm_package.functions.empty()
          && uvm_package.systemverilog_class_method_definitions.size()
              == 4
          && uvm_package.variables.front().initializer
          && uvm_package.variables.back().type.named_type
              == "uvm_mask_e",
      "UVM package enums, untyped parameters, and initialized variables must survive");
  const auto uvm_byte_resource = std::ranges::find(
      uvm_package.systemverilog_classes,
      std::string{"uvm_byte_rsrc"},
      &SystemVerilogClassDeclaration::name);
  require(
      uvm_wrapper != uvm_package.systemverilog_classes.end()
          && uvm_wrapper->parameters.size() == 3
          && uvm_wrapper->parameters[0].kind == ParameterKind::Type
          && uvm_wrapper->parameters[1].kind == ParameterKind::Type
          && uvm_wrapper->parameters[2].type.domain
              == ValueDomain::String
          && uvm_wrapper->type_aliases.size() == 3
          && uvm_types != nullptr
          && uvm_types->type.systemverilog_container
          && uvm_types->type.systemverilog_container->kind
              == SystemVerilogContainerKind::Queue
          && uvm_types->type.systemverilog_container
                 ->element_types.size() == 1
          && uvm_types->type.systemverilog_container
                 ->element_types.front().named_type == "T"
          && uvm_wrapper->properties.size() == 3
          && uvm_wrapper->properties[0].is_static
          && uvm_wrapper->properties[0].is_const
          && uvm_wrapper->properties[0].declaration.initializer
          && uvm_wrapper->methods.size() == 5
          && uvm_required != nullptr
          && uvm_required->arguments.size() == 1
          && uvm_required->arguments.front().reference
          && uvm_required->arguments.front().type.systemverilog_container
          && uvm_required->arguments.front().type
                 .systemverilog_container->kind
              == SystemVerilogContainerKind::DynamicArray
          && uvm_constructor != nullptr
          && uvm_constructor->kind
              == SystemVerilogClassMethodKind::Constructor
          && uvm_constructor->defined
          && uvm_constructor->out_of_block_definition
          && uvm_constructor->canonical_identity
              == "work::uvm_type_surface::uvm_wrapper::new"
          && uvm_byte_resource
              != uvm_package.systemverilog_classes.end()
          && uvm_byte_resource->base
          && uvm_byte_resource->base->parameter_actuals.size() == 1
          && uvm_byte_resource->base->parameter_actuals.front()
                 .type_actual
          && uvm_byte_resource->base->parameter_actuals.front()
                 .type_actual->systemverilog_packed_dimensions.size()
              == 2,
      "UVM class shorthand, scoped aliases/enums/queues, static data, and "
      "method qualifiers must survive (aliases="
          + std::to_string(
              uvm_wrapper == uvm_package.systemverilog_classes.end()
                  ? 0U
                  : uvm_wrapper->type_aliases.size())
          + ", methods="
          + std::to_string(
              uvm_wrapper == uvm_package.systemverilog_classes.end()
                  ? 0U
                  : uvm_wrapper->methods.size())
          + ", queue_element="
          + (uvm_types && uvm_types->type.systemverilog_container
                     && !uvm_types->type.systemverilog_container
                              ->element_types.empty()
                 ? uvm_types->type.systemverilog_container
                       ->element_types.front().named_type
                 : "<missing>")
          + ", constructor="
          + (uvm_constructor ? uvm_constructor->canonical_identity
                             : "<missing>")
          + ", constructor_kind="
          + std::to_string(
              uvm_constructor
                  ? static_cast<int>(uvm_constructor->kind)
                  : -1)
          + ", constructor_defined="
          + std::to_string(
              uvm_constructor && uvm_constructor->defined)
          + ", constructor_out_of_block="
          + std::to_string(
              uvm_constructor
                  && uvm_constructor->out_of_block_definition)
          + ", parameters="
          + std::to_string(
              uvm_wrapper == uvm_package.systemverilog_classes.end()
                  ? 0U
                  : uvm_wrapper->parameters.size())
          + ", properties="
          + std::to_string(
              uvm_wrapper == uvm_package.systemverilog_classes.end()
                  ? 0U
                  : uvm_wrapper->properties.size())
          + ", first_static="
          + std::to_string(
              uvm_wrapper != uvm_package.systemverilog_classes.end()
                  && !uvm_wrapper->properties.empty()
                  && uvm_wrapper->properties.front().is_static)
          + ")");
  const auto uvm_specialized =
      specialize_systemverilog_classes(uvm_surface.design);
  require(
      uvm_specialized.ok()
          && std::ranges::any_of(
              uvm_specialized.specializations,
              [](const SystemVerilogClassSpecialization& specialization) {
                return specialization.declaration_identity
                           == "work::uvm_type_surface::uvm_wrapper"
                    && std::ranges::any_of(
                        specialization.parameter_identity_values,
                        [](const auto& parameter) {
                          return parameter.first == "FIELD"
                              && parameter.second.ends_with(
                                  "=s6:config");
                        });
              }),
      "UVM wrapper string defaults must create deterministic specialization identities");

  auto parameterized_static_call = parse_text(
      "uvm_parameterized_static_call.sv",
      R"(
package uvm_parameterized_static_call;
  class uvm_formatter #(
      type T = int,
      int WIDTH = 32,
      string FIELD = "config");
    static function string format(input T value);
      return FIELD;
    endfunction
  endclass
  uvm_formatter #(string, 32, "config") string_formatter;
  function string stringify(input int value);
    return uvm_formatter #(int, 32, "config")::format(value);
  endfunction
endpackage
)",
      Language::SystemVerilog2017);
  require(
      parameterized_static_call.ok()
          && parameterized_static_call.design.units.size() == 1
          && parameterized_static_call.design.units.front()
                 .functions.size() == 1
          && parameterized_static_call.design.units.front()
                 .variables.size() == 1
          && parameterized_static_call.design.units.front()
                 .variables.front().type
                 .systemverilog_class_parameter_actuals.size() == 3
          && parameterized_static_call.design.units.front()
                 .variables.front().type
                 .systemverilog_class_parameter_actuals.front().type_actual
          && parameterized_static_call.design.units.front()
                 .variables.front().type
                 .systemverilog_class_parameter_actuals.front().type_actual
                 ->domain == ValueDomain::String
          && parameterized_static_call.design.units.front()
                 .functions.front().statements.size() == 1
          && parameterized_static_call.design.units.front()
                 .functions.front().statements.front().value.kind
              == ExpressionKind::Call
          && parameterized_static_call.design.units.front()
                 .functions.front().statements.front().value.text
              == "uvm_formatter#(int,32,\"config\")::format",
      "parameterized UVM static calls and string-specialized class handles "
      "must retain deterministic type, value, and string actual identities");
  std::vector<Diagnostic> parameterized_static_diagnostics;
  require(
      resolve_systemverilog_classes(
          parameterized_static_call.design,
          parameterized_static_diagnostics)
          && parameterized_static_call.design.units.front()
                 .functions.front().statements.front().value.text
              == "@sv-static-method:work::uvm_parameterized_static_call::"
                 "uvm_formatter::format",
      "parameterized static class calls must bind to canonical executable "
      "methods");

  auto registry_alias_static_call = parse_text(
      "uvm_registry_alias_static_call.sv",
      R"(
typedef class Registry;
class Product;
  typedef Registry type_id;
endclass
class Registry;
  static function Product create(input Product parent);
    Product result;
    result = new;
    return result;
  endfunction
endclass
module registry_alias_static_call;
  Product value;
  initial value = Product::type_id::create(null);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      registry_alias_static_call.ok(),
      "a UVM-shaped class-scoped registry alias must parse");
  std::vector<Diagnostic> registry_alias_static_diagnostics;
  require(
      resolve_systemverilog_classes(
          registry_alias_static_call.design,
          registry_alias_static_diagnostics),
      "a UVM-shaped registry alias static call must resolve");
  const auto& registry_alias_call =
      registry_alias_static_call.design.units.front()
          .processes.front().statements.front().value;
  require(
      registry_alias_call.text
              == "@sv-static-method:work::$unit::Registry::create"
          && registry_alias_call.call_result_width == 64
          && registry_alias_call.call_result_domain == ValueDomain::Bit2
          && registry_alias_call.operands.size() == 1
          && registry_alias_call.operands.front().call_result_width == 64,
      "registry alias calls and context-typed null actuals must retain the "
      "64-bit class-handle execution shape");

  auto uvm_parameterized_calls = parse_text(
      "uvm_parameterized_calls.sv",
      R"(
class uvm_object;
endclass
class uvm_component extends uvm_object;
endclass
class uvm_object_registry #(
    type T = uvm_object,
    string Tname = "<unknown>");
  static function T create(string name = "");
    return null;
  endfunction
endclass
class uvm_config_db #(type T = int);
  static function void set(
      uvm_component cntxt,
      string inst_name,
      string field_name,
      T value);
  endfunction
  static function bit get(
      uvm_component cntxt,
      string inst_name,
      string field_name,
      inout T value);
    return 1;
  endfunction
endclass
class UvmProduct extends uvm_object;
  typedef uvm_object_registry #(UvmProduct, "UvmProduct") type_id;
endclass
module uvm_parameterized_calls;
  UvmProduct product;
  int configured;
  initial product = UvmProduct::type_id::create("product");
  initial uvm_config_db #(int)::set(null, "*", "field", 7);
  initial if (!uvm_config_db #(int)::get(
      null, "", "field", configured)) configured = 0;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      uvm_parameterized_calls.ok(),
      "UVM parameterized registry and config calls must parse");
  std::vector<Diagnostic> uvm_parameterized_call_diagnostics;
  require(
      resolve_systemverilog_classes(
          uvm_parameterized_calls.design,
          uvm_parameterized_call_diagnostics),
      "UVM parameterized registry and config calls must resolve");
  std::vector<Diagnostic> repeated_uvm_call_diagnostics;
  require(
      resolve_systemverilog_classes(
          uvm_parameterized_calls.design,
          repeated_uvm_call_diagnostics),
      "cached UVM registry/config calls must resolve repeatedly");
  const auto& uvm_call_unit = uvm_parameterized_calls.design.units.front();
  const auto& uvm_create_call =
      uvm_call_unit.processes[0].statements.front().value;
  const auto& uvm_set_call =
      uvm_call_unit.processes[1].statements.front();
  const auto& uvm_get_call =
      uvm_call_unit.processes[2].statements.front().condition.operands.front();
  require(
      uvm_create_call.text.starts_with(
          "@sv-static-method:@uvm-registry-create:")
          && uvm_create_call.call_result_width == 64
          && uvm_set_call.task_name
              == "@sv-static-task:@uvm-config-db-set:packed:int:32"
          && uvm_set_call.task_arguments.front().call_result_width == 64
          && uvm_get_call.text
              == "@sv-static-method:@uvm-config-db-get:packed:int:32"
          && uvm_get_call.call_result_width == 1
          && uvm_get_call.operands.front().call_result_width == 64
          && uvm_get_call.call_argument_directions.back()
              == PortDirection::Inout,
      "selected UVM type actuals must specialize registry/config return and "
      "argument profiles before native lowering (create="
          + uvm_create_call.text + "/"
          + std::to_string(uvm_create_call.call_result_width)
          + ", set=" + uvm_set_call.task_name + "/"
          + std::to_string(
              uvm_set_call.task_arguments.front().call_result_width)
          + ", get=" + uvm_get_call.text + "/"
          + std::to_string(uvm_get_call.call_result_width) + "/"
          + std::to_string(
              uvm_get_call.operands.front().call_result_width) + "/"
          + std::to_string(
              uvm_get_call.operands.back().call_result_width)
          + ")");

  auto uvm_compare_call = parse_text(
      "uvm_compare_call.sv",
      R"(
class uvm_object;
  virtual function int compare(
      input uvm_object rhs,
      input uvm_object comparer);
    return 1;
  endfunction
endclass
class uvm_comparer;
  function int compare_object(
      input uvm_object lhs,
      input uvm_object rhs,
      input uvm_object comparer);
    return lhs.compare(rhs, comparer);
  endfunction
endclass
)",
      Language::SystemVerilog2017);
  if (!uvm_compare_call.ok()) {
    throw std::runtime_error(
        uvm_compare_call.diagnostics.front().code + ": "
        + uvm_compare_call.diagnostics.front().message);
  }
  std::vector<Diagnostic> uvm_compare_diagnostics;
  require(
      resolve_systemverilog_classes(
          uvm_compare_call.design, uvm_compare_diagnostics),
      "UVM-shaped object compare calls must use class overload resolution");

  auto indexed_member_calls = parse_text(
      "indexed_member_calls.sv",
      R"(
class IndexedMemberItem;
  string label;
  function int zero();
    return 0;
  endfunction
endclass
class IndexedMemberBucket;
  IndexedMemberItem queue[$];
endclass
class IndexedMemberCalls;
  IndexedMemberBucket buckets[int];
  IndexedMemberItem nested[string][string];
  IndexedMemberItem item;
  function int exercise(input int index);
    buckets[index].queue.push_back(item);
    return item.zero
        + buckets[index].queue[index].label.len()
        + buckets[index].queue[index].zero
        + nested["outer"].exists("inner");
  endfunction
endclass
class IndexedAliasOwner;
  typedef IndexedAliasOwner this_type;
  static this_type singleton;
  IndexedMemberBucket bucket;
  static function void initialize();
    singleton.bucket = new;
  endfunction
endclass
)",
      Language::SystemVerilog2017);
  require(
      indexed_member_calls.ok(),
      "member selections after container indices must parse structurally: "
          + (indexed_member_calls.diagnostics.empty()
                 ? std::string{"<missing diagnostic>"}
                 : indexed_member_calls.diagnostics.front().code + ": "
                     + indexed_member_calls.diagnostics.front().message));
  std::vector<Diagnostic> indexed_member_diagnostics;
  require(
      resolve_systemverilog_classes(
          indexed_member_calls.design,
          indexed_member_diagnostics),
      "nested container properties and indexed class string properties must "
      "resolve");

  auto indexed_aggregate_member = parse_text(
      "indexed_aggregate_member.sv",
      R"(
module IndexedAggregateMember;
  typedef struct packed { int value; } aggregate_item_t;
  aggregate_item_t items[1:0];
  int observed;
  initial observed = items[0].value;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      indexed_aggregate_member.ok(),
      "non-class member selections after aggregate indices must parse");
  std::vector<Diagnostic> indexed_aggregate_diagnostics;
  require(
      resolve_systemverilog_classes(
          indexed_aggregate_member.design,
          indexed_aggregate_diagnostics),
      "non-class indexed aggregate selections must survive class resolution");
  const auto& indexed_aggregate_value = indexed_aggregate_member.design
      .units.front().processes.front().statements.front().value;
  require(
      indexed_aggregate_value.kind == ExpressionKind::Index
          && indexed_aggregate_value.text == "index.value",
      "non-class @sv-select markers must fold back to indexed aggregate "
      "member syntax");

  auto out_of_block_argument_types = parse_text(
      "out_of_block_argument_types.sv",
      R"(
class OutOfBlockParent;
  function new(string name = "");
  endfunction
endclass
class OutOfBlockItem;
  OutOfBlockParent parent;
endclass
class OutOfBlockMap;
  extern task apply(OutOfBlockItem rw);
endclass
task OutOfBlockMap::apply(OutOfBlockItem rw);
  if (rw.parent == null)
    rw.parent = new("parent");
endtask
)",
      Language::SystemVerilog2017);
  require(
      out_of_block_argument_types.ok(),
      "out-of-block task argument types must parse");
  std::vector<Diagnostic> out_of_block_argument_diagnostics;
  require(
      resolve_systemverilog_classes(
          out_of_block_argument_types.design,
          out_of_block_argument_diagnostics),
      "out-of-block task arguments and their nested allocation targets must "
      "retain canonical class types");

  auto function_statements = parse_text(
      "class_function_statements.sv",
      R"(
class FunctionStatementBase;
  int observed;
  virtual function void note(input int value = 1);
    observed = value;
  endfunction
  static function void record(input int value = 1);
  endfunction
endclass
class FunctionStatementDerived extends FunctionStatementBase;
  function void invoke();
    note();
    this.note(2);
    FunctionStatementBase::record();
    this.srandom(3);
  endfunction
endclass
)",
      Language::SystemVerilog2017);
  require(
      function_statements.ok(),
      "void class functions used as statements must parse");
  std::vector<Diagnostic> function_statement_diagnostics;
  require(
      resolve_systemverilog_classes(
          function_statements.design,
          function_statement_diagnostics),
      "inherited, selected, and static void-function statements must resolve");
  const auto function_statement_derived = std::ranges::find(
      function_statements.design.systemverilog_classes,
      std::string{"FunctionStatementDerived"},
      &SystemVerilogClassDeclaration::name);
  require(
      function_statement_derived
              != function_statements.design.systemverilog_classes.end()
          && function_statement_derived->methods.size() == 1
          && function_statement_derived->methods.front().statements.size() == 4
          && function_statement_derived->methods.front()
                 .statements[0].task_name.starts_with("@sv-task:")
          && function_statement_derived->methods.front()
                 .statements[1].task_name.starts_with("@sv-task:")
          && function_statement_derived->methods.front()
                 .statements[2].task_name.starts_with("@sv-static-task:")
          && function_statement_derived->methods.front()
                 .statements[3].task_name == "@sv-object-srandom",
      "function statements must retain executable class-call identities");

  auto recursive_class_task = parse_text(
      "recursive_class_task.sv",
      R"(
class RecursiveTask;
  task visit(input int depth);
    int pending[$];
    if (pending.size() != 0)
      pending.delete();
    if (depth > 0)
      visit(depth - 1);
  endtask
  task start();
    visit(1);
    visit(2);
  endtask
endclass
)",
      Language::SystemVerilog2017);
  require(
      recursive_class_task.ok(),
      "recursive class tasks and local container calls must parse");
  std::vector<Diagnostic> recursive_task_diagnostics;
  require(
      resolve_systemverilog_classes(
          recursive_class_task.design,
          recursive_task_diagnostics),
      "recursive class-task profiles must resolve without eager body "
      "expansion");
  const auto& recursive_methods =
      recursive_class_task.design.systemverilog_classes.front().methods;
  require(
      recursive_methods.size() == 2
          && recursive_methods.back().statements.size() == 2
          && !recursive_methods.back().statements.front()
                  .class_method_arguments.empty()
          && recursive_methods.back().statements.front()
                 .statements.empty()
          && recursive_methods.back().statements.back()
                 .statements.empty(),
      "class-task declarations must retain resolved call profiles without "
      "eager body expansion");

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

  auto interface_inheritance = parse_verilog(
      SourceText { "interface_inheritance_2023.sv",
      R"(
interface class RootContract;
  parameter int ID = 1;
  typedef int item_t;
  pure virtual function int apply(input int value);
endclass
interface class LeftContract extends RootContract;
  pure virtual function int left(input int value);
endclass
interface class RightContract;
  pure virtual function int right(input int value);
endclass
interface class CombinedContract extends LeftContract, RightContract;
endclass
class ConcreteContract implements CombinedContract;
  function int apply(input int value);
    return value;
  endfunction
  function int left(input int value);
    return value;
  endfunction
  function int right(input int value);
    return value;
  endfunction
endclass
)" },
      StandardRevision::SystemVerilog2023);
  require(
      interface_inheritance.ok(),
      "2023 interface-class multiple inheritance must parse");
  std::vector<Diagnostic> interface_resolution;
  require(
      resolve_systemverilog_classes(
          interface_inheritance.design, interface_resolution),
      "2023 interface-class multiple inheritance must resolve");
  std::vector<Diagnostic> interface_legality;
  require(
      validate_systemverilog_class_inheritance(
          interface_inheritance.design, interface_legality),
      "compatible interface inheritance and implementation must be legal");
  const auto& combined = interface_inheritance.design
      .systemverilog_classes[3];
  const auto& root_contract = interface_inheritance.design
      .systemverilog_classes.front();
  require(
      combined.is_interface && combined.base
          && combined.base->name == "LeftContract"
          && combined.extended_interfaces.size() == 1
          && combined.extended_interfaces.front().name == "RightContract"
          && root_contract.properties.size() == 1
          && root_contract.properties.front().is_parameter,
      "interface bases and body parameters must remain explicit");

  auto revised_inheritance_negatives = parse_verilog(
      SourceText { "invalid_interface_inheritance_2023.sv",
      R"(
class Plain;
endclass
interface class Contract;
  pure virtual function int apply(input int value);
endclass
interface class BadExtends extends Plain;
endclass
interface class BadImplements implements Contract;
endclass
class BadClassExtends extends Contract;
endclass
interface class BadMembers;
  int value;
  function int body(input int value);
    return value;
  endfunction
  constraint invalid_constraint { value > 0; }
endclass
class StaticVirtual;
  static virtual function int invalid(input int value);
    return value;
  endfunction
endclass
class VirtualBase;
  virtual function int transform(input int value);
    return value;
  endfunction
endclass
class BadOverride extends VirtualBase;
  function int transform(input int renamed);
    return renamed;
  endfunction
endclass
class DuplicateMethods;
  function int duplicate(input int value);
    return value;
  endfunction
  function int duplicate(input byte value);
    return value;
  endfunction
endclass
interface class FirstConflict;
  pure virtual function int collide(input int value);
endclass
interface class SecondConflict;
  pure virtual function int collide(input int value);
endclass
interface class UnresolvedConflict extends FirstConflict, SecondConflict;
endclass
interface class FirstDeclarations;
  typedef int item_t;
  parameter int ID = 1;
endclass
interface class SecondDeclarations;
  typedef int item_t;
  parameter int ID = 2;
endclass
interface class UnresolvedDeclarations
    extends FirstDeclarations, SecondDeclarations;
endclass
interface class ResolvedDeclarations
    extends FirstDeclarations, SecondDeclarations;
  typedef int item_t;
  parameter int ID = 3;
endclass
)" },
      StandardRevision::SystemVerilog2023);
  require(
      revised_inheritance_negatives.ok(),
      "2023 inheritance legality negatives must parse before validation");
  std::vector<Diagnostic> revised_resolution;
  require(
      resolve_systemverilog_classes(
          revised_inheritance_negatives.design, revised_resolution),
      "2023 inheritance legality negatives must resolve names");
  std::vector<Diagnostic> revised_legality;
  require(
      !validate_systemverilog_class_inheritance(
          revised_inheritance_negatives.design, revised_legality),
      "2023 inheritance legality negatives must reject transactionally");
  for (const auto code : {
           "FSIM-SV-CLASS-INHERIT-002",
           "FSIM-SV-CLASS-INHERIT-004",
           "FSIM-SV-CLASS-INHERIT-008",
           "FSIM-SV-CLASS-INHERIT-010",
           "FSIM-SV-CLASS-INHERIT-011",
           "FSIM-SV-CLASS-INHERIT-012",
           "FSIM-SV-CLASS-INHERIT-013" }) {
    require(
        std::ranges::any_of(
            revised_legality,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "2023 class and interface inheritance diagnostic must be stable");
  }

  const auto nested_interface = parse_verilog(
      SourceText { "nested_interface_class_2023.sv",
      R"(
class Outer;
  interface class Nested;
  endclass
endclass
)" },
      StandardRevision::SystemVerilog2023);
  require(
      std::ranges::any_of(
          nested_interface.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-245";
          }),
      "an interface class nested in a class must reject at its declaration");

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
class AliasFactoryBase;
  typedef ResultBase result_type;
  virtual function result_type make();
    return null;
  endfunction
endclass
class AliasFactoryDerived extends AliasFactoryBase;
  typedef ResultDerived result_type;
  function result_type make();
    return null;
  endfunction
endclass
class StaticFactoryHiding extends FactoryBase;
  static function Unrelated make();
    return null;
  endfunction
endclass
virtual class ScopedAliasInterface;
  typedef int unsigned size_t;
  pure virtual function size_t size();
endclass
class ScopedAliasImplementation extends ScopedAliasInterface;
  function ScopedAliasImplementation::size_t size();
    return 0;
  endfunction
endclass
virtual class GenericPureInterface #(type T = int);
  pure virtual function void collect(T value, ref T values[$]);
endclass
class GenericPureImplementation
    extends GenericPureInterface #(string);
  function void collect(string value, ref string values[$]);
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

  auto allowed_modes = parse_text(
      "allowed_class_modes.sv",
      R"(
class ModeBase;
  rand int public_value;
  protected rand int protected_value;
  local rand int local_value;
  constraint public_rule { public_value >= 0; }
  protected constraint protected_rule { protected_value >= 0; }
  local constraint local_rule { local_value >= 0; }
  function int local_modes();
    return this.local_value.rand_mode()
        + this.local_rule.constraint_mode();
  endfunction
endclass
class ModeDerived extends ModeBase;
  function int inherited_modes();
    return this.protected_value.rand_mode()
        + this.protected_rule.constraint_mode();
  endfunction
endclass
module allowed_mode_user;
  ModeDerived object;
  int result;
  initial result = object.public_value.rand_mode()
      + object.public_rule.constraint_mode();
endmodule
)",
      Language::SystemVerilog2017);
  require(allowed_modes.ok(), "valid randomization mode access must parse");
  std::vector<Diagnostic> allowed_mode_diagnostics;
  require(
      resolve_systemverilog_classes(
          allowed_modes.design, allowed_mode_diagnostics),
      "local owner, protected derived, and public external mode access must resolve");

  auto denied_modes = parse_text(
      "denied_class_modes.sv",
      R"(
class ModeBase;
  protected rand int protected_value;
  local rand int local_value;
  protected constraint protected_rule { protected_value >= 0; }
  local constraint local_rule { local_value >= 0; }
endclass
class ModeDerived extends ModeBase;
  function int denied_local_mode();
    return this.local_rule.constraint_mode();
  endfunction
endclass
module denied_mode_user;
  ModeDerived object;
  int result;
  initial result = object.protected_value.rand_mode()
      + object.local_value.rand_mode()
      + object.protected_rule.constraint_mode()
      + object.local_rule.constraint_mode();
endmodule
)",
      Language::SystemVerilog2017);
  require(denied_modes.ok(), "invalid randomization mode access must parse");
  std::vector<Diagnostic> denied_mode_diagnostics;
  require(
      !resolve_systemverilog_classes(
          denied_modes.design, denied_mode_diagnostics)
          && std::ranges::count(
              denied_mode_diagnostics,
              std::string{"FSIM-SV-CLASS-021"},
              &Diagnostic::code) == 5,
      "local derived and nonpublic external mode access must reject precisely");

  auto invalid_randomization_calls = parse_text(
      "invalid_randomization_calls.sv",
      R"(
class RandomizationCalls;
  int fixed_value;
  rand int choice;
  constraint legal { choice >= 0; }
endclass
module invalid_randomization_user;
  RandomizationCalls object;
  int result;
  initial result = object.randomize(fixed_value)
      + object.randomize(null, choice)
      + object.fixed_value.rand_mode()
      + object.choice.rand_mode(0, 1)
      + object.missing.constraint_mode();
endmodule
)",
      Language::SystemVerilog2017);
  require(
      invalid_randomization_calls.ok(),
      "invalid randomization selections must remain syntactically valid");
  std::vector<Diagnostic> invalid_randomization_diagnostics;
  require(
      !resolve_systemverilog_classes(
          invalid_randomization_calls.design,
          invalid_randomization_diagnostics)
          && std::ranges::count(
              invalid_randomization_diagnostics,
              std::string{"FSIM-SV-CLASS-019"},
              &Diagnostic::code) == 1
          && std::ranges::count(
              invalid_randomization_diagnostics,
              std::string{"FSIM-SV-CLASS-020"},
              &Diagnostic::code) == 3
          && std::ranges::count(
              invalid_randomization_diagnostics,
              std::string{"FSIM-SV-CLASS-022"},
              &Diagnostic::code) == 1,
      "nonrandom, mixed-null, and invalid mode selections must diagnose precisely");

  const auto malformed_constraint = parse_text(
      "malformed_randomization_constraint.sv",
      R"(
class MalformedConstraint;
  rand int value;
  constraint broken value > 0;
endclass
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          malformed_constraint.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-PARSE-264";
          }),
      "a missing constraint opening brace must retain its cataloged parse diagnostic");
}

}  // namespace fsim::tests::frontend
