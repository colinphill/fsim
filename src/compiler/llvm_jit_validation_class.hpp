// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace fsim::compiler::llvm_detail {

template <typename OperationType, typename RecordUse,
          typename RecordDefinition, typename ConstrainWidth>
void validate_class_operation(
    const runtime::simir::Process& process,
    const std::size_t index,
    const OperationType& operation,
    RecordUse&& record_use,
    RecordDefinition&& record_definition,
    ConstrainWidth&& constrain_width) {
  using namespace runtime::simir;
  if constexpr (std::is_same_v<OperationType, ClassAllocate>) {
    if (operation.specialization_identity.empty()
        || operation.declared_type.empty()) {
      reject(
          process, index,
          "ClassAllocate requires specialization and declared types");
    }
    if (!operation.constructor_actual_names.empty()
        && operation.constructor_actual_names.size()
            != operation.constructor_actuals.size()) {
      reject(
          process, index,
          "ClassAllocate actual names must align with actual registers");
    }
    for (const auto actual : operation.constructor_actuals) {
      record_use(actual, index);
    }
    record_definition(operation.destination, index);
    constrain_width(operation.destination, 64U, index);
  } else if constexpr (std::is_same_v<OperationType, ClassPropertyRead>) {
    if (operation.property_identity.empty() || operation.width == 0) {
      reject(process, index, "ClassPropertyRead requires an identity");
    }
    record_use(operation.receiver, index);
    constrain_width(operation.receiver, 64U, index);
    record_definition(operation.destination, index);
    constrain_width(operation.destination, operation.width, index);
  } else if constexpr (std::is_same_v<OperationType, ClassPropertyWrite>) {
    if (operation.property_identity.empty()) {
      reject(process, index, "ClassPropertyWrite requires an identity");
    }
    record_use(operation.receiver, index);
    constrain_width(operation.receiver, 64U, index);
    record_use(operation.source, index);
  } else if constexpr (std::is_same_v<OperationType, ClassMethodCall>) {
    if (operation.method_identity.empty() || operation.result_width == 0
        || operation.actual_names.size() != operation.actuals.size()
        || operation.actual_directions.size() != operation.actuals.size()) {
      reject(
          process, index,
          "ClassMethodCall requires aligned method actual metadata");
    }
    record_use(operation.receiver, index);
    constrain_width(operation.receiver, 64U, index);
    for (const auto actual : operation.actuals) record_use(actual, index);
    record_definition(operation.destination, index);
    constrain_width(operation.destination, operation.result_width, index);
  } else if constexpr (
      std::is_same_v<OperationType, ClassStaticPropertyRead>) {
    if (operation.property_identity.empty() || operation.width == 0) {
      reject(
          process, index,
          "ClassStaticPropertyRead requires an identity and width");
    }
    record_definition(operation.destination, index);
    constrain_width(operation.destination, operation.width, index);
  } else if constexpr (
      std::is_same_v<OperationType, ClassStaticPropertyWrite>) {
    if (operation.property_identity.empty()) {
      reject(process, index, "ClassStaticPropertyWrite requires an identity");
    }
    record_use(operation.source, index);
  } else if constexpr (std::is_same_v<OperationType, ClassStaticMethodCall>) {
    if (operation.method_identity.empty() || operation.result_width == 0
        || operation.actual_names.size() != operation.actuals.size()
        || operation.actual_directions.size() != operation.actuals.size()) {
      reject(
          process, index,
          "ClassStaticMethodCall requires aligned method metadata");
    }
    for (const auto actual : operation.actuals) record_use(actual, index);
    record_definition(operation.destination, index);
    constrain_width(operation.destination, operation.result_width, index);
  }
}

}  // namespace fsim::compiler::llvm_detail
