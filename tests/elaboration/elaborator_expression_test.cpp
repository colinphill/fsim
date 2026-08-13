// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_case_and_expression_lowering() {
const auto case_process = fsim::frontend::parse_text(
        "case_process.sv",
        R"(
module case_process;
  logic [1:0] selector;
  logic [1:0] result;
  always_comb case (selector)
    2'b00: result = 2'b00;
    2'b01, 2'b10: result = 2'b01;
    2'bx0: result = 2'b10;
    2'bz1: result = 2'b11;
    default: result = 2'b00;
  endcase
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(case_process.ok());
    const auto elaborated_case =
        fsim::elaboration::elaborate(
            case_process.design, "sv:work.case_process");
    assert(elaborated_case.ok());
    const auto case_selector =
        elaborated_case.design->find_signal("selector");
    const auto case_result =
        elaborated_case.design->find_signal("result");
    assert(case_selector && case_result);
    assert((
        elaborated_case.design->processes().front()
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*case_selector,
             fsim::runtime::simir::EdgeKind::any}}));
    const auto has_case_equality = std::any_of(
        elaborated_case.design->processes().front()
            .operations.begin(),
        elaborated_case.design->processes().front()
            .operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
            const auto* binary =
                fsim::runtime::simir::operation_get_if<fsim::runtime::simir::Binary>(
                    &operation);
            return binary != nullptr
                && binary->operation
                    == fsim::runtime::simir::BinaryOperator::
                        case_equal;
        });
    assert(has_case_equality);

    for (const auto& [keyword, expected_operator] :
         std::array{
             std::pair{
                 std::string_view{"casez"},
                 fsim::runtime::simir::BinaryOperator::casez_equal},
             std::pair{
                 std::string_view{"casex"},
                 fsim::runtime::simir::BinaryOperator::casex_equal}}) {
        const auto wildcard_case =
            fsim::frontend::parse_text(
                "wildcard_case.sv",
                "module wildcard_case;\n"
                "  logic [1:0] selector;\n"
                "  logic result;\n"
                "  always_comb "
                    + std::string{keyword}
                    + " (selector)\n"
                      "    2'b0z: result = 1'b1;\n"
                      "    default: result = 1'b0;\n"
                      "  endcase\n"
                      "endmodule\n",
                fsim::frontend::Language::SystemVerilog2017);
        assert(wildcard_case.ok());
        const auto elaborated_wildcard_case =
            fsim::elaboration::elaborate(
                wildcard_case.design, "sv:work.wildcard_case");
        assert(elaborated_wildcard_case.ok());
        assert(std::any_of(
            elaborated_wildcard_case.design->processes().front()
                .operations.begin(),
            elaborated_wildcard_case.design->processes().front()
                .operations.end(),
            [&](const fsim::runtime::simir::Operation& operation) {
                const auto* binary =
                    fsim::runtime::simir::operation_get_if<fsim::runtime::simir::Binary>(
                        &operation);
                return binary != nullptr
                    && binary->operation == expected_operator;
            }));
    }

    auto malformed_case_design = case_process.design;
    malformed_case_design.units.front().processes.front()
        .statements.front().case_match_kind =
        static_cast<fsim::frontend::CaseMatchKind>(255);
    const auto rejected_matching_mode =
        fsim::elaboration::elaborate(
            malformed_case_design, "sv:work.case_process");
    assert(!rejected_matching_mode.ok());
    assert(has_diagnostic(
        rejected_matching_mode, "FSIM-ELAB-081"));

    auto case_interpreter =
        elaborated_case.design->create_interpreter();
    (void)case_interpreter->run();
    for (const auto& [selector_value, expected] :
         std::vector<std::pair<std::string, std::string>>{
             {"00", "00"},
             {"01", "01"},
             {"10", "01"},
             {"x0", "10"},
             {"z1", "11"},
             {"11", "00"}}) {
        case_interpreter->deposit_signal(
            *case_selector,
            fsim::runtime::PackedLogic4::from_msb_string(
                selector_value));
        (void)case_interpreter->run();
        assert(
            case_interpreter
                ->signal_value(*case_result)
                .to_msb_string()
            == expected);
    }

    const auto mismatched_case = fsim::frontend::parse_text(
        "mismatched_case.sv",
        R"(
module mismatched_case;
  logic [1:0] selector;
  logic result;
  always_comb case (selector)
    1'b0: result = 1'b0;
    default: result = 1'b1;
  endcase
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mismatched_case.ok());
    const auto rejected_case =
        fsim::elaboration::elaborate(
            mismatched_case.design, "sv:work.mismatched_case");
    assert(!rejected_case.ok());
    assert(has_diagnostic(rejected_case, "FSIM-ELAB-063"));

    const auto conditional_process = fsim::frontend::parse_text(
        "conditional_process.sv",
        R"(
module conditional_process;
  logic select;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic [3:0] result;
  bit [7:0] bit_lhs;
  bit [7:0] bit_rhs;
  bit [15:0] bit_result;
  logic signed [7:0] signed_lhs;
  logic signed [7:0] signed_rhs;
  logic signed [15:0] signed_result;
  always_comb result = select ? lhs : rhs;
  always_comb bit_result = bit_lhs + bit_rhs;
  always_comb signed_result = signed_lhs + signed_rhs;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(conditional_process.ok());
    const auto elaborated_conditional =
        fsim::elaboration::elaborate(
            conditional_process.design,
            "sv:work.conditional_process");
    assert(elaborated_conditional.ok());
    const auto conditional_select =
        elaborated_conditional.design->find_signal("select");
    const auto conditional_lhs =
        elaborated_conditional.design->find_signal("lhs");
    const auto conditional_rhs =
        elaborated_conditional.design->find_signal("rhs");
    const auto conditional_result =
        elaborated_conditional.design->find_signal("result");
    assert(
        conditional_select && conditional_lhs
        && conditional_rhs && conditional_result);
    assert((
        elaborated_conditional.design->processes().front()
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*conditional_lhs,
             fsim::runtime::simir::EdgeKind::any},
            {*conditional_rhs,
             fsim::runtime::simir::EdgeKind::any},
            {*conditional_select,
             fsim::runtime::simir::EdgeKind::any}}));
    const auto& conditional_profiles =
        elaborated_conditional.design->processes().front()
            .expression_profiles;
    assert(std::ranges::any_of(
        conditional_profiles,
        [](const auto& profile) {
          return profile.width == 4
              && profile.sizing
                  == fsim::runtime::simir::ExpressionSizingKind::
                      context_determined
              && profile.domain
                  == fsim::runtime::simir::ExpressionValueDomain::
                      four_state;
        }));
    const auto has_resolved_profile =
        [&](const bool is_signed,
            const fsim::runtime::simir::ExpressionValueDomain domain) {
          return std::ranges::any_of(
              elaborated_conditional.design->processes(),
              [&](const auto& process) {
                return std::ranges::any_of(
                    process.expression_profiles,
                    [&](const auto& profile) {
                      return profile.width == 16
                          && profile.is_signed == is_signed
                          && profile.sizing
                              == fsim::runtime::simir::
                                  ExpressionSizingKind::
                                      context_determined
                          && profile.domain == domain;
                    });
              });
        };
    assert(has_resolved_profile(
        false,
        fsim::runtime::simir::ExpressionValueDomain::two_state));
    assert(has_resolved_profile(
        true,
        fsim::runtime::simir::ExpressionValueDomain::four_state));
    auto conditional_interpreter =
        elaborated_conditional.design->create_interpreter();
    conditional_interpreter->deposit_signal(
        *conditional_lhs,
        fsim::runtime::PackedLogic4::from_msb_string("101z"));
    conditional_interpreter->deposit_signal(
        *conditional_rhs,
        fsim::runtime::PackedLogic4::from_msb_string("100z"));
    conditional_interpreter->deposit_signal(
        *conditional_select,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    (void)conditional_interpreter->run();
    assert(
        conditional_interpreter
            ->signal_value(*conditional_result)
            .to_msb_string()
        == "100Z");
    conditional_interpreter->deposit_signal(
        *conditional_select,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)conditional_interpreter->run();
    assert(
        conditional_interpreter
            ->signal_value(*conditional_result)
            .to_msb_string()
        == "101Z");
    for (const auto unknown : {"x", "z"}) {
        conditional_interpreter->deposit_signal(
            *conditional_select,
            fsim::runtime::PackedLogic4::from_msb_string(unknown));
        (void)conditional_interpreter->run();
        assert(
            conditional_interpreter
                ->signal_value(*conditional_result)
                .to_msb_string()
            == "10XZ");
    }

    const auto sized_conditional = fsim::frontend::parse_text(
        "sized_conditional.sv",
        R"(
module vector_condition;
  logic [1:0] select;
  logic result;
  always_comb result = select ? 1'b0 : 1'b1;
endmodule
module mismatched_alternatives;
  logic select;
  logic [1:0] result;
  always_comb result = select ? 2'b00 : 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(sized_conditional.ok());
    const auto elaborated_vector_condition =
        fsim::elaboration::elaborate(
            sized_conditional.design,
            "sv:work.vector_condition");
    assert(elaborated_vector_condition.ok());
    const auto elaborated_alternatives =
        fsim::elaboration::elaborate(
            sized_conditional.design,
            "sv:work.mismatched_alternatives");
    assert(elaborated_alternatives.ok());

    const auto comparison_process = fsim::frontend::parse_text(
        "comparison_process.sv",
        R"(
module comparison_process;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic neq;
  logic lt;
  logic le;
  logic gt;
  logic ge;
  logic logical_not;
  logic case_eq;
  logic case_neq;
  logic wildcard_eq;
  logic wildcard_neq;
  always_comb begin
    neq = lhs != rhs;
    lt = lhs < rhs;
    le = lhs <= rhs;
    gt = lhs > rhs;
    ge = lhs >= rhs;
    logical_not = !lhs;
    case_eq = lhs === rhs;
    case_neq = lhs !== rhs;
    wildcard_eq = lhs ==? rhs;
    wildcard_neq = lhs !=? rhs;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(comparison_process.ok());
    const auto elaborated_comparisons =
        fsim::elaboration::elaborate(
            comparison_process.design,
            "sv:work.comparison_process");
    assert(elaborated_comparisons.ok());
    const auto comparison_lhs =
        elaborated_comparisons.design->find_signal("lhs");
    const auto comparison_rhs =
        elaborated_comparisons.design->find_signal("rhs");
    const auto comparison_neq =
        elaborated_comparisons.design->find_signal("neq");
    const auto comparison_lt =
        elaborated_comparisons.design->find_signal("lt");
    const auto comparison_le =
        elaborated_comparisons.design->find_signal("le");
    const auto comparison_gt =
        elaborated_comparisons.design->find_signal("gt");
    const auto comparison_ge =
        elaborated_comparisons.design->find_signal("ge");
    const auto comparison_not =
        elaborated_comparisons.design->find_signal("logical_not");
    const auto comparison_case_eq =
        elaborated_comparisons.design->find_signal("case_eq");
    const auto comparison_case_neq =
        elaborated_comparisons.design->find_signal("case_neq");
    const auto comparison_wildcard_eq =
        elaborated_comparisons.design->find_signal("wildcard_eq");
    const auto comparison_wildcard_neq =
        elaborated_comparisons.design->find_signal("wildcard_neq");
    assert(
        comparison_lhs && comparison_rhs && comparison_neq
        && comparison_lt && comparison_le && comparison_gt
        && comparison_ge && comparison_not
        && comparison_case_eq && comparison_case_neq
        && comparison_wildcard_eq && comparison_wildcard_neq);
    assert(std::any_of(
        elaborated_comparisons.design->processes().front()
            .operations.begin(),
        elaborated_comparisons.design->processes().front()
            .operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          const auto* binary =
              fsim::runtime::simir::operation_get_if<fsim::runtime::simir::Binary>(&operation);
          return binary != nullptr
              && binary->operation
                  == fsim::runtime::simir::BinaryOperator::
                      wildcard_equal;
        }));
    auto comparison_interpreter =
        elaborated_comparisons.design->create_interpreter();
    const auto run_comparison =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::array<std::string_view, 10>& expected) {
          comparison_interpreter->deposit_signal(
              *comparison_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          comparison_interpreter->deposit_signal(
              *comparison_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          (void)comparison_interpreter->run();
          const std::array signals{
              *comparison_neq,
              *comparison_lt,
              *comparison_le,
              *comparison_gt,
              *comparison_ge,
              *comparison_not,
              *comparison_case_eq,
              *comparison_case_neq,
              *comparison_wildcard_eq,
              *comparison_wildcard_neq};
          for (std::size_t index = 0; index < signals.size();
               ++index) {
            assert(
                comparison_interpreter
                    ->signal_value(signals[index])
                    .to_msb_string()
                == expected[index]);
          }
        };
    run_comparison(
        "0010", "0011",
        {"1", "1", "1", "0", "0", "0", "0", "1", "0", "1"});
    run_comparison(
        "0000", "0000",
        {"0", "0", "1", "0", "1", "1", "1", "0", "1", "0"});
    run_comparison(
        "00X0", "0011",
        {"X", "X", "X", "X", "X", "X", "0", "1", "X", "X"});
    run_comparison(
        "01X0", "0011",
        {"X", "X", "X", "X", "X", "0", "0", "1", "X", "X"});
    run_comparison(
        "00X0", "00X0",
        {"X", "X", "X", "X", "X", "X", "1", "0", "1", "0"});
    run_comparison(
        "01Z0", "01Z0",
        {"X", "X", "X", "X", "X", "0", "1", "0", "1", "0"});
    run_comparison(
        "00X0", "00Z0",
        {"X", "X", "X", "X", "X", "X", "0", "1", "1", "0"});
    run_comparison(
        "X101", "0001",
        {"X", "X", "X", "X", "X", "0", "0", "1", "X", "X"});

    const auto signed_comparison = fsim::frontend::parse_text(
        "signed_comparison.sv",
        R"(
module signed_comparison;
  logic signed [3:0] lhs;
  logic signed [3:0] rhs;
  logic result;
  always_comb result = lhs < rhs;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(signed_comparison.ok());
    const auto elaborated_signed_comparison =
        fsim::elaboration::elaborate(
            signed_comparison.design,
            "sv:work.signed_comparison");
    assert(elaborated_signed_comparison.ok());
    const auto signed_comparison_lhs =
        elaborated_signed_comparison.design->find_signal("lhs");
    const auto signed_comparison_rhs =
        elaborated_signed_comparison.design->find_signal("rhs");
    const auto signed_comparison_result =
        elaborated_signed_comparison.design->find_signal("result");
    assert(
        signed_comparison_lhs && signed_comparison_rhs
        && signed_comparison_result);
    auto signed_comparison_interpreter =
        elaborated_signed_comparison.design->create_interpreter();
    signed_comparison_interpreter->deposit_signal(
        *signed_comparison_lhs,
        fsim::runtime::PackedLogic4::from_msb_string("1111"));
    signed_comparison_interpreter->deposit_signal(
        *signed_comparison_rhs,
        fsim::runtime::PackedLogic4::from_msb_string("0001"));
    (void)signed_comparison_interpreter->run();
    assert(
        signed_comparison_interpreter
            ->signal_value(*signed_comparison_result)
            .to_msb_string()
        == "1");

    const auto systemverilog_power =
        fsim::frontend::parse_text(
            "systemverilog_power.sv",
            R"(
module systemverilog_power #(
  parameter int PARAMETER_POWER = 3 ** 4
);
  logic [7:0] positive;
  logic [7:0] zero_exponent;
  logic [15:0] left_associative;
  logic signed [7:0] negative_exponent;
  logic signed [7:0] minus_one_negative;
  logic signed [7:0] zero_negative;
  logic [7:0] unknown_operand;
  logic [7:0] parameter_power;
  initial begin
    positive = 8'd3 ** 8'd4;
    zero_exponent = 8'd7 ** 8'd0;
    left_associative = 16'd2 ** 16'd3 ** 16'd2;
    negative_exponent =
        $signed(8'hfe) ** $signed(8'hfd);
    minus_one_negative =
        $signed(8'hff) ** $signed(8'hfd);
    zero_negative =
        $signed(8'h00) ** $signed(8'hff);
    unknown_operand = 8'b000000x1 ** 8'd2;
    parameter_power = PARAMETER_POWER;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(systemverilog_power.ok());
    const auto elaborated_systemverilog_power =
        fsim::elaboration::elaborate(
            systemverilog_power.design,
            "sv:work.systemverilog_power");
    if (!elaborated_systemverilog_power.ok()) {
        for (const auto& diagnostic :
             elaborated_systemverilog_power.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_systemverilog_power.ok());
    auto systemverilog_power_interpreter =
        elaborated_systemverilog_power.design
            ->create_interpreter();
    const auto systemverilog_power_result =
        systemverilog_power_interpreter->run();
    assert(
        systemverilog_power_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto& [name, expected] :
         std::initializer_list<
             std::pair<std::string_view, std::string_view>>{
             {"positive", "01010001"},
             {"zero_exponent", "00000001"},
             {"left_associative", "0000000001000000"},
             {"negative_exponent", "00000000"},
             {"minus_one_negative", "11111111"},
             {"zero_negative", "XXXXXXXX"},
             {"unknown_operand", "XXXXXXXX"},
             {"parameter_power", "01010001"}}) {
        const auto signal =
            elaborated_systemverilog_power.design
                ->find_signal(name);
        assert(signal);
        assert(
            systemverilog_power_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == expected);
    }

    const auto power_context = fsim::frontend::parse_text(
        "power_context.sv",
        R"(
module power_context;
  logic [15:0] result;
  initial result = 8'd2 ** 32'd8;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(power_context.ok());
    const auto elaborated_power_context =
        fsim::elaboration::elaborate(
            power_context.design, "sv:work.power_context");
    assert(elaborated_power_context.ok());
    const auto& power_profiles =
        elaborated_power_context.design->processes().front()
            .expression_profiles;
    assert(std::ranges::any_of(
        power_profiles,
        [](const auto& profile) {
          return profile.source.path == "power_context.sv"
              && profile.source.line == 4
              && profile.width == 16
              && !profile.is_signed
              && profile.sizing
                  == fsim::runtime::simir::ExpressionSizingKind::
                      context_determined;
        }));
    auto power_context_interpreter =
        elaborated_power_context.design->create_interpreter();
    assert(
        power_context_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto power_context_result =
        elaborated_power_context.design->find_signal("result");
    assert(power_context_result);
    assert(
        power_context_interpreter
            ->signal_value(*power_context_result)
            .to_msb_string()
        == "0000000100000000");

    const auto vhdl_power = fsim::frontend::parse_text(
        "vhdl_power.vhd",
        R"(
entity vhdl_power is
end entity;

architecture rtl of vhdl_power is
  signal positive : unsigned(7 downto 0);
  signal negative_base : signed(7 downto 0);
  signal zero_exponent : unsigned(7 downto 0);
begin
  positive <= "00000011" ** 4;
  negative_base <= "11111110" ** 3;
  zero_exponent <= "00000111" ** 0;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_power.ok());
    const auto elaborated_vhdl_power =
        fsim::elaboration::elaborate(
            vhdl_power.design,
            "vhdl:work.vhdl_power(rtl)");
    if (!elaborated_vhdl_power.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_power.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_power.ok());
    auto vhdl_power_interpreter =
        elaborated_vhdl_power.design->create_interpreter();
    const auto vhdl_power_result =
        vhdl_power_interpreter->run();
    assert(
        vhdl_power_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto& [name, expected] :
         std::initializer_list<
             std::pair<std::string_view, std::string_view>>{
             {"positive", "01010001"},
             {"negative_base", "11111000"},
             {"zero_exponent", "00000001"}}) {
        const auto signal =
            elaborated_vhdl_power.design->find_signal(name);
        assert(signal);
        assert(
            vhdl_power_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == expected);
    }

    const auto invalid_vhdl_power =
        fsim::frontend::parse_text(
            "invalid_vhdl_power.vhd",
            R"(
entity invalid_vhdl_power is
end entity;

architecture rtl of invalid_vhdl_power is
  signal result : unsigned(7 downto 0);
begin
  result <= "00000010" ** (-1);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_power.ok());
    const auto rejected_vhdl_power =
        fsim::elaboration::elaborate(
            invalid_vhdl_power.design,
            "vhdl:work.invalid_vhdl_power(rtl)");
    assert(!rejected_vhdl_power.ok());
    assert(has_diagnostic(
        rejected_vhdl_power, "FSIM-ELAB-091"));

    const auto invalid_vhdl_conditional =
        fsim::frontend::parse_text(
            "invalid_vhdl_conditional.vhd",
            R"(
entity invalid_vhdl_conditional is
end entity;

architecture rtl of invalid_vhdl_conditional is
  signal choose : std_logic;
  signal result : std_logic;
begin
  result <= '1' when choose else '0';
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_conditional.ok());
    const auto rejected_vhdl_conditional =
        fsim::elaboration::elaborate(
            invalid_vhdl_conditional.design,
            "vhdl:work.invalid_vhdl_conditional(rtl)");
    assert(!rejected_vhdl_conditional.ok());
    assert(has_diagnostic(
        rejected_vhdl_conditional, "FSIM-ELAB-092"));

    const auto signedness_casts = fsim::frontend::parse_text(
        "signedness_casts.sv",
        R"(
module signedness_casts;
  logic [3:0] unsigned_value;
  logic signed [3:0] signed_value;
  logic signed_less;
  logic unsigned_less;
  logic [3:0] signed_shift;
  logic [3:0] unsigned_shift;
  logic known_is_unknown;
  logic xz_is_unknown;
  logic [31:0] object_bits;
  logic [31:0] concatenation_bits;
  logic onehot_zero;
  logic onehot_single;
  logic onehot_multiple;
  logic onehot_unknown;
  logic onehot0_zero;
  logic onehot0_single;
  logic onehot0_multiple;
  logic onehot0_unknown;
  logic signed [31:0] countones_zero;
  logic signed [31:0] countones_single;
  logic signed [31:0] countones_multiple;
  logic signed [31:0] countones_unknown;
  logic signed [31:0] countbits_known;
  logic signed [31:0] countbits_unknown;
  logic signed [31:0] countbits_zero_x;
  always_comb begin
    signed_less = $signed(unsigned_value) < signed_value;
    unsigned_less = $unsigned(signed_value) < unsigned_value;
    signed_shift = $signed(unsigned_value) >>> 1;
    unsigned_shift = $unsigned(signed_value) >>> 1;
    known_is_unknown = $isunknown(unsigned_value);
    xz_is_unknown = $isunknown(4'b10xz);
    object_bits = $bits(unsigned_value);
    concatenation_bits = $bits({unsigned_value, signed_value});
    onehot_zero = $onehot(4'b0000);
    onehot_single = $onehot(4'b0010);
    onehot_multiple = $onehot(4'b1010);
    onehot_unknown = $onehot(4'bx001);
    onehot0_zero = $onehot0(4'b0000);
    onehot0_single = $onehot0(4'b0010);
    onehot0_multiple = $onehot0(4'b1010);
    onehot0_unknown = $onehot0(4'bz001);
    countones_zero = $countones(4'b0000);
    countones_single = $countones(4'b0010);
    countones_multiple = $countones(4'b1011);
    countones_unknown = $countones(4'bxz01);
    countbits_known = $countbits(4'b10xz, 1'b0, 1'b1);
    countbits_unknown = $countbits(4'b10xz, 1'bx, 1'bz);
    countbits_zero_x = $countbits(4'b10xz, 1'b0, 1'bx);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(signedness_casts.ok());
    const auto elaborated_signedness_casts =
        fsim::elaboration::elaborate(
            signedness_casts.design,
            "sv:work.signedness_casts");
    assert(elaborated_signedness_casts.ok());
    const auto unsigned_cast_input =
        elaborated_signedness_casts.design->find_signal(
            "unsigned_value");
    const auto signed_cast_input =
        elaborated_signedness_casts.design->find_signal(
            "signed_value");
    const std::array signedness_cast_outputs{
        elaborated_signedness_casts.design->find_signal(
            "signed_less"),
        elaborated_signedness_casts.design->find_signal(
            "unsigned_less"),
        elaborated_signedness_casts.design->find_signal(
            "signed_shift"),
        elaborated_signedness_casts.design->find_signal(
            "unsigned_shift"),
        elaborated_signedness_casts.design->find_signal(
            "known_is_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "xz_is_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "object_bits"),
        elaborated_signedness_casts.design->find_signal(
            "concatenation_bits"),
        elaborated_signedness_casts.design->find_signal(
            "onehot_zero"),
        elaborated_signedness_casts.design->find_signal(
            "onehot_single"),
        elaborated_signedness_casts.design->find_signal(
            "onehot_multiple"),
        elaborated_signedness_casts.design->find_signal(
            "onehot_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "onehot0_zero"),
        elaborated_signedness_casts.design->find_signal(
            "onehot0_single"),
        elaborated_signedness_casts.design->find_signal(
            "onehot0_multiple"),
        elaborated_signedness_casts.design->find_signal(
            "onehot0_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "countones_zero"),
        elaborated_signedness_casts.design->find_signal(
            "countones_single"),
        elaborated_signedness_casts.design->find_signal(
            "countones_multiple"),
        elaborated_signedness_casts.design->find_signal(
            "countones_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "countbits_known"),
        elaborated_signedness_casts.design->find_signal(
            "countbits_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "countbits_zero_x")};
    assert(unsigned_cast_input && signed_cast_input);
    assert(std::ranges::all_of(
        signedness_cast_outputs,
        [](const auto signal) { return signal.has_value(); }));
    const auto& signedness_operations =
        elaborated_signedness_casts.design->processes().front()
            .operations;
    assert(
        std::count_if(
            signedness_operations.begin(),
            signedness_operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              const auto* point =
                  fsim::runtime::simir::operation_get_if<
                      fsim::runtime::simir::DebugPoint>(
                      &operation);
              return point != nullptr
                  && point->kind
                      == fsim::runtime::simir::DebugPointKind::call
                  && point->source.path == "signedness_casts.sv"
                  && point->source.line != 0
                  && point->source.column != 0;
            })
        == 23);
    auto signedness_cast_interpreter =
        elaborated_signedness_casts.design
            ->create_interpreter();
    signedness_cast_interpreter->deposit_signal(
        *unsigned_cast_input,
        fsim::runtime::PackedLogic4::from_msb_string("1111"));
    signedness_cast_interpreter->deposit_signal(
        *signed_cast_input,
        fsim::runtime::PackedLogic4::from_msb_string("0001"));
    (void)signedness_cast_interpreter->run();
    const std::array<std::string_view, 23> expected_signedness_casts{
        "1",
        "1",
        "1111",
        "0000",
        "0",
        "1",
        "00000000000000000000000000000100",
        "00000000000000000000000000001000",
        "0",
        "1",
        "0",
        "1",
        "1",
        "1",
        "0",
        "1",
        "00000000000000000000000000000000",
        "00000000000000000000000000000001",
        "00000000000000000000000000000011",
        "00000000000000000000000000000001",
        "00000000000000000000000000000010",
        "00000000000000000000000000000010",
        "00000000000000000000000000000010"};
    for (std::size_t index = 0;
         index < signedness_cast_outputs.size();
         ++index) {
        assert(
            signedness_cast_interpreter
                ->signal_value(*signedness_cast_outputs[index])
                .to_msb_string()
            == expected_signedness_casts[index]);
    }

    const auto invalid_signedness_cast =
        fsim::frontend::parse_text(
            "invalid_signedness_cast.sv",
            R"(
module invalid_signedness_cast;
  logic result;
  always_comb result = $signed();
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_signedness_cast.ok());
    const auto rejected_signedness_cast =
        fsim::elaboration::elaborate(
            invalid_signedness_cast.design,
            "sv:work.invalid_signedness_cast");
    assert(!rejected_signedness_cast.ok());
    assert(has_diagnostic(
        rejected_signedness_cast, "FSIM-ELAB-083"));

    const auto invalid_isunknown = fsim::frontend::parse_text(
        "invalid_isunknown.sv",
        R"(
module invalid_isunknown;
  logic result;
  always_comb result = $isunknown();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_isunknown.ok());
    const auto rejected_isunknown =
        fsim::elaboration::elaborate(
            invalid_isunknown.design,
            "sv:work.invalid_isunknown");
    assert(!rejected_isunknown.ok());
    assert(has_diagnostic(
        rejected_isunknown, "FSIM-ELAB-084"));

    const auto verilog_isunknown = fsim::frontend::parse_text(
        "verilog_isunknown.v",
        R"(
module verilog_isunknown;
  reg result;
  always @* result = $isunknown(1'bx);
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(verilog_isunknown.ok());
    const auto rejected_verilog_isunknown =
        fsim::elaboration::elaborate(
            verilog_isunknown.design,
            "sv:work.verilog_isunknown");
    assert(!rejected_verilog_isunknown.ok());
    assert(has_diagnostic(
        rejected_verilog_isunknown, "FSIM-ELAB-084"));

    const auto invalid_bits = fsim::frontend::parse_text(
        "invalid_bits.sv",
        R"(
module invalid_bits;
  logic [31:0] result;
  always_comb result = $bits();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_bits.ok());
    const auto rejected_bits =
        fsim::elaboration::elaborate(
            invalid_bits.design, "sv:work.invalid_bits");
    assert(!rejected_bits.ok());
    assert(has_diagnostic(
        rejected_bits, "FSIM-ELAB-085"));

    const auto verilog_bits = fsim::frontend::parse_text(
        "verilog_bits.v",
        R"(
module verilog_bits;
  reg [31:0] result;
  always @* result = $bits(8'b0);
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(verilog_bits.ok());
    const auto rejected_verilog_bits =
        fsim::elaboration::elaborate(
            verilog_bits.design, "sv:work.verilog_bits");
    assert(!rejected_verilog_bits.ok());
    assert(has_diagnostic(
        rejected_verilog_bits, "FSIM-ELAB-085"));

    const auto packed_array_queries =
        fsim::frontend::parse_text(
            "packed_array_queries.sv",
            R"(
module packed_array_queries;
  logic [7:4] descending;
  logic [2:5] ascending;
  logic signed [31:0] descending_left;
  logic signed [31:0] descending_right;
  logic signed [31:0] descending_low;
  logic signed [31:0] descending_high;
  logic signed [31:0] descending_size;
  logic signed [31:0] descending_increment;
  logic signed [31:0] ascending_left;
  logic signed [31:0] ascending_right;
  logic signed [31:0] ascending_low;
  logic signed [31:0] ascending_high;
  logic signed [31:0] ascending_size;
  logic signed [31:0] ascending_increment;
  logic signed [31:0] dimensions;
  logic signed [31:0] unpacked_dimensions;
  always_comb begin
    descending_left = $left(descending, 1);
    descending_right = $right(descending);
    descending_low = $low(descending);
    descending_high = $high(descending);
    descending_size = $size(descending);
    descending_increment = $increment(descending);
    ascending_left = $left(ascending);
    ascending_right = $right(ascending);
    ascending_low = $low(ascending);
    ascending_high = $high(ascending);
    ascending_size = $size(ascending, 1);
    ascending_increment = $increment(ascending);
    dimensions = $dimensions(descending);
    unpacked_dimensions = $unpacked_dimensions(ascending);
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(packed_array_queries.ok());
    const auto elaborated_packed_array_queries =
        fsim::elaboration::elaborate(
            packed_array_queries.design,
            "sv:work.packed_array_queries");
    assert(elaborated_packed_array_queries.ok());
    const std::array packed_query_names{
        "descending_left",
        "descending_right",
        "descending_low",
        "descending_high",
        "descending_size",
        "descending_increment",
        "ascending_left",
        "ascending_right",
        "ascending_low",
        "ascending_high",
        "ascending_size",
        "ascending_increment",
        "dimensions",
        "unpacked_dimensions"};
    std::array<
        std::optional<fsim::runtime::simir::SignalId>,
        14>
        packed_query_outputs;
    for (std::size_t index = 0;
         index < packed_query_names.size();
         ++index) {
        packed_query_outputs[index] =
            elaborated_packed_array_queries.design
                ->find_signal(packed_query_names[index]);
        assert(packed_query_outputs[index]);
    }
    auto packed_query_interpreter =
        elaborated_packed_array_queries.design
            ->create_interpreter();
    (void)packed_query_interpreter->run();
    const std::array<std::uint32_t, 14> expected_packed_queries{
        7,
        4,
        4,
        7,
        4,
        1,
        2,
        5,
        2,
        5,
        4,
        std::numeric_limits<std::uint32_t>::max(),
        1,
        0};
    for (std::size_t index = 0;
         index < packed_query_outputs.size();
         ++index) {
        assert(
            packed_query_interpreter
                ->signal_value(*packed_query_outputs[index])
                .low_word()
                .aval
            == expected_packed_queries[index]);
    }

    const auto invalid_packed_query =
        fsim::frontend::parse_text(
            "invalid_packed_query.sv",
            R"(
module invalid_packed_query;
  logic [3:0] value;
  logic signed [31:0] result;
  always_comb result = $left(value, 2);
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_packed_query.ok());
    const auto rejected_packed_query =
        fsim::elaboration::elaborate(
            invalid_packed_query.design,
            "sv:work.invalid_packed_query");
    assert(!rejected_packed_query.ok());
    assert(has_diagnostic(
        rejected_packed_query, "FSIM-ELAB-086"));

    const auto invalid_dimensions =
        fsim::frontend::parse_text(
            "invalid_dimensions.sv",
            R"(
module invalid_dimensions;
  logic signed [31:0] result;
  always_comb result = $dimensions();
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_dimensions.ok());
    const auto rejected_dimensions =
        fsim::elaboration::elaborate(
            invalid_dimensions.design,
            "sv:work.invalid_dimensions");
    assert(!rejected_dimensions.ok());
    assert(has_diagnostic(
        rejected_dimensions, "FSIM-ELAB-090"));

    const auto invalid_onehot = fsim::frontend::parse_text(
        "invalid_onehot.sv",
        R"(
module invalid_onehot;
  logic result;
  always_comb result = $onehot();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_onehot.ok());
    const auto rejected_onehot =
        fsim::elaboration::elaborate(
            invalid_onehot.design, "sv:work.invalid_onehot");
    assert(!rejected_onehot.ok());
    assert(has_diagnostic(
        rejected_onehot, "FSIM-ELAB-087"));

    const auto invalid_countones = fsim::frontend::parse_text(
        "invalid_countones.sv",
        R"(
module invalid_countones;
  logic signed [31:0] result;
  always_comb result = $countones();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_countones.ok());
    const auto rejected_countones =
        fsim::elaboration::elaborate(
            invalid_countones.design,
            "sv:work.invalid_countones");
    assert(!rejected_countones.ok());
    assert(has_diagnostic(
        rejected_countones, "FSIM-ELAB-088"));

    const auto invalid_countbits = fsim::frontend::parse_text(
        "invalid_countbits.sv",
        R"(
module invalid_countbits;
  logic [3:0] value;
  logic control;
  logic signed [31:0] result;
  always_comb result = $countbits(value, control);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_countbits.ok());
    const auto rejected_countbits =
        fsim::elaboration::elaborate(
            invalid_countbits.design,
            "sv:work.invalid_countbits");
    assert(!rejected_countbits.ok());
    assert(has_diagnostic(
        rejected_countbits, "FSIM-ELAB-089"));

    const auto logical_process = fsim::frontend::parse_text(
        "logical_process.sv",
        R"(
module logical_process;
  logic [3:0] lhs;
  logic [1:0] rhs;
  logic conjunction;
  logic disjunction;
  always_comb begin
    conjunction = lhs && rhs;
    disjunction = lhs || rhs;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(logical_process.ok());
    const auto elaborated_logical =
        fsim::elaboration::elaborate(
            logical_process.design, "sv:work.logical_process");
    assert(elaborated_logical.ok());
    const auto logical_lhs =
        elaborated_logical.design->find_signal("lhs");
    const auto logical_rhs =
        elaborated_logical.design->find_signal("rhs");
    const auto logical_and =
        elaborated_logical.design->find_signal("conjunction");
    const auto logical_or =
        elaborated_logical.design->find_signal("disjunction");
    assert(logical_lhs && logical_rhs && logical_and && logical_or);
    auto logical_interpreter =
        elaborated_logical.design->create_interpreter();
    const auto run_logical =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::string_view expected_and,
            const std::string_view expected_or) {
          logical_interpreter->deposit_signal(
              *logical_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          logical_interpreter->deposit_signal(
              *logical_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          (void)logical_interpreter->run();
          assert(
              logical_interpreter
                  ->signal_value(*logical_and)
                  .to_msb_string()
              == expected_and);
          assert(
              logical_interpreter
                  ->signal_value(*logical_or)
                  .to_msb_string()
              == expected_or);
        };
    run_logical("0000", "X1", "0", "1");
    run_logical("00X0", "00", "0", "X");
    run_logical("00X0", "01", "X", "1");
    run_logical("0010", "ZZ", "X", "1");
    run_logical("0010", "01", "1", "1");

    const auto reduction_shift_process =
        fsim::frontend::parse_text(
            "reduction_shift_process.sv",
            R"(
module reduction_shift_process;
  logic [3:0] value;
  logic [2:0] amount;
  logic reduced_and;
  logic reduced_or;
  logic reduced_xor;
  logic reduced_nand;
  logic reduced_nor;
  logic reduced_xnor;
  logic reduced_xnor_alias;
  logic [3:0] xnor_value;
  logic [3:0] xnor_value_alias;
  logic [3:0] shifted_left;
  logic [3:0] shifted_right;
  always_comb begin
    reduced_and = &value;
    reduced_or = |value;
    reduced_xor = ^value;
    reduced_nand = ~&value;
    reduced_nor = ~|value;
    reduced_xnor = ~^value;
    reduced_xnor_alias = ^~value;
    xnor_value = value ~^ 4'b1010;
    xnor_value_alias = value ^~ 4'b1010;
    shifted_left = value << amount;
    shifted_right = value >> amount;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(reduction_shift_process.ok());
    const auto elaborated_reduction_shift =
        fsim::elaboration::elaborate(
            reduction_shift_process.design,
            "sv:work.reduction_shift_process");
    assert(elaborated_reduction_shift.ok());
    const auto reduction_value =
        elaborated_reduction_shift.design->find_signal("value");
    const auto shift_amount =
        elaborated_reduction_shift.design->find_signal("amount");
    const auto reduced_and =
        elaborated_reduction_shift.design->find_signal("reduced_and");
    const auto reduced_or =
        elaborated_reduction_shift.design->find_signal("reduced_or");
    const auto reduced_xor =
        elaborated_reduction_shift.design->find_signal("reduced_xor");
    const auto reduced_nand =
        elaborated_reduction_shift.design->find_signal("reduced_nand");
    const auto reduced_nor =
        elaborated_reduction_shift.design->find_signal("reduced_nor");
    const auto reduced_xnor =
        elaborated_reduction_shift.design->find_signal("reduced_xnor");
    const auto reduced_xnor_alias =
        elaborated_reduction_shift.design->find_signal(
            "reduced_xnor_alias");
    const auto xnor_value =
        elaborated_reduction_shift.design->find_signal("xnor_value");
    const auto xnor_value_alias =
        elaborated_reduction_shift.design->find_signal(
            "xnor_value_alias");
    const auto shifted_left =
        elaborated_reduction_shift.design->find_signal("shifted_left");
    const auto shifted_right =
        elaborated_reduction_shift.design->find_signal("shifted_right");
    assert(
        reduction_value && shift_amount && reduced_and
        && reduced_or && reduced_xor && reduced_nand
        && reduced_nor && reduced_xnor
        && reduced_xnor_alias && xnor_value
        && xnor_value_alias && shifted_left && shifted_right);
    auto reduction_shift_interpreter =
        elaborated_reduction_shift.design->create_interpreter();
    const auto run_reduction_shift =
        [&](const std::string_view value,
            const std::string_view amount,
            const std::string_view expected_and,
            const std::string_view expected_or,
            const std::string_view expected_xor,
            const std::string_view expected_nand,
            const std::string_view expected_nor,
            const std::string_view expected_xnor,
            const std::string_view expected_xnor_value,
            const std::string_view expected_left,
            const std::string_view expected_right) {
          reduction_shift_interpreter->deposit_signal(
              *reduction_value,
              fsim::runtime::PackedLogic4::from_msb_string(value));
          reduction_shift_interpreter->deposit_signal(
              *shift_amount,
              fsim::runtime::PackedLogic4::from_msb_string(amount));
          (void)reduction_shift_interpreter->run();
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_and)
                  .to_msb_string()
              == expected_and);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_or)
                  .to_msb_string()
              == expected_or);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_xor)
                  .to_msb_string()
              == expected_xor);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_nand)
                  .to_msb_string()
              == expected_nand);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_nor)
                  .to_msb_string()
              == expected_nor);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_xnor)
                  .to_msb_string()
              == expected_xnor);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_xnor_alias)
                  .to_msb_string()
              == expected_xnor);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*xnor_value)
                  .to_msb_string()
              == expected_xnor_value);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*xnor_value_alias)
                  .to_msb_string()
              == expected_xnor_value);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*shifted_left)
                  .to_msb_string()
              == expected_left);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*shifted_right)
                  .to_msb_string()
              == expected_right);
        };
    run_reduction_shift(
        "1111", "001", "1", "1", "0", "0", "0", "1",
        "1010", "1110", "0111");
    run_reduction_shift(
        "1011", "000", "0", "1", "1", "1", "0", "0",
        "1110", "1011", "1011");
    run_reduction_shift(
        "10X1", "001", "0", "1", "X", "1", "0", "X",
        "11X0", "0X10", "010X");
    run_reduction_shift(
        "11X1", "011", "X", "1", "X", "X", "0", "X",
        "10X0", "1000", "0001");
    run_reduction_shift(
        "00X0", "0X1", "0", "X", "X", "1", "X", "X",
        "01X1", "XXXX", "XXXX");
    run_reduction_shift(
        "Z001", "100", "0", "1", "X", "1", "0", "X",
        "X100", "0000", "0000");

    const auto arithmetic_process =
        fsim::frontend::parse_text(
            "arithmetic_process.sv",
            R"(
module arithmetic_process;
  logic [7:0] lhs;
  logic [7:0] rhs;
  logic [7:0] difference;
  logic [7:0] product;
  logic [7:0] quotient;
  logic [7:0] remainder;
  logic [7:0] positive;
  logic [7:0] negative;
  always_comb begin
    difference = lhs - rhs;
    product = lhs * rhs;
    quotient = lhs / rhs;
    remainder = lhs % rhs;
    positive = +lhs;
    negative = -lhs;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(arithmetic_process.ok());
    const auto elaborated_arithmetic =
        fsim::elaboration::elaborate(
            arithmetic_process.design,
            "sv:work.arithmetic_process");
    assert(elaborated_arithmetic.ok());
    const auto arithmetic_lhs =
        elaborated_arithmetic.design->find_signal("lhs");
    const auto arithmetic_rhs =
        elaborated_arithmetic.design->find_signal("rhs");
    const std::array arithmetic_outputs{
        elaborated_arithmetic.design->find_signal("difference"),
        elaborated_arithmetic.design->find_signal("product"),
        elaborated_arithmetic.design->find_signal("quotient"),
        elaborated_arithmetic.design->find_signal("remainder"),
        elaborated_arithmetic.design->find_signal("positive"),
        elaborated_arithmetic.design->find_signal("negative")};
    assert(arithmetic_lhs && arithmetic_rhs);
    assert(std::ranges::all_of(
        arithmetic_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto arithmetic_interpreter =
        elaborated_arithmetic.design->create_interpreter();
    const auto run_arithmetic =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::array<std::string_view, 6>& expected) {
          arithmetic_interpreter->deposit_signal(
              *arithmetic_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          arithmetic_interpreter->deposit_signal(
              *arithmetic_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          (void)arithmetic_interpreter->run();
          for (std::size_t index = 0;
               index < arithmetic_outputs.size(); ++index) {
            assert(
                arithmetic_interpreter
                    ->signal_value(*arithmetic_outputs[index])
                    .to_msb_string()
                == expected[index]);
          }
        };
    run_arithmetic(
        "11001000",
        "00000111",
        {"11000001", "01111000", "00011100",
         "00000100", "11001000", "00111000"});
    run_arithmetic(
        "10X01000",
        "00000111",
        {"XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
         "XXXXXXXX", "10X01000", "XXXXXXXX"});
    run_arithmetic(
        "11001000",
        "00000000",
        {"11001000", "00000000", "XXXXXXXX",
         "XXXXXXXX", "11001000", "00111000"});

    const auto signed_arithmetic = fsim::frontend::parse_text(
        "signed_arithmetic.sv",
        R"(
module signed_arithmetic;
  logic signed [7:0] lhs;
  logic signed [7:0] rhs;
  logic [7:0] unsigned_rhs;
  logic signed [7:0] sum;
  logic signed [7:0] difference;
  logic signed [7:0] product;
  logic signed [7:0] quotient;
  logic signed [7:0] remainder;
  logic less;
  logic mixed_less;
  always_comb begin
    sum = lhs + rhs;
    difference = lhs - rhs;
    product = lhs * rhs;
    quotient = lhs / rhs;
    remainder = lhs % rhs;
    less = lhs < rhs;
    mixed_less = lhs < unsigned_rhs;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(signed_arithmetic.ok());
    const auto elaborated_signed_arithmetic =
        fsim::elaboration::elaborate(
            signed_arithmetic.design,
            "sv:work.signed_arithmetic");
    assert(elaborated_signed_arithmetic.ok());
    const auto signed_lhs =
        elaborated_signed_arithmetic.design->find_signal("lhs");
    const auto signed_rhs =
        elaborated_signed_arithmetic.design->find_signal("rhs");
    const auto unsigned_rhs =
        elaborated_signed_arithmetic.design->find_signal(
            "unsigned_rhs");
    const std::array signed_outputs{
        elaborated_signed_arithmetic.design->find_signal("sum"),
        elaborated_signed_arithmetic.design->find_signal(
            "difference"),
        elaborated_signed_arithmetic.design->find_signal("product"),
        elaborated_signed_arithmetic.design->find_signal(
            "quotient"),
        elaborated_signed_arithmetic.design->find_signal(
            "remainder"),
        elaborated_signed_arithmetic.design->find_signal("less"),
        elaborated_signed_arithmetic.design->find_signal(
            "mixed_less")};
    assert(signed_lhs && signed_rhs && unsigned_rhs);
    assert(std::ranges::all_of(
        signed_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto signed_interpreter =
        elaborated_signed_arithmetic.design->create_interpreter();
    const auto run_signed =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::string_view unsigned_value,
            const std::array<std::string_view, 7>& expected) {
          signed_interpreter->deposit_signal(
              *signed_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          signed_interpreter->deposit_signal(
              *signed_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          signed_interpreter->deposit_signal(
              *unsigned_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(
                  unsigned_value));
          (void)signed_interpreter->run();
          for (std::size_t index = 0;
               index < signed_outputs.size(); ++index) {
            assert(
                signed_interpreter
                    ->signal_value(*signed_outputs[index])
                    .to_msb_string()
                == expected[index]);
          }
        };
    run_signed(
        "11111011",
        "00000011",
        "00000001",
        {"11111110", "11111000", "11110001",
         "11111111", "11111110", "1", "0"});
    run_signed(
        "00000101",
        "11111101",
        "11111111",
        {"00000010", "00001000", "11110001",
         "11111111", "00000010", "0", "1"});
}

void test_systemverilog_membership_lowering() {
    const auto parsed = fsim::frontend::parse_text(
        "membership_expression.sv",
        R"(
module membership_expression;
  logic [7:0] selector;
  logic signed [7:0] signed_selector;
  bit [7:0] bit_selector;
  logic [7:0] low_bound;
  logic [7:0] high_bound;
  logic exact_match;
  logic range_match;
  logic mixed_match;
  logic wildcard_match;
  logic unknown_miss;
  logic unknown_then_match;
  logic reversed_range;
  logic signed_range;
  logic bit_match;
  logic unknown_range;
  logic short_circuit;
  logic sized_match;
  logic signedness_match;
  logic concat_lhs_match;
  logic concat_rhs_match;
  logic nested_match;
  logic wide_match;
  logic sized_range;
  logic signed_sized_range;

  function automatic logic [7:0] observed(input logic [7:0] value);
    return value;
  endfunction
  function automatic logic [7:0] failing(input logic [7:0] divisor);
    return 8'hff / divisor;
  endfunction

  initial begin
    selector = 8'h15;
    signed_selector = -8'sd5;
    bit_selector = 8'h2a;
    low_bound = 8'h10;
    high_bound = 8'h1f;
    exact_match = selector inside {8'h14, 8'h15, 8'h16};
    range_match = selector inside {[8'h10:8'h1f]};
    mixed_match = selector inside {8'h01, [8'h10:8'h1f], 8'hff};
    wildcard_match = 8'ha5 inside {8'b10xz_0101};
    unknown_miss = 8'bx001_0001 inside {8'b0001_0001};
    unknown_then_match =
        8'bx001_0001 inside {8'b0001_0001, 8'bxxxx_xxxx};
    reversed_range = selector inside {[8'h1f:8'h10]};
    signed_range = signed_selector inside {[-8'sd8:-8'sd2]};
    bit_match = bit_selector inside {8'h2a};
    unknown_range = selector inside {[8'bx000_0000:8'h1f]};
    short_circuit =
        observed(selector) inside {
          [observed(low_bound):observed(high_bound)], failing(8'h00)};
    sized_match = 8'h05 inside {4'h5};
    signedness_match = signed_selector inside {8'hfb};
    concat_lhs_match = {4'h1, 4'h5} inside {8'h15};
    concat_rhs_match = selector inside {{4'h1, 4'h5}};
    nested_match = 1'b1 inside {selector inside {8'h15}};
    wide_match = 137'h15 inside {8'h15};
    sized_range = selector inside {[4'h0:5'h1f]};
    signed_sized_range = signed_selector inside {[-4'sd8:-16'sd2]};
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    if (!parsed.ok()) {
      for (const auto& diagnostic : parsed.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "sv:work.membership_expression");
    assert(elaborated.ok());
    const auto& process = elaborated.design->processes().front();
    assert(std::ranges::count_if(
        process.operations,
        [](const fsim::runtime::simir::Operation& operation) {
          const auto* binary =
              fsim::runtime::simir::operation_get_if<fsim::runtime::simir::Binary>(&operation);
          return binary != nullptr
              && binary->operation
                  == fsim::runtime::simir::BinaryOperator::wildcard_equal;
        }) >= 8);
    assert(std::ranges::any_of(
        process.operations,
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::Branch>(operation);
        }));
    const auto membership_calls = std::ranges::count_if(
        process.operations,
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::Call>(operation);
        });
    if (membership_calls != 4) {
      std::cerr << "membership call count: " << membership_calls << '\n';
    }
    assert(membership_calls == 4);
    auto interpreter = elaborated.design->create_interpreter();
    (void)interpreter->run();
    const auto value = [&](const std::string_view name) {
      const auto signal = elaborated.design->find_signal(name);
      assert(signal);
      return interpreter->signal_value(*signal).to_msb_string();
    };
    assert(value("exact_match") == "1");
    assert(value("range_match") == "1");
    assert(value("mixed_match") == "1");
    assert(value("wildcard_match") == "1");
    assert(value("unknown_miss") == "X");
    assert(value("unknown_then_match") == "1");
    assert(value("reversed_range") == "0");
    assert(value("signed_range") == "1");
    assert(value("bit_match") == "1");
    assert(value("unknown_range") == "X");
    assert(value("short_circuit") == "1");
    assert(value("sized_match") == "1");
    assert(value("signedness_match") == "1");
    assert(value("concat_lhs_match") == "1");
    assert(value("concat_rhs_match") == "1");
    assert(value("nested_match") == "1");
    assert(value("wide_match") == "1");
    assert(value("sized_range") == "1");
    assert(value("signed_sized_range") == "1");

    const auto reject = [](
                            const std::string_view path,
                            const std::string_view source,
                            const std::string_view code) {
        const auto candidate = fsim::frontend::parse_text(
            path, source,
            fsim::frontend::Language::SystemVerilog2017);
        assert(candidate.ok());
        const auto rejected = fsim::elaboration::elaborate(
            candidate.design, "sv:work.membership_negative");
        assert(!rejected.ok());
        assert(has_diagnostic(rejected, code));
    };
    reject(
        "membership_container.sv",
        R"(
module membership_negative;
  logic [7:0] value;
  logic [7:0] values[1:0];
  logic result;
  initial result = value inside {values};
endmodule
)",
        "FSIM-ELAB-SVMEMBER-004");
}

void test_systemverilog_case_inside_lowering() {
    const auto parsed = fsim::frontend::parse_text(
        "case_inside.sv",
        R"(
module case_inside;
  logic [7:0] selector;
  logic signed [7:0] signed_selector;
  bit [7:0] bit_selector;
  logic [7:0] low_bound;
  logic [7:0] high_bound;
  logic [3:0] exact_range;
  logic [3:0] wildcard_choice;
  logic [3:0] unknown_default;
  logic [3:0] unknown_later_match;
  logic [3:0] reversed_range;
  logic [3:0] signed_range;
  logic [3:0] bit_choice;
  logic [3:0] first_selected;
  logic [3:0] short_circuit;
  logic [3:0] sized_choice;
  logic [3:0] signedness_choice;
  logic [3:0] concat_choice;
  logic [3:0] wide_choice;
  logic [3:0] qualified_sized_choice;

  function automatic logic [7:0] observed(input logic [7:0] value);
    return value;
  endfunction
  function automatic logic [7:0] failing(input logic [7:0] divisor);
    return 8'hff / divisor;
  endfunction

  initial begin
    selector = 8'h15;
    signed_selector = -8'sd5;
    bit_selector = 8'h2a;
    low_bound = 8'h10;
    high_bound = 8'h1f;
    case (selector) inside
      8'h01, [8'h10:8'h1f]: exact_range = 4'h1;
      default: exact_range = 4'hf;
    endcase
    case (8'ha5) inside
      8'b10xz_0101: wildcard_choice = 4'h2;
      default: wildcard_choice = 4'hf;
    endcase
    case (8'bx001_0001) inside
      8'b0001_0001: unknown_default = 4'h3;
      default: unknown_default = 4'hd;
    endcase
    case (8'bx001_0001) inside
      8'b0001_0001: unknown_later_match = 4'h4;
      8'bxxxx_xxxx: unknown_later_match = 4'h5;
      default: unknown_later_match = 4'he;
    endcase
    case (selector) inside
      [8'h1f:8'h10]: reversed_range = 4'h6;
      default: reversed_range = 4'hc;
    endcase
    case (signed_selector) inside
      [-8'sd8:-8'sd2]: signed_range = 4'h7;
      default: signed_range = 4'hb;
    endcase
    case (bit_selector) inside
      8'h2a: bit_choice = 4'h8;
      default: bit_choice = 4'ha;
    endcase
    case (selector) inside
      [8'h10:8'h20]: first_selected = 4'h9;
      8'h15: first_selected = 4'h0;
      default: first_selected = 4'hf;
    endcase
    case (observed(selector)) inside
      [observed(low_bound):observed(high_bound)], failing(8'h00):
        short_circuit = 4'ha;
      default: short_circuit = 4'hf;
    endcase
    case (8'h05) inside
      4'h5: sized_choice = 4'h1;
      default: sized_choice = 4'hf;
    endcase
    case (signed_selector) inside
      8'hfb: signedness_choice = 4'h2;
      default: signedness_choice = 4'hf;
    endcase
    case ({4'h1, 4'h5}) inside
      8'h15: concat_choice = 4'h3;
      default: concat_choice = 4'hf;
    endcase
    case (137'h15) inside
      8'h15: wide_choice = 4'h4;
      default: wide_choice = 4'hf;
    endcase
    unique case (8'h05) inside
      4'h5: qualified_sized_choice = 4'h5;
      default: qualified_sized_choice = 4'hf;
    endcase
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    if (!parsed.ok()) {
      for (const auto& diagnostic : parsed.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "sv:work.case_inside");
    if (!elaborated.ok()) {
      for (const auto& diagnostic : elaborated.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    assert(elaborated.ok());
    const auto& process = elaborated.design->processes().front();
    assert(std::ranges::any_of(
        process.operations,
        [](const fsim::runtime::simir::Operation& operation) {
          const auto* binary =
              fsim::runtime::simir::operation_get_if<fsim::runtime::simir::Binary>(&operation);
          return binary != nullptr
              && binary->operation
                  == fsim::runtime::simir::BinaryOperator::wildcard_equal;
        }));
    const auto case_calls = std::ranges::count_if(
        process.operations,
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<fsim::runtime::simir::Call>(
              operation);
        });
    assert(case_calls == 4);
    auto interpreter = elaborated.design->create_interpreter();
    (void)interpreter->run();
    const auto value = [&](const std::string_view name) {
      const auto signal = elaborated.design->find_signal(name);
      assert(signal);
      return interpreter->signal_value(*signal).to_msb_string();
    };
    assert(value("exact_range") == "0001");
    assert(value("wildcard_choice") == "0010");
    assert(value("unknown_default") == "1101");
    assert(value("unknown_later_match") == "0101");
    assert(value("reversed_range") == "1100");
    assert(value("signed_range") == "0111");
    assert(value("bit_choice") == "1000");
    assert(value("first_selected") == "1001");
    assert(value("short_circuit") == "1010");
    assert(value("sized_choice") == "0001");
    assert(value("signedness_choice") == "0010");
    assert(value("concat_choice") == "0011");
    assert(value("wide_choice") == "0100");
    assert(value("qualified_sized_choice") == "0101");

    const auto reject = [](
                            const std::string_view path,
                            const std::string_view source,
                            const std::string_view code) {
        const auto candidate = fsim::frontend::parse_text(
            path, source,
            fsim::frontend::Language::SystemVerilog2017);
        assert(candidate.ok());
        const auto rejected = fsim::elaboration::elaborate(
            candidate.design, "sv:work.case_inside_negative");
        assert(!rejected.ok());
        assert(has_diagnostic(rejected, code));
    };
    reject(
        "case_inside_choice_container.sv",
        "module case_inside_negative; logic [7:0] s; logic [7:0] a[1:0]; "
        "logic r; initial case (s) inside a: r = 1; endcase endmodule",
        "FSIM-ELAB-SVCASEINSIDE-003");
    reject(
        "case_inside_selector_container.sv",
        "module case_inside_negative; logic [7:0] a[1:0]; logic r; "
        "initial case (a) inside 8'h1: r = 1; endcase endmodule",
        "FSIM-ELAB-SVCASEINSIDE-002");
    const auto malformed_parsed = fsim::frontend::parse_text(
        "case_inside_malformed_hir.sv",
        "module case_inside_malformed_hir; logic [7:0] s; logic r; "
        "initial case (s) inside 8'h1: r = 1; endcase endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    assert(malformed_parsed.ok());
    auto empty = malformed_parsed.design;
    empty.units.front().processes.front().statements.front()
        .case_alternatives.front().choices.clear();
    const auto empty_result = fsim::elaboration::elaborate(
        empty, "sv:work.case_inside_malformed_hir");
    assert(!empty_result.ok());
    assert(has_diagnostic(
        empty_result, "FSIM-ELAB-SVCASEINSIDE-005"));

    auto malformed = malformed_parsed.design;
    auto& malformed_case = malformed.units.front().processes.front()
                               .statements.front();
    malformed_case.case_alternatives.front().choices.front() =
        fsim::frontend::Expression{
            fsim::frontend::ExpressionKind::Call,
            "@inside-range",
            {},
            malformed_case.span};
    const auto malformed_result = fsim::elaboration::elaborate(
        malformed, "sv:work.case_inside_malformed_hir");
    assert(!malformed_result.ok());
    assert(has_diagnostic(
        malformed_result, "FSIM-ELAB-SVCASEINSIDE-006"));

    const auto verilog_parsed = fsim::frontend::parse_text(
        "case_inside_wrong_language.v",
        "module case_inside_wrong_language; reg s; reg r; "
        "initial case (s) 1'b0: r = 1; endcase endmodule",
        fsim::frontend::Language::Verilog2005);
    assert(verilog_parsed.ok());
    auto wrong_language = verilog_parsed.design;
    wrong_language.units.front().processes.front().statements.front()
        .case_match_kind = fsim::frontend::CaseMatchKind::Inside;
    const auto wrong_language_result = fsim::elaboration::elaborate(
        wrong_language, "sv:work.case_inside_wrong_language");
    assert(!wrong_language_result.ok());
    assert(has_diagnostic(
        wrong_language_result, "FSIM-ELAB-SVCASEINSIDE-001"));

    const auto vhdl_attribute_negative = fsim::frontend::parse_text(
        "vhdl_attribute_negative.vhd",
        R"(
entity vhdl_attribute_negative is
end entity;

architecture rtl of vhdl_attribute_negative is
  signal source : std_logic;
  signal duration : signed(63 downto 0);
  signal flag : boolean;
  signal value : std_logic;
begin
  invalid: process
  begin
    flag <= source'stable(-1);
    flag <= source'quiet(duration);
    flag <= source'transaction(1);
    value <= source'delayed(-1);
    value <= source'driving_value;
    wait;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_attribute_negative.ok());
    const auto rejected_vhdl_attributes = fsim::elaboration::elaborate(
        vhdl_attribute_negative.design,
        "vhdl:work.vhdl_attribute_negative(rtl)");
    assert(!rejected_vhdl_attributes.ok());
    for (const auto code : {
             "FSIM-ELAB-VHATTR-003",
             "FSIM-ELAB-VHATTR-004",
             "FSIM-ELAB-VHATTR-005",
             "FSIM-ELAB-VHATTR-006",
             "FSIM-ELAB-VHATTR-007"}) {
      assert(has_diagnostic(rejected_vhdl_attributes, code));
    }
}

} // namespace fsim::tests::elaboration
