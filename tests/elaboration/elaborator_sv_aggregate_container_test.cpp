// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <iostream>
#include <string_view>
#include <vector>

namespace fsim::tests::elaboration {

void test_systemverilog_aggregate_containers() {
  const auto parsed = fsim::frontend::parse_text(
      "aggregate-containers.sv",
      R"(
module aggregate_containers;
  typedef enum logic [1:0] { ZERO, ONE, TWO, THREE } code_t;
  typedef struct packed {
    code_t code;
    logic [5:0] payload;
  } packet_t;
  packet_t fixed[1:0];
  packet_t matrix[1:0][0:1];
  packet_t dynamic[];
  packet_t pending[$:3];
  packet_t lookup[int];
  int row;
  int column;
  initial begin
    fixed = '{
      '{code: code_t'(2'd1), payload: 6'h01},
      '{code: code_t'(2'd2), payload: 6'h02}
    };
    fixed[0] = '{code: code_t'(2'd3), payload: 6'h03};
    dynamic = '{
      '{code: code_t'(2'd1), payload: 6'h04},
      '{code: code_t'(2'd2), payload: 6'h05}
    };
    dynamic = new[3](dynamic);
    pending = '{
      '{code: code_t'(2'd0), payload: 6'h06},
      '{code: code_t'(2'd1), payload: 6'h07}
    };
    pending.insert(
        1, '{code: code_t'(2'd3), payload: 6'h08});
    pending.delete(0);
    lookup = '{
        2: '{code: code_t'(2'd2), payload: 6'h09}};
    matrix = '{
      '{
        '{code: code_t'(2'd0), payload: 6'h0a},
        '{code: code_t'(2'd1), payload: 6'h0b}
      },
      '{
        '{code: code_t'(2'd2), payload: 6'h0c},
        '{code: code_t'(2'd3), payload: 6'h0d}
      }
    };
    row = 0;
    column = 1;
    matrix[row][column] =
        '{code: code_t'(2'd3), payload: 6'h0e};
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "aggregate_containers");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  const auto fixed = elaborated.design->find_container(
      "aggregate_containers.fixed");
  assert(
      fixed && !elaborated.design->container_objects().at(*fixed)
                    .type.element_nominal_type.empty());
  auto interpreter = elaborated.design->create_interpreter();
  assert(
      interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto value = [&](const std::string_view name) -> const auto& {
    const auto id = elaborated.design->find_container(name);
    assert(id);
    return interpreter->container_object_value(*id);
  };
  const auto bits = [](const std::uint64_t word) {
    return fsim::runtime::PackedLogic4::from_aval_bval(8, word, 0);
  };
  assert((value("aggregate_containers.fixed").elements
          == std::vector{bits(0x41), bits(0xc3)}));
  const auto& dynamic = value("aggregate_containers.dynamic");
  assert(
      dynamic.elements.size() == 3
      && dynamic.elements[0] == bits(0x44)
      && dynamic.elements[1] == bits(0x85)
      && dynamic.elements[2].to_msb_string() == "XXXXXXXX");
  assert((value("aggregate_containers.pending").elements
          == std::vector{bits(0xc8), bits(0x47)}));
  assert((value("aggregate_containers.lookup").elements
          == std::vector{bits(0x89)}));
  assert((value("aggregate_containers.matrix").elements
          == std::vector{
              bits(0x0a), bits(0x4b), bits(0x8c), bits(0xce)}));

  const auto mismatch = fsim::frontend::parse_text(
      "aggregate-container-nominal-invalid.sv",
      R"(
module aggregate_container_nominal_invalid;
  typedef struct packed { logic [7:0] value; } first_t;
  typedef struct packed { logic [7:0] value; } second_t;
  first_t first[];
  second_t second[];
  initial first = second;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(mismatch.ok());
  const auto rejected = fsim::elaboration::elaborate(
      mismatch.design, "aggregate_container_nominal_invalid");
  assert(
      !rejected.ok()
      && has_diagnostic(rejected, "FSIM-ELAB-SVCONTAINER-010"));
}

}  // namespace fsim::tests::elaboration
