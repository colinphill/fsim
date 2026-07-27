// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace {

volatile std::sig_atomic_t restored_interrupt_count = 0;

extern "C" void record_restored_interrupt(int) {
  restored_interrupt_count = 1;
}

class InterruptingOutputBuffer final : public std::stringbuf {
 protected:
  std::streamsize xsputn(
      const char* value,
      const std::streamsize count) override {
    const auto written = std::stringbuf::xsputn(value, count);
    if (!raised_ && str().find("(fsim) ") != std::string::npos) {
      raised_ = true;
      (void)std::raise(SIGINT);
    }
    return written;
  }

 private:
  bool raised_{};
};

}  // namespace

int main() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-application-test-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  const auto source = directory / "tb.sv";
  {
    std::ofstream output(source);
    output << R"(
module child(input logic value, output logic inverted);
  assign inverted = ~value;
endmodule

module tb;
  logic q;
  logic child_y;
  bit two_state;
  child u_child(.value(q), .inverted(child_y));
  initial begin
    logic local_state = 1'b0;
    q = local_state;
    #2 local_state = 1'b1;
    q = local_state;
    #1 $finish;
  end
endmodule
)";
  }
  const auto scheduled_source = directory / "scheduled.sv";
  {
    std::ofstream output(scheduled_source);
    output << R"(
module scheduled;
  logic q;
  initial begin
    q <= 1'b0;
    q <= #2 1'b1;
    #3 $finish;
  end
endmodule

module scheduled_overflow;
  logic q;
  initial begin
    #1 q <= #18446744073709551615 1'b1;
  end
endmodule
)";
  }
  const auto sensitivity_source = directory / "sensitivity.sv";
  {
    std::ofstream output(sensitivity_source);
    output << R"(
module sensitivity;
  logic trigger;
  logic observed;
  logic dynamic_observed;
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 trigger = 1'b0;
    #1 $finish;
  end
  always @(posedge trigger) observed <= trigger;
  initial begin
    @(posedge trigger);
    dynamic_observed = trigger;
    @(negedge trigger) dynamic_observed = trigger;
  end
endmodule
)";
  }
  const auto vhdl_wait_source = directory / "vhdl_wait.vhd";
  {
    std::ofstream output(vhdl_wait_source);
    output << R"(
entity vhdl_wait is
end entity;
architecture rtl of vhdl_wait is
  signal q : std_logic;
begin
  worker: process
  begin
    q <= '0';
    wait for 1 ns;
    q <= '1';
    wait for 1 ns;
  end process;
end architecture;
)";
  }
  const auto wildcard_source = directory / "wildcard.sv";
  {
    std::ofstream output(wildcard_source);
    output << R"(
module wildcard_app;
  logic a;
  logic q;
  logic y;
  logic latched;
  always @* q = a;
  always_comb y = ~q;
  always_latch if (a) latched = q;
  initial begin
    a = 1'b0;
    #1 a = 1'b1;
    #1 $finish;
  end
endmodule
)";
  }
  const auto case_source = directory / "case.sv";
  {
    std::ofstream output(case_source);
    output << R"(
module case_app;
  logic [1:0] selector;
  logic [1:0] result;
  always_comb case (selector)
    2'b00: result = 2'b00;
    2'b01, 2'b10: result = 2'b01;
    2'bx0: result = 2'b10;
    2'bz1: result = 2'b11;
    default: result = 2'b00;
  endcase
  initial begin
    selector = 2'b00;
    #1 selector = 2'b10;
    #1 selector = 2'bx0;
    #1 selector = 2'bz1;
    #1 selector = 2'b11;
    #1 $finish;
  end
endmodule
)";
  }
  const auto conditional_source = directory / "conditional.sv";
  {
    std::ofstream output(conditional_source);
    output << R"(
module conditional_app;
  logic select;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic [3:0] result;
  always_comb result = select ? lhs : rhs;
  initial begin
    lhs = 4'b101z;
    rhs = 4'b100z;
    select = 1'b0;
    #1 select = 1'b1;
    #1 select = 1'bx;
    #1 select = 1'bz;
    #1 $finish;
  end
endmodule
)";
  }
  const auto comparison_source = directory / "comparison.sv";
  {
    std::ofstream output(comparison_source);
    output << R"(
module comparison_app;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic neq;
  logic lt;
  logic le;
  logic gt;
  logic ge;
  logic logical_not;
  always_comb begin
    neq = lhs != rhs;
    lt = lhs < rhs;
    le = lhs <= rhs;
    gt = lhs > rhs;
    ge = lhs >= rhs;
    logical_not = !lhs;
  end
  initial begin
    lhs = 4'b0010;
    rhs = 4'b0011;
    #1 lhs = 4'b0000;
    rhs = 4'b0000;
    #1 lhs = 4'b00x0;
    rhs = 4'b0011;
    #1 lhs = 4'b01z0;
    #1 $finish;
  end
endmodule
)";
  }
  const auto logical_source = directory / "logical.sv";
  {
    std::ofstream output(logical_source);
    output << R"(
module logical_app;
  logic [3:0] lhs;
  logic [1:0] rhs;
  logic [2:0] amount;
  logic conjunction;
  logic disjunction;
  logic reduced_and;
  logic reduced_or;
  logic reduced_xor;
  logic [3:0] shifted_left;
  logic [3:0] shifted_right;
  always_comb begin
    conjunction = lhs && rhs;
    disjunction = lhs || rhs;
    reduced_and = &lhs;
    reduced_or = |lhs;
    reduced_xor = ^lhs;
    shifted_left = lhs << amount;
    shifted_right = lhs >> amount;
  end
  initial begin
    lhs = 4'b0000;
    rhs = 2'bx1;
    amount = 3'b001;
    #1 lhs = 4'b00x0;
    rhs = 2'b00;
    amount = 3'b0x1;
    #1 rhs = 2'b01;
    amount = 3'b100;
    #1 lhs = 4'b0010;
    rhs = 2'bzz;
    #1 rhs = 2'b01;
    amount = 3'b001;
    #1 $finish;
  end
endmodule
)";
  }
  const auto arithmetic_source = directory / "arithmetic.sv";
  {
    std::ofstream output(arithmetic_source);
    output << R"(
module arithmetic_app;
  logic [7:0] lhs;
  logic [7:0] rhs;
  logic [7:0] difference;
  logic [7:0] product;
  logic [7:0] quotient;
  logic [7:0] remainder;
  logic [7:0] positive;
  logic [7:0] negative;
  logic signed [7:0] signed_lhs;
  logic signed [7:0] signed_rhs;
  logic signed [7:0] signed_sum;
  logic signed [7:0] signed_difference;
  logic signed [7:0] signed_product;
  logic signed [7:0] signed_quotient;
  logic signed [7:0] signed_remainder;
  logic signed_less;
  always_comb begin
    difference = lhs - rhs;
    product = lhs * rhs;
    quotient = lhs / rhs;
    remainder = lhs % rhs;
    positive = +lhs;
    negative = -lhs;
    signed_sum = signed_lhs + signed_rhs;
    signed_difference = signed_lhs - signed_rhs;
    signed_product = signed_lhs * signed_rhs;
    signed_quotient = signed_lhs / signed_rhs;
    signed_remainder = signed_lhs % signed_rhs;
    signed_less = signed_lhs < signed_rhs;
  end
  initial begin
    lhs = 8'b11001000;
    rhs = 8'b00000111;
    signed_lhs = 8'b11111011;
    signed_rhs = 8'b00000011;
    #1 lhs = 8'b10x01000;
    signed_lhs = 8'b00000101;
    signed_rhs = 8'b11111101;
    #1 lhs = 8'b11001000;
    rhs = 8'b00000000;
    #1 rhs = 8'b00000111;
    #1 $finish;
  end
endmodule
)";
  }
  const auto select_concat_source =
      directory / "select_concat.sv";
  {
    std::ofstream output(select_concat_source);
    output << R"(
module select_concat_app;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic selected_descending;
  logic selected_ascending;
  logic selected_local;
  logic [3:0] descending_part;
  logic [3:0] ascending_part;
  logic [8:0] joined;
  logic [7:0] assigned;
  logic [5:2] local_assigned;
  always_comb begin
    logic [5:2] local_copy;
    local_copy = descending[15:12];
    selected_descending = descending[10];
    selected_ascending = ascending[2];
    selected_local = local_copy[3];
    descending_part = descending[15:12];
    ascending_part = ascending[2:5];
    joined = {
      descending[15:12], descending[10], ascending[4:7]
    };
  end
  initial begin
    logic [5:2] assignment_local;
    descending = 8'b10xz0110;
    ascending = 8'b01zx1100;
    assigned[0] <= 1'b1;
    assigned = 8'b00000000;
    assigned[1] = 1'b1;
    assigned[7:4] = 4'b10xz;
    assigned[3:2] <= #1 2'b11;
    assignment_local = 4'b0000;
    assignment_local[3] = 1'b1;
    assignment_local[5:4] = 2'bxz;
    local_assigned = assignment_local;
    #1 descending = 8'bz10100x1;
    ascending = 8'b1100xz01;
    #1 $finish;
  end
endmodule
)";
  }
  const auto vhdl_select_concat_source =
      directory / "vhdl_select_concat.vhd";
  {
    std::ofstream output(vhdl_select_concat_source);
    output << R"(
entity vhdl_select_concat_app is
end entity;

architecture rtl of vhdl_select_concat_app is
  signal descending : std_logic_vector(7 downto 4);
  signal ascending : std_logic_vector(2 to 5);
  signal selected_descending : std_logic;
  signal selected_concurrent : std_logic;
  signal selected_ascending : std_logic;
  signal selected_local : std_logic;
  signal descending_part : std_logic_vector(1 downto 0);
  signal ascending_part : std_logic_vector(1 downto 0);
  signal joined : std_logic_vector(5 downto 0);
  signal assigned : std_logic_vector(7 downto 0);
  signal local_assigned : std_logic_vector(5 downto 2);
begin
  descending <= "1XZ0";
  ascending <= "01Z1";
  selected_concurrent <= descending(5);

  observe: process(descending, ascending)
    variable local_copy : std_logic_vector(9 downto 8);
    variable assignment_local : std_logic_vector(5 downto 2);
  begin
    local_copy := descending(7 downto 6);
    selected_descending <= descending(5);
    selected_ascending <= ascending(4);
    selected_local <= local_copy(8);
    descending_part <= descending(7 downto 6);
    ascending_part <= ascending(3 to 4);
    joined <= descending(7 downto 6) & "10" & ascending(4 to 5);
    assigned <= "00000000";
    assigned(1) <= '1';
    assigned(7 downto 4) <= "10XZ";
    assigned(3 downto 2) <= "11" after 5 ns;
    assignment_local := "0000";
    assignment_local(3) := '1';
    assignment_local(5 downto 4) := "XZ";
    local_assigned <= assignment_local;
  end process;
end architecture;
)";
  }
  const auto vhdl_signed_source =
      directory / "vhdl_signed.vhd";
  {
    std::ofstream output(vhdl_signed_source);
    output << R"(
entity vhdl_signed_app is
end entity;

architecture rtl of vhdl_signed_app is
  signal lhs : signed(7 downto 0);
  signal rhs : signed(7 downto 0);
  signal sum : signed(7 downto 0);
  signal difference : signed(7 downto 0);
  signal product : signed(7 downto 0);
  signal quotient : signed(7 downto 0);
  signal remainder : signed(7 downto 0);
  signal modulo : signed(7 downto 0);
  signal less : std_logic;
begin
  lhs <= "11111011";
  rhs <= "00000011";
  calculate: process(lhs, rhs)
  begin
    sum <= lhs + rhs;
    difference <= lhs - rhs;
    product <= lhs * rhs;
    quotient <= lhs / rhs;
    remainder <= lhs rem rhs;
    modulo <= lhs mod rhs;
    less <= lhs < rhs;
  end process;
end architecture;
)";
  }
  const auto conditional_statement_source =
      directory / "conditional_statements.sv";
  {
    std::ofstream output(conditional_statement_source);
    output << R"(
module conditional_statement_app;
  logic [3:0] selector;
  logic zero_case;
  logic one_x_case;
  logic unknown_case;
  logic [3:0] nested_case;
  always_comb begin
    if (selector) begin
      if (selector[3])
        nested_case = 4'b0001;
      else
        nested_case = 4'b0010;
    end else begin
      nested_case = 4'b0011;
    end
  end
  initial begin
    if (4'b0000)
      zero_case = 1'b1;
    else
      zero_case = 1'b0;
    if (4'bx001)
      one_x_case = 1'b1;
    else
      one_x_case = 1'b0;
    if (4'bx000)
      unknown_case = 1'b1;
    else
      unknown_case = 1'b0;
    selector = 4'b0000;
    #1 selector = 4'b0010;
    #1 selector = 4'b1000;
    #1 $finish;
  end
endmodule
)";
  }
  const auto vhdl_conditional_statement_source =
      directory / "vhdl_conditional_statements.vhd";
  {
    std::ofstream output(vhdl_conditional_statement_source);
    output << R"(
entity vhdl_conditional_statement_app is
end entity;

architecture rtl of vhdl_conditional_statement_app is
  signal trigger : std_logic;
  signal true_case : boolean;
  signal elsif_case : boolean;
  signal nested_case : boolean;
  signal boolean_expression_case : boolean;
begin
  choose: process(trigger)
  begin
    if true then
      true_case <= true;
    else
      true_case <= false;
    end if;
    if false then
      elsif_case <= false;
    elsif true /= false then
      elsif_case <= true;
    else
      elsif_case <= false;
    end if;
    if 1 = 1 then
      if false then
        nested_case <= false;
      else
        nested_case <= true;
      end if;
    else
      nested_case <= false;
    end if;
    if (not false) and (true nand false)
       and (false nor false) and (true xnor true)
       and (true /= false) then
      boolean_expression_case <= true;
    else
      boolean_expression_case <= false;
    end if;
  end process;
end architecture;
)";
  }
  const auto partial_group_source = directory / "partial_group.sv";
  {
    std::ofstream output(partial_group_source);
    output << R"(
module partial_group;
  logic narrow;
  logic [64:0] wide;
  initial narrow = 1'b1;
  initial begin
    wide = 65'b1;
    #1 $finish;
  end
endmodule
)";
  }
  const auto assertion_source = directory / "assertion.vhd";
  {
    std::ofstream output(assertion_source);
    output << R"(
entity assertion_test is
end entity;
architecture rtl of assertion_test is
  signal trigger : std_logic;
begin
  check: process(trigger)
  begin
    assert 0 = 1 report "cross-engine mismatch" severity failure;
  end process;
end architecture;
)";
  }
  const auto provenance_source = directory / "provenance.sv";
  const auto unused_source = directory / "unused.sv";
  const auto write_provenance_source =
      [&](const std::string_view comment) {
        std::ofstream output(provenance_source);
        output << R"(
module provenance;
  logic q;
  initial begin
    q = 1'b1;
    #1 $finish;
  end
endmodule
)";
        output << "// " << comment << '\n';
      };
  const auto write_unused_source =
      [&](const std::string_view comment) {
        std::ofstream output(unused_source);
        output << R"(
module unused;
  logic q;
  initial q = 1'b0;
endmodule
)";
        output << "// " << comment << '\n';
      };
  write_provenance_source("top revision 1");
  write_unused_source("unused revision 1");
  const auto systemc_source = directory / "model.cpp";
  {
    std::ofstream output(systemc_source);
    output << R"(
#include <fsim/systemc_abi.h>

namespace {
const fsim_sc_host_v1* active_host = nullptr;

void* create_model(void*, const char*, fsim_sc_handle_v1) {
  return reinterpret_cast<void*>(0x1);
}
void destroy_model(void*, void*) {}

fsim_sc_status_v1 elaborate_bridge(
    void*,
    const char* instance_name,
    fsim_sc_handle_v1 module,
    fsim_sc_handle_v1,
    void** result) {
  if (active_host == nullptr || instance_name == nullptr
      || *instance_name == '\0' || result == nullptr) {
    return FSIM_SC_INVALID_ARGUMENT;
  }
  fsim_sc_handle_v1 value = 0;
  fsim_sc_handle_v1 inverted = 0;
  fsim_sc_handle_v1 child = 0;
  auto status = active_host->register_port(
      active_host->context,
      module,
      "value",
      FSIM_SC_INPUT,
      FSIM_SC_LOGIC4,
      1,
      &value);
  if (status != FSIM_SC_OK) {
    return status;
  }
  status = active_host->register_port(
      active_host->context,
      module,
      "inverted",
      FSIM_SC_OUTPUT,
      FSIM_SC_LOGIC4,
      1,
      &inverted);
  if (status != FSIM_SC_OK) {
    return status;
  }
  status = active_host->register_foreign_child(
      active_host->context, module, "u_hdl", &child);
  if (status != FSIM_SC_OK) {
    return status;
  }
  status = active_host->connect_foreign_port(
      active_host->context,
      child,
      "value",
      FSIM_SC_INPUT,
      FSIM_SC_LOGIC4,
      1,
      value);
  if (status != FSIM_SC_OK) {
    return status;
  }
  status = active_host->connect_foreign_port(
      active_host->context,
      child,
      "inverted",
      FSIM_SC_OUTPUT,
      FSIM_SC_LOGIC4,
      1,
      inverted);
  if (status != FSIM_SC_OK) {
    return status;
  }
  *result = new int{42};
  return FSIM_SC_OK;
}

void destroy_bridge(void*, void* object) {
  delete static_cast<int*>(object);
}
}

extern "C" fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
  if (host == nullptr || registrar == nullptr
      || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || host->struct_size < sizeof(fsim_sc_host_v1)
      || registrar->struct_size < sizeof(fsim_sc_registrar_v1)
      || registrar->register_factory == nullptr
      || registrar->register_elaboration_factory == nullptr) {
    return FSIM_SC_ABI_MISMATCH;
  }
  active_host = host;
  const auto status = registrar->register_factory(
      registrar->context,
      "model",
      create_model,
      destroy_model,
      nullptr);
  if (status != FSIM_SC_OK) {
    return status;
  }
  return registrar->register_elaboration_factory(
      registrar->context,
      "bridge",
      elaborate_bridge,
      destroy_bridge,
      nullptr);
}
)";
  }

  const auto systemc_boundary_source =
      directory / "systemc_boundary.sv";
  {
    std::ofstream output(systemc_boundary_source);
    output << R"(
module systemc_hdl_child(
  input logic value,
  output logic inverted
);
  assign inverted = ~value;
endmodule

module systemc_host;
  logic value;
  logic inverted;
  bridge_placeholder u_bridge(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule
)";
  }

  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "application-test";
  config.project.top = "sv:work.tb";
  config.project.time_resolution = "1ns";
  config.build.cache_path = directory / "cache";
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  fsim::project::SourceSet systemc_sources;
  systemc_sources.language = fsim::project::Language::systemc;
  systemc_sources.standard = "2023-subset";
  systemc_sources.files.push_back(systemc_source);
  systemc_sources.include_directories.emplace_back(
      std::filesystem::path{FSIM_TEST_SOURCE_DIR} / "include");
  config.source_sets.push_back(std::move(systemc_sources));

  fsim::diagnostic::Engine diagnostics;
  auto checked = fsim::app::check_project(config, diagnostics);
  assert(checked);
  assert(checked->source_count == 2);
  assert(checked->hdl_sources.size() == 1);
  assert(checked->hdl_sources.front().path == source);
  assert(checked->hdl_sources.front().content_digest.size() == 64);
  assert(checked->parsed.units.size() == 2);

  auto first = fsim::app::build_project(config, diagnostics);
  assert(first);
  assert(first->systemc_plugins.size() == 1);
  assert(!first->cache_hit);
  auto second = fsim::app::build_project(config, diagnostics);
  assert(second);
  assert(second->cache_hit);
  assert(first->systemc_hierarchy == second->systemc_hierarchy);
  const auto child_q = first->design.find_signal("tb.u_child.value");
  assert(child_q);
  const auto paths = first->design.signal_paths();
  assert(std::find_if(
             paths.begin(), paths.end(),
             [](const auto& path) {
               return path.first == "tb.u_child.value";
         })
         != paths.end());

  auto hdl_systemc_config = config;
  hdl_systemc_config.project.top = "sv:work.systemc_host";
  hdl_systemc_config.build.cache_path =
      directory / "hdl-systemc-cache";
  hdl_systemc_config.source_sets.front().files = {
      systemc_boundary_source};
  hdl_systemc_config.bindings = {
      {"systemc_host.u_bridge",
       "systemc:models.bridge",
       std::nullopt},
      {"systemc_host.u_bridge.u_hdl",
       "sv:work.systemc_hdl_child",
       std::nullopt},
  };
  fsim::diagnostic::Engine hdl_systemc_diagnostics;
  auto hdl_systemc_project = fsim::app::build_project(
      hdl_systemc_config, hdl_systemc_diagnostics);
  assert(hdl_systemc_project);
  assert(hdl_systemc_project->systemc_hierarchy);
  assert(
      hdl_systemc_project->design.systemc_instances().size()
      == 1);
  const auto hdl_systemc_value =
      hdl_systemc_project->design.find_signal("value");
  const auto hdl_systemc_child_value =
      hdl_systemc_project->design.find_signal(
          "systemc_host.u_bridge.u_hdl.value");
  const auto hdl_systemc_inverted =
      hdl_systemc_project->design.find_signal("inverted");
  assert(
      hdl_systemc_value && hdl_systemc_child_value
      && hdl_systemc_inverted);
  assert(*hdl_systemc_value == *hdl_systemc_child_value);
  auto hdl_systemc_interpreter =
      hdl_systemc_project->design.create_interpreter();
  const auto hdl_systemc_result =
      hdl_systemc_interpreter->run();
  assert(
      hdl_systemc_result.status
      == fsim::runtime::RunStatus::stopped);
  assert(
      hdl_systemc_interpreter
          ->signal_value(*hdl_systemc_inverted)
          .to_msb_string()
      == "0");

  auto legacy_systemc_config = hdl_systemc_config;
  legacy_systemc_config.build.cache_path =
      directory / "legacy-systemc-cache";
  legacy_systemc_config.bindings.front().target =
      "systemc:models.model";
  fsim::diagnostic::Engine legacy_systemc_diagnostics;
  assert(!fsim::app::build_project(
      legacy_systemc_config, legacy_systemc_diagnostics));
  assert(std::any_of(
      legacy_systemc_diagnostics.diagnostics().begin(),
      legacy_systemc_diagnostics.diagnostics().end(),
      [](const fsim::diagnostic::Diagnostic& diagnostic) {
        return diagnostic.code == "FSIM-SC-A003";
      }));

  auto systemc_hdl_config = hdl_systemc_config;
  systemc_hdl_config.project.top = "systemc:models.bridge";
  systemc_hdl_config.build.cache_path =
      directory / "systemc-hdl-cache";
  systemc_hdl_config.bindings = {
      {"bridge.u_hdl",
       "sv:work.systemc_hdl_child",
       std::nullopt},
  };
  fsim::diagnostic::Engine systemc_hdl_diagnostics;
  auto systemc_hdl_project = fsim::app::build_project(
      systemc_hdl_config, systemc_hdl_diagnostics);
  assert(systemc_hdl_project);
  assert(systemc_hdl_project->systemc_hierarchy);
  assert(
      systemc_hdl_project->design.systemc_instances().size()
      == 1);
  assert(
      systemc_hdl_project->design.systemc_instances().front().instance
      == "bridge");
  const auto systemc_root_value =
      systemc_hdl_project->design.find_signal("bridge.value");
  const auto systemc_root_inverted =
      systemc_hdl_project->design.find_signal("bridge.inverted");
  const auto systemc_root_child_inverted =
      systemc_hdl_project->design.find_signal(
          "bridge.u_hdl.inverted");
  assert(
      systemc_root_value && systemc_root_inverted
      && systemc_root_child_inverted);
  assert(*systemc_root_inverted == *systemc_root_child_inverted);
  auto systemc_hdl_interpreter =
      systemc_hdl_project->design.create_interpreter();
  systemc_hdl_interpreter->deposit_signal(
      *systemc_root_value,
      fsim::runtime::PackedLogic4::from_msb_string("1"));
  const auto systemc_hdl_result =
      systemc_hdl_interpreter->run();
  assert(
      systemc_hdl_result.status
      == fsim::runtime::RunStatus::completed);
  assert(
      systemc_hdl_interpreter
          ->signal_value(*systemc_root_inverted)
          .to_msb_string()
      == "0");

  struct CapturedSimulation {
    fsim::runtime::RunResult result;
    std::vector<std::tuple<
        fsim::runtime::simir::SignalId,
        std::string,
        fsim::runtime::SimulationTick,
        std::uint64_t>> changes;
    std::vector<std::string> final_values;
    std::string normalized_vcd;
    fsim::app::NativeCacheStatistics native_cache;
    std::size_t compiled_processes{};
    std::size_t compiled_modules{};
    std::size_t process_count{};
  };
  const auto capture_simulation =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine,
          const std::optional<fsim::runtime::SimulationTick> until =
              std::nullopt) {
        CapturedSimulation captured;
        captured.process_count = project.design.processes().size();
        fsim::app::Simulation candidate(
            std::move(project), config.run.max_deltas, engine);
        captured.compiled_processes =
            candidate.compiled_process_count();
        captured.compiled_modules =
            candidate.compiled_module_count();
        captured.native_cache =
            candidate.native_cache_statistics();
        std::ostringstream vcd_output;
        fsim::runtime::VcdWriter vcd(vcd_output, "1ns", 256);
        std::vector<fsim::runtime::VcdSignal> vcd_signals;
        vcd_signals.reserve(candidate.design().signals().size());
        for (const auto& signal : candidate.design().signals()) {
          vcd_signals.push_back(
              vcd.declare_signal(signal.name, signal.width));
        }
        vcd.begin(candidate.now());
        for (const auto& signal : candidate.design().signals()) {
          vcd.change(
              vcd_signals.at(signal.id),
              candidate.read_signal(signal.id));
        }
        candidate.set_signal_change_hook(
            [&captured, &vcd, &vcd_signals](
                const fsim::runtime::simir::SignalId signal,
                const fsim::runtime::PackedLogic4& value,
                const fsim::runtime::SimulationTick time,
                const std::uint64_t delta) {
              captured.changes.emplace_back(
                  signal, value.to_msb_string(), time, delta);
              vcd.set_time(time);
              vcd.change(vcd_signals.at(signal), value);
            });
        captured.result = candidate.run(until);
        vcd.flush();
        captured.normalized_vcd = vcd_output.str();
        captured.final_values.reserve(
            candidate.design().signals().size());
        for (const auto& signal : candidate.design().signals()) {
          captured.final_values.push_back(
              candidate.read_signal(signal.id).to_msb_string());
        }
        return captured;
      };
  const auto compare_captures =
      [](const CapturedSimulation& reference,
         const CapturedSimulation& hybrid) {
        assert(reference.result.status == hybrid.result.status);
        assert(reference.result.time == hybrid.result.time);
        assert(reference.result.delta == hybrid.result.delta);
        assert(
            reference.result.callbacks_executed
            == hybrid.result.callbacks_executed);
        assert(reference.changes == hybrid.changes);
        assert(reference.final_values == hybrid.final_values);
        assert(reference.normalized_vcd == hybrid.normalized_vcd);
        assert(
            reference.normalized_vcd.find("$version fsim $end")
            != std::string::npos);
        assert(reference.compiled_processes == 0);
        assert(reference.compiled_modules == 0);
        assert(
            reference.native_cache
            == fsim::app::NativeCacheStatistics{});
#if defined(FSIM_HAS_LLVM)
        assert(hybrid.compiled_processes > 0);
        assert(
            hybrid.compiled_processes
            <= hybrid.process_count);
        assert(hybrid.compiled_modules > 0);
        assert(
            hybrid.compiled_modules
            <= hybrid.compiled_processes);
#else
        assert(hybrid.compiled_processes == 0);
        assert(hybrid.compiled_modules == 0);
#endif
      };

  auto differential_config = config;
  std::erase_if(
      differential_config.source_sets,
      [](const fsim::project::SourceSet& source_set) {
        return source_set.language
            == fsim::project::Language::systemc;
      });
  differential_config.build.cache_path =
      directory / "differential-cache";
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    differential_config.build.optimization = optimization;
    fsim::diagnostic::Engine differential_diagnostics;
    auto reference_project = fsim::app::build_project(
        differential_config, differential_diagnostics);
    auto hybrid_project = fsim::app::build_project(
        differential_config, differential_diagnostics);
    assert(reference_project);
    assert(hybrid_project);
    const auto reference = capture_simulation(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto hybrid = capture_simulation(
        std::move(*hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(reference, hybrid);
#if defined(FSIM_HAS_LLVM)
    assert(hybrid.process_count == 2);
    assert(
        hybrid.compiled_processes
        == hybrid.process_count);
    assert(hybrid.compiled_modules == 2);
#endif

    auto warm_project = fsim::app::build_project(
        differential_config, differential_diagnostics);
    assert(warm_project);
    const auto warm = capture_simulation(
        std::move(*warm_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(reference, warm);
#if defined(FSIM_HAS_LLVM)
    assert(
        hybrid.native_cache.misses
        == hybrid.compiled_modules);
    assert(
        hybrid.native_cache.stores
        == hybrid.compiled_modules);
    assert(hybrid.native_cache.hits == 0);
    assert(
        warm.native_cache.hits
        == warm.compiled_modules);
    assert(warm.native_cache.misses == 0);
    assert(warm.native_cache.load_failures == 0);
    assert(warm.native_cache.store_failures == 0);
#else
    assert(
        hybrid.native_cache
        == fsim::app::NativeCacheStatistics{});
    assert(
        warm.native_cache
        == fsim::app::NativeCacheStatistics{});
#endif
  }

  struct CapturedAssertion {
    fsim::runtime::simir::ProcessId process{};
    fsim::runtime::simir::InstructionIndex instruction{};
    fsim::runtime::simir::AssertionSeverity severity{
        fsim::runtime::simir::AssertionSeverity::error};
    fsim::runtime::simir::SourceLocation source;
    std::string message;
    std::size_t compiled_processes{};
    std::size_t compiled_modules{};
  };
  auto assertion_config = config;
  assertion_config.project.name = "assertion-differential";
  assertion_config.project.top = "vhdl:work.assertion_test(rtl)";
  assertion_config.build.cache_path = directory / "assertion-cache";
  assertion_config.source_sets.clear();
  fsim::project::SourceSet assertion_sources;
  assertion_sources.language = fsim::project::Language::vhdl;
  assertion_sources.standard = "2008";
  assertion_sources.library = "work";
  assertion_sources.files.push_back(assertion_source);
  assertion_config.source_sets.push_back(std::move(assertion_sources));
  const auto capture_assertion =
      [&](const fsim::project::Optimization optimization,
          const fsim::app::SimulationEngine engine) {
        assertion_config.build.optimization = optimization;
        fsim::diagnostic::Engine assertion_diagnostics;
        auto project =
            fsim::app::build_project(assertion_config, assertion_diagnostics);
        assert(project);
        fsim::app::Simulation simulation(
            std::move(*project), assertion_config.run.max_deltas, engine);
        CapturedAssertion captured;
        captured.compiled_processes =
            simulation.compiled_process_count();
        captured.compiled_modules = simulation.compiled_module_count();
        try {
          (void)simulation.run();
        } catch (const fsim::runtime::simir::AssertionError& error) {
          captured.process = error.process();
          captured.instruction = error.instruction();
          captured.severity = error.severity();
          captured.source = error.source();
          captured.message = error.what();
          return captured;
        }
        throw std::runtime_error("false assertion completed successfully");
      };
  const auto assertion_reference = capture_assertion(
      fsim::project::Optimization::o0,
      fsim::app::SimulationEngine::interpreter);
  const auto compare_assertion =
      [&](const CapturedAssertion& candidate) {
        assert(candidate.process == assertion_reference.process);
        assert(candidate.instruction == assertion_reference.instruction);
        assert(candidate.severity == assertion_reference.severity);
        assert(candidate.source.path == assertion_reference.source.path);
        assert(candidate.source.line == assertion_reference.source.line);
        assert(candidate.source.column == assertion_reference.source.column);
        assert(candidate.message == assertion_reference.message);
      };
  assert(
      assertion_reference.severity
      == fsim::runtime::simir::AssertionSeverity::failure);
  assert(assertion_reference.source.path == assertion_source.string());
  assert(assertion_reference.source.line == 9);
  assert(assertion_reference.source.column == 5);
  assert(
      assertion_reference.message.find("cross-engine mismatch")
      != std::string::npos);
  assert(assertion_reference.compiled_processes == 0);
  assert(assertion_reference.compiled_modules == 0);
  const auto assertion_o0 = capture_assertion(
      fsim::project::Optimization::o0,
      fsim::app::SimulationEngine::compiled);
  const auto assertion_o2 = capture_assertion(
      fsim::project::Optimization::o2,
      fsim::app::SimulationEngine::compiled);
  compare_assertion(assertion_o0);
  compare_assertion(assertion_o2);
#if defined(FSIM_HAS_LLVM)
  assert(assertion_o0.compiled_processes == 1);
  assert(assertion_o0.compiled_modules == 1);
  assert(assertion_o2.compiled_processes == 1);
  assert(assertion_o2.compiled_modules == 1);
#else
  assert(assertion_o0.compiled_processes == 0);
  assert(assertion_o0.compiled_modules == 0);
  assert(assertion_o2.compiled_processes == 0);
  assert(assertion_o2.compiled_modules == 0);
#endif

  auto scheduled_config = config;
  scheduled_config.project.name = "scheduled-write-test";
  scheduled_config.project.top = "sv:work.scheduled";
  scheduled_config.build.optimization =
      fsim::project::Optimization::o2;
  scheduled_config.build.cache_path =
      directory / "scheduled-write-cache";
  scheduled_config.source_sets.clear();
  fsim::project::SourceSet scheduled_sources;
  scheduled_sources.language =
      fsim::project::Language::system_verilog;
  scheduled_sources.standard = "2017";
  scheduled_sources.library = "work";
  scheduled_sources.files.push_back(scheduled_source);
  scheduled_config.source_sets.push_back(
      std::move(scheduled_sources));
  fsim::diagnostic::Engine scheduled_diagnostics;
  auto scheduled_reference_project =
      fsim::app::build_project(
          scheduled_config, scheduled_diagnostics);
  auto scheduled_hybrid_project =
      fsim::app::build_project(
          scheduled_config, scheduled_diagnostics);
  assert(scheduled_reference_project);
  assert(scheduled_hybrid_project);
  const auto scheduled_reference = capture_simulation(
      std::move(*scheduled_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto scheduled_hybrid = capture_simulation(
      std::move(*scheduled_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      scheduled_reference, scheduled_hybrid);
  assert(scheduled_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
  assert(scheduled_hybrid.compiled_processes == 1);
  assert(scheduled_hybrid.compiled_modules == 1);
#endif
  assert(
      scheduled_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(scheduled_hybrid.result.time == 3);
  assert(scheduled_hybrid.final_values.size() == 1);
  assert(scheduled_hybrid.final_values.front() == "1");
  assert(scheduled_hybrid.changes.size() == 2);
  assert(
      std::get<1>(scheduled_hybrid.changes.front())
      == "0");
  assert(
      std::get<2>(scheduled_hybrid.changes.front())
      == 0);
  assert(
      std::get<3>(scheduled_hybrid.changes.front())
      == 0);
  assert(
      std::get<1>(scheduled_hybrid.changes.back())
      == "1");
  assert(
      std::get<2>(scheduled_hybrid.changes.back())
      == 2);
  assert(
      std::get<3>(scheduled_hybrid.changes.back())
      == 0);

  auto overflow_config = scheduled_config;
  overflow_config.project.name =
      "scheduled-write-overflow-test";
  overflow_config.project.top =
      "sv:work.scheduled_overflow";
  overflow_config.build.cache_path =
      directory / "scheduled-write-overflow-cache";
  for (const auto engine : {
           fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::compiled}) {
    fsim::diagnostic::Engine overflow_diagnostics;
    auto overflow_project = fsim::app::build_project(
        overflow_config, overflow_diagnostics);
    assert(overflow_project);
    fsim::app::Simulation overflow_simulation(
        std::move(*overflow_project),
        overflow_config.run.max_deltas,
        engine);
#if defined(FSIM_HAS_LLVM)
    assert(
        overflow_simulation.compiled_process_count()
        == (engine == fsim::app::SimulationEngine::compiled
                ? 1U
                : 0U));
#endif
    const auto overflow_q =
        overflow_simulation.find_signal("q");
    assert(overflow_q);
    bool overflow_thrown = false;
    try {
      (void)overflow_simulation.run();
    } catch (const std::overflow_error& exception) {
      overflow_thrown =
          std::string_view{exception.what()}
          == "simulation time overflow while scheduling event";
    }
    assert(overflow_thrown);
    assert(overflow_simulation.poisoned());
    assert(!overflow_simulation.finished());
    assert(overflow_simulation.now() == 1);
    assert(
        overflow_simulation.read_signal(*overflow_q)
            .to_msb_string()
        == "X");
  }

  auto sensitivity_config = config;
  sensitivity_config.project.name =
      "sensitivity-wakeup-test";
  sensitivity_config.project.top =
      "sv:work.sensitivity";
  sensitivity_config.build.optimization =
      fsim::project::Optimization::o2;
  sensitivity_config.build.cache_path =
      directory / "sensitivity-cache";
  sensitivity_config.source_sets.clear();
  fsim::project::SourceSet sensitivity_sources;
  sensitivity_sources.language =
      fsim::project::Language::system_verilog;
  sensitivity_sources.standard = "2017";
  sensitivity_sources.library = "work";
  sensitivity_sources.files.push_back(sensitivity_source);
  sensitivity_config.source_sets.push_back(
      std::move(sensitivity_sources));
  fsim::diagnostic::Engine sensitivity_diagnostics;
  auto sensitivity_reference_project =
      fsim::app::build_project(
          sensitivity_config, sensitivity_diagnostics);
  auto sensitivity_hybrid_project =
      fsim::app::build_project(
          sensitivity_config, sensitivity_diagnostics);
  assert(sensitivity_reference_project);
  assert(sensitivity_hybrid_project);
  const auto sensitivity_trigger =
      sensitivity_reference_project->design.find_signal(
          "sensitivity.trigger");
  const auto sensitivity_observed =
      sensitivity_reference_project->design.find_signal(
          "sensitivity.observed");
  const auto sensitivity_dynamic_observed =
      sensitivity_reference_project->design.find_signal(
          "sensitivity.dynamic_observed");
  assert(sensitivity_trigger);
  assert(sensitivity_observed);
  assert(sensitivity_dynamic_observed);
  const auto sensitivity_reference = capture_simulation(
      std::move(*sensitivity_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto sensitivity_hybrid = capture_simulation(
      std::move(*sensitivity_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      sensitivity_reference, sensitivity_hybrid);
  assert(sensitivity_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
  assert(sensitivity_hybrid.compiled_processes == 3);
  assert(sensitivity_hybrid.compiled_modules == 1);
#endif
  assert(
      sensitivity_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(sensitivity_hybrid.result.time == 3);
  const decltype(sensitivity_reference.changes)
      expected_sensitivity_changes = {
          {*sensitivity_trigger, "0", 0, 0},
          {*sensitivity_trigger, "1", 1, 0},
          {*sensitivity_dynamic_observed, "1", 1, 1},
          {*sensitivity_observed, "1", 1, 1},
          {*sensitivity_trigger, "0", 2, 0},
          {*sensitivity_dynamic_observed, "0", 2, 1},
      };
  assert(
      sensitivity_reference.changes
      == expected_sensitivity_changes);
  assert(
      sensitivity_hybrid.changes
      == expected_sensitivity_changes);
  assert((
      sensitivity_hybrid.final_values
      == std::vector<std::string>{"0", "1", "0"}));
#if defined(FSIM_HAS_LLVM)
  assert(sensitivity_hybrid.native_cache.hits == 0);
  assert(sensitivity_hybrid.native_cache.misses == 1);
  assert(sensitivity_hybrid.native_cache.stores == 1);
  auto sensitivity_warm_project =
      fsim::app::build_project(
          sensitivity_config, sensitivity_diagnostics);
  assert(sensitivity_warm_project);
  const auto sensitivity_warm = capture_simulation(
      std::move(*sensitivity_warm_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      sensitivity_reference, sensitivity_warm);
  assert(sensitivity_warm.compiled_processes == 3);
  assert(sensitivity_warm.compiled_modules == 1);
  assert(sensitivity_warm.native_cache.hits == 1);
  assert(sensitivity_warm.native_cache.misses == 0);
  assert(sensitivity_warm.native_cache.stores == 0);
#endif

  auto vhdl_wait_config = config;
  vhdl_wait_config.project.name = "vhdl-explicit-wait-test";
  vhdl_wait_config.project.top = "vhdl:work.vhdl_wait(rtl)";
  vhdl_wait_config.build.optimization =
      fsim::project::Optimization::o2;
  vhdl_wait_config.build.cache_path =
      directory / "vhdl-wait-cache";
  vhdl_wait_config.source_sets.clear();
  fsim::project::SourceSet vhdl_wait_sources;
  vhdl_wait_sources.language = fsim::project::Language::vhdl;
  vhdl_wait_sources.standard = "2008";
  vhdl_wait_sources.library = "work";
  vhdl_wait_sources.files.push_back(vhdl_wait_source);
  vhdl_wait_config.source_sets.push_back(
      std::move(vhdl_wait_sources));
  fsim::diagnostic::Engine vhdl_wait_diagnostics;
  auto vhdl_wait_reference_project =
      fsim::app::build_project(
          vhdl_wait_config, vhdl_wait_diagnostics);
  auto vhdl_wait_hybrid_project =
      fsim::app::build_project(
          vhdl_wait_config, vhdl_wait_diagnostics);
  assert(vhdl_wait_reference_project);
  assert(vhdl_wait_hybrid_project);
  const auto vhdl_wait_q =
      vhdl_wait_reference_project->design.find_signal(
          "vhdl_wait.q");
  assert(vhdl_wait_q);
  const auto vhdl_wait_reference = capture_simulation(
      std::move(*vhdl_wait_reference_project),
      fsim::app::SimulationEngine::interpreter,
      4);
  const auto vhdl_wait_hybrid = capture_simulation(
      std::move(*vhdl_wait_hybrid_project),
      fsim::app::SimulationEngine::compiled,
      4);
  compare_captures(vhdl_wait_reference, vhdl_wait_hybrid);
  assert(
      vhdl_wait_hybrid.result.status
      == fsim::runtime::RunStatus::time_limit);
  assert(vhdl_wait_hybrid.result.time == 4);
  assert(vhdl_wait_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
  assert(vhdl_wait_hybrid.compiled_processes == 1);
  assert(vhdl_wait_hybrid.compiled_modules == 1);
#endif
  const decltype(vhdl_wait_reference.changes)
      expected_vhdl_wait_changes = {
          {*vhdl_wait_q, "0", 0, 0},
          {*vhdl_wait_q, "1", 1, 0},
          {*vhdl_wait_q, "0", 2, 0},
          {*vhdl_wait_q, "1", 3, 0},
          {*vhdl_wait_q, "0", 4, 0},
      };
  assert(
      vhdl_wait_reference.changes
      == expected_vhdl_wait_changes);
  assert(
      vhdl_wait_hybrid.changes
      == expected_vhdl_wait_changes);
  assert((
      vhdl_wait_hybrid.final_values
      == std::vector<std::string>{"0"}));

  auto wildcard_config = config;
  wildcard_config.project.name = "wildcard-sensitivity-test";
  wildcard_config.project.top = "sv:work.wildcard_app";
  wildcard_config.build.optimization =
      fsim::project::Optimization::o2;
  wildcard_config.build.cache_path =
      directory / "wildcard-cache";
  wildcard_config.source_sets.clear();
  fsim::project::SourceSet wildcard_sources;
  wildcard_sources.language =
      fsim::project::Language::system_verilog;
  wildcard_sources.standard = "2017";
  wildcard_sources.library = "work";
  wildcard_sources.files.push_back(wildcard_source);
  wildcard_config.source_sets.push_back(
      std::move(wildcard_sources));
  fsim::diagnostic::Engine wildcard_diagnostics;
  auto wildcard_reference_project =
      fsim::app::build_project(
          wildcard_config, wildcard_diagnostics);
  auto wildcard_hybrid_project =
      fsim::app::build_project(
          wildcard_config, wildcard_diagnostics);
  assert(wildcard_reference_project);
  assert(wildcard_hybrid_project);
  const auto wildcard_a =
      wildcard_reference_project->design.find_signal(
          "wildcard_app.a");
  const auto wildcard_q =
      wildcard_reference_project->design.find_signal(
          "wildcard_app.q");
  const auto wildcard_y =
      wildcard_reference_project->design.find_signal(
          "wildcard_app.y");
  const auto wildcard_latched =
      wildcard_reference_project->design.find_signal(
          "wildcard_app.latched");
  assert(
      wildcard_a && wildcard_q && wildcard_y
      && wildcard_latched);
  const auto wildcard_reference = capture_simulation(
      std::move(*wildcard_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto wildcard_hybrid = capture_simulation(
      std::move(*wildcard_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(wildcard_reference, wildcard_hybrid);
  assert(
      wildcard_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(wildcard_hybrid.result.time == 2);
  assert(wildcard_hybrid.process_count == 4);
#if defined(FSIM_HAS_LLVM)
  assert(wildcard_hybrid.compiled_processes == 4);
  assert(wildcard_hybrid.compiled_modules == 1);
#endif
  const decltype(wildcard_reference.changes)
      expected_wildcard_changes = {
          {*wildcard_a, "0", 0, 0},
          {*wildcard_q, "0", 0, 1},
          {*wildcard_y, "1", 0, 2},
          {*wildcard_a, "1", 1, 0},
          {*wildcard_q, "1", 1, 1},
          {*wildcard_latched, "1", 1, 1},
          {*wildcard_y, "0", 1, 2},
      };
  assert(
      wildcard_reference.changes
      == expected_wildcard_changes);
  assert(
      wildcard_hybrid.changes
      == expected_wildcard_changes);
  assert((
      wildcard_hybrid.final_values
      == std::vector<std::string>{"1", "1", "0", "1"}));

  auto case_config = config;
  case_config.project.name = "case-statement-test";
  case_config.project.top = "sv:work.case_app";
  case_config.build.optimization =
      fsim::project::Optimization::o2;
  case_config.build.cache_path = directory / "case-cache";
  case_config.source_sets.clear();
  fsim::project::SourceSet case_sources;
  case_sources.language =
      fsim::project::Language::system_verilog;
  case_sources.standard = "2017";
  case_sources.library = "work";
  case_sources.files.push_back(case_source);
  case_config.source_sets.push_back(std::move(case_sources));
  fsim::diagnostic::Engine case_diagnostics;
  auto case_reference_project =
      fsim::app::build_project(case_config, case_diagnostics);
  auto case_hybrid_project =
      fsim::app::build_project(case_config, case_diagnostics);
  assert(case_reference_project);
  assert(case_hybrid_project);
  const auto case_selector =
      case_reference_project->design.find_signal(
          "case_app.selector");
  const auto case_result =
      case_reference_project->design.find_signal(
          "case_app.result");
  assert(case_selector && case_result);
  const auto case_reference = capture_simulation(
      std::move(*case_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto case_hybrid = capture_simulation(
      std::move(*case_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(case_reference, case_hybrid);
  assert(
      case_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(case_hybrid.result.time == 5);
  assert(case_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(case_hybrid.compiled_processes == 2);
  assert(case_hybrid.compiled_modules == 1);
#endif
  assert((
      case_hybrid.final_values
      == std::vector<std::string>{"11", "00"}));
  assert(std::find(
             case_hybrid.changes.begin(),
             case_hybrid.changes.end(),
             std::tuple{
                 *case_result, std::string{"10"},
                 fsim::runtime::SimulationTick{2},
                 std::uint64_t{1}})
         != case_hybrid.changes.end());
  assert(std::find(
             case_hybrid.changes.begin(),
             case_hybrid.changes.end(),
             std::tuple{
                 *case_result, std::string{"11"},
                 fsim::runtime::SimulationTick{3},
                 std::uint64_t{1}})
         != case_hybrid.changes.end());

  auto conditional_config = config;
  conditional_config.project.name =
      "conditional-expression-test";
  conditional_config.project.top =
      "sv:work.conditional_app";
  conditional_config.build.optimization =
      fsim::project::Optimization::o2;
  conditional_config.build.cache_path =
      directory / "conditional-cache";
  conditional_config.source_sets.clear();
  fsim::project::SourceSet conditional_sources;
  conditional_sources.language =
      fsim::project::Language::system_verilog;
  conditional_sources.standard = "2017";
  conditional_sources.library = "work";
  conditional_sources.files.push_back(conditional_source);
  conditional_config.source_sets.push_back(
      std::move(conditional_sources));
  fsim::diagnostic::Engine conditional_diagnostics;
  auto conditional_reference_project =
      fsim::app::build_project(
          conditional_config, conditional_diagnostics);
  auto conditional_hybrid_project =
      fsim::app::build_project(
          conditional_config, conditional_diagnostics);
  assert(conditional_reference_project);
  assert(conditional_hybrid_project);
  const auto conditional_reference = capture_simulation(
      std::move(*conditional_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto conditional_hybrid = capture_simulation(
      std::move(*conditional_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      conditional_reference, conditional_hybrid);
  assert(
      conditional_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(conditional_hybrid.result.time == 4);
  assert(conditional_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(conditional_hybrid.compiled_processes == 2);
  assert(conditional_hybrid.compiled_modules == 1);
#endif
  assert((
      conditional_hybrid.final_values
      == std::vector<std::string>{
          "Z", "101Z", "100Z", "10XZ"}));

  auto comparison_config = config;
  comparison_config.project.name =
      "comparison-expression-test";
  comparison_config.project.top = "sv:work.comparison_app";
  comparison_config.build.optimization =
      fsim::project::Optimization::o2;
  comparison_config.build.cache_path =
      directory / "comparison-cache";
  comparison_config.source_sets.clear();
  fsim::project::SourceSet comparison_sources;
  comparison_sources.language =
      fsim::project::Language::system_verilog;
  comparison_sources.standard = "2017";
  comparison_sources.library = "work";
  comparison_sources.files.push_back(comparison_source);
  comparison_config.source_sets.push_back(
      std::move(comparison_sources));
  fsim::diagnostic::Engine comparison_diagnostics;
  auto comparison_reference_project =
      fsim::app::build_project(
          comparison_config, comparison_diagnostics);
  auto comparison_hybrid_project =
      fsim::app::build_project(
          comparison_config, comparison_diagnostics);
  assert(comparison_reference_project);
  assert(comparison_hybrid_project);
  const auto comparison_reference = capture_simulation(
      std::move(*comparison_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto comparison_hybrid = capture_simulation(
      std::move(*comparison_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(comparison_reference, comparison_hybrid);
  assert(
      comparison_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(comparison_hybrid.result.time == 4);
  assert(comparison_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(comparison_hybrid.compiled_processes == 2);
  assert(comparison_hybrid.compiled_modules == 1);
#endif
  assert((
      comparison_hybrid.final_values
      == std::vector<std::string>{
          "01Z0", "0011", "X", "X", "X", "X", "X", "0"}));

  auto logical_config = config;
  logical_config.project.name = "logical-expression-test";
  logical_config.project.top = "sv:work.logical_app";
  logical_config.build.optimization =
      fsim::project::Optimization::o2;
  logical_config.build.cache_path =
      directory / "logical-cache";
  logical_config.source_sets.clear();
  fsim::project::SourceSet logical_sources;
  logical_sources.language =
      fsim::project::Language::system_verilog;
  logical_sources.standard = "2017";
  logical_sources.library = "work";
  logical_sources.files.push_back(logical_source);
  logical_config.source_sets.push_back(
      std::move(logical_sources));
  fsim::diagnostic::Engine logical_diagnostics;
  auto logical_reference_project =
      fsim::app::build_project(
          logical_config, logical_diagnostics);
  auto logical_hybrid_project =
      fsim::app::build_project(
          logical_config, logical_diagnostics);
  assert(logical_reference_project);
  assert(logical_hybrid_project);
  const auto logical_reference = capture_simulation(
      std::move(*logical_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto logical_hybrid = capture_simulation(
      std::move(*logical_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(logical_reference, logical_hybrid);
  assert(
      logical_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(logical_hybrid.result.time == 5);
  assert(logical_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(logical_hybrid.compiled_processes == 2);
  assert(logical_hybrid.compiled_modules == 1);
#endif
  assert((
      logical_hybrid.final_values
      == std::vector<std::string>{
          "0010", "01", "001", "1", "1",
          "0", "1", "1", "0100", "0001"}));

  auto arithmetic_config = config;
  arithmetic_config.project.name = "arithmetic-expression-test";
  arithmetic_config.project.top = "sv:work.arithmetic_app";
  arithmetic_config.build.optimization =
      fsim::project::Optimization::o2;
  arithmetic_config.build.cache_path =
      directory / "arithmetic-cache";
  arithmetic_config.source_sets.clear();
  fsim::project::SourceSet arithmetic_sources;
  arithmetic_sources.language =
      fsim::project::Language::system_verilog;
  arithmetic_sources.standard = "2017";
  arithmetic_sources.library = "work";
  arithmetic_sources.files.push_back(arithmetic_source);
  arithmetic_config.source_sets.push_back(
      std::move(arithmetic_sources));
  fsim::diagnostic::Engine arithmetic_diagnostics;
  auto arithmetic_reference_project =
      fsim::app::build_project(
          arithmetic_config, arithmetic_diagnostics);
  auto arithmetic_hybrid_project =
      fsim::app::build_project(
          arithmetic_config, arithmetic_diagnostics);
  assert(arithmetic_reference_project);
  assert(arithmetic_hybrid_project);
  const auto arithmetic_reference = capture_simulation(
      std::move(*arithmetic_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto arithmetic_hybrid = capture_simulation(
      std::move(*arithmetic_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(arithmetic_reference, arithmetic_hybrid);
  assert(
      arithmetic_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(arithmetic_hybrid.result.time == 4);
  assert(arithmetic_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(arithmetic_hybrid.compiled_processes == 2);
  assert(arithmetic_hybrid.compiled_modules == 1);
#endif
  assert((
      arithmetic_hybrid.final_values
      == std::vector<std::string>{
          "11001000", "00000111", "11000001", "01111000",
          "00011100", "00000100", "11001000", "00111000",
          "00000101", "11111101", "00000010", "00001000",
          "11110001", "11111111", "00000010", "0"}));

  auto select_concat_config = config;
  select_concat_config.project.name = "select-concat-test";
  select_concat_config.project.top = "sv:work.select_concat_app";
  select_concat_config.build.optimization =
      fsim::project::Optimization::o2;
  select_concat_config.build.cache_path =
      directory / "select-concat-cache";
  select_concat_config.source_sets.clear();
  fsim::project::SourceSet select_concat_sources;
  select_concat_sources.language =
      fsim::project::Language::system_verilog;
  select_concat_sources.standard = "2017";
  select_concat_sources.library = "work";
  select_concat_sources.files.push_back(
      select_concat_source);
  select_concat_config.source_sets.push_back(
      std::move(select_concat_sources));
  fsim::diagnostic::Engine select_concat_diagnostics;
  auto select_concat_reference_project =
      fsim::app::build_project(
          select_concat_config, select_concat_diagnostics);
  auto select_concat_hybrid_project =
      fsim::app::build_project(
          select_concat_config, select_concat_diagnostics);
  assert(select_concat_reference_project);
  assert(select_concat_hybrid_project);
  const auto select_concat_reference = capture_simulation(
      std::move(*select_concat_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto select_concat_hybrid = capture_simulation(
      std::move(*select_concat_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      select_concat_reference, select_concat_hybrid);
  assert(
      select_concat_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(select_concat_hybrid.result.time == 2);
  assert(select_concat_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(select_concat_hybrid.compiled_processes == 2);
  assert(select_concat_hybrid.compiled_modules == 1);
#endif
  assert((
      select_concat_hybrid.final_values
      == std::vector<std::string>{
          "Z10100X1", "1100XZ01", "0", "0", "0",
          "Z101", "00XZ", "Z1010XZ01", "10XZ1111",
          "XZ10"}));

  auto vhdl_select_concat_config = config;
  vhdl_select_concat_config.project.name =
      "vhdl-select-concat-test";
  vhdl_select_concat_config.project.top =
      "vhdl:work.vhdl_select_concat_app(rtl)";
  vhdl_select_concat_config.build.optimization =
      fsim::project::Optimization::o2;
  vhdl_select_concat_config.build.cache_path =
      directory / "vhdl-select-concat-cache";
  vhdl_select_concat_config.source_sets.clear();
  fsim::project::SourceSet vhdl_select_concat_sources;
  vhdl_select_concat_sources.language =
      fsim::project::Language::vhdl;
  vhdl_select_concat_sources.standard = "2008";
  vhdl_select_concat_sources.library = "work";
  vhdl_select_concat_sources.files.push_back(
      vhdl_select_concat_source);
  vhdl_select_concat_config.source_sets.push_back(
      std::move(vhdl_select_concat_sources));
  fsim::diagnostic::Engine vhdl_select_concat_diagnostics;
  auto vhdl_select_concat_reference_project =
      fsim::app::build_project(
          vhdl_select_concat_config,
          vhdl_select_concat_diagnostics);
  auto vhdl_select_concat_hybrid_project =
      fsim::app::build_project(
          vhdl_select_concat_config,
          vhdl_select_concat_diagnostics);
  assert(vhdl_select_concat_reference_project);
  assert(vhdl_select_concat_hybrid_project);
  const auto vhdl_select_concat_reference =
      capture_simulation(
          std::move(*vhdl_select_concat_reference_project),
          fsim::app::SimulationEngine::interpreter);
  const auto vhdl_select_concat_hybrid =
      capture_simulation(
          std::move(*vhdl_select_concat_hybrid_project),
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      vhdl_select_concat_reference,
      vhdl_select_concat_hybrid);
  assert(
      vhdl_select_concat_hybrid.result.status
      == fsim::runtime::RunStatus::completed);
  assert(vhdl_select_concat_hybrid.result.time == 5);
  assert(vhdl_select_concat_hybrid.process_count == 4);
#if defined(FSIM_HAS_LLVM)
  assert(vhdl_select_concat_hybrid.compiled_processes == 4);
  assert(vhdl_select_concat_hybrid.compiled_modules == 1);
#endif
  assert((
      vhdl_select_concat_hybrid.final_values
      == std::vector<std::string>{
          "1XZ0", "01Z1", "Z", "Z", "Z", "X",
          "1X", "1Z", "1X10Z1", "10XZ1110",
          "XZ10"}));

  auto vhdl_signed_config = config;
  vhdl_signed_config.project.name = "vhdl-signed-test";
  vhdl_signed_config.project.top =
      "vhdl:work.vhdl_signed_app(rtl)";
  vhdl_signed_config.build.optimization =
      fsim::project::Optimization::o2;
  vhdl_signed_config.build.cache_path =
      directory / "vhdl-signed-cache";
  vhdl_signed_config.source_sets.clear();
  fsim::project::SourceSet vhdl_signed_sources;
  vhdl_signed_sources.language =
      fsim::project::Language::vhdl;
  vhdl_signed_sources.standard = "2008";
  vhdl_signed_sources.library = "work";
  vhdl_signed_sources.files.push_back(vhdl_signed_source);
  vhdl_signed_config.source_sets.push_back(
      std::move(vhdl_signed_sources));
  fsim::diagnostic::Engine vhdl_signed_diagnostics;
  auto vhdl_signed_reference_project =
      fsim::app::build_project(
          vhdl_signed_config, vhdl_signed_diagnostics);
  auto vhdl_signed_hybrid_project =
      fsim::app::build_project(
          vhdl_signed_config, vhdl_signed_diagnostics);
  assert(vhdl_signed_reference_project);
  assert(vhdl_signed_hybrid_project);
  const auto vhdl_signed_reference = capture_simulation(
      std::move(*vhdl_signed_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto vhdl_signed_hybrid = capture_simulation(
      std::move(*vhdl_signed_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      vhdl_signed_reference, vhdl_signed_hybrid);
  assert(
      vhdl_signed_hybrid.result.status
      == fsim::runtime::RunStatus::completed);
  assert(vhdl_signed_hybrid.result.time == 0);
  assert(vhdl_signed_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
  assert(vhdl_signed_hybrid.compiled_processes == 3);
  assert(vhdl_signed_hybrid.compiled_modules == 1);
#endif
  assert((
      vhdl_signed_hybrid.final_values
      == std::vector<std::string>{
          "11111011", "00000011", "11111110", "11111000",
          "11110001", "11111111", "11111110", "00000001",
          "1"}));

  auto conditional_statement_config = config;
  conditional_statement_config.project.name =
      "conditional-statement-test";
  conditional_statement_config.project.top =
      "sv:work.conditional_statement_app";
  conditional_statement_config.build.cache_path =
      directory / "conditional-statement-cache";
  conditional_statement_config.source_sets.clear();
  fsim::project::SourceSet conditional_statement_sources;
  conditional_statement_sources.language =
      fsim::project::Language::system_verilog;
  conditional_statement_sources.standard = "2017";
  conditional_statement_sources.library = "work";
  conditional_statement_sources.files.push_back(
      conditional_statement_source);
  conditional_statement_config.source_sets.push_back(
      std::move(conditional_statement_sources));
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    conditional_statement_config.build.optimization =
        optimization;
    fsim::diagnostic::Engine conditional_statement_diagnostics;
    auto conditional_statement_reference_project =
        fsim::app::build_project(
            conditional_statement_config,
            conditional_statement_diagnostics);
    auto conditional_statement_hybrid_project =
        fsim::app::build_project(
            conditional_statement_config,
            conditional_statement_diagnostics);
    assert(conditional_statement_reference_project);
    assert(conditional_statement_hybrid_project);
    const auto conditional_statement_reference =
        capture_simulation(
            std::move(*conditional_statement_reference_project),
            fsim::app::SimulationEngine::interpreter);
    const auto conditional_statement_hybrid =
        capture_simulation(
            std::move(*conditional_statement_hybrid_project),
            fsim::app::SimulationEngine::compiled);
    compare_captures(
        conditional_statement_reference,
        conditional_statement_hybrid);
    assert(
        conditional_statement_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(conditional_statement_hybrid.result.time == 3);
    assert(conditional_statement_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
    assert(conditional_statement_hybrid.compiled_processes == 2);
    assert(conditional_statement_hybrid.compiled_modules == 1);
#endif
    assert((
        conditional_statement_hybrid.final_values
        == std::vector<std::string>{
            "1000", "0", "1", "0", "0001"}));
  }

  auto vhdl_conditional_statement_config = config;
  vhdl_conditional_statement_config.project.name =
      "vhdl-conditional-statement-test";
  vhdl_conditional_statement_config.project.top =
      "vhdl:work.vhdl_conditional_statement_app(rtl)";
  vhdl_conditional_statement_config.build.cache_path =
      directory / "vhdl-conditional-statement-cache";
  vhdl_conditional_statement_config.source_sets.clear();
  fsim::project::SourceSet vhdl_conditional_statement_sources;
  vhdl_conditional_statement_sources.language =
      fsim::project::Language::vhdl;
  vhdl_conditional_statement_sources.standard = "2008";
  vhdl_conditional_statement_sources.library = "work";
  vhdl_conditional_statement_sources.files.push_back(
      vhdl_conditional_statement_source);
  vhdl_conditional_statement_config.source_sets.push_back(
      std::move(vhdl_conditional_statement_sources));
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    vhdl_conditional_statement_config.build.optimization =
        optimization;
    fsim::diagnostic::Engine vhdl_conditional_statement_diagnostics;
    auto vhdl_conditional_statement_reference_project =
        fsim::app::build_project(
            vhdl_conditional_statement_config,
            vhdl_conditional_statement_diagnostics);
    auto vhdl_conditional_statement_hybrid_project =
        fsim::app::build_project(
            vhdl_conditional_statement_config,
            vhdl_conditional_statement_diagnostics);
    assert(vhdl_conditional_statement_reference_project);
    assert(vhdl_conditional_statement_hybrid_project);
    const auto vhdl_conditional_statement_reference =
        capture_simulation(
            std::move(*vhdl_conditional_statement_reference_project),
            fsim::app::SimulationEngine::interpreter);
    const auto vhdl_conditional_statement_hybrid =
        capture_simulation(
            std::move(*vhdl_conditional_statement_hybrid_project),
            fsim::app::SimulationEngine::compiled);
    compare_captures(
        vhdl_conditional_statement_reference,
        vhdl_conditional_statement_hybrid);
    assert(
        vhdl_conditional_statement_hybrid.result.status
        == fsim::runtime::RunStatus::completed);
    assert(vhdl_conditional_statement_hybrid.result.time == 0);
    assert(vhdl_conditional_statement_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
    assert(
        vhdl_conditional_statement_hybrid.compiled_processes
        == 1);
    assert(
        vhdl_conditional_statement_hybrid.compiled_modules
        == 1);
#endif
    assert((
        vhdl_conditional_statement_hybrid.final_values
        == std::vector<std::string>{
            "X", "1", "1", "1", "1"}));
  }

  auto partial_group_config = config;
  partial_group_config.project.name =
      "partial-specialization-group-test";
  partial_group_config.project.top =
      "sv:work.partial_group";
  partial_group_config.build.optimization =
      fsim::project::Optimization::o2;
  partial_group_config.build.cache_path =
      directory / "partial-group-cache";
  partial_group_config.source_sets.clear();
  fsim::project::SourceSet partial_group_sources;
  partial_group_sources.language =
      fsim::project::Language::system_verilog;
  partial_group_sources.standard = "2017";
  partial_group_sources.library = "work";
  partial_group_sources.files.push_back(partial_group_source);
  partial_group_config.source_sets.push_back(
      std::move(partial_group_sources));
  fsim::diagnostic::Engine partial_group_diagnostics;
  auto partial_group_reference_project =
      fsim::app::build_project(
          partial_group_config, partial_group_diagnostics);
  auto partial_group_hybrid_project =
      fsim::app::build_project(
          partial_group_config, partial_group_diagnostics);
  assert(partial_group_reference_project);
  assert(partial_group_hybrid_project);
  const auto partial_group_reference = capture_simulation(
      std::move(*partial_group_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto partial_group_hybrid = capture_simulation(
      std::move(*partial_group_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      partial_group_reference, partial_group_hybrid);
  assert(partial_group_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(partial_group_hybrid.compiled_processes == 1);
  assert(partial_group_hybrid.compiled_modules == 1);
  assert(partial_group_hybrid.native_cache.misses == 1);
  assert(partial_group_hybrid.native_cache.stores == 1);
#endif
  assert(
      partial_group_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(partial_group_hybrid.result.time == 1);

  auto provenance_config = config;
  provenance_config.project.name =
      "specialization-provenance-test";
  provenance_config.project.top = "sv:work.provenance";
  provenance_config.build.optimization =
      fsim::project::Optimization::o2;
  provenance_config.build.cache_path =
      directory / "provenance-cache";
  provenance_config.source_sets.clear();
  fsim::project::SourceSet provenance_sources;
  provenance_sources.language =
      fsim::project::Language::system_verilog;
  provenance_sources.standard = "2017";
  provenance_sources.library = "work";
  provenance_sources.files = {
      provenance_source,
      unused_source,
  };
  provenance_config.source_sets.push_back(
      std::move(provenance_sources));
  struct ProvenanceRun {
    std::string specialization_key;
    bool analysis_cache_hit{};
    CapturedSimulation simulation;
  };
  const auto run_provenance =
      [&](const fsim::project::Config& run_config) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            run_config, run_diagnostics);
        assert(project);
        assert(project->design.specializations().size() == 1);
        assert(project->specialization_cache_keys.size() == 1);
        auto key = project->specialization_cache_keys.front();
        const auto analysis_cache_hit = project->cache_hit;
        auto captured = capture_simulation(
            std::move(*project),
            fsim::app::SimulationEngine::compiled);
        assert(
            captured.result.status
            == fsim::runtime::RunStatus::stopped);
        assert(captured.result.time == 1);
        assert(captured.process_count == 1);
        return ProvenanceRun{
            std::move(key),
            analysis_cache_hit,
            std::move(captured)};
      };

  const auto provenance_cold =
      run_provenance(provenance_config);
  const auto provenance_warm =
      run_provenance(provenance_config);
  assert(!provenance_cold.analysis_cache_hit);
  assert(provenance_warm.analysis_cache_hit);
  assert(
      provenance_warm.specialization_key
      == provenance_cold.specialization_key);
  assert(
      provenance_warm.simulation.final_values
      == provenance_cold.simulation.final_values);
#if defined(FSIM_HAS_LLVM)
  assert(provenance_cold.simulation.compiled_processes == 1);
  assert(provenance_cold.simulation.compiled_modules == 1);
  assert(provenance_cold.simulation.native_cache.hits == 0);
  assert(provenance_cold.simulation.native_cache.misses == 1);
  assert(provenance_cold.simulation.native_cache.stores == 1);
  assert(provenance_warm.simulation.native_cache.hits == 1);
  assert(provenance_warm.simulation.native_cache.misses == 0);
#endif

  // An uninstantiated source changes the project-analysis key, but not the
  // instantiated specialization's native provenance.
  write_unused_source("unused revision 2");
  const auto provenance_unrelated =
      run_provenance(provenance_config);
  assert(!provenance_unrelated.analysis_cache_hit);
  assert(
      provenance_unrelated.specialization_key
      == provenance_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(provenance_unrelated.simulation.native_cache.hits == 1);
  assert(provenance_unrelated.simulation.native_cache.misses == 0);
#endif

  // A comment-only owning-source edit leaves SimIR unchanged but must still
  // invalidate the specialization object by source provenance.
  write_provenance_source("top revision 2");
  const auto provenance_changed_source =
      run_provenance(provenance_config);
  assert(!provenance_changed_source.analysis_cache_hit);
  assert(
      provenance_changed_source.specialization_key
      != provenance_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(provenance_changed_source.simulation.native_cache.hits == 0);
  assert(provenance_changed_source.simulation.native_cache.misses == 1);
  assert(provenance_changed_source.simulation.native_cache.stores == 1);
#endif

  auto provenance_standard_config = provenance_config;
  provenance_standard_config.source_sets.front().standard = "2012";
  const auto provenance_changed_standard =
      run_provenance(provenance_standard_config);
  assert(!provenance_changed_standard.analysis_cache_hit);
  assert(
      provenance_changed_standard.specialization_key
      != provenance_changed_source.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(provenance_changed_standard.simulation.native_cache.hits == 0);
  assert(provenance_changed_standard.simulation.native_cache.misses == 1);
  assert(provenance_changed_standard.simulation.native_cache.stores == 1);
#endif

  fsim::diagnostic::Engine mixed_diagnostics;
  const auto mixed_manifest =
      std::filesystem::path{FSIM_TEST_SOURCE_DIR}
      / "examples/vertical_slice/fsim.toml";
  auto mixed_config =
      fsim::project::load(mixed_manifest, mixed_diagnostics);
  assert(mixed_config);
  mixed_config->build.cache_path = directory / "mixed-cache";
  mixed_config->run.trace_file.reset();
  auto mixed_reference_project =
      fsim::app::build_project(*mixed_config, mixed_diagnostics);
  auto mixed_hybrid_project =
      fsim::app::build_project(*mixed_config, mixed_diagnostics);
  assert(mixed_reference_project);
  assert(mixed_hybrid_project);
  const auto mixed_reference = capture_simulation(
      std::move(*mixed_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto mixed_hybrid = capture_simulation(
      std::move(*mixed_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(mixed_reference, mixed_hybrid);
  assert(
      mixed_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(mixed_hybrid.result.time == 6);

  fsim::app::Simulation simulation(std::move(*first), config.run.max_deltas);
  const auto q = simulation.find_signal("q");
  const auto two_state = simulation.find_signal("two_state");
  assert(q && two_state);
  assert(simulation.read_signal(*two_state).to_msb_string() == "0");
  bool rejected_lossy_deposit = false;
  try {
    simulation.deposit_signal(
        *two_state,
        fsim::runtime::PackedLogic4::from_msb_string("X"));
  } catch (const std::invalid_argument&) {
    rejected_lossy_deposit = true;
  }
  assert(rejected_lossy_deposit);
  const auto result = simulation.run();
  assert(result.status == fsim::runtime::RunStatus::stopped);
  assert(result.time == 3);
  assert(simulation.finished());
  assert(!simulation.poisoned());
  assert(simulation.read_signal(*q).to_msb_string() == "1");

  simulation.force_signal(
      *q, fsim::runtime::PackedLogic4::from_msb_string("0"));
  simulation.deposit_signal(
      *q, fsim::runtime::PackedLogic4::from_msb_string("1"));
  assert(simulation.read_signal(*q).to_msb_string() == "0");
  simulation.release_signal(*q);
  assert(simulation.read_signal(*q).to_msb_string() == "1");

  std::string error;
  assert(fsim::app::parse_time("25ns", "1ns", error) == 25);
  assert(!fsim::app::parse_time("1ps", "1ns", error));
  const auto value = fsim::app::parse_value("10xz", 4, error);
  assert(value && value->to_msb_string() == "10XZ");

  auto compiled_debug_project =
      fsim::app::build_project(config, diagnostics);
  assert(compiled_debug_project);
  const std::string debug_commands =
      "scope\n"
      "scopes\n"
      "scope u_child\n"
      "signals\n"
      "show value\n"
      "scope ..\n"
      "break signal q == 1\n"
      "break time 1ns\n"
      "breakpoints\n"
      "continue\n"
      "delete 2\n"
      "break source tb.sv:14\n"
      "run-until 3ns\n"
      "delete 3\n"
      "locals\n"
      "step statement\n"
      "locals\n"
      "step statement\n"
      "delete 1\n"
      "locals\n"
      "step process\n"
      "where\n"
      "break signal child_y\n"
      "clear\n"
      "breakpoints\n"
      "continue\n"
      "continue\n"
      "step delta\n"
      "quit\n";
  fsim::app::Simulation debug_simulation(
      std::move(*second),
      config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter);
  assert(debug_simulation.compiled_process_count() == 0);
  std::size_t observed_changes = 0;
  debug_simulation.set_signal_change_hook(
      [&observed_changes](
          fsim::runtime::simir::SignalId,
          const fsim::runtime::PackedLogic4&,
          fsim::runtime::SimulationTick,
          std::uint64_t) { ++observed_changes; });
  debug_simulation.start();
  std::istringstream debug_input{debug_commands};
  std::ostringstream debug_output;
  std::ostringstream debug_error;
  assert(
      fsim::app::run_debug_repl(
          debug_simulation, debug_input, debug_output, debug_error)
      == 0);
  assert(debug_error.str().empty());
  const auto transcript = debug_output.str();
  assert(transcript.find("tb.u_child") != std::string::npos);
  assert(
      transcript.find("tb.u_child.value = X") != std::string::npos);
  assert(
      transcript.find("breakpoint 1 set on tb.q == 1")
      != std::string::npos);
  assert(
      transcript.find("breakpoint 2 set at time 1") != std::string::npos);
  assert(
      transcript.find("hit breakpoint 1: tb.q changed to 1 at time 2")
      != std::string::npos);
  assert(
      transcript.find("hit breakpoint 2: time 1") != std::string::npos);
  assert(
      transcript.find("breakpoint 3 set at tb.sv:14")
      != std::string::npos);
  assert(
      transcript.find(
          "hit breakpoint 3: " + source.string() + ":14:")
      != std::string::npos);
  assert(
      transcript.find(" at " + source.string() + ":15:")
      != std::string::npos);
  assert(
      transcript.find(" at " + source.string() + ":16:")
      != std::string::npos);
  assert(
      transcript.find("local_state = 0") != std::string::npos);
  assert(
      transcript.find("local_state = 1") != std::string::npos);
  assert(transcript.find("stopped at time 2") != std::string::npos);
  assert(transcript.find("time 2, delta") != std::string::npos);
  assert(transcript.find("cleared all breakpoints") != std::string::npos);
  assert(transcript.find("no breakpoints") != std::string::npos);
  assert(
      transcript.find("simulation finished at time 3")
      != std::string::npos);
  const auto first_finished =
      transcript.find("simulation has finished");
  assert(first_finished != std::string::npos);
  assert(
      transcript.find("simulation has finished", first_finished + 1)
      != std::string::npos);
  assert(debug_simulation.finished());
  assert(!debug_simulation.poisoned());
  assert(observed_changes > 0);

  fsim::app::Simulation compiled_debug_simulation(
      std::move(*compiled_debug_project),
      config.run.max_deltas,
      fsim::app::SimulationEngine::debug);
#if defined(FSIM_HAS_LLVM)
  assert(compiled_debug_simulation.compiled_process_count() == 2);
  assert(compiled_debug_simulation.compiled_module_count() == 2);
  const auto debug_native_cache =
      compiled_debug_simulation.native_cache_statistics();
  // The same specialization modules were already cached at the configured O2
  // run setting. Cold objects here therefore prove that debug forces O0.
  assert(debug_native_cache.hits == 0);
  assert(debug_native_cache.misses == 2);
  assert(debug_native_cache.stores == 2);
#else
  assert(compiled_debug_simulation.compiled_process_count() == 0);
  assert(compiled_debug_simulation.compiled_module_count() == 0);
#endif
  std::size_t compiled_observed_changes = 0;
  compiled_debug_simulation.set_signal_change_hook(
      [&compiled_observed_changes](
          fsim::runtime::simir::SignalId,
          const fsim::runtime::PackedLogic4&,
          fsim::runtime::SimulationTick,
          std::uint64_t) { ++compiled_observed_changes; });
  compiled_debug_simulation.start();
  std::istringstream compiled_debug_input{debug_commands};
  std::ostringstream compiled_debug_output;
  std::ostringstream compiled_debug_error;
  assert(
      fsim::app::run_debug_repl(
          compiled_debug_simulation,
          compiled_debug_input,
          compiled_debug_output,
          compiled_debug_error)
      == 0);
  assert(compiled_debug_error.str().empty());
  assert(compiled_debug_output.str() == transcript);
  assert(compiled_debug_simulation.finished());
  assert(!compiled_debug_simulation.poisoned());
  assert(compiled_observed_changes == observed_changes);
  for (const auto& signal : debug_simulation.design().signals()) {
    assert(
        compiled_debug_simulation.read_signal(signal.id)
        == debug_simulation.read_signal(signal.id));
  }

  auto poisoned_project = fsim::app::build_project(config, diagnostics);
  assert(poisoned_project);
  fsim::app::Simulation poisoned_simulation(
      std::move(*poisoned_project), config.run.max_deltas);
#if defined(FSIM_HAS_LLVM)
  assert(poisoned_simulation.compiled_process_count() > 0);
#else
  assert(poisoned_simulation.compiled_process_count() == 0);
#endif
  poisoned_simulation.set_signal_change_hook(
      [](
          fsim::runtime::simir::SignalId,
          const fsim::runtime::PackedLogic4&,
          fsim::runtime::SimulationTick,
          std::uint64_t) {
        throw std::runtime_error("fatal signal observer");
      });
  poisoned_simulation.start();
  std::istringstream poisoned_input{
      "continue\n"
      "continue\n"
      "step delta\n"
      "quit\n"};
  std::ostringstream poisoned_output;
  std::ostringstream poisoned_error;
  assert(
      fsim::app::run_debug_repl(
          poisoned_simulation,
          poisoned_input,
          poisoned_output,
          poisoned_error)
      == 0);
  assert(poisoned_simulation.poisoned());
  assert(!poisoned_simulation.finished());
  assert(
      poisoned_error.str().find("fatal signal observer")
      != std::string::npos);
  const auto unavailable =
      poisoned_output.str().find(
          "simulation is unavailable after a fatal runtime error");
  assert(unavailable != std::string::npos);
  assert(
      poisoned_output.str().find(
          "simulation is unavailable after a fatal runtime error",
          unavailable + 1)
      != std::string::npos);

  const auto manifest = directory / "fsim.toml";
  const auto debug_trace = directory / "debug-select.vcd";
  {
    std::ofstream output(manifest);
    output << R"(
schema = 1

[project]
name = "debug-cli-test"
top = "sv:work.tb"
time_resolution = "1ns"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "cli-cache"

[run]
max_deltas = 1000
trace_file = "debug-select.vcd"
trace_filters = ["__none__"]
)";
  }
  std::istringstream cli_input{
      "where\n"
      "trace list\n"
      "trace add q\n"
      "trace list\n"
      "run 1ns\n"
      "trace remove q\n"
      "trace list\n"
      "continue\n"
      "quit\n"};
  std::ostringstream cli_output;
  std::ostringstream cli_error;
  auto services = fsim::app::make_cli_services(cli_input);
  const auto manifest_text = manifest.string();
  const std::vector<const char*> arguments{
      "fsim", "debug", "-p", manifest_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(arguments.size()),
          arguments.data(),
          services,
          cli_output,
          cli_error)
      == 0);
  assert(
      cli_output.str().find("fsim debugger: tb") != std::string::npos);
#if defined(FSIM_HAS_LLVM)
  assert(
      cli_output.str().find(
          "(O0 hybrid, 2 compiled process(es) in "
          "2 specialization module(s))")
      != std::string::npos);
#else
  assert(
      cli_output.str().find("(reference evaluator)")
      != std::string::npos);
#endif
  assert(
      cli_output.str().find("time 0, delta 0, scope tb")
      != std::string::npos);
  assert(
      cli_output.str().find("(no traced signals)")
      != std::string::npos);
  assert(
      cli_output.str().find("tracing tb.q")
      != std::string::npos);
  assert(
      cli_output.str().find("stopped tracing tb.q")
      != std::string::npos);
  std::ifstream debug_trace_stream(debug_trace);
  const std::string debug_vcd{
      std::istreambuf_iterator<char>{debug_trace_stream},
      std::istreambuf_iterator<char>{}};
  assert(!debug_vcd.empty());
  std::string q_identifier;
  std::istringstream debug_vcd_lines{debug_vcd};
  for (std::string line; std::getline(debug_vcd_lines, line);) {
    if (line.starts_with("$var wire 1 ")
        && line.ends_with(" q $end")) {
      std::istringstream declaration{line};
      std::string directive;
      std::string kind;
      std::string width;
      declaration >> directive >> kind >> width >> q_identifier;
      break;
    }
  }
  assert(!q_identifier.empty());
  assert(
      debug_vcd.find("\nx" + q_identifier + "\n")
      != std::string::npos);
  assert(
      debug_vcd.find("\n0" + q_identifier + "\n")
      != std::string::npos);
  assert(
      debug_vcd.find("\n1" + q_identifier + "\n")
      == std::string::npos);

  restored_interrupt_count = 0;
  const auto previous_interrupt_handler =
      std::signal(SIGINT, record_restored_interrupt);
  assert(previous_interrupt_handler != SIG_ERR);
  std::istringstream interrupted_cli_input{
      "continue\n"
      "continue\n"
      "quit\n"};
  InterruptingOutputBuffer interrupted_output_buffer;
  std::ostream interrupted_cli_output{&interrupted_output_buffer};
  std::ostringstream interrupted_cli_error;
  auto interrupted_services =
      fsim::app::make_cli_services(interrupted_cli_input);
  assert(
      fsim::cli::run(
          static_cast<int>(arguments.size()),
          arguments.data(),
          interrupted_services,
          interrupted_cli_output,
          interrupted_cli_error)
      == 0);
  assert(interrupted_cli_error.str().empty());
  const auto interrupted_transcript =
      interrupted_output_buffer.str();
  assert(
      interrupted_transcript.find("process ")
      != std::string::npos);
  assert(
      interrupted_transcript.find("stopped at time 0")
      != std::string::npos);
  assert(
      interrupted_transcript.find("simulation finished at time 3")
      != std::string::npos);
  (void)std::raise(SIGINT);
  assert(restored_interrupt_count == 1);
  assert(std::signal(SIGINT, previous_interrupt_handler) != SIG_ERR);

  const auto differently_named = directory / "different_filename.sv";
  {
    std::ofstream output(differently_named);
    output << "module actual_top; endmodule\n";
  }
  std::ostringstream direct_output;
  std::ostringstream direct_error;
  const auto direct_text = differently_named.string();
  const std::vector<const char*> direct_arguments{
      "fsim", "run", direct_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(direct_arguments.size()),
          direct_arguments.data(),
          services,
          direct_output,
          direct_error)
      == 0);
  assert(
      direct_output.str().find("simulation completed at tick 0")
      != std::string::npos);

  std::ostringstream json_output;
  std::ostringstream json_error;
  const std::vector<const char*> json_arguments{
      "fsim",
      "check",
      "--diagnostics=json",
      "--definitely-invalid"};
  assert(
      fsim::cli::run(
          static_cast<int>(json_arguments.size()),
          json_arguments.data(),
          services,
          json_output,
          json_error)
      == 2);
  assert(
      json_error.str().find("\"code\":\"FSIM-CLI-0001\"")
      != std::string::npos);

  std::ostringstream standard_output;
  std::ostringstream standard_error;
  const std::vector<const char*> standard_arguments{
      "fsim",
      "check",
      "--standard=bogus",
      direct_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(standard_arguments.size()),
          standard_arguments.data(),
          services,
          standard_output,
          standard_error)
      == 1);
  assert(
      standard_error.str().find("unsupported standard 'bogus'")
      != std::string::npos);

  const auto scaled_manifest = directory / "scaled.toml";
  const auto scaled_trace = directory / "scaled.vcd";
  {
    std::ofstream output(scaled_manifest);
    output << R"(
schema = 1
[project]
name = "scaled-vcd"
top = "sv:work.tb"
time_resolution = "2ps"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "scaled-cache"

[run]
max_deltas = 1000
trace_file = "scaled.vcd"
)";
  }
  std::ostringstream scaled_output;
  std::ostringstream scaled_error;
  const auto scaled_manifest_text = scaled_manifest.string();
  const std::vector<const char*> scaled_arguments{
      "fsim", "run", "-p", scaled_manifest_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(scaled_arguments.size()),
          scaled_arguments.data(),
          services,
          scaled_output,
          scaled_error)
      == 0);
  std::ifstream scaled_stream(scaled_trace);
  const std::string scaled_vcd{
      std::istreambuf_iterator<char>{scaled_stream},
      std::istreambuf_iterator<char>{}};
  assert(
      scaled_vcd.find("$timescale 1ps $end") != std::string::npos);
  assert(scaled_vcd.find("#4") != std::string::npos);

  const auto timescale_source = directory / "timescale.sv";
  {
    std::ofstream output(timescale_source);
    output << R"(`timescale 10ns/100ps
module timed;
  initial #2 $finish;
endmodule
)";
  }
  fsim::project::Config timescale_config;
  timescale_config.base_directory = directory;
  timescale_config.project.name = "timescale";
  timescale_config.project.top = "sv:work.timed";
  timescale_config.project.time_resolution = "auto";
  timescale_config.build.cache_path = directory / "timescale-cache";
  timescale_config.run.max_deltas = 1000;
  fsim::project::SourceSet timescale_sources;
  timescale_sources.language =
      fsim::project::Language::system_verilog;
  timescale_sources.standard = "2017";
  timescale_sources.library = "work";
  timescale_sources.files.push_back(timescale_source);
  timescale_config.source_sets.push_back(
      std::move(timescale_sources));
  fsim::diagnostic::Engine timescale_diagnostics;
  auto timed_project =
      fsim::app::build_project(
          timescale_config, timescale_diagnostics);
  assert(timed_project);
  assert(timed_project->time_resolution == "100ps");
  fsim::app::Simulation timed_simulation(
      std::move(*timed_project),
      timescale_config.run.max_deltas);
  const auto timed_result = timed_simulation.run();
  assert(timed_result.status == fsim::runtime::RunStatus::stopped);
  assert(timed_result.time == 200);
  timescale_config.project.time_resolution = "1ns";
  fsim::diagnostic::Engine coarse_time_diagnostics;
  assert(!fsim::app::build_project(
      timescale_config, coarse_time_diagnostics));
  assert(coarse_time_diagnostics.has_error());

  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  std::cout << "application tests passed\n";
}
