// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/semantic/systemverilog_structure_image.hpp"

namespace fsim::app::application_detail {

runtime::SystemVerilogClassPropertyDescriptor class_property_descriptor(
    const semantic::sv::SpecializedClassProperty& property,
    const semantic::sv::Hir& hir,
    const bool qualified_name) {
  runtime::SystemVerilogClassPropertyDescriptor result;
  result.name = qualified_name
      ? property.owner_identity + "::" + property.name
      : property.name;
  // A static random variable is shared by every instance; its random
  // state lives with its value in the static store (IEEE 1800-2017 18.4).
  {
    result.random_kind = property.random_kind
            == semantic::sv::ClassRandomKind::rand
        ? runtime::SystemVerilogClassRandomKind::Rand
        : property.random_kind == semantic::sv::ClassRandomKind::randc
        ? runtime::SystemVerilogClassRandomKind::Randc
        : runtime::SystemVerilogClassRandomKind::None;
  }
  result.signed_value = property.type.signed_value;
  result.nominal_type = !property.type.class_identity.empty()
      ? property.type.class_identity
      : !property.type.target.spelling.empty()
      ? property.type.target.spelling
      : property.type.value_form == semantic::sv::TypeForm::string
      ? "string"
      : property.type.four_state ? "logic" : "bit";

  const auto container_form = property.type.container_form;
  if (!property.type.class_identity.empty() && container_form) {
    runtime::SystemVerilogClassHandleContainerDescriptor container;
    container.declared_element_type = property.type.class_identity;
    const auto addressable_elements = std::numeric_limits<std::size_t>::max()
        / sizeof(runtime::SystemVerilogClassHandle);
    container.maximum_elements = addressable_elements;
    if (*container_form == semantic::sv::TypeForm::static_array) {
      container.kind = runtime::SystemVerilogClassContainerKind::FixedArray;
      if (property.type.unpacked_dimensions.empty()) {
        throw std::invalid_argument {
            "class handle static-array property requires a resolved range"
        };
      }
      std::size_t total_elements { 1U };
      for (const auto& range : property.type.unpacked_dimensions) {
        if (!range.left || !range.right) {
          throw std::invalid_argument {
              "class handle static-array property requires resolved ranges"
          };
        }
        const auto low = std::min(*range.left, *range.right);
        const auto high = std::max(*range.left, *range.right);
        const auto distance = static_cast<std::uint64_t>(high)
            - static_cast<std::uint64_t>(low);
        if (distance == std::numeric_limits<std::uint64_t>::max()) {
          throw std::length_error {
              "class handle static-array property exceeds addressable storage"
          };
        }
        const auto width = distance + 1U;
        if (width > addressable_elements / total_elements) {
          throw std::length_error {
              "class handle static-array property exceeds addressable storage"
          };
        }
        total_elements *= static_cast<std::size_t>(width);
      }
      container.maximum_elements = total_elements;
      container.initial_elements = container.maximum_elements;
    } else if (*container_form == semantic::sv::TypeForm::dynamic_array) {
      container.kind = runtime::SystemVerilogClassContainerKind::DynamicArray;
      container.reserve_maximum_storage = false;
    } else if (*container_form == semantic::sv::TypeForm::queue) {
      container.kind = runtime::SystemVerilogClassContainerKind::Queue;
      if (property.type.queue_maximum) {
        const auto maximum = std::ranges::find(
            hir.expressions(), *property.type.queue_maximum,
            &semantic::sv::Expression::id);
        if (maximum == hir.expressions().end()
            || maximum->kind != semantic::sv::ExpressionKind::integer_literal) {
          throw std::invalid_argument {
              "class handle queue property requires a resolved bound"
          };
        }
        std::uint64_t maximum_index { };
        const auto converted = std::from_chars(
            maximum->text.data(), maximum->text.data() + maximum->text.size(),
            maximum_index, 10);
        if (converted.ec != std::errc { }
            || converted.ptr != maximum->text.data() + maximum->text.size()
            || maximum_index >= addressable_elements) {
          throw std::length_error {
              "class handle queue bound exceeds addressable storage"
          };
        }
        container.maximum_elements
            = static_cast<std::size_t>(maximum_index + 1U);
      } else {
        container.reserve_maximum_storage = false;
      }
    } else if (*container_form
        == semantic::sv::TypeForm::associative_array) {
      container.kind
          = runtime::SystemVerilogClassContainerKind::AssociativeArray;
      container.reserve_maximum_storage = false;
    }
    result.kind = runtime::SystemVerilogClassPropertyKind::Container;
    result.width = 0;
    result.handle_container = std::move(container);
    return result;
  }
  if (!property.type.class_identity.empty()
      || property.type.value_form == semantic::sv::TypeForm::class_handle) {
    result.kind = runtime::SystemVerilogClassPropertyKind::ClassHandle;
    result.width = 64;
    return result;
  }
  // An unpacked structure with a member that has no packed image is held
  // as an aggregate container value (IEEE 1800-2017 7.2, 8.4).
  if (container_form
      || semantic::sv::systemverilog_boxed_aggregate(hir, property.type)) {
    result.kind = runtime::SystemVerilogClassPropertyKind::Container;
    result.width = 0;
    return result;
  }
  if (property.type.value_form == semantic::sv::TypeForm::string) {
    result.kind = runtime::SystemVerilogClassPropertyKind::String;
    result.width = 0;
  } else if (property.type.target.spelling == "event") {
    // An event property holds its event object's 64-bit handle (IEEE
    // 1800-2017 15.5); 0 until the object is first used.
    result.kind = runtime::SystemVerilogClassPropertyKind::Bit2;
    result.width = 64;
  } else {
    result.kind = property.type.four_state
        ? runtime::SystemVerilogClassPropertyKind::Logic4
        : runtime::SystemVerilogClassPropertyKind::Bit2;
    result.width = property.type.executable_width
        ? static_cast<std::size_t>(*property.type.executable_width)
        : std::max<std::size_t>(property.bit_width, 1U);
  }
  if (!property.initializer)
    return result;
  const auto initializer = std::ranges::find(
      hir.expressions(), *property.initializer,
      &semantic::sv::Expression::id);
  if (initializer == hir.expressions().end())
    return result;
  if (initializer->kind == semantic::sv::ExpressionKind::string_literal
      && initializer->decoded_string) {
    result.initial_string = *initializer->decoded_string;
    return result;
  }
  if (initializer->kind == semantic::sv::ExpressionKind::boolean_literal) {
    result.initial_packed = runtime::PackedLogic4::from_aval_bval(
        std::max<std::size_t>(result.width, 1U),
        initializer->text == "true" ? 1U : 0U, 0);
    return result;
  }
  if (initializer->kind == semantic::sv::ExpressionKind::integer_literal) {
    std::int64_t value { };
    const auto* begin = initializer->text.data();
    const auto* end = begin + initializer->text.size();
    const auto converted = std::from_chars(begin, end, value, 10);
    if (converted.ec == std::errc { } && converted.ptr == end) {
      result.initial_packed = runtime::PackedLogic4::from_aval_bval(
          std::max<std::size_t>(result.width, 1U),
          static_cast<std::uint64_t>(value), 0);
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

std::vector<ClassRandomizationTraceState>
Simulation::class_randomization_trace_states() const {
  std::vector<ClassRandomizationTraceState> result;
  for (const auto handle : class_heap().live_handles()) {
    const auto& object = class_heap().object(handle);
    const auto prefix = "$class." + std::to_string(handle) + ".";
    for (std::size_t index = 0; index < object.properties.size(); ++index) {
      const auto& random = object.properties[index].random_state;
      if (!random) continue;
      result.push_back({
          prefix + object.property_names[index] + ".$random-state",
          handle,
          ClassRandomizationTraceKind::property,
          random->enabled,
          random->revision,
          random->stream_seed,
          random->randc_domain_signature,
          random->randc_cycle,
          static_cast<std::uint64_t>(random->randc_used_values.size())
              + random->randc_position});
    }
    for (const auto& [identity, enabled] : object.constraint_modes) {
      result.push_back({
          prefix + identity + ".$constraint-mode",
          handle,
          ClassRandomizationTraceKind::constraint,
          enabled});
    }
  }
  return result;
}

}  // namespace fsim::app
