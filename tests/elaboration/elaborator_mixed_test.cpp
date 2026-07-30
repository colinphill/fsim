// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_mixed_language_and_systemc() {
constexpr std::string_view vhdl_source = R"(
entity counter_vhdl is
  port (
    clk : in std_logic;
    q : out std_logic_vector(7 downto 0)
  );
end entity counter_vhdl;

architecture rtl of counter_vhdl is
  signal count : std_logic_vector(7 downto 0);
begin
  q <= count;
  update: process(clk)
  begin
    if rising_edge(clk) then
      count <= count + 1;
    end if;
  end process update;
end architecture rtl;
)";
    const auto parsed_vhdl = fsim::frontend::parse_text(
        "counter.vhd", vhdl_source, fsim::frontend::Language::Vhdl2008);
    assert(parsed_vhdl.ok());
    auto elaborated_vhdl =
        fsim::elaboration::elaborate(parsed_vhdl.design, "counter_vhdl");
    assert(elaborated_vhdl.ok());

    auto vhdl_interpreter = elaborated_vhdl.design->create_interpreter();
    const auto vhdl_clock = elaborated_vhdl.design->find_signal("clk");
    const auto count = elaborated_vhdl.design->find_signal("count");
    const auto vhdl_q = elaborated_vhdl.design->find_signal("q");
    assert(vhdl_clock && count && vhdl_q);
    vhdl_interpreter->deposit_signal(
        *count, fsim::runtime::PackedLogic4::from_msb_string("00000000"));
    vhdl_interpreter->deposit_signal(
        *vhdl_clock, fsim::runtime::PackedLogic4::from_msb_string("0"));
    vhdl_interpreter->start();
    (void)vhdl_interpreter->run();
    assert(
        vhdl_interpreter->signal_value(*vhdl_q).to_msb_string()
        == "00000000");
    vhdl_interpreter->deposit_signal(
        *vhdl_clock, fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)vhdl_interpreter->run();
    assert(
        vhdl_interpreter->signal_value(*vhdl_q).to_msb_string()
        == "00000001");

    constexpr std::string_view mixed_sv = R"(
module tb;
  logic clk;
  logic reset;
  logic [7:0] q;
  logic [7:0] inverted;

  counter u_counter(.clk(clk), .reset(reset), .q(q));
  child u_child(.value(q), .inverted(inverted));

  initial begin
    clk = 1'b0;
    reset = 1'b1;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 reset = 1'b0;
    #1 clk = 1'b1;
    #1 $finish;
  end
endmodule

module child(
  input logic [7:0] value,
  output logic [7:0] inverted
);
  assign inverted = ~value;
endmodule
)";
    constexpr std::string_view mixed_vhdl = R"(
entity counter is
  port (
    clk : in std_logic;
    reset : in std_logic;
    q : out unsigned(7 downto 0)
  );
end entity counter;

architecture rtl of counter is
begin
  update: process(clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then
        q <= "00000000";
      else
        q <= q + 1;
      end if;
    end if;
  end process update;
end architecture rtl;
)";
    auto parsed_mixed_sv = fsim::frontend::parse_text(
        "tb.sv", mixed_sv, fsim::frontend::Language::SystemVerilog2017);
    auto parsed_mixed_vhdl = fsim::frontend::parse_text(
        "counter.vhd", mixed_vhdl, fsim::frontend::Language::Vhdl2008);
    assert(parsed_mixed_sv.ok() && parsed_mixed_vhdl.ok());
    for (auto& unit : parsed_mixed_vhdl.design.units) {
        parsed_mixed_sv.design.units.push_back(std::move(unit));
    }
    const std::vector<fsim::elaboration::Binding> mixed_bindings{
        {"tb.u_counter", "vhdl:work.counter(rtl)", std::nullopt},
        {"tb.u_child", "sv:work.child", std::nullopt},
    };
    auto elaborated_mixed = fsim::elaboration::elaborate(
        parsed_mixed_sv.design, "sv:work.tb", mixed_bindings);
    if (!elaborated_mixed.ok()) {
        for (const auto& diagnostic : elaborated_mixed.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated_mixed.ok());
    assert(elaborated_mixed.design->signals().size() == 4);
    assert(elaborated_mixed.design->specializations().size() == 3);
    const auto& mixed_specializations =
        elaborated_mixed.design->specializations();
    assert(mixed_specializations[0].id == 0);
    assert(mixed_specializations[0].unit == "sv:work.tb");
    assert(mixed_specializations[0].instance == "tb");
    assert(mixed_specializations[0].source == "tb.sv");
    assert((
        mixed_specializations[0].processes
        == std::vector<fsim::runtime::simir::ProcessId>{0}));
    assert(mixed_specializations[1].id == 1);
    assert(
        mixed_specializations[1].unit
        == "vhdl:work.counter(rtl)");
    assert(mixed_specializations[1].instance == "tb.u_counter");
    assert(mixed_specializations[1].source == "counter.vhd");
    assert((
        mixed_specializations[1].processes
        == std::vector<fsim::runtime::simir::ProcessId>{1}));
    assert(mixed_specializations[2].id == 2);
    assert(mixed_specializations[2].unit == "sv:work.child");
    assert(mixed_specializations[2].instance == "tb.u_child");
    assert(mixed_specializations[2].source == "tb.sv");
    assert((
        mixed_specializations[2].processes
        == std::vector<fsim::runtime::simir::ProcessId>{2}));
    const auto mixed_q = elaborated_mixed.design->find_signal("q");
    const auto mixed_child_q =
        elaborated_mixed.design->find_signal("tb.u_counter.q");
    const auto mixed_inverted =
        elaborated_mixed.design->find_signal("inverted");
    assert(mixed_q && mixed_child_q && mixed_inverted);
    assert(*mixed_q == *mixed_child_q);
    auto mixed_interpreter =
        elaborated_mixed.design->create_interpreter();
    const auto mixed_result = mixed_interpreter->run();
    assert(mixed_result.status == fsim::runtime::RunStatus::stopped);
    assert(mixed_result.time == 5);
    assert(
        mixed_interpreter->signal_value(*mixed_q).to_msb_string()
        == "00000001");
    assert(
        mixed_interpreter->signal_value(*mixed_inverted).to_msb_string()
        == "11111110");

    const auto integer_boundary_vhdl =
        fsim::frontend::parse_text(
            "integer_boundary.vhd",
            R"(
entity integer_boundary is
end entity;
architecture rtl of integer_boundary is
  signal source : integer;
  signal result : integer;
begin
  source <= -2;
  child: sv_integer_child
    port map (value => source, result => result);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    const auto integer_boundary_sv =
        fsim::frontend::parse_text(
            "integer_child.sv",
            R"(
module sv_integer_child(
  input bit signed [31:0] value,
  output bit signed [31:0] result
);
  assign result = value + 1;
endmodule

module sv_logic_integer_child(
  input bit signed [31:0] value,
  output logic signed [31:0] result
);
  assign result = value + 1;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(integer_boundary_vhdl.ok());
    assert(integer_boundary_sv.ok());
    auto integer_boundary_design =
        integer_boundary_vhdl.design;
    integer_boundary_design.units.insert(
        integer_boundary_design.units.end(),
        integer_boundary_sv.design.units.begin(),
        integer_boundary_sv.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        integer_boundary_binding{
            {
                "integer_boundary.child",
                "sv:work.sv_integer_child",
                std::nullopt},
        };
    const auto elaborated_integer_boundary =
        fsim::elaboration::elaborate(
            integer_boundary_design,
            "vhdl:work.integer_boundary(rtl)",
            integer_boundary_binding);
    if (!elaborated_integer_boundary.ok()) {
      for (const auto& diagnostic :
           elaborated_integer_boundary.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_integer_boundary.ok());
    const auto integer_source =
        elaborated_integer_boundary.design->find_signal(
            "source");
    const auto integer_result =
        elaborated_integer_boundary.design->find_signal(
            "result");
    const auto integer_child_source =
        elaborated_integer_boundary.design->find_signal(
            "integer_boundary.child.value");
    assert(
        integer_source && integer_result
        && integer_child_source);
    assert(*integer_source == *integer_child_source);
    auto integer_boundary_interpreter =
        elaborated_integer_boundary.design
            ->create_interpreter();
    const auto integer_boundary_result =
        integer_boundary_interpreter->run();
    assert(
        integer_boundary_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        integer_boundary_interpreter
            ->signal_value(*integer_result)
            .to_msb_string()
        == "11111111111111111111111111111111");
    const std::vector<fsim::elaboration::Binding>
        lossy_integer_boundary_binding{
            {
                "integer_boundary.child",
                "sv:work.sv_logic_integer_child",
                std::nullopt},
        };
    const auto rejected_lossy_integer_boundary =
        fsim::elaboration::elaborate(
            integer_boundary_design,
            "vhdl:work.integer_boundary(rtl)",
            lossy_integer_boundary_binding);
    assert(!rejected_lossy_integer_boundary.ok());
    assert(has_diagnostic(
        rejected_lossy_integer_boundary,
        "FSIM-ELAB-BIND-022"));

    const auto vhdl_integer_output =
        fsim::frontend::parse_text(
            "vhdl_integer_output.vhd",
            R"(
entity vhdl_integer_output is
  port (result : out integer range 1 to 4);
end entity;
architecture rtl of vhdl_integer_output is
begin
  result <= 2;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    const auto sv_integer_parent =
        fsim::frontend::parse_text(
            "sv_integer_parent.sv",
            R"(
module sv_integer_parent;
  bit signed [31:0] result;
  vhdl_integer_output child(.result(result));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(vhdl_integer_output.ok());
    assert(sv_integer_parent.ok());
    auto reverse_integer_design = sv_integer_parent.design;
    reverse_integer_design.units.insert(
        reverse_integer_design.units.end(),
        vhdl_integer_output.design.units.begin(),
        vhdl_integer_output.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        reverse_integer_binding{
            {
                "sv_integer_parent.child",
                "vhdl:work.vhdl_integer_output(rtl)",
                std::nullopt},
        };
    const auto elaborated_reverse_integer =
        fsim::elaboration::elaborate(
            reverse_integer_design,
            "sv:work.sv_integer_parent",
            reverse_integer_binding);
    assert(elaborated_reverse_integer.ok());
    assert(
        std::ranges::any_of(
            elaborated_reverse_integer.design
                ->processes().front().operations,
            [](const auto& operation) {
              const auto* check =
                  std::get_if<
                      fsim::runtime::simir::IntegerCheck>(
                      &operation);
              return check != nullptr
                  && check->lower == 1
                  && check->upper == 4;
            }));
    const auto reverse_integer_result_signal =
        elaborated_reverse_integer.design->find_signal("result");
    assert(reverse_integer_result_signal);
    auto reverse_integer_interpreter =
        elaborated_reverse_integer.design->create_interpreter();
    assert(
        reverse_integer_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        reverse_integer_interpreter
            ->signal_value(*reverse_integer_result_signal)
            .to_msb_string()
        == "00000000000000000000000000000010");

    const auto unsafe_integer_alias =
        fsim::frontend::parse_text(
            "unsafe_integer_alias.vhd",
            R"(
entity positive_child is
  port (value : in positive);
end entity;
architecture rtl of positive_child is
begin
end architecture;

entity unsafe_integer_alias is
end entity;
architecture rtl of unsafe_integer_alias is
  signal source : integer;
  component positive_child is
    port (value : in positive);
  end component;
begin
  child: positive_child port map (value => source);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(unsafe_integer_alias.ok());
    const auto rejected_integer_alias =
        fsim::elaboration::elaborate(
            unsafe_integer_alias.design,
            "vhdl:work.unsafe_integer_alias(rtl)");
    assert(!rejected_integer_alias.ok());
    assert(has_diagnostic(
        rejected_integer_alias,
        "FSIM-ELAB-BIND-051"));

    const auto struct_boundary_sv =
        fsim::frontend::parse_text(
            "struct_boundary.sv",
            R"(
module struct_boundary;
  typedef struct packed {
    logic [3:0] payload;
    bit valid;
  } packet_t;
  packet_t packet;
  struct_sink u_sink(.packet(packet));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    const auto struct_boundary_vhdl =
        fsim::frontend::parse_text(
            "struct_sink.vhd",
            R"(
entity struct_sink is
  port (packet : in std_logic_vector(4 downto 0));
end entity;
architecture rtl of struct_sink is
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(struct_boundary_sv.ok());
    assert(struct_boundary_vhdl.ok());
    auto struct_boundary_design = struct_boundary_sv.design;
    struct_boundary_design.units.insert(
        struct_boundary_design.units.end(),
        struct_boundary_vhdl.design.units.begin(),
        struct_boundary_vhdl.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        struct_boundary_binding{
            {"struct_boundary.u_sink",
             "vhdl:work.struct_sink(rtl)",
             std::nullopt},
        };
    const auto rejected_struct_boundary =
        fsim::elaboration::elaborate(
            struct_boundary_design,
            "sv:work.struct_boundary",
            struct_boundary_binding);
    assert(!rejected_struct_boundary.ok());
    assert(has_diagnostic(
        rejected_struct_boundary, "FSIM-ELAB-BIND-049"));

    constexpr std::string_view vhdl_parent = R"(
entity vhdl_top is
end entity vhdl_top;

architecture rtl of vhdl_top is
  signal source : std_logic_vector(3 downto 0);
  signal inverted : std_logic_vector(3 downto 0);
begin
  source <= "1010";
  u_child: sv_child
    port map (value => source, inverted => inverted);
end architecture rtl;
)";
    constexpr std::string_view sv_bound_child = R"(
module sv_child(
  input logic [3:0] value,
  output logic [3:0] inverted
);
  assign inverted = ~value;
endmodule
)";
    auto parsed_vhdl_parent = fsim::frontend::parse_text(
        "vhdl_top.vhd",
        vhdl_parent,
        fsim::frontend::Language::Vhdl2008);
    auto parsed_sv_child = fsim::frontend::parse_text(
        "sv_child.sv",
        sv_bound_child,
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_vhdl_parent.ok() && parsed_sv_child.ok());
    for (auto& unit : parsed_sv_child.design.units) {
        parsed_vhdl_parent.design.units.push_back(std::move(unit));
    }
    const std::vector<fsim::elaboration::Binding> reverse_binding{
        {"vhdl_top.u_child", "sv:work.sv_child", std::nullopt},
    };
    auto reverse_mixed = fsim::elaboration::elaborate(
        parsed_vhdl_parent.design,
        "vhdl:work.vhdl_top(rtl)",
        reverse_binding);
    if (!reverse_mixed.ok()) {
        for (const auto& diagnostic : reverse_mixed.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(reverse_mixed.ok());
    const auto reverse_source =
        reverse_mixed.design->find_signal("source");
    const auto reverse_output =
        reverse_mixed.design->find_signal("inverted");
    const auto child_source =
        reverse_mixed.design->find_signal("vhdl_top.u_child.value");
    assert(reverse_source && reverse_output && child_source);
    assert(*reverse_source == *child_source);
    auto reverse_interpreter =
        reverse_mixed.design->create_interpreter();
    const auto reverse_result = reverse_interpreter->run();
    assert(reverse_result.status == fsim::runtime::RunStatus::completed);
    assert(
        reverse_interpreter->signal_value(*reverse_source).to_msb_string()
        == "1010");
    assert(
        reverse_interpreter->signal_value(*reverse_output).to_msb_string()
        == "0101");

    constexpr std::string_view systemc_boundary_sv = R"(
module systemc_parent;
  bit clock;
  logic [7:0] value;
  bridge_placeholder u_bridge(.clock(clock), .value(value));
  initial begin
    clock = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_hdl_child #(
  parameter VALUE = 8'hA5
) (
  input bit clock,
  output bit [7:0] value
);
  assign value = VALUE;
endmodule
)";
    const auto parsed_systemc_boundary = fsim::frontend::parse_text(
        "systemc_boundary.sv",
        systemc_boundary_sv,
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_systemc_boundary.ok());
    fsim::frontend::Type systemc_bit{
        fsim::frontend::ValueDomain::Bit2,
        "systemc.bit",
        std::nullopt,
        false};
    fsim::frontend::Type systemc_unsigned{
        fsim::frontend::ValueDomain::Bit2,
        "systemc.unsigned",
        fsim::frontend::PackedRange{7, 0, true},
        false};
    const auto parsed_systemc_parameters =
        fsim::frontend::parse_text(
            "systemc_parameters.sv",
            R"(
module systemc_parameter_host;
  bit [3:0] value;
  bridge_placeholder #(.WIDTH(4)) u_bridge(.value(value));
endmodule

module systemc_invalid_parameter_host;
  bit value;
  bridge_placeholder #(.WIDTH(0)) u_bridge(.value(value));
endmodule

module systemc_unknown_parameter_host;
  bit value;
  bridge_placeholder #(.MISSING(1)) u_bridge(.value(value));
endmodule

module systemc_missing_parameter_host;
  bit value;
  bridge_placeholder u_bridge(.value(value));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_systemc_parameters.ok());
    TestSystemCFactoryProvider parameter_provider;
    parameter_provider.parameters = {
        {"WIDTH", FSIM_SC_CONSTRUCTION_POSITIVE, std::nullopt},
    };
    parameter_provider.prototype.target =
        "systemc:models.parameter_bridge";
    parameter_provider.prototype.ports = {
        {10'001,
         "value",
         systemc_unsigned,
         fsim::frontend::PortDirection::Output,
         0},
    };
    const std::vector<fsim::elaboration::Binding>
        systemc_parameter_binding{
            {"systemc_parameter_host.u_bridge",
             "systemc:models.parameter_bridge",
             std::nullopt},
        };
    const auto systemc_parameterized =
        fsim::elaboration::elaborate(
            parsed_systemc_parameters.design,
            "sv:work.systemc_parameter_host",
            systemc_parameter_binding,
            std::span<const
                fsim::elaboration::SystemCInstanceDescription>{},
            &parameter_provider);
    assert(systemc_parameterized.ok());
    assert((
        parameter_provider.last_values
        == std::vector<std::pair<std::string, std::int64_t>>{
            {"WIDTH", 4}}));
    assert((
        systemc_parameterized.design->systemc_instances().front()
            .construction_values
        == parameter_provider.last_values));
    const auto parameterized_value =
        systemc_parameterized.design->find_signal("value");
    assert(parameterized_value);
    assert(
        systemc_parameterized.design->signals()
            .at(*parameterized_value)
            .width
        == 4);

    const auto reject_systemc_parameter =
        [&](const std::string_view top,
            const std::string_view instance,
            const std::string_view diagnostic) {
          TestSystemCFactoryProvider provider = parameter_provider;
          const std::vector<fsim::elaboration::Binding> binding{
              {std::string{instance},
               "systemc:models.parameter_bridge",
               std::nullopt},
          };
          const auto result = fsim::elaboration::elaborate(
              parsed_systemc_parameters.design,
              top,
              binding,
              std::span<const
                  fsim::elaboration::SystemCInstanceDescription>{},
              &provider);
          assert(!result.ok());
          assert(has_diagnostic(result, diagnostic));
        };
    reject_systemc_parameter(
        "sv:work.systemc_invalid_parameter_host",
        "systemc_invalid_parameter_host.u_bridge",
        "FSIM-ELAB-SC-PARAM-005");
    reject_systemc_parameter(
        "sv:work.systemc_unknown_parameter_host",
        "systemc_unknown_parameter_host.u_bridge",
        "FSIM-ELAB-SC-PARAM-001");
    reject_systemc_parameter(
        "sv:work.systemc_missing_parameter_host",
        "systemc_missing_parameter_host.u_bridge",
        "FSIM-ELAB-SC-PARAM-001");
    auto unavailable_schema_provider = parameter_provider;
    unavailable_schema_provider.schema_failure =
        "intentional schema failure";
    const auto unavailable_schema =
        fsim::elaboration::elaborate(
            parsed_systemc_parameters.design,
            "sv:work.systemc_parameter_host",
            systemc_parameter_binding,
            std::span<const
                fsim::elaboration::SystemCInstanceDescription>{},
            &unavailable_schema_provider);
    assert(!unavailable_schema.ok());
    assert(has_diagnostic(
        unavailable_schema, "FSIM-ELAB-SC-PARAM-007"));
    auto failed_construction_provider = parameter_provider;
    failed_construction_provider.construction_failure =
        "intentional construction failure";
    const auto failed_construction =
        fsim::elaboration::elaborate(
            parsed_systemc_parameters.design,
            "sv:work.systemc_parameter_host",
            systemc_parameter_binding,
            std::span<const
                fsim::elaboration::SystemCInstanceDescription>{},
            &failed_construction_provider);
    assert(!failed_construction.ok());
    assert(has_diagnostic(
        failed_construction, "FSIM-ELAB-SC-PARAM-008"));

    const fsim::elaboration::SystemCInstanceDescription
        nested_systemc{
            "systemc_parent.u_bridge",
            "systemc:models.bridge",
            100,
            0,
            {},
            {
                {101, "clock", systemc_bit,
                 fsim::frontend::PortDirection::Input, 0},
                {102, "value", systemc_unsigned,
                 fsim::frontend::PortDirection::Output, 0},
            },
            {
                {"u_hdl",
                 {{"VALUE", 0x3c}},
                 {
                     {"clock", systemc_bit,
                      fsim::frontend::PortDirection::Input, 101},
                     {"value", systemc_unsigned,
                      fsim::frontend::PortDirection::Output, 102},
                 }},
            },
            {},
            {},
            {},
            {},
            {},
            {}};
    const std::vector<fsim::elaboration::Binding>
        hdl_to_systemc_bindings{
            {"systemc_parent.u_bridge",
             "systemc:models.bridge",
             std::nullopt},
            {"systemc_parent.u_bridge.u_hdl",
             "sv:work.systemc_hdl_child",
             std::nullopt},
        };
    const std::array hdl_to_systemc_instances{nested_systemc};
    auto hdl_to_systemc = fsim::elaboration::elaborate(
        parsed_systemc_boundary.design,
        "sv:work.systemc_parent",
        hdl_to_systemc_bindings,
        hdl_to_systemc_instances);
    if (!hdl_to_systemc.ok()) {
        for (const auto& diagnostic : hdl_to_systemc.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(hdl_to_systemc.ok());
    assert(hdl_to_systemc.design->systemc_instances().size() == 1);
    assert(
        hdl_to_systemc.design->systemc_instances().front().instance
        == "systemc_parent.u_bridge");
    const auto systemc_parent_value =
        hdl_to_systemc.design->find_signal("value");
    const auto systemc_port_value =
        hdl_to_systemc.design->find_signal(
            "systemc_parent.u_bridge.value");
    const auto systemc_child_value =
        hdl_to_systemc.design->find_signal(
            "systemc_parent.u_bridge.u_hdl.value");
    assert(
        systemc_parent_value && systemc_port_value
        && systemc_child_value);
    assert(*systemc_parent_value == *systemc_port_value);
    assert(*systemc_parent_value == *systemc_child_value);
    auto hdl_to_systemc_interpreter =
        hdl_to_systemc.design->create_interpreter();
    const auto hdl_to_systemc_result =
        hdl_to_systemc_interpreter->run();
    assert(
        hdl_to_systemc_result.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        hdl_to_systemc_interpreter
            ->signal_value(*systemc_parent_value)
            .to_msb_string()
        == "00111100");

    auto systemc_root = nested_systemc;
    systemc_root.path = "bridge";
    const std::vector<fsim::elaboration::Binding>
        systemc_to_hdl_bindings{
            {"bridge.u_hdl",
             "sv:work.systemc_hdl_child",
             std::nullopt},
        };
    const std::array systemc_root_instances{systemc_root};
    auto systemc_to_hdl = fsim::elaboration::elaborate(
        parsed_systemc_boundary.design,
        "systemc:models.bridge",
        systemc_to_hdl_bindings,
        systemc_root_instances);
    if (!systemc_to_hdl.ok()) {
        for (const auto& diagnostic : systemc_to_hdl.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(systemc_to_hdl.ok());
    assert(systemc_to_hdl.design->systemc_instances().size() == 1);
    assert(systemc_to_hdl.design->specializations().size() == 1);
    assert((
        systemc_to_hdl.design->specializations().front()
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"VALUE", "60"}}));
    const auto systemc_root_value =
        systemc_to_hdl.design->find_signal("bridge.value");
    const auto systemc_root_child_value =
        systemc_to_hdl.design->find_signal(
            "bridge.u_hdl.value");
    assert(systemc_root_value && systemc_root_child_value);
    assert(*systemc_root_value == *systemc_root_child_value);
    auto systemc_to_hdl_interpreter =
        systemc_to_hdl.design->create_interpreter();
    const auto systemc_to_hdl_result =
        systemc_to_hdl_interpreter->run();
    assert(
        systemc_to_hdl_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        systemc_to_hdl_interpreter
            ->signal_value(*systemc_root_value)
            .to_msb_string()
        == "00111100");

    auto invalid_systemc_actual = systemc_root;
    invalid_systemc_actual.foreign_children.front()
        .construction_actuals = {{"MISSING", 1}};
    const std::array invalid_systemc_actual_instances{
        invalid_systemc_actual};
    const auto rejected_systemc_actual =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            systemc_to_hdl_bindings,
            invalid_systemc_actual_instances);
    assert(!rejected_systemc_actual.ok());
    assert(has_diagnostic(
        rejected_systemc_actual, "FSIM-ELAB-PARAM-001"));

    auto duplicate_systemc_actual = systemc_root;
    duplicate_systemc_actual.foreign_children.front()
        .construction_actuals = {
            {"VALUE", 1},
            {"VALUE", 2},
        };
    const std::array duplicate_systemc_actual_instances{
        duplicate_systemc_actual};
    const auto rejected_duplicate_systemc_actual =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            systemc_to_hdl_bindings,
            duplicate_systemc_actual_instances);
    assert(!rejected_duplicate_systemc_actual.ok());
    assert(has_diagnostic(
        rejected_duplicate_systemc_actual,
        "FSIM-ELAB-PARAM-002"));

    const std::vector<fsim::elaboration::Binding>
        missing_systemc_child_binding;
    const auto missing_systemc_child =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            missing_systemc_child_binding,
            systemc_root_instances);
    assert(!missing_systemc_child.ok());
    assert(has_diagnostic(
        missing_systemc_child, "FSIM-ELAB-BIND-040"));

    auto thread_systemc = systemc_root;
    thread_systemc.foreign_children.clear();
    thread_systemc.processes.push_back({
        150,
        "thread",
        FSIM_SC_THREAD,
        nullptr,
        nullptr,
        {},
        true});
    const std::array thread_systemc_instances{thread_systemc};
    const auto systemc_thread =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            std::span<const fsim::elaboration::Binding>{},
            thread_systemc_instances);
#if defined(FSIM_HAS_BOOST_CONTEXT)
    assert(systemc_thread.ok());
    assert(systemc_thread.design->systemc_processes().size() == 1);
#else
    assert(!systemc_thread.ok());
    assert(has_diagnostic(
        systemc_thread, "FSIM-ELAB-BIND-042"));
#endif

    constexpr std::string_view systemc_boundary_vhdl = R"(
entity systemc_vhdl_parent is
end entity systemc_vhdl_parent;

architecture rtl of systemc_vhdl_parent is
  signal value : std_logic;
  signal inverted : std_logic;
begin
  value <= '1';
  u_bridge: bridge_placeholder
    port map (value => value, inverted => inverted);
end architecture rtl;

entity systemc_vhdl_child is
  generic (
    choose : integer := 1
  );
  port (
    value : in std_logic;
    inverted : out std_logic
  );
end entity systemc_vhdl_child;

architecture rtl of systemc_vhdl_child is
begin
  inverted <= not value;
end architecture rtl;
)";
    const auto parsed_systemc_vhdl = fsim::frontend::parse_text(
        "systemc_boundary.vhd",
        systemc_boundary_vhdl,
        fsim::frontend::Language::Vhdl2008);
    assert(parsed_systemc_vhdl.ok());
    fsim::frontend::Type systemc_logic{
        fsim::frontend::ValueDomain::Logic4,
        "systemc.logic",
        std::nullopt,
        false};
    const fsim::elaboration::SystemCInstanceDescription
        vhdl_nested_systemc{
            "systemc_vhdl_parent.u_bridge",
            "systemc:models.bridge",
            200,
            0,
            {},
            {
                {201, "value", systemc_logic,
                 fsim::frontend::PortDirection::Input, 0},
                {202, "inverted", systemc_logic,
                 fsim::frontend::PortDirection::Output, 0},
            },
            {
                {"u_hdl",
                 {{"CHOOSE", 0}},
                 {
                     {"value", systemc_logic,
                      fsim::frontend::PortDirection::Input, 201},
                     {"inverted", systemc_logic,
                      fsim::frontend::PortDirection::Output, 202},
                 }},
            },
            {},
            {},
            {},
            {},
            {},
            {}};
    const std::vector<fsim::elaboration::Binding>
        vhdl_systemc_bindings{
            {"systemc_vhdl_parent.u_bridge",
             "systemc:models.bridge",
             std::nullopt},
            {"systemc_vhdl_parent.u_bridge.u_hdl",
             "vhdl:work.systemc_vhdl_child(rtl)",
             std::nullopt},
        };
    const std::array vhdl_systemc_instances{
        vhdl_nested_systemc};
    auto vhdl_systemc = fsim::elaboration::elaborate(
        parsed_systemc_vhdl.design,
        "vhdl:work.systemc_vhdl_parent(rtl)",
        vhdl_systemc_bindings,
        vhdl_systemc_instances);
    if (!vhdl_systemc.ok()) {
        for (const auto& diagnostic : vhdl_systemc.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl_systemc.ok());
    assert(vhdl_systemc.design->systemc_instances().size() == 1);
    const auto vhdl_systemc_child_specialization =
        std::find_if(
            vhdl_systemc.design->specializations().begin(),
            vhdl_systemc.design->specializations().end(),
            [](const auto& specialization) {
                return specialization.instance
                    == "systemc_vhdl_parent.u_bridge.u_hdl";
            });
    assert(
        vhdl_systemc_child_specialization
        != vhdl_systemc.design->specializations().end());
    assert((
        vhdl_systemc_child_specialization->parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"choose", "0"}}));
    const auto vhdl_systemc_value =
        vhdl_systemc.design->find_signal("value");
    const auto vhdl_systemc_child_value =
        vhdl_systemc.design->find_signal(
            "systemc_vhdl_parent.u_bridge.u_hdl.value");
    const auto vhdl_systemc_inverted =
        vhdl_systemc.design->find_signal("inverted");
    assert(
        vhdl_systemc_value && vhdl_systemc_child_value
        && vhdl_systemc_inverted);
    assert(*vhdl_systemc_value == *vhdl_systemc_child_value);
    auto vhdl_systemc_interpreter =
        vhdl_systemc.design->create_interpreter();
    const auto vhdl_systemc_result =
        vhdl_systemc_interpreter->run();
    assert(
        vhdl_systemc_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        vhdl_systemc_interpreter
            ->signal_value(*vhdl_systemc_inverted)
            .to_msb_string()
        == "0");

    auto colliding_sv = fsim::frontend::parse_text(
        "duplicate.sv",
        "module duplicate; logic sv_only; endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    auto colliding_vhdl = fsim::frontend::parse_text(
        "duplicate.vhd",
        R"(
entity duplicate is
  port (vhdl_only : in std_logic);
end entity duplicate;
architecture rtl of duplicate is
begin
end architecture rtl;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(colliding_sv.ok() && colliding_vhdl.ok());
    colliding_sv.design.units.front().library = "other";
    for (auto& unit : colliding_vhdl.design.units) {
        unit.library = "work";
        colliding_sv.design.units.push_back(std::move(unit));
    }
    auto selected_vhdl = fsim::elaboration::elaborate(
        colliding_sv.design, "vhdl:work.duplicate(rtl)");
    assert(selected_vhdl.ok());
    assert(selected_vhdl.design->find_signal("vhdl_only"));
    assert(!selected_vhdl.design->find_signal("sv_only"));
    const auto ambiguous_vhdl_top = fsim::elaboration::elaborate(
        colliding_sv.design, "vhdl:work.duplicate");
    assert(!ambiguous_vhdl_top.ok());
    assert(has_diagnostic(ambiguous_vhdl_top, "FSIM-ELAB-004"));

    auto cross_library = fsim::frontend::parse_text(
        "cross_library.vhd",
        R"(
entity child is
  port (value : in std_logic);
end entity;
architecture rtl of child is
begin
end architecture;

entity parent is
  port (value : in std_logic);
end entity;
architecture rtl of parent is
begin
  selected: entity other.child(rtl)
    port map (value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(cross_library.ok());
    for (auto& unit : cross_library.design.units) {
        unit.library =
            unit.name == "child"
                || (unit.kind
                        == fsim::frontend::UnitKind::VhdlArchitecture
                    && unit.primary_name == "child")
            ? "other"
            : "work";
    }
    const auto selected_library = fsim::elaboration::elaborate(
        cross_library.design, "vhdl:work.parent(rtl)");
    assert(selected_library.ok());
    assert(selected_library.design->find_signal(
        "parent.selected.value"));

    const std::vector<fsim::elaboration::Binding>
        architectureless_binding{
            {"tb.u_counter", "vhdl:work.counter", std::nullopt},
        };
    const auto rejected_architectureless =
        fsim::elaboration::elaborate(
            parsed_mixed_sv.design,
            "sv:work.tb",
            architectureless_binding);
    assert(!rejected_architectureless.ok());
    assert(has_diagnostic(
        rejected_architectureless, "FSIM-ELAB-BIND-016"));

    const auto conflicting_systemc_alias_source =
        fsim::frontend::parse_text(
            "conflicting_systemc_alias.sv",
            R"(
module conflict_host;
  logic first;
  logic second;
  conflict_placeholder u_conflict(
    .first(first),
    .second(second));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(conflicting_systemc_alias_source.ok());
    const fsim::elaboration::SystemCInstanceDescription
        conflicting_systemc_instance{
            "conflict_host.u_conflict",
            "systemc:models.conflict",
            1000,
            0,
            {},
            {
                {1001,
                 "first",
                 systemc_logic,
                 fsim::frontend::PortDirection::Input,
                 1003},
                {1002,
                 "second",
                 systemc_logic,
                 fsim::frontend::PortDirection::Input,
                 1003},
            },
            {},
            {},
            {},
            {{1003, "shared"}},
            {{1003,
              "shared",
              systemc_logic,
              fsim::runtime::PackedLogic4::from_msb_string("0")}},
            {},
            {}};
    const std::vector<fsim::elaboration::Binding>
        conflicting_systemc_bindings{
            {"conflict_host.u_conflict",
             "systemc:models.conflict",
             std::nullopt},
        };
    const auto rejected_systemc_alias =
        fsim::elaboration::elaborate(
            conflicting_systemc_alias_source.design,
            "sv:work.conflict_host",
            conflicting_systemc_bindings,
            std::span{&conflicting_systemc_instance, 1});
    assert(!rejected_systemc_alias.ok());
    assert(has_diagnostic(
        rejected_systemc_alias, "FSIM-ELAB-BIND-046"));

    auto invalid_native_hierarchy = conflicting_systemc_instance;
    for (auto& port : invalid_native_hierarchy.ports) {
        port.bound_object = 0;
    }
    fsim::elaboration::SystemCInstanceDescription invalid_native_child;
    invalid_native_child.path = "conflict_host.u_conflict.leaf";
    invalid_native_child.target = "systemc:models.conflict";
    invalid_native_child.handle = 2000;
    invalid_native_child.parent = 9999;
    invalid_native_hierarchy.native_children.push_back(
        std::move(invalid_native_child));
    const std::array invalid_native_instances{
        invalid_native_hierarchy};
    const auto rejected_native_hierarchy =
        fsim::elaboration::elaborate(
            conflicting_systemc_alias_source.design,
            "sv:work.conflict_host",
            conflicting_systemc_bindings,
            invalid_native_instances);
    assert(!rejected_native_hierarchy.ok());
    assert(has_diagnostic(
        rejected_native_hierarchy, "FSIM-ELAB-BIND-047"));
}

} // namespace fsim::tests::elaboration
