// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_systemverilog_composite_container_types() {
  using namespace fsim::runtime::simir;
  const auto parsed = fsim::frontend::parse_text(
      "composite-container-types.sv",
      R"(
module composite_container_types;
  typedef struct {
    real weight;
    time ticks[1:0];
    string label;
    chandle cookie;
  } record_t;
  real samples[1:0];
  time timeline[];
  string names[$];
  chandle handles[int];
  string nested[1:0][];
  record_t records[1:0];
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "composite_container_types");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  const auto type = [&](const std::string_view name)
      -> const ContainerType& {
    const auto id = elaborated.design->find_container(name);
    assert(id);
    return elaborated.design->container_objects().at(*id).type;
  };
  assert(
      type("composite_container_types.samples").element_kind
          == ContainerElementKind::Scalar
      && type("composite_container_types.samples").scalar_kind
          == fsim::frontend::SystemVerilogScalarKind::Real
      && type("composite_container_types.timeline").scalar_kind
          == fsim::frontend::SystemVerilogScalarKind::Time
      && type("composite_container_types.names").element_kind
          == ContainerElementKind::String
      && type("composite_container_types.handles").scalar_kind
          == fsim::frontend::SystemVerilogScalarKind::Chandle);
  const auto& nested_type = type("composite_container_types.nested");
  assert(
      nested_type.element_kind == ContainerElementKind::Container
      && nested_type.element_types.size() == 1
      && nested_type.element_types.front().element_kind
          == ContainerElementKind::String);
  const auto records_id = elaborated.design->find_container(
      "composite_container_types.records");
  assert(records_id);
  const auto& records_object =
      elaborated.design->container_objects().at(*records_id);
  auto interpreter = elaborated.design->create_interpreter();
  const auto& records_value =
      interpreter->container_object_value(*records_id);
  assert((
      records_object.type.element_kind
          == ContainerElementKind::Aggregate
      && records_object.type.member_names
          == std::vector<std::string>{
              "weight", "ticks", "label", "cookie"}
      && records_value.nested_elements.size() == 2
      && records_value.nested_elements.front()
             .nested_elements.size() == 4));

  const auto unsupported = fsim::frontend::parse_text(
      "composite-container-operations-invalid.sv",
      R"(
module composite_container_operations_invalid;
  typedef struct { string label; chandle cookie; } record_t;
  string names[1:0];
  string copies[1:0];
  string selected[$];
  record_t records[1:0];
  int total;
  initial begin
    names[0] = "selected";
    records[0] = records[1];
    copies[1:0] = names[1:0];
    total = names.sum();
    names.sort();
    selected = names.min();
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(unsupported.ok());
  const auto rejected = fsim::elaboration::elaborate(
      unsupported.design,
      "composite_container_operations_invalid");
  assert(!rejected.ok());
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVCONTAINER-024"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVSLICE-006"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVREDUCE-006"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVORDER-008"));
  assert(has_diagnostic(rejected, "FSIM-ELAB-SVLOCATOR-008"));
}

}  // namespace fsim::tests::elaboration
