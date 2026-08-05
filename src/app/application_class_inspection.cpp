// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

runtime::SystemVerilogClassPropertyDescriptor class_property_descriptor(
    const frontend::SystemVerilogClassPropertyLayout& property,
    const bool qualified_name) {
  runtime::SystemVerilogClassPropertyDescriptor result;
  result.name = qualified_name
      ? property.owner_identity + "::" + property.name
      : property.name;
  if (!property.type.systemverilog_class_declaration.empty()
      && property.type.systemverilog_container) {
    const auto& source = *property.type.systemverilog_container;
    runtime::SystemVerilogClassHandleContainerDescriptor container;
    container.declared_element_type =
        property.type.systemverilog_class_declaration;
    const auto addressable_elements = std::numeric_limits<std::size_t>::max()
        / sizeof(runtime::SystemVerilogClassHandle);
    container.maximum_elements = addressable_elements;
    switch (source.kind) {
      case frontend::SystemVerilogContainerKind::StaticArray:
        if (!source.static_range) {
          throw std::invalid_argument{
              "class handle static-array property requires a resolved range"};
        }
        container.kind = runtime::SystemVerilogClassContainerKind::FixedArray;
        container.maximum_elements =
            static_cast<std::size_t>(source.static_range->width());
        container.initial_elements = container.maximum_elements;
        break;
      case frontend::SystemVerilogContainerKind::DynamicArray:
        container.kind =
            runtime::SystemVerilogClassContainerKind::DynamicArray;
        container.reserve_maximum_storage = false;
        break;
      case frontend::SystemVerilogContainerKind::Queue:
        container.kind = runtime::SystemVerilogClassContainerKind::Queue;
        if (source.queue_maximum
            && source.queue_maximum->kind
                == frontend::ExpressionKind::IntegerLiteral) {
          std::uint64_t maximum_index{};
          const auto& spelling = source.queue_maximum->text;
          const auto converted = std::from_chars(
              spelling.data(), spelling.data() + spelling.size(),
              maximum_index, 10);
          if (converted.ec != std::errc{}
              || converted.ptr != spelling.data() + spelling.size()
              || maximum_index >= addressable_elements) {
            throw std::length_error{
                "class handle queue bound exceeds addressable storage"};
          }
          container.maximum_elements =
              static_cast<std::size_t>(maximum_index + 1U);
        } else {
          container.reserve_maximum_storage = false;
        }
        break;
      case frontend::SystemVerilogContainerKind::AssociativeArray:
        container.kind =
            runtime::SystemVerilogClassContainerKind::AssociativeArray;
        container.reserve_maximum_storage = false;
        break;
    }
    result.kind = runtime::SystemVerilogClassPropertyKind::Container;
    result.width = 0;
    result.handle_container = std::move(container);
    return result;
  }
  if (!property.type.systemverilog_class_declaration.empty()) {
    result.kind = runtime::SystemVerilogClassPropertyKind::ClassHandle;
    result.width = 64;
    return result;
  }
  if (property.type.systemverilog_container) {
    result.kind = runtime::SystemVerilogClassPropertyKind::Container;
    result.width = 0;
    return result;
  }
  switch (property.type.domain) {
    case frontend::ValueDomain::Bit2:
    case frontend::ValueDomain::Boolean:
      result.kind = runtime::SystemVerilogClassPropertyKind::Bit2;
      break;
    case frontend::ValueDomain::Logic9:
      result.kind = runtime::SystemVerilogClassPropertyKind::Logic9;
      break;
    case frontend::ValueDomain::Integer:
      result.kind = runtime::SystemVerilogClassPropertyKind::Integer;
      break;
    case frontend::ValueDomain::String:
      result.kind = runtime::SystemVerilogClassPropertyKind::String;
      break;
    default:
      result.kind = runtime::SystemVerilogClassPropertyKind::Logic4;
      break;
  }
  const auto width = property.type.width();
  if (width && *width > std::numeric_limits<std::size_t>::max()) {
    throw std::length_error{"class property width exceeds host storage"};
  }
  result.width = width ? static_cast<std::size_t>(*width) : 1U;
  if (property.initializer) {
    if (property.initializer->kind
        == frontend::ExpressionKind::IntegerLiteral) {
      std::int64_t value{};
      const auto* begin = property.initializer->text.data();
      const auto* end = begin + property.initializer->text.size();
      const auto converted = std::from_chars(begin, end, value, 10);
      if (converted.ec == std::errc{} && converted.ptr == end) {
        const auto initial_width =
            result.kind == runtime::SystemVerilogClassPropertyKind::Integer
            ? std::size_t{64}
            : result.width;
        result.initial_packed = runtime::PackedLogic4::from_aval_bval(
            initial_width, static_cast<std::uint64_t>(value), 0);
      }
    } else if (
        property.initializer->kind
            == frontend::ExpressionKind::StringLiteral
        && property.initializer->decoded_string) {
      result.initial_string = *property.initializer->decoded_string;
    }
  }
  return result;
}

}  // namespace fsim::app::application_detail

namespace fsim::app {

std::vector<ClassPackedTraceValue>
Simulation::class_packed_trace_values() const {
  std::vector<ClassPackedTraceValue> result;
  for (const auto handle : class_heap().live_handles()) {
    const auto& object = class_heap().object(handle);
    for (std::size_t index = 0; index < object.properties.size(); ++index) {
      const auto& property = object.properties[index];
      if (property.packed.width() == 0) continue;
      result.push_back({
          "$class." + std::to_string(handle) + "."
              + object.property_names[index],
          property.packed,
          handle});
    }
  }
  for (const auto& state : class_static_store().snapshots()) {
    for (std::size_t index = 0; index < state.properties.size(); ++index) {
      const auto& property = state.properties[index];
      if (property.packed.width() == 0) continue;
      result.push_back({
          "$class-static." + state.specialization_identity + "."
              + state.property_names[index],
          property.packed,
          std::nullopt});
    }
  }
  return result;
}

}  // namespace fsim::app
