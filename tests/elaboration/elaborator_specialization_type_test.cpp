// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_specialization_type_identity()
{
    const auto nominal_legality = fsim::frontend::parse_text(
        "nominal_legality.sv",
        R"(
package nominal_legality_types;
  typedef struct packed { logic [1:0] value; } first_t;
  typedef struct packed { logic [1:0] value; } second_t;
  typedef enum logic [1:0] { FIRST_ZERO, FIRST_ONE } first_e;
  typedef enum logic [1:0] { SECOND_ZERO, SECOND_ONE } second_e;
  typedef struct packed {
    first_t nested;
    first_e code;
  } wrapper_t;
endpackage

import nominal_legality_types::*;

module nominal_legality_positive #(
  parameter first_t PACKED_DEFAULT = '{value: 2'b01},
  parameter first_e ENUM_DEFAULT = FIRST_ONE
)(
  input first_t incoming,
  output first_t outgoing
);
  function automatic first_t pass(input first_t value);
    return value;
  endfunction
  task automatic copy(
    input first_t source,
    output first_t destination
  );
    destination = source;
  endtask
  first_t local_value;
  first_e enum_value;
  wrapper_t wrapped;
  logic same;
  initial begin
    local_value = pass(incoming);
    copy(local_value, outgoing);
    wrapped = '{nested: local_value, code: FIRST_ONE};
    enum_value = ENUM_DEFAULT;
    same = local_value == first_t'(2'b01);
    same = enum_value == first_e'(1);
    same = enum_value == FIRST_ONE;
  end
endmodule

module nominal_legality_host;
  first_t incoming;
  first_t outgoing;
  first_t default_outgoing;
  nominal_legality_positive #(
    .PACKED_DEFAULT(first_t'(2'b11)),
    .ENUM_DEFAULT(FIRST_ONE)
  ) child(
    .incoming(incoming),
    .outgoing(outgoing)
  );
  nominal_legality_positive defaults(
    .incoming(incoming),
    .outgoing(default_outgoing)
  );
endmodule

module nominal_first_port(input first_t value); endmodule
module nominal_first_enum_port(input first_e value); endmodule

module invalid_nominal_parameter #(
  parameter second_t OTHER = '{value: 2'b10},
  parameter first_t BAD = OTHER
)(); endmodule

module invalid_enum_parameter #(
  parameter second_e OTHER = SECOND_ONE,
  parameter first_e BAD = OTHER
)(); endmodule

module invalid_enum_assignment;
  first_e first;
  second_e second;
  initial first = second;
endmodule

module invalid_enum_raw_assignment;
  first_e first;
  initial first = 2'b01;
endmodule

module invalid_function_argument;
  function automatic first_t pass(input first_t value);
    return value;
  endfunction
  first_t first;
  second_t second;
  initial first = pass(second);
endmodule

module invalid_function_return;
  function automatic first_t bad();
    second_t second;
    return second;
  endfunction
  first_t first;
  initial first = bad();
endmodule

module invalid_task_copyout;
  task automatic produce(output first_t value);
    value = '{value: 2'b01};
  endtask
  second_t second;
  initial produce(second);
endmodule

module invalid_nested_pattern;
  wrapper_t wrapped;
  second_t second;
  initial wrapped = '{nested: second, code: FIRST_ZERO};
endmodule

module invalid_enum_equality;
  first_e first;
  second_e second;
  logic same;
  initial same = first == second;
endmodule

module invalid_explicit_cast;
  first_t first;
  second_t second;
  initial first = second_t'(second);
endmodule

module invalid_nominal_port;
  second_t value;
  nominal_first_port child(.value(value));
endmodule

module invalid_enum_port;
  second_e value;
  nominal_first_enum_port child(.value(value));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(nominal_legality.ok());
    const auto nominal_positive = fsim::elaboration::elaborate(
        nominal_legality.design,
        "sv:work.nominal_legality_host");
    if (!nominal_positive.ok()) {
        for (const auto& diagnostic : nominal_positive.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(nominal_positive.ok());
    const auto rejects = [&](const std::string_view top,
                             const std::string_view code) {
        const auto result = fsim::elaboration::elaborate(
            nominal_legality.design, "sv:work." + std::string { top });
        if (result.ok() || !has_diagnostic(result, code)) {
            for (const auto& diagnostic : result.diagnostics) {
                std::cerr << top << ": " << diagnostic.code << ": "
                          << diagnostic.message << '\n';
            }
        }
        assert(!result.ok() && has_diagnostic(result, code));
    };
    rejects("invalid_nominal_parameter", "FSIM-ELAB-SVCONST-001");
    rejects("invalid_enum_parameter", "FSIM-ELAB-SVCONST-001");
    rejects("invalid_enum_assignment", "FSIM-ELAB-SVTYPE-004");
    rejects("invalid_enum_raw_assignment", "FSIM-ELAB-SVTYPE-004");
    rejects("invalid_function_argument", "FSIM-ELAB-SVTYPE-004");
    rejects("invalid_function_return", "FSIM-ELAB-SVTYPE-004");
    rejects("invalid_task_copyout", "FSIM-ELAB-SVTYPE-004");
    rejects("invalid_nested_pattern", "FSIM-ELAB-SVTYPE-004");
    rejects("invalid_enum_equality", "FSIM-ELAB-SVTYPE-005");
    rejects("invalid_explicit_cast", "FSIM-ELAB-SVTYPE-004");
    rejects("invalid_nominal_port", "FSIM-ELAB-BIND-057");
    rejects("invalid_enum_port", "FSIM-ELAB-BIND-053");

    const auto invalid_aggregate = fsim::frontend::parse_text(
        "invalid_nested_aggregate.sv",
        R"(
package invalid_aggregate_types;
  typedef struct packed {
    logic [1:0] value;
  } first_t;
  typedef struct packed {
    logic [1:0] value;
  } second_t;
endpackage

import invalid_aggregate_types::*;

module invalid_nominal;
  first_t first;
  second_t second;
  initial first = second;
endmodule

module invalid_pattern;
  first_t first;
  initial first = '{missing: 2'b01};
endmodule

module invalid_cast;
  logic [1:0] value;
  initial value = absent_t'(2'b01);
endmodule

module invalid_comparison;
  first_t first;
  second_t second;
  logic same;
  initial same = first == second;
endmodule

module invalid_multidimensional_index;
  logic [3:0] matrix[1:0][0:2];
  initial matrix[1][3] = 4'h0;
endmodule

module invalid_multidimensional_rank;
  logic [3:0] matrix[1:0][0:2];
  initial matrix[1] = 4'h0;
endmodule

module invalid_multidimensional_pattern;
  logic [3:0] matrix[1:0][0:2];
  initial matrix = '{'{4'h1, 4'h2, 4'h3}};
endmodule

module invalid_tagged_member;
  typedef union tagged packed {
    logic [15:0] wide;
    logic [7:0] narrow;
  } tagged_t;
  tagged_t value;
  initial value = tagged missing 8'h01;
endmodule

module invalid_tagged_context;
  logic [16:0] raw;
  initial raw = tagged narrow 8'h01;
endmodule

module invalid_delayed_union_write;
  typedef union tagged packed {
    logic [15:0] wide;
    logic [7:0] narrow;
  } tagged_t;
  tagged_t value;
  initial value.narrow = #1 8'h01;
endmodule

module invalid_member_initializer;
  typedef struct packed {
    logic [3:0] payload;
    logic valid;
  } inner_t;
  typedef struct packed {
    inner_t nested = '{payload: 4'h1};
  } outer_t;
  outer_t value;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_aggregate.ok());
    const auto invalid_nominal = fsim::elaboration::elaborate(
        invalid_aggregate.design, "sv:work.invalid_nominal");
    const auto invalid_pattern = fsim::elaboration::elaborate(
        invalid_aggregate.design, "sv:work.invalid_pattern");
    const auto invalid_cast = fsim::elaboration::elaborate(
        invalid_aggregate.design, "sv:work.invalid_cast");
    const auto invalid_comparison = fsim::elaboration::elaborate(
        invalid_aggregate.design, "sv:work.invalid_comparison");
    const auto invalid_multidimensional_index = fsim::elaboration::elaborate(
        invalid_aggregate.design,
        "sv:work.invalid_multidimensional_index");
    const auto invalid_multidimensional_rank = fsim::elaboration::elaborate(
        invalid_aggregate.design,
        "sv:work.invalid_multidimensional_rank");
    const auto invalid_multidimensional_pattern = fsim::elaboration::elaborate(
        invalid_aggregate.design,
        "sv:work.invalid_multidimensional_pattern");
    const auto invalid_tagged_member = fsim::elaboration::elaborate(
        invalid_aggregate.design, "sv:work.invalid_tagged_member");
    const auto invalid_tagged_context = fsim::elaboration::elaborate(
        invalid_aggregate.design, "sv:work.invalid_tagged_context");
    const auto invalid_delayed_union_write = fsim::elaboration::elaborate(
        invalid_aggregate.design,
        "sv:work.invalid_delayed_union_write");
    const auto invalid_member_initializer = fsim::elaboration::elaborate(
        invalid_aggregate.design,
        "sv:work.invalid_member_initializer");
    assert(
        !invalid_nominal.ok()
        && has_diagnostic(
            invalid_nominal, "FSIM-ELAB-SVTYPE-004"));
    assert(
        !invalid_pattern.ok()
        && has_diagnostic(
            invalid_pattern, "FSIM-ELAB-SVAGG-002"));
    assert(
        !invalid_cast.ok()
        && has_diagnostic(
            invalid_cast, "FSIM-ELAB-SVCAST-002"));
    assert(
        !invalid_comparison.ok()
        && has_diagnostic(
            invalid_comparison, "FSIM-ELAB-SVTYPE-005"));
    assert(
        !invalid_multidimensional_index.ok()
        && has_diagnostic(
            invalid_multidimensional_index,
            "FSIM-ELAB-SVMDARRAY-003"));
    assert(
        !invalid_multidimensional_rank.ok()
        && has_diagnostic(
            invalid_multidimensional_rank,
            "FSIM-ELAB-SVMDARRAY-001"));
    assert(
        !invalid_multidimensional_pattern.ok()
        && has_diagnostic(
            invalid_multidimensional_pattern,
            "FSIM-ELAB-SVPATTERN-002"));
    assert(
        !invalid_tagged_member.ok()
        && has_diagnostic(
            invalid_tagged_member, "FSIM-ELAB-SVAGG-002"));
    assert(
        !invalid_tagged_context.ok()
        && has_diagnostic(
            invalid_tagged_context, "FSIM-ELAB-SVUNION-001"));
    assert(
        !invalid_delayed_union_write.ok()
        && has_diagnostic(
            invalid_delayed_union_write,
            "FSIM-ELAB-SVUNION-001"));
    assert(
        !invalid_member_initializer.ok()
        && has_diagnostic(
            invalid_member_initializer,
            "FSIM-ELAB-SVAGG-007"));

    const auto multidimensional = fsim::frontend::parse_text(
        "multidimensional_static.sv",
        R"(
module multidimensional_static(
  output logic [23:0] observed,
  output int dimensions,
  output int unpacked_dimensions,
  output int outer_size,
  output int inner_size,
  output int inner_left,
  output int inner_right,
  output logic [3:0] dynamic_observed
);
  logic [3:0] matrix[1:0][0:2];
  int row;
  int column;
  initial begin
    matrix[1][0] = 4'h1;
    matrix[1][1] = 4'h2;
    matrix[1][2] = 4'h3;
    matrix[0][0] = 4'h4;
    matrix[0][1] = 4'h5;
    matrix[0][2] = 4'h6;
    dimensions = $dimensions(matrix);
    unpacked_dimensions = $unpacked_dimensions(matrix);
    outer_size = $size(matrix, 1);
    inner_size = $size(matrix, 2);
    inner_left = $left(matrix, 2);
    inner_right = $right(matrix, 2);
    observed = {
      matrix[1][0], matrix[1][1], matrix[1][2],
      matrix[0][0], matrix[0][1], matrix[0][2]
    };
    row = 0;
    column = 1;
    matrix[row][column] = 4'ha;
    dynamic_observed = matrix[row][column];
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(multidimensional.ok());
    const auto multidimensional_elaborated = fsim::elaboration::elaborate(
        multidimensional.design,
        "sv:work.multidimensional_static");
    if (!multidimensional_elaborated.ok()) {
        for (const auto& diagnostic :
            multidimensional_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(multidimensional_elaborated.ok());
    auto multidimensional_interpreter = multidimensional_elaborated.design->create_interpreter();
    assert(
        multidimensional_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto multidimensional_value =
        [&](const std::string_view name) {
            const auto signal = multidimensional_elaborated.design->find_signal(name);
            assert(signal);
            return multidimensional_interpreter->signal_value(*signal);
        };
    const auto observed_multidimensional = multidimensional_value("observed").to_msb_string();
    if (observed_multidimensional
        != "000100100011010001010110") {
        std::cerr << "multidimensional observed: "
                  << observed_multidimensional << '\n';
    }
    assert(
        observed_multidimensional
        == "000100100011010001010110");
    assert(multidimensional_value("dimensions").low_word().aval == 3);
    assert(
        multidimensional_value("unpacked_dimensions").low_word().aval
        == 2);
    assert(multidimensional_value("outer_size").low_word().aval == 2);
    assert(multidimensional_value("inner_size").low_word().aval == 3);
    assert(multidimensional_value("inner_left").low_word().aval == 0);
    assert(multidimensional_value("inner_right").low_word().aval == 2);
    assert(
        multidimensional_value("dynamic_observed").to_msb_string()
        == "1010");
}

} // namespace fsim::tests::elaboration
