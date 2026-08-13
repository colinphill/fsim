// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_error.hpp"
#include "fsim/runtime/vpi_object.hpp"

#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {

namespace {

void require_vpi_state(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

void test_systemverilog_vpi_objects_and_errors() {
  using fsim::runtime::SystemVerilogVpiErrorState;
  using fsim::runtime::SystemVerilogVpiErrorStateError;
  using fsim::runtime::SystemVerilogVpiIteratorError;
  using fsim::runtime::SystemVerilogVpiObjectDescriptor;
  using fsim::runtime::SystemVerilogVpiObjectError;
  using fsim::runtime::SystemVerilogVpiObjectKind;
  using fsim::runtime::SystemVerilogVpiObjectRegistry;
  using fsim::runtime::SystemVerilogVpiSourceLocation;

  SystemVerilogVpiErrorState invalid_errors{0};
  require_vpi_state(
      invalid_errors.publish(
          FSIM_VPI_ERROR_ERROR, "FSIM-VPI-TEST", "invalid simulation")
          == SystemVerilogVpiErrorStateError::InvalidSimulation,
      "VPI error state requires explicit simulation ownership");

  SystemVerilogVpiErrorState errors{41};
  std::string code{"FSIM-VPI-TEST"};
  std::string message{"owned diagnostic"};
  require_vpi_state(
      errors.publish(FSIM_VPI_ERROR_WARNING, code, message)
          == SystemVerilogVpiErrorStateError::None,
      "VPI error state accepts bounded owning diagnostics");
  code.assign("changed");
  message.assign("changed");
  const auto retained = errors.check();
  require_vpi_state(
      retained && retained->severity == FSIM_VPI_ERROR_WARNING
          && retained->code == "FSIM-VPI-TEST"
          && retained->message == "owned diagnostic",
      "VPI last error owns code and message independently of caller storage");
  require_vpi_state(
      errors.publish(
          99, "FSIM-VPI-TEST", "message")
          == SystemVerilogVpiErrorStateError::InvalidSeverity,
      "VPI error state rejects an unknown standard severity");
  require_vpi_state(
      errors.publish(FSIM_VPI_ERROR_ERROR, "", "message")
          == SystemVerilogVpiErrorStateError::InvalidCode,
      "VPI error state rejects an empty code");
  const std::string embedded_message{"bad\0message", 11};
  require_vpi_state(
      errors.publish(FSIM_VPI_ERROR_ERROR, "FSIM-VPI-TEST", embedded_message)
          == SystemVerilogVpiErrorStateError::InvalidMessage,
      "VPI error state rejects embedded-NUL messages");
  errors.begin_call();
  require_vpi_state(
      !errors.check(), "VPI begin-call clears the prior standard error state");

  SystemVerilogVpiObjectRegistry invalid_registry{0};
  require_vpi_state(
      !invalid_registry.valid()
          && invalid_registry.create(
                 SystemVerilogVpiObjectKind::Root, 0, "top")
                 .error
              == SystemVerilogVpiObjectError::InvalidSimulation,
      "VPI object registry rejects zero simulation ownership");

  SystemVerilogVpiObjectRegistry first{41};
  SystemVerilogVpiObjectRegistry second{42};
  const auto root =
      first.create(SystemVerilogVpiObjectKind::Root, 0, "top");
  const auto second_root =
      second.create(SystemVerilogVpiObjectKind::Root, 0, "top");
  require_vpi_state(
      root && second_root && root.value != second_root.value,
      "VPI registries allocate distinct nonpointer simulation handles");
  require_vpi_state(
      second.lookup(root.value).error
          == SystemVerilogVpiObjectError::CrossSimulation,
      "VPI lookup rejects a handle from another simulation exactly");

  const auto module = first.create(
      SystemVerilogVpiObjectKind::Module, root.value, "u");
  const auto net =
      first.create(SystemVerilogVpiObjectKind::Net, module.value, "value");
  require_vpi_state(
      module && net, "VPI registry creates a stable parented hierarchy");
  const auto net_info = first.lookup(net.value);
  require_vpi_state(
      net_info && net_info.value->parent == module.value
          && net_info.value->kind == SystemVerilogVpiObjectKind::Net
          && net_info.value->name == "value"
          && net_info.value->full_name == "top.u.value",
      "VPI lookup preserves exact kind, parent, simple, and full names");

  fsim::runtime::SystemVerilogVpiTypeInfo provenance_type;
  provenance_type.semantic_unit_id = 7U;
  provenance_type.source_id = 11U;
  provenance_type.semantic_unit = "work::escaped";
  provenance_type.source_path = "logical/source.sv";
  provenance_type.source_line = 7U;
  provenance_type.source_column = 13U;
  provenance_type.standard = "systemverilog-2009";
  provenance_type.compatibility_profile = "keyword-profile";
  const auto escaped = first.create(SystemVerilogVpiObjectDescriptor{
      SystemVerilogVpiObjectKind::Module,
      root.value,
      "\\u.core ",
      SystemVerilogVpiSourceLocation{"logical/source.sv", 7, 13},
      provenance_type,
  });
  const auto escaped_leaf = first.create(
      SystemVerilogVpiObjectKind::Net, escaped.value, "leaf");
  const auto escaped_info = first.lookup(escaped.value);
  require_vpi_state(
      escaped && escaped_leaf && escaped_info
          && escaped_info.value->name == "\\u.core"
          && escaped_info.value->full_name == "top.\\u.core "
          && escaped_info.value->source
          && escaped_info.value->source->file == "logical/source.sv"
          && escaped_info.value->source->line == 7
          && escaped_info.value->source->column == 13
          && escaped_info.value->type
          && escaped_info.value->type->semantic_unit == "work::escaped"
          && first.lookup(escaped_leaf.value).value->type->semantic_unit
              == "work::escaped",
      "VPI hierarchy normalizes escaped names and owns source metadata");
  auto partial_provenance = provenance_type;
  partial_provenance.compatibility_profile.clear();
  require_vpi_state(
      first.create(SystemVerilogVpiObjectDescriptor{
          SystemVerilogVpiObjectKind::Module,
          root.value,
          "partial_provenance",
          std::nullopt,
          partial_provenance,
      }).error == SystemVerilogVpiObjectError::InvalidType,
      "VPI hierarchy rejects partial semantic-unit provenance");
  require_vpi_state(
      first.find("top.\\u.core ").value->handle == escaped.value
          && first.find("top.\\u.core .leaf").value->handle
              == escaped_leaf.value
          && first.find_child(root.value, "\\u.core ").value->handle
              == escaped.value
          && first.find("top.missing").error
              == SystemVerilogVpiObjectError::NotFound,
      "VPI exact full and parent-relative name lookup is deterministic");
  require_vpi_state(
      first.create(SystemVerilogVpiObjectDescriptor{
          SystemVerilogVpiObjectKind::Net,
          module.value,
          "bad_source",
          SystemVerilogVpiSourceLocation{"source.sv", 0, 1},
          fsim::runtime::SystemVerilogVpiTypeInfo{},
      }).error == SystemVerilogVpiObjectError::InvalidSource
          && first.create(
                 SystemVerilogVpiObjectKind::Net,
                 module.value,
                 "dotted.name")
                 .error
              == SystemVerilogVpiObjectError::InvalidName,
      "VPI hierarchy rejects malformed source and simple-name metadata");

  const auto children = first.iterate_children(root.value);
  require_vpi_state(
      static_cast<bool>(children), "VPI child iterator captures a stable snapshot");
  const auto first_child = first.scan(children.value);
  const auto second_child = first.scan(children.value);
  require_vpi_state(
      first_child.value == module.value && second_child.value == escaped.value
          && first.scan(children.value).error
              == SystemVerilogVpiIteratorError::End,
      "VPI child iteration follows deterministic creation order");
  require_vpi_state(
      second.scan(children.value).error
          == SystemVerilogVpiIteratorError::CrossSimulation
          && first.lookup(children.value).error
              == SystemVerilogVpiObjectError::InvalidHandle,
      "VPI iterator handles reject cross-simulation and object misuse");
  require_vpi_state(
      first.release_iterator(children.value)
          == SystemVerilogVpiIteratorError::None
          && first.scan(children.value).error
              == SystemVerilogVpiIteratorError::ReleasedHandle,
      "VPI iterators release independently of hierarchy objects");
  const auto replacement_iterator = first.iterate_children(root.value);
  require_vpi_state(
      replacement_iterator
          && first.scan(children.value).error
              == SystemVerilogVpiIteratorError::StaleHandle
          && first.release_iterator(replacement_iterator.value)
              == SystemVerilogVpiIteratorError::None,
      "VPI iterator slot reuse advances its generation");
  require_vpi_state(
      first.create(
               SystemVerilogVpiObjectKind::Variable,
               module.value,
               "value")
              .error
          == SystemVerilogVpiObjectError::DuplicateName,
      "VPI registry rejects duplicate sibling names");
  require_vpi_state(
      first.create(
               static_cast<SystemVerilogVpiObjectKind>(999),
               module.value,
               "invalid")
              .error
          == SystemVerilogVpiObjectError::InvalidKind,
      "VPI registry rejects unknown object kinds");
  require_vpi_state(
      first.create(
               SystemVerilogVpiObjectKind::Net,
               second_root.value,
               "foreign")
              .error
          == SystemVerilogVpiObjectError::InvalidParent,
      "VPI registry rejects cross-simulation parents transactionally");
  require_vpi_state(
      first.release(module.value) == SystemVerilogVpiObjectError::HasChildren
          && first.release(root.value)
              == SystemVerilogVpiObjectError::HasChildren,
      "VPI registry prevents releasing live hierarchy owners");

  require_vpi_state(
      first.release(net.value) == SystemVerilogVpiObjectError::None
          && first.lookup(net.value).error
              == SystemVerilogVpiObjectError::ReleasedHandle,
      "VPI registry distinguishes a released handle before slot reuse");
  const auto variable = first.create(
      SystemVerilogVpiObjectKind::Variable, module.value, "replacement");
  require_vpi_state(
      variable && variable.value != net.value
          && first.lookup(net.value).error
              == SystemVerilogVpiObjectError::StaleHandle,
      "VPI registry increments the generation before slot reuse");
  require_vpi_state(
      first.lookup(root.value + 100U).error
          == SystemVerilogVpiObjectError::InvalidHandle,
      "VPI registry distinguishes malformed same-simulation handles");

  require_vpi_state(
      first.release(variable.value) == SystemVerilogVpiObjectError::None
          && first.release(module.value) == SystemVerilogVpiObjectError::None
          && first.release(escaped_leaf.value)
              == SystemVerilogVpiObjectError::None
          && first.release(escaped.value)
              == SystemVerilogVpiObjectError::None
          && first.release(root.value) == SystemVerilogVpiObjectError::None,
      "VPI registry releases a hierarchy in deterministic leaf-first order");
  require_vpi_state(
      first.lookup(root.value).error
          == SystemVerilogVpiObjectError::ReleasedHandle,
      "VPI registry retains released-root identity until safe reuse");
}

}  // namespace fsim::tests::runtime
