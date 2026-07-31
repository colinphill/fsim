// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<ContainerType> HierarchyBuilder::container_port_type(
    const frontend::Type& type,
    const frontend::SourceSpan& source,
    const ConstantEnvironment& environment) {
  const auto width = type.width();
  if (!type.systemverilog_container
      || !width || *width == 0 || *width > 64
      || !type.packed_members.empty()
      || type.domain == frontend::ValueDomain::String
      || type.domain == frontend::ValueDomain::Unknown) {
    report(
        "FSIM-ELAB-SVPORT-001",
        "SystemVerilog container ports require one-dimensional integral "
        "elements with width in 1..64",
        source);
    return std::nullopt;
  }
  ContainerType result;
  result.element_width = static_cast<std::uint32_t>(*width);
  result.two_state = is_two_state_domain(type.domain);
  result.signed_elements = type.is_signed;
  result.queue =
      type.systemverilog_container->kind
      == frontend::SystemVerilogContainerKind::Queue;
  result.associative =
      type.systemverilog_container->kind
      == frontend::SystemVerilogContainerKind::AssociativeArray;
  result.fixed =
      type.systemverilog_container->kind
      == frontend::SystemVerilogContainerKind::StaticArray;
  if (result.associative) {
    const auto& index_type =
        type.systemverilog_container->associative_index_type;
    const auto index_width =
        index_type ? index_type->width() : std::nullopt;
    if (!index_type || !index_width || *index_width == 0
        || *index_width > 64
        || index_type->domain == frontend::ValueDomain::String
        || index_type->domain == frontend::ValueDomain::Unknown
        || !index_type->packed_members.empty()
        || index_type->vhdl_array) {
      report(
          "FSIM-ELAB-SVPORT-001",
          "associative-array ports require a resolved integral index "
          "type with width in 1..64",
          type.systemverilog_container->span);
      return std::nullopt;
    }
    result.index_width =
        static_cast<std::uint32_t>(*index_width);
    result.two_state_indices =
        is_two_state_domain(index_type->domain);
    result.signed_indices = index_type->is_signed;
  }
  if (type.systemverilog_container->queue_maximum) {
    std::string error;
    const auto maximum =
        evaluate_systemverilog_constant_expression(
            *type.systemverilog_container->queue_maximum,
            {},
            environment,
            error);
    const auto maximum_index =
        maximum ? maximum->integer_value() : std::nullopt;
    if (!maximum_index || *maximum_index < 0
        || *maximum_index
            >= static_cast<std::int64_t>(
                maximum_container_elements)) {
      report(
          "FSIM-ELAB-SVPORT-002",
          "bounded queue port maximum index must specialize to a value "
          "in 0..4095",
          type.systemverilog_container->queue_maximum->span);
      return std::nullopt;
    }
    result.maximum_elements =
        static_cast<std::uint32_t>(*maximum_index + 1);
  }
  if (result.fixed) {
    const auto& ranges =
        type.systemverilog_container->static_range_expressions;
    const auto* range =
        ranges.empty() ? nullptr : &ranges.front();
    std::string left_error;
    std::string right_error;
    const auto left_value =
        range
            ? evaluate_systemverilog_constant_expression(
                  range->left, {}, environment, left_error)
            : std::nullopt;
    const auto right_value =
        range
            ? evaluate_systemverilog_constant_expression(
                  range->right, {}, environment, right_error)
            : std::nullopt;
    const auto left =
        left_value ? left_value->integer_value() : std::nullopt;
    const auto right =
        right_value ? right_value->integer_value() : std::nullopt;
    const auto in_int32 =
        [](const std::int64_t value) {
          return value
                  >= std::numeric_limits<std::int32_t>::min()
              && value
                  <= std::numeric_limits<std::int32_t>::max();
        };
    if (!left || !right
        || !in_int32(left.value())
        || !in_int32(right.value())) {
      report(
          "FSIM-ELAB-SVPORT-002",
          "static-array port bounds must specialize to signed 32-bit "
          "values spanning 1..4096 elements",
          type.systemverilog_container->span);
      return std::nullopt;
    }
    const auto left_bound = left.value();
    const auto right_bound = right.value();
    const auto count =
        static_cast<std::uint64_t>(
            left_bound >= right_bound
                ? left_bound - right_bound
                : right_bound - left_bound)
        + 1U;
    if (count > maximum_container_elements) {
      report(
          "FSIM-ELAB-SVPORT-002",
          "static-array port bounds must specialize to signed 32-bit "
          "values spanning 1..4096 elements",
          type.systemverilog_container->span);
      return std::nullopt;
    }
    result.index_left =
        static_cast<std::int32_t>(left_bound);
    result.index_right =
        static_cast<std::int32_t>(right_bound);
  }
  return result;
}

std::optional<ContainerObjectId>
HierarchyBuilder::add_owned_container_port(
    const frontend::SignalDeclaration& declaration,
    const std::string_view path,
    ContainerMap& local,
    const ConstantEnvironment& environment) {
  if (const auto existing = local.find(declaration.name);
      existing != local.end()) {
    return existing->second;
  }
  const auto type = container_port_type(
      declaration.type, declaration.span, environment);
  if (!type) {
    return std::nullopt;
  }
  const auto index = design_.container_objects_.size();
  const auto id = static_cast<ContainerObjectId>(index);
  if (static_cast<std::size_t>(id) != index) {
    throw std::length_error{
        "too many elaborated container objects"};
  }
  const auto full_name =
      std::string{path} + "." + declaration.name;
  design_.container_object_info_.push_back(
      ContainerObjectInfo{
          id,
          full_name,
          *type,
          declaration.span,
          true,
          declaration.direction,
          std::nullopt});
  design_.container_objects_.push_back(
      ContainerObject{
          full_name,
          default_container_value(*type),
          std::nullopt});
  local.emplace(declaration.name, id);
  local.emplace(full_name, id);
  design_.container_by_name_.emplace(full_name, id);
  if (path == design_.top_) {
    design_.container_by_name_.emplace(declaration.name, id);
  }
  return id;
}

std::optional<ContainerObjectId>
HierarchyBuilder::connect_container_port(
    const frontend::SignalDeclaration& port,
    const frontend::PortConnection& connection,
    const std::string& path,
    const ContainerMap& parent_containers,
    const std::unordered_set<std::string>&
        parent_read_only_containers,
    const bool cross_language) {
  if (cross_language) {
    report(
        "FSIM-ELAB-SVPORT-004",
        "SystemVerilog container ports cannot cross a language "
        "boundary at '" + path + "." + port.name + "'",
        connection.span);
    return std::nullopt;
  }
  const auto expected =
      container_port_type(port.type, port.span, {});
  if (!expected) {
    return std::nullopt;
  }

  const auto& expression = connection.value;
  const bool sliced =
      expression.kind == frontend::ExpressionKind::Slice;
  const frontend::Expression* base = &expression;
  if (sliced) {
    if ((expression.text != ":"
         && expression.text != "+:"
         && expression.text != "-:")
        || expression.operands.size() != 3
        || expression.operands.front().kind
            != frontend::ExpressionKind::Identifier) {
      report(
          "FSIM-ELAB-SVPORT-005",
          "container port slice actuals must be direct left:right "
          "or indexed selections of static-array objects",
          expression.span);
      return std::nullopt;
    }
    base = &expression.operands.front();
  } else if (
      expression.kind
      != frontend::ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-SVPORT-005",
        "container port actuals must be direct whole-container "
        "objects or static-array slices",
        expression.span);
    return std::nullopt;
  }

  const auto actual = parent_containers.find(base->text);
  if (actual == parent_containers.end()) {
    report(
        "FSIM-ELAB-SVPORT-006",
        "unknown container connection object '" + base->text
            + "' on instance '" + path + "'",
        base->span);
    return std::nullopt;
  }
  const auto& actual_info =
      design_.container_object_info_.at(actual->second);
  auto selected_type = actual_info.type;
  std::optional<ContainerSliceAlias> slice_alias;
  std::optional<std::pair<std::int32_t, std::int32_t>>
      driver_interval;

  const auto element_count =
      [](const ContainerType& type) {
        return static_cast<std::uint64_t>(
                   type.index_left >= type.index_right
                       ? static_cast<std::int64_t>(type.index_left)
                             - type.index_right
                       : static_cast<std::int64_t>(type.index_right)
                             - type.index_left)
            + 1U;
      };
  const auto same_element_profile =
      [](const ContainerType& left,
         const ContainerType& right) {
        return left.element_width == right.element_width
            && left.two_state == right.two_state
            && left.signed_elements
                == right.signed_elements;
      };

  if (sliced) {
    if (!expected->fixed || !actual_info.type.fixed) {
      report(
          "FSIM-ELAB-SVPORT-005",
          "container port slice actuals require a fixed "
          "one-dimensional static-array port and object",
          expression.span);
      return std::nullopt;
    }
    const auto bound =
        [&](const frontend::Expression& value)
            -> std::optional<std::int32_t> {
          std::string error;
          const auto constant =
              evaluate_systemverilog_constant_expression(
                  value, {}, {}, error);
          const auto integer =
              constant && constant->known()
                  ? constant->integer_value()
                  : std::nullopt;
          if (!integer
              || *integer
                  < std::numeric_limits<std::int32_t>::min()
              || *integer
                  > std::numeric_limits<std::int32_t>::max()) {
            report(
                "FSIM-ELAB-SVSLICE-002",
                "static-array slice port bounds must be locally "
                "constant known signed 32-bit values",
                value.span);
            return std::nullopt;
          }
          return static_cast<std::int32_t>(*integer);
        };
    const auto first = bound(expression.operands[1]);
    const auto second = bound(expression.operands[2]);
    if (!first || !second) {
      return std::nullopt;
    }
    auto left = static_cast<std::int64_t>(*first);
    auto right = static_cast<std::int64_t>(*second);
    const bool actual_descending =
        actual_info.type.index_left
        >= actual_info.type.index_right;
    if (expression.text != ":") {
      if (*second <= 0) {
        report(
            "FSIM-ELAB-SVSLICE-002",
            "a static-array indexed slice port width must be "
            "positive",
            expression.operands[2].span);
        return std::nullopt;
      }
      const auto distance =
          static_cast<std::int64_t>(*second) - 1;
      const auto lower =
          expression.text == "+:"
              ? static_cast<std::int64_t>(*first)
              : static_cast<std::int64_t>(*first) - distance;
      const auto upper =
          expression.text == "+:"
              ? static_cast<std::int64_t>(*first) + distance
              : static_cast<std::int64_t>(*first);
      if (actual_descending) {
        left = upper;
        right = lower;
      } else {
        left = lower;
        right = upper;
      }
    }
    const bool selected_descending = left >= right;
    const auto actual_low =
        std::min(
            actual_info.type.index_left,
            actual_info.type.index_right);
    const auto actual_high =
        std::max(
            actual_info.type.index_left,
            actual_info.type.index_right);
    if ((left != right
         && actual_descending != selected_descending)
        || left < actual_low || left > actual_high
        || right < actual_low || right > actual_high) {
      report(
          "FSIM-ELAB-SVSLICE-003",
          "a static-array slice port actual must preserve its "
          "declared direction and remain in range",
          expression.span);
      return std::nullopt;
    }
    selected_type.index_left =
        static_cast<std::int32_t>(left);
    selected_type.index_right =
        static_cast<std::int32_t>(right);
    if (element_count(selected_type)
            != element_count(*expected)
        || !same_element_profile(
            selected_type, *expected)) {
      report(
          "FSIM-ELAB-SVPORT-007",
          "static-array slice port actuals require equal element "
          "counts and identical element width, signedness, and "
          "state domain",
          connection.span);
      return std::nullopt;
    }
    slice_alias = ContainerSliceAlias{
        actual->second,
        selected_type.index_left,
        selected_type.index_right};
    driver_interval =
        std::pair{
            std::min(
                selected_type.index_left,
                selected_type.index_right),
            std::max(
                selected_type.index_left,
                selected_type.index_right)};
  } else {
    if (actual_info.type != *expected) {
      report(
          "FSIM-ELAB-SVPORT-007",
          "whole-container port actuals require an exact kind, "
          "element, index, bound, and range match",
          connection.span);
      return std::nullopt;
    }
    if (actual_info.type.fixed) {
      driver_interval =
          std::pair{
              std::min(
                  actual_info.type.index_left,
                  actual_info.type.index_right),
              std::max(
                  actual_info.type.index_left,
                  actual_info.type.index_right)};
    }
  }

  if ((port.direction
           == frontend::PortDirection::Output
       || port.direction
           == frontend::PortDirection::Inout)
      && parent_read_only_containers.contains(base->text)) {
    report(
        "FSIM-ELAB-SVPORT-009",
        "an input container port cannot be connected to a "
        "descendant output or inout port",
        connection.span);
    return std::nullopt;
  }

  auto connected_object = actual->second;
  if (slice_alias) {
    const auto index = design_.container_objects_.size();
    connected_object =
        static_cast<ContainerObjectId>(index);
    if (static_cast<std::size_t>(connected_object)
        != index) {
      throw std::length_error{
          "too many elaborated container objects"};
    }
    const auto full_name = path + "." + port.name;
    design_.container_object_info_.push_back(
        ContainerObjectInfo{
            connected_object,
            full_name,
            *expected,
            port.span,
            true,
            port.direction,
            slice_alias});
    design_.container_objects_.push_back(
        ContainerObject{
            full_name,
            default_container_value(*expected),
            slice_alias});
  }

  if (port.direction == frontend::PortDirection::Output
      || port.direction
          == frontend::PortDirection::Inout) {
    auto& drivers =
        container_boundary_driver_paths_[actual->second];
    const auto nested_with =
        [](const std::string_view left,
           const std::string_view right) {
          const auto left_prefix =
              std::string{left} + ".";
          const auto right_prefix =
              std::string{right} + ".";
          return left.starts_with(right_prefix)
              || right.starts_with(left_prefix);
        };
    const auto overlaps =
        [&](const ContainerBoundaryDriver& driver) {
          if (!driver_interval
              || !driver.selected_interval) {
            return true;
          }
          return driver_interval->first
                     <= driver.selected_interval->second
              && driver.selected_interval->first
                     <= driver_interval->second;
        };
    if (std::ranges::any_of(
            drivers,
            [&](const auto& driver) {
              return overlaps(driver)
                  && !nested_with(
                      path, driver.path);
            })) {
      report(
          "FSIM-ELAB-SVPORT-008",
          "container object '" + actual_info.name
              + "' has overlapping output/inout module "
              "container-port drivers",
          connection.span);
    }
    drivers.push_back(
        ContainerBoundaryDriver{
            path, driver_interval});
  }
  return connected_object;
}

void HierarchyBuilder::validate_boundary_type(
    const frontend::SignalDeclaration& port,
    const SignalInfo& actual,
    const std::string& path,
    const frontend::SourceSpan& source,
    const bool cross_language) {
  const auto separator = port.type.spelling.find_last_of('.');
  const auto simple_type_name =
      port.type.spelling.substr(
          separator == std::string::npos ? 0 : separator + 1);
  if (!port.type.packed_range
      && !port.type.packed_range_expression
      && (simple_type_name == "bit_vector"
          || simple_type_name == "std_logic_vector"
          || simple_type_name == "std_ulogic_vector")) {
    report(
        "FSIM-ELAB-VHARRAY-005",
        "VHDL array port '" + path + "." + port.name
            + "' requires a concrete non-null index constraint",
        source);
    return;
  }
  const auto unsupported_domain =
      [](const frontend::ValueDomain domain) {
        return domain == frontend::ValueDomain::Unknown;
      };
  if (unsupported_domain(port.type.domain)
      || unsupported_domain(actual.source_domain)) {
    report(
        "FSIM-ELAB-BIND-019",
        "unsupported value domain on boundary '"
            + path + "." + port.name + "'",
        source);
    return;
  }
  if (cross_language
      && (!port.type.packed_members.empty()
          || !actual.packed_members.empty())) {
    report(
        "FSIM-ELAB-BIND-049",
        "packed aggregate boundary '" + path + "." + port.name
            + "' requires a same-language scalar/vector wrapper",
        source);
    return;
  }
  const bool port_record =
      !port.type.packed_members.empty()
      && !port.type.nominal_type.empty();
  const bool actual_record =
      !actual.packed_members.empty()
      && !actual.nominal_type.empty();
  if (!cross_language
      && (port_record || actual_record)
      && (!port_record || !actual_record
          || port.type.nominal_type != actual.nominal_type)) {
    report(
        "FSIM-ELAB-BIND-057",
        "VHDL record boundary '" + path + "." + port.name
            + "' requires the same nominal record type",
        source);
    return;
  }
  const bool port_array = port.type.vhdl_array.has_value();
  const bool actual_array = actual.vhdl_array.has_value();
  if (cross_language && (port_array || actual_array)) {
    report(
        "FSIM-ELAB-BIND-055",
        "VHDL array boundary '" + path + "." + port.name
            + "' requires a same-language scalar/vector wrapper",
        source);
    return;
  }
  if (!cross_language
      && (port_array || actual_array)
      && (!port_array || !actual_array
          || port.type.nominal_type != actual.nominal_type)) {
    report(
        "FSIM-ELAB-BIND-056",
        "VHDL array boundary '" + path + "." + port.name
            + "' requires the same nominal array type",
        source);
    return;
  }
  const bool port_enumeration =
      !port.type.enumeration_literals.empty();
  const bool actual_enumeration =
      !actual.enumeration_literals.empty();
  if (cross_language && (port_enumeration || actual_enumeration)) {
    report(
        "FSIM-ELAB-BIND-052",
        "VHDL enumeration boundary '" + path + "." + port.name
            + "' requires a same-language scalar/vector wrapper",
        source);
    return;
  }
  if (!cross_language
      && (port_enumeration || actual_enumeration)
      && (!port_enumeration || !actual_enumeration
          || port.type.nominal_type != actual.nominal_type)) {
    report(
        "FSIM-ELAB-BIND-053",
        "VHDL enumeration boundary '" + path + "." + port.name
            + "' requires the same nominal enumeration type",
        source);
    return;
  }
  if (!cross_language && port_enumeration && actual_enumeration) {
    const auto bounds =
        [](const std::optional<frontend::EnumerationRange>& range,
           const std::size_t literal_count) {
          if (!range) {
            return std::pair{
                std::int64_t{0},
                static_cast<std::int64_t>(literal_count - 1U)};
          }
          return std::pair{
              std::min(range->left, range->right),
              std::max(range->left, range->right)};
        };
    const auto port_bounds =
        bounds(
            port.type.enumeration_range,
            port.type.enumeration_literals.size());
    const auto actual_bounds =
        bounds(
            actual.enumeration_range,
            actual.enumeration_literals.size());
    const auto contains =
        [](const auto& outer, const auto& inner) {
          return outer.first <= inner.first
              && outer.second >= inner.second;
        };
    const bool compatible =
        port.direction == frontend::PortDirection::Input
            ? contains(port_bounds, actual_bounds)
        : port.direction == frontend::PortDirection::Output
            ? contains(actual_bounds, port_bounds)
            : port_bounds == actual_bounds;
    if (!compatible) {
      report(
          "FSIM-ELAB-BIND-054",
          "enumeration subtype ranges on boundary '" + path + "."
              + port.name
              + "' cannot guarantee a range-safe alias",
          source);
    }
  }
  const auto width = port.type.width().value_or(1);
  if (width != actual.width) {
    report(
        "FSIM-ELAB-BIND-020",
        "width mismatch on '" + path + "." + port.name + "': "
            + std::to_string(width) + " versus "
            + std::to_string(actual.width),
        source);
  }
  if (port.type.is_signed != actual.is_signed && width > 1) {
    report(
        "FSIM-ELAB-BIND-021",
        "signedness mismatch on '" + path + "." + port.name + "'",
        source);
  }
  if (port.type.domain == frontend::ValueDomain::Integer
      || actual.source_domain == frontend::ValueDomain::Integer) {
    const auto bounds =
        [](const std::optional<frontend::IntegerRange>& range) {
          if (!range) {
            return std::pair{
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()};
          }
          return std::pair{
              static_cast<std::int32_t>(
                  std::min(range->left, range->right)),
              static_cast<std::int32_t>(
                  std::max(range->left, range->right))};
        };
    const auto port_bounds = bounds(port.type.integer_range);
    const auto actual_bounds = bounds(actual.integer_range);
    const auto contains =
        [](const auto& outer, const auto& inner) {
          return outer.first <= inner.first
              && outer.second >= inner.second;
        };
    const bool compatible =
        port.direction == frontend::PortDirection::Input
            ? contains(port_bounds, actual_bounds)
        : port.direction == frontend::PortDirection::Output
            ? contains(actual_bounds, port_bounds)
            : port_bounds == actual_bounds;
    if (!compatible) {
      report(
          "FSIM-ELAB-BIND-051",
          "integer subtype ranges on boundary '" + path + "."
              + port.name
              + "' cannot guarantee a range-safe alias",
          source);
    }
  }
  const auto lossy_into_two_state =
      [](const frontend::ValueDomain destination,
         const frontend::ValueDomain source_domain) {
        return is_two_state_domain(destination)
            && !is_two_state_domain(source_domain);
      };
  const bool lossy =
      port.direction == frontend::PortDirection::Output
          ? lossy_into_two_state(
                actual.source_domain, port.type.domain)
          : lossy_into_two_state(
                port.type.domain, actual.source_domain);
  if (lossy) {
    report(
        "FSIM-ELAB-BIND-022",
        "implicit lossy conversion into a 2-state boundary at '"
            + path + "." + port.name + "' is forbidden",
        source);
  }
}

HierarchyBuilder::PortAliases HierarchyBuilder::connect_instance(
    const frontend::Instance& instance,
    const DesignUnit& target,
    const std::string& path,
    const SignalMap& parent_signals,
    const ContainerMap& parent_containers,
    const std::unordered_set<std::string>&
        parent_read_only_containers,
    const Binding* binding,
    const bool cross_language) {
  const auto* ports = unit_ports(parsed_, target);
  if (ports == nullptr) {
    report(
        "FSIM-ELAB-002",
        "architecture '" + target.name
            + "' has no matching entity",
        target.span);
    return {};
  }
  return connect_ports(
      instance,
      *ports,
      path,
      parent_signals,
      parent_containers,
      parent_read_only_containers,
      binding,
      cross_language,
      target.language == frontend::Language::Vhdl2008);
}

frontend::SignalDeclaration
HierarchyBuilder::external_port_declaration(
    const ExternalPort& port) {
  return {
      port.name,
      port.type,
      port.direction,
      true,
      {}};
}

frontend::SignalDeclaration
HierarchyBuilder::foreign_port_declaration(
    const ForeignPort& port) {
  return {
      port.name,
      port.type,
      port.direction,
      true,
      {}};
}

}  // namespace fsim::elaboration
