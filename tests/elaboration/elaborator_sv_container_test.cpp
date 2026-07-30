// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <iostream>
#include <variant>
#include <vector>

namespace fsim::tests::elaboration {

void test_systemverilog_container_lowering() {
  using namespace fsim::runtime::simir;
  const auto parsed = fsim::frontend::parse_text(
      "container-lowering.sv",
      R"(
module container_lowering #(
    parameter int STATIC_LEFT = 3);
  typedef logic signed [31:0] key_t;
  int values[];
  byte pending[$:2];
  byte lookup[key_t];
  logic [7:0] fixed_down[STATIC_LEFT:1];
  bit [3:0] fixed_up[-1:1];

  function automatic int count(input byte source[$:2]);
    return source.size();
  endfunction

  function automatic int isolated_count(input byte source[key_t]);
    byte copy[key_t];
    copy = source;
    copy.delete(3);
    return copy.size();
  endfunction

  task automatic mutate(inout byte target[$:2]);
    target.push_back(4);
    #1;
    target.pop_front();
  endtask

  task automatic mutate_lookup(inout byte target[key_t]);
    target[-1] = 9;
    #1;
    target.delete(3);
  endtask

  function automatic byte copied_static(
      input logic [7:0] source[STATIC_LEFT:1]);
    logic [7:0] copy[STATIC_LEFT:1];
    copy = source;
    copy[2] = 8'h99;
    return copy[3];
  endfunction

  task automatic mutate_static(
      inout logic [7:0] target[STATIC_LEFT:1]);
    target[2] = 8'h22;
    #1;
    target[1] = 8'h11;
  endtask

  initial begin
    key_t key;
    values = new[2];
    values[0] = 7;
    pending.push_back(1);
    pending.push_back(2);
    lookup[3] = 30;
    lookup[-1] = 10;
    assert ($isunknown(fixed_down[2]));
    fixed_down[3] = 8'h33;
    fixed_up[-1] = 4'ha;
    fixed_up[1] = 4'hc;
    assert (copied_static(fixed_down) == 8'h33);
    assert ($isunknown(fixed_down[2]));
    assert (fixed_up[-1] == 4'ha);
    assert (fixed_up[0] == 0);
    assert (lookup.size() == 2);
    assert (isolated_count(lookup) == 1);
    assert (lookup.size() == 2);
    assert (lookup.exists(3) == 1);
    assert (lookup[4] == 0);
    assert (lookup.first(key) == 1);
    assert (key == -1);
    assert (lookup.next(key) == 1);
    assert (key == 3);
    mutate(pending);
    mutate_lookup(lookup);
    mutate_static(fixed_down);
    assert (values[0] == 7);
    assert (count(pending) == 2);
    assert (fixed_down[3] == 8'h33);
    assert (fixed_down[2] == 8'h22);
    assert (fixed_down[1] == 8'h11);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "container_lowering");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  assert(elaborated.design->container_objects().size() == 5);
  const auto& process = elaborated.design->processes().front();
  assert(process.container_register_count != 0);
  assert(!process.debug_container_locals.empty());
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        return std::holds_alternative<ResizeContainer>(operation);
      }));
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        return std::holds_alternative<ContainerExists>(operation);
      }));
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        return std::holds_alternative<TraverseContainer>(operation);
      }));
  const auto values =
      elaborated.design->container_objects()[0].id;
  const auto pending =
      elaborated.design->container_objects()[1].id;
  const auto lookup =
      elaborated.design->container_objects()[2].id;
  const auto fixed_down =
      elaborated.design->container_objects()[3].id;
  const auto fixed_up =
      elaborated.design->container_objects()[4].id;
  auto interpreter = elaborated.design->create_interpreter();
  const auto result = interpreter->run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 3);
  const auto& values_result =
      interpreter->container_object_value(values);
  const auto& pending_result =
      interpreter->container_object_value(pending);
  const auto& lookup_result =
      interpreter->container_object_value(lookup);
  const auto& fixed_down_result =
      interpreter->container_object_value(fixed_down);
  const auto& fixed_up_result =
      interpreter->container_object_value(fixed_up);
  assert(
      values_result.elements.size() == 2
      && values_result.elements[0].low_word().aval == 7
      && pending_result.elements.size() == 2
      && pending_result.elements[0].low_word().aval == 2
      && pending_result.elements[1].low_word().aval == 4
      && lookup_result.keys.size() == 1
      && lookup_result.keys[0].low_word().aval
          == UINT64_C(0xffffffff)
      && lookup_result.elements[0].low_word().aval == 9
      && fixed_down_result.type.fixed
      && fixed_down_result.type.index_left == 3
      && fixed_down_result.type.index_right == 1
      && fixed_down_result.elements.size() == 3
      && fixed_down_result.elements[0].low_word().aval == 0x33
      && fixed_down_result.elements[1].low_word().aval == 0x22
      && fixed_down_result.elements[2].low_word().aval == 0x11
      && fixed_up_result.type.fixed
      && fixed_up_result.type.index_left == -1
      && fixed_up_result.type.index_right == 1
      && fixed_up_result.elements[0].low_word().aval == 0xa
      && fixed_up_result.elements[1].low_word().aval == 0
      && fixed_up_result.elements[2].low_word().aval == 0xc);

  const auto port_parsed = fsim::frontend::parse_text(
      "container-ports.sv",
      R"(
module static_port_leaf #(
    parameter int LEFT = 3,
    parameter int RIGHT = 0) (
    input logic signed [7:0] source[LEFT:RIGHT],
    output logic signed [7:0] result[LEFT:RIGHT],
    inout bit [3:0] shared[-1:1]);
  initial begin
    #1;
    assert (source[LEFT] == 8'h31);
    assert (source[RIGHT] == 8'h04);
    result = source;
    result[LEFT] = source[LEFT] + 8'h01;
    result[RIGHT] = source[RIGHT] + 8'h02;
    shared[-1] = 4'ha;
    shared[1] = 4'hc;
  end
endmodule

module static_port_mid #(
    parameter int HIGH = 3,
    parameter int LOW = 0) (
    input logic signed [7:0] source[HIGH:LOW],
    output logic signed [7:0] result[HIGH:LOW],
    inout bit [3:0] shared[-1:1]);
  generate
    if (HIGH == 3) begin : generated
      static_port_leaf #(
          .LEFT(HIGH), .RIGHT(LOW)) child(
          .source(source), .result(result), .shared(shared));
    end
  endgenerate
endmodule

module non_ansi_port_leaf(result);
  parameter int LEFT = 2;
  output logic [7:0] result[LEFT:0];
  initial begin
    #1;
    result[LEFT] = 8'h5a;
  end
endmodule

module static_port_top;
  logic signed [7:0] source[3:0];
  logic signed [7:0] result[3:0];
  bit [3:0] shared[-1:1];
  logic [7:0] non_ansi_result[2:0];
  static_port_mid #(
      .HIGH(3), .LOW(0)) mid(
      .source(source), .result(result), .shared(shared));
  non_ansi_port_leaf non_ansi(
      .result(non_ansi_result));
  initial begin
    source[3] = 8'h31;
    source[0] = 8'h04;
    #2;
    assert (result[3] == 8'h32);
    assert (result[0] == 8'h06);
    assert (shared[-1] == 4'ha);
    assert (shared[0] == 0);
    assert (shared[1] == 4'hc);
    assert (non_ansi_result[2] == 8'h5a);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(port_parsed.ok());
  const auto port_elaborated = fsim::elaboration::elaborate(
      port_parsed.design, "static_port_top");
  if (!port_elaborated.ok()) {
    for (const auto& diagnostic : port_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(port_elaborated.ok());
  assert(
      port_elaborated.design->container_objects().size() == 4);
  const auto paths =
      port_elaborated.design->container_paths();
  assert(
      std::ranges::any_of(
          paths,
          [](const auto& path) {
            return path.first
                == "static_port_top.mid.source";
          })
      && std::ranges::any_of(
          paths,
          [](const auto& path) {
            return path.first
                == "static_port_top.mid.generated.child.result";
          }));
  const auto source_id =
      port_elaborated.design->find_container(
          "static_port_top.source");
  const auto child_source_id =
      port_elaborated.design->find_container(
          "static_port_top.mid.generated.child.source");
  assert(source_id && child_source_id);
  assert(*source_id == *child_source_id);
  auto port_interpreter =
      port_elaborated.design->create_interpreter();
  const auto port_result = port_interpreter->run();
  assert(
      port_result.status
          == fsim::runtime::RunStatus::completed
      && port_result.time == 2);
  const auto result_id =
      port_elaborated.design->find_container(
          "static_port_top.result");
  const auto shared_id =
      port_elaborated.design->find_container(
          "static_port_top.shared");
  assert(result_id && shared_id);
  const auto& result_value =
      port_interpreter->container_object_value(*result_id);
  const auto& shared_value =
      port_interpreter->container_object_value(*shared_id);
  assert(
      result_value.elements[0].low_word().aval == 0x32
      && result_value.elements[3].low_word().aval == 0x06
      && shared_value.elements[0].low_word().aval == 0xa
      && shared_value.elements[1].low_word().aval == 0
      && shared_value.elements[2].low_word().aval == 0xc);

  const auto dynamic_port_parsed = fsim::frontend::parse_text(
      "dynamic-container-ports.sv",
      R"(
module dynamic_port_leaf #(
    parameter int LIMIT = 3,
    parameter type KEY = logic signed [3:0]) (
    input int source[],
    output logic [7:0] result[$],
    inout bit bounded[$:LIMIT],
    inout logic [15:0] scores[KEY],
    inout int work[]);
  initial begin
    KEY cursor;
    #1;
    assert (source.size() == 2);
    assert (source[0] == 11);
    result.push_back(8'h21);
    result.push_back(8'h22);
    bounded.push_back(1);
    bounded.push_front(0);
    scores[-1] = 16'h1234;
    scores[2] = 16'h5678;
    assert (scores.first(cursor) == 1);
    assert (cursor == -1);
    assert (scores.next(cursor) == 1);
    assert (cursor == 2);
    work = new[3];
    work[0] = 31;
    work[2] = 33;
  end
endmodule

module dynamic_port_mid #(
    parameter int MAXIMUM = 3,
    parameter type INDEX = logic signed [3:0]) (
    input int source[],
    output logic [7:0] result[$],
    inout bit bounded[$:MAXIMUM],
    inout logic [15:0] scores[INDEX],
    inout int work[]);
  generate
    if (MAXIMUM == 3) begin : generated
      dynamic_port_leaf #(
          .LIMIT(MAXIMUM), .KEY(INDEX)) child(
          .source(source),
          .result(result),
          .bounded(bounded),
          .scores(scores),
          .work(work));
    end
  endgenerate
endmodule

module dynamic_port_top;
  typedef logic signed [3:0] key_t;
  int source[];
  logic [7:0] result[$];
  bit bounded[$:3];
  logic [15:0] scores[key_t];
  int work[];
  dynamic_port_mid #(
      .MAXIMUM(3), .INDEX(key_t)) mid(
      .source(source),
      .result(result),
      .bounded(bounded),
      .scores(scores),
      .work(work));
  initial begin
    source = new[2];
    source[0] = 11;
    source[1] = 12;
    #2;
    assert (result.size() == 2);
    assert (result[0] == 8'h21);
    assert (result[1] == 8'h22);
    assert (bounded.size() == 2);
    assert (bounded[0] == 0);
    assert (bounded[1] == 1);
    assert (scores.size() == 2);
    assert (scores[-1] == 16'h1234);
    assert (scores[2] == 16'h5678);
    assert (work.size() == 3);
    assert (work[0] == 31);
    assert (work[2] == 33);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(dynamic_port_parsed.ok());
  const auto dynamic_port_elaborated =
      fsim::elaboration::elaborate(
          dynamic_port_parsed.design, "dynamic_port_top");
  if (!dynamic_port_elaborated.ok()) {
    for (const auto& diagnostic :
         dynamic_port_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(dynamic_port_elaborated.ok());
  assert(
      dynamic_port_elaborated.design
          ->container_objects().size() == 5);
  const auto dynamic_source =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.source");
  const auto nested_dynamic_source =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.mid.generated.child.source");
  const auto dynamic_bounded =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.bounded");
  assert(
      dynamic_source && nested_dynamic_source
      && *dynamic_source == *nested_dynamic_source
      && dynamic_bounded);
  const auto& bounded_info =
      dynamic_port_elaborated.design->container_objects().at(
          *dynamic_bounded);
  assert(
      bounded_info.type.queue
      && bounded_info.type.maximum_elements
      && *bounded_info.type.maximum_elements == 4);
  auto dynamic_port_interpreter =
      dynamic_port_elaborated.design->create_interpreter();
  const auto dynamic_port_result =
      dynamic_port_interpreter->run();
  assert(
      dynamic_port_result.status
          == fsim::runtime::RunStatus::completed
      && dynamic_port_result.time == 2);
  const auto dynamic_result =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.result");
  const auto dynamic_scores =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.scores");
  const auto dynamic_work =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.work");
  assert(dynamic_result && dynamic_scores && dynamic_work);
  assert(
      dynamic_port_interpreter
              ->container_object_value(*dynamic_result)
              .elements.size()
          == 2
      && dynamic_port_interpreter
              ->container_object_value(*dynamic_scores)
              .elements.size()
          == 2
      && dynamic_port_interpreter
              ->container_object_value(*dynamic_work)
              .elements.size()
          == 3);

  const auto invalid = fsim::frontend::parse_text(
      "container-invalid-lowering.sv",
      R"(
module container_invalid_lowering;
  typedef struct packed {
    logic [3:0] value;
  } pair_t;
  byte composite_key[pair_t];
  pair_t composite_element[int];
  byte lookup[int];
  byte dynamic[];
  byte fixed[1:0];
  byte too_large[0:4096];
  int runtime_bound;
  byte nonconstant[runtime_bound:0];
  int result;
  initial begin
    lookup.push_back(1);
    result = lookup.sort();
    lookup[0] <= 1;
    dynamic.delete(0);
    fixed.delete();
    fixed = new[2];
    $readmemh("invalid.hex", dynamic);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto rejected = fsim::elaboration::elaborate(
      invalid.design, "container_invalid_lowering");
  assert(!rejected.ok());
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-003"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-008"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-013"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-018"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-019"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-009"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-020"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-021"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-014"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVMEMORY-003"));

  const auto invalid_ports = fsim::frontend::parse_text(
      "container-port-invalid.sv",
      R"(
module bad_input(
    input logic [7:0] memory[3:0]);
  initial memory[3] = 8'hff;
endmodule

module incompatible(
    input logic [7:0] memory[0:3]);
endmodule

module output_driver(
    output logic [7:0] memory[3:0]);
  initial memory[3] = 8'h01;
endmodule

module input_forward(
    input logic [7:0] memory[3:0]);
  output_driver illegal_descendant(.memory(memory));
endmodule

module bad_port_top;
  logic [7:0] memory[3:0];
  bad_input input_child(.memory(memory));
  incompatible wrong_range(.memory(memory));
  incompatible expression_actual(.memory(memory[3]));
  incompatible unknown_actual(.memory(missing));
  output_driver first(.memory(memory));
  output_driver second(.memory(memory));
  input_forward forward(.memory(memory));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid_ports.ok());
  const auto rejected_ports = fsim::elaboration::elaborate(
      invalid_ports.design, "bad_port_top");
  assert(!rejected_ports.ok());
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-005"));
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-006"));
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-007"));
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-008"));
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-009"));

  const auto invalid_dynamic_ports =
      fsim::frontend::parse_text(
          "dynamic-container-port-invalid.sv",
          R"(
module dynamic_input(input int value[]);
  initial value = new[1];
endmodule

module dynamic_output(output int value[]);
  initial value = new[1];
endmodule

module dynamic_forward(input int value[]);
  dynamic_output illegal_descendant(.value(value));
endmodule

module dynamic_accept(input int value[]);
endmodule

module queue_accept(input int value[$]);
endmodule

module bounded_accept #(
    parameter int LIMIT = 3) (
    input int value[$:LIMIT]);
endmodule

module associative_accept(
    input int value[logic signed [3:0]]);
endmodule

module byte_dynamic_accept(input byte value[]);
endmodule

module bad_dynamic_port_top;
  int dynamic_value[];
  int queue_value[$];
  int bounded_value[$:2];
  int associative_value[logic signed [4:0]];
  logic [7:0] four_state_value[];
  dynamic_accept expression_actual(
      .value(dynamic_value[0]));
  dynamic_accept unknown_actual(.value(missing));
  queue_accept wrong_kind(.value(dynamic_value));
  bounded_accept #(.LIMIT(3)) wrong_bound(
      .value(bounded_value));
  associative_accept wrong_index(
      .value(associative_value));
  byte_dynamic_accept wrong_element(
      .value(four_state_value));
  dynamic_output first(.value(dynamic_value));
  dynamic_output second(.value(dynamic_value));
  dynamic_input read_only(.value(dynamic_value));
  dynamic_forward forward(.value(dynamic_value));
endmodule
)",
          fsim::frontend::Language::SystemVerilog2017);
  assert(invalid_dynamic_ports.ok());
  const auto rejected_dynamic_ports =
      fsim::elaboration::elaborate(
          invalid_dynamic_ports.design,
          "bad_dynamic_port_top");
  assert(!rejected_dynamic_ports.ok());
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-005"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-006"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-007"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-008"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-009"));

  const auto mixed_parent = fsim::frontend::parse_text(
      "mixed-container-port.vhd",
      R"(
entity mixed_port_top is
end entity;

architecture rtl of mixed_port_top is
  signal source : std_logic_vector(7 downto 0);
  signal result : std_logic_vector(7 downto 0);
  signal shared : std_logic_vector(3 downto 0);
  component static_port_leaf is
    port (
      source : in std_logic_vector(7 downto 0);
      result : out std_logic_vector(7 downto 0);
      shared : inout std_logic_vector(3 downto 0));
  end component;
begin
  child: static_port_leaf
    port map (
      source => source,
      result => result,
      shared => shared);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(mixed_parent.ok());
  auto mixed_design = port_parsed.design;
  mixed_design.units.insert(
      mixed_design.units.end(),
      mixed_parent.design.units.begin(),
      mixed_parent.design.units.end());
  const std::vector<fsim::elaboration::Binding> bindings{
      {"mixed_port_top.child",
       "sv:work.static_port_leaf",
       std::nullopt}};
  const auto mixed_rejected = fsim::elaboration::elaborate(
      mixed_design,
      "vhdl:work.mixed_port_top(rtl)",
      bindings);
  assert(!mixed_rejected.ok());
  assert(has_diagnostic(
      mixed_rejected, "FSIM-ELAB-SVPORT-004"));

  const auto mixed_dynamic_parent =
      fsim::frontend::parse_text(
          "mixed-dynamic-container-port.vhd",
          R"(
entity mixed_dynamic_port_top is
end entity;

architecture rtl of mixed_dynamic_port_top is
  signal source : integer;
  signal result : std_logic_vector(7 downto 0);
  signal bounded : bit;
  signal scores : std_logic_vector(15 downto 0);
  signal work : integer;
  component dynamic_port_leaf is
    port (
      source : in integer;
      result : out std_logic_vector(7 downto 0);
      bounded : inout bit;
      scores : inout std_logic_vector(15 downto 0);
      work : inout integer);
  end component;
begin
  child: dynamic_port_leaf
    port map (
      source => source,
      result => result,
      bounded => bounded,
      scores => scores,
      work => work);
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(mixed_dynamic_parent.ok());
  auto mixed_dynamic_design = dynamic_port_parsed.design;
  mixed_dynamic_design.units.insert(
      mixed_dynamic_design.units.end(),
      mixed_dynamic_parent.design.units.begin(),
      mixed_dynamic_parent.design.units.end());
  const std::vector<fsim::elaboration::Binding>
      dynamic_bindings{
          {"mixed_dynamic_port_top.child",
           "sv:work.dynamic_port_leaf",
           std::nullopt}};
  const auto mixed_dynamic_rejected =
      fsim::elaboration::elaborate(
          mixed_dynamic_design,
          "vhdl:work.mixed_dynamic_port_top(rtl)",
          dynamic_bindings);
  assert(!mixed_dynamic_rejected.ok());
  assert(has_diagnostic(
      mixed_dynamic_rejected, "FSIM-ELAB-SVPORT-004"));
}

}  // namespace fsim::tests::elaboration
