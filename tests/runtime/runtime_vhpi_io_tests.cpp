// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/vhpi_io.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require_vhpi_io(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

fsim::runtime::VhdlVhpiObjectResult io_object(
    fsim::runtime::VhdlVhpiObjectRegistry& objects,
    const fsim::runtime::VhdlVhpiObjectKind kind,
    const fsim_vhpi_handle_v1 parent,
    const std::string& name) {
  return objects.create_object(fsim::runtime::VhdlVhpiObjectDescriptor{
      kind, parent, name, {}, std::nullopt});
}

}  // namespace

void test_vhdl_vhpi_io() {
  using fsim::runtime::VhdlVhpiIoError;
  using fsim::runtime::VhdlVhpiIoEvent;
  using fsim::runtime::VhdlVhpiIoKind;
  using fsim::runtime::VhdlVhpiIoSystem;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiSeverity;
  using fsim::runtime::VhdlVhpiSourceLocation;

  VhdlVhpiObjectRegistry objects{1501};
  const auto root_a = io_object(
      objects, VhdlVhpiObjectKind::Root, 0, "root_a");
  const auto root_b = io_object(
      objects, VhdlVhpiObjectKind::Root, 0, "root_b");
  require_vhpi_io(
      root_a && root_b, "VHPI I/O root creation failed");
  const auto process_a = io_object(
      objects, VhdlVhpiObjectKind::Process, root_a.value, "producer");
  const auto process_b = io_object(
      objects, VhdlVhpiObjectKind::Process, root_b.value, "consumer");
  require_vhpi_io(
      process_a && process_b, "VHPI I/O process creation failed");

  VhdlVhpiIoSystem io{objects};
  std::vector<VhdlVhpiIoEvent> delivered;
  std::uint64_t context_a2_identity{};
  bool nested{};
  const auto context_a1 = io.register_context(
      {root_a.value,
       "a-primary",
       [&](const VhdlVhpiIoEvent& event) {
         delivered.push_back(event);
         if (!nested && event.kind == VhdlVhpiIoKind::Report) {
           nested = true;
           require_vhpi_io(
               io.output(context_a2_identity, "nested output")
                   == VhdlVhpiIoError::None,
               "VHPI nested output failed");
         }
       }});
  const auto context_a2 = io.register_context(
      {root_a.value,
       "a-secondary",
       [&](const VhdlVhpiIoEvent& event) {
         delivered.push_back(event);
       }});
  context_a2_identity = context_a2.value.identity;
  const auto context_b = io.register_context(
      {root_b.value,
       "b-primary",
       [&](const VhdlVhpiIoEvent& event) {
         delivered.push_back(event);
       }});
  const auto throwing = io.register_context(
      {root_a.value,
       "throwing",
       [](const VhdlVhpiIoEvent&) {
         throw std::runtime_error("contained VHPI sink failure");
       }});
  require_vhpi_io(
      context_a1 && context_a2 && context_b && throwing
          && context_a1.value.ordinal < context_a2.value.ordinal
          && context_a1.value.root == context_a2.value.root
          && context_a1.value.root != context_b.value.root,
      "VHPI I/O context registration/isolation failed");

  const std::array<std::string_view, 1> report_arguments{"1010"};
  require_vhpi_io(
      io.report(
          context_a1.value.identity,
          process_a.value,
          VhdlVhpiSeverity::Warning,
          "value {} {{stable}}",
          report_arguments,
          VhdlVhpiSourceLocation{"design_a.vhd", 12, 5})
              == VhdlVhpiIoError::None
          && io.assertion(
                 context_a1.value.identity,
                 process_a.value,
                 true,
                 VhdlVhpiSeverity::Failure,
                 "must not publish")
              == VhdlVhpiIoError::None
          && io.assertion(
                 context_b.value.identity,
                 process_b.value,
                 false,
                 VhdlVhpiSeverity::Error,
                 "assert {}",
                 std::array<std::string_view, 1>{"failed"},
                 VhdlVhpiSourceLocation{"design_b.vhd", 28, 9})
              == VhdlVhpiIoError::None
          && io.output(context_b.value.identity, "plain output")
              == VhdlVhpiIoError::None,
      "VHPI report/assertion/output publication failed");
  require_vhpi_io(
      delivered.size() == 4
          && delivered[0].ordinal == 0
          && delivered[0].kind == VhdlVhpiIoKind::Report
          && delivered[0].message == "value 1010 {stable}"
          && delivered[0].source->file == "design_a.vhd"
          && delivered[0].severity == VhdlVhpiSeverity::Warning
          && delivered[1].ordinal == 1
          && delivered[1].context == context_a2.value.identity
          && delivered[1].message == "nested output"
          && delivered[2].ordinal == 2
          && delivered[2].kind == VhdlVhpiIoKind::Assertion
          && delivered[2].root == root_b.value
          && delivered[3].ordinal == 3
          && delivered[3].kind == VhdlVhpiIoKind::Output,
      "VHPI I/O deterministic nested interleaving or metadata failed");

  require_vhpi_io(
      io.report(
          context_a1.value.identity,
          process_b.value,
          VhdlVhpiSeverity::Note,
          "wrong root")
              == VhdlVhpiIoError::CrossRoot
          && io.report(
                 context_a1.value.identity,
                 process_a.value,
                 static_cast<VhdlVhpiSeverity>(99),
                 "bad severity")
              == VhdlVhpiIoError::InvalidSeverity
          && io.report(
                 context_a1.value.identity,
                 process_a.value,
                 VhdlVhpiSeverity::Note,
                 "missing {}")
              == VhdlVhpiIoError::InvalidFormat
          && io.report(
                 context_a1.value.identity,
                 process_a.value,
                 VhdlVhpiSeverity::Note,
                 "bad source",
                 {},
                 VhdlVhpiSourceLocation{"", 0, 0})
              == VhdlVhpiIoError::InvalidSource
          && io.output(
                 context_a1.value.identity, std::string(65'537, 'x'))
              == VhdlVhpiIoError::TextTooLong,
      "VHPI I/O invalid root/severity/format/source/bound was accepted");

  require_vhpi_io(
      io.output(throwing.value.identity, "retained despite sink")
              == VhdlVhpiIoError::SinkFailed
          && io.history(throwing.value.identity).value.size() == 1
          && io.context(throwing.value.identity).value.events == 1
          && io.history(context_a1.value.identity).value.size() == 1
          && io.history(context_a2.value.identity).value.size() == 1
          && io.history(context_b.value.identity).value.size() == 2
          && io.history().size() == 5,
      "VHPI sink containment or per-context/global history failed");

  VhdlVhpiObjectRegistry foreign_objects{1502};
  const auto foreign_root = io_object(
      foreign_objects, VhdlVhpiObjectKind::Root, 0, "foreign");
  const auto foreign_process = io_object(
      foreign_objects,
      VhdlVhpiObjectKind::Process,
      foreign_root.value,
      "process");
  VhdlVhpiIoSystem foreign{foreign_objects};
  const auto foreign_context = foreign.register_context(
      {foreign_root.value, "foreign", [](const auto&) {}});
  require_vhpi_io(
      foreign.context(context_a1.value.identity).error
              == VhdlVhpiIoError::CrossSimulation
          && foreign.output(context_a1.value.identity, "wrong")
              == VhdlVhpiIoError::CrossSimulation
          && io.report(
                 context_a1.value.identity,
                 foreign_process.value,
                 VhdlVhpiSeverity::Note,
                 "wrong simulation")
              == VhdlVhpiIoError::CrossSimulation
          && foreign_context,
      "VHPI I/O contexts or subjects crossed simulations");

  io.teardown();
  io.teardown();
  require_vhpi_io(
      io.output(context_a1.value.identity, "after close")
              == VhdlVhpiIoError::Closed
          && !io.context(context_a1.value.identity).value.active
          && io.history().size() == 5,
      "VHPI I/O teardown did not close sinks and retain history");
}

}  // namespace fsim::tests::runtime
