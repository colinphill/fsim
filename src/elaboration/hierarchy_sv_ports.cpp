// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "vhdl_array_boundary.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<StringObjectId> HierarchyBuilder::add_owned_string_port(
    const frontend::SignalDeclaration& declaration,
    const std::string_view path,
    StringMap& local) {
  if (const auto existing = local.find(declaration.name);
      existing != local.end()) {
    return existing->second;
  }
  if (declaration.type.domain != frontend::ValueDomain::String
      || declaration.type.systemverilog_container) {
    report(
        "FSIM-ELAB-SVPORT-010",
        "a mutable string port must use the direct SystemVerilog string "
        "data type",
        declaration.span);
    return std::nullopt;
  }
  const auto index = design_.string_objects_.size();
  const auto id = static_cast<StringObjectId>(index);
  if (static_cast<std::size_t>(id) != index) {
    throw std::length_error{"too many elaborated string objects"};
  }
  const auto full_name = std::string{path} + "." + declaration.name;
  design_.string_object_info_.push_back(
      StringObjectInfo{
          id, full_name, declaration.span, true,
          declaration.direction});
  design_.string_objects_.push_back(StringObject{full_name, {}});
  local.emplace(declaration.name, id);
  local.emplace(full_name, id);
  design_.string_by_name_.emplace(full_name, id);
  if (design_.roots_.size() == 1 && path == active_root_) {
    design_.string_by_name_.emplace(declaration.name, id);
  }
  return id;
}

std::optional<StringObjectId> HierarchyBuilder::connect_string_port(
    const frontend::SignalDeclaration& port,
    const frontend::PortConnection& connection,
    const std::string& path,
    const StringMap& parent_strings,
    const std::unordered_set<StringObjectId>& parent_read_only_strings,
    const bool cross_language) {
  if (cross_language) {
    report(
        "FSIM-ELAB-SVPORT-010",
        "SystemVerilog mutable string ports cannot cross a language "
        "boundary at '" + path + "." + port.name + "'",
        connection.span);
    return std::nullopt;
  }
  if (port.type.domain != frontend::ValueDomain::String
      || port.type.systemverilog_container
      || connection.value.kind
          != frontend::ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-SVPORT-010",
        "a mutable string port requires a direct same-language string "
        "object actual",
        connection.span);
    return std::nullopt;
  }
  const auto actual = parent_strings.find(connection.value.text);
  if (actual == parent_strings.end()) {
    report(
        "FSIM-ELAB-SVPORT-010",
        "unknown mutable string port actual '"
            + connection.value.text + "' on instance '" + path + "'",
        connection.span);
    return std::nullopt;
  }
  if ((port.direction == frontend::PortDirection::Output
       || port.direction == frontend::PortDirection::Inout)
      && parent_read_only_strings.contains(actual->second)) {
    report(
        "FSIM-ELAB-SVPORT-011",
        "an input mutable string port cannot be connected to a descendant "
        "output or inout port",
        connection.span);
    return std::nullopt;
  }
  if (port.direction == frontend::PortDirection::Output
      || port.direction == frontend::PortDirection::Inout) {
    auto& drivers = string_boundary_driver_paths_[actual->second];
    const auto nested_with =
        [](const std::string_view left, const std::string_view right) {
          const auto left_prefix = std::string{left} + ".";
          const auto right_prefix = std::string{right} + ".";
          return left.starts_with(right_prefix)
              || right.starts_with(left_prefix);
        };
    if (std::ranges::any_of(
            drivers,
            [&](const std::string& driver) {
              return !nested_with(path, driver);
            })) {
      report(
          "FSIM-ELAB-SVPORT-012",
          "a mutable string object has conflicting output/inout module "
          "port drivers",
          connection.span);
    }
    drivers.push_back(path);
  }
  return actual->second;
}

std::optional<ContainerType> HierarchyBuilder::container_port_type(
    const frontend::Type& type,
    const frontend::SourceSpan& source,
    const ConstantEnvironment& environment) {
  const auto evaluate =
      [&](const frontend::Expression& expression)
          -> std::optional<std::int64_t> {
        std::string error;
        const auto value =
            evaluate_systemverilog_constant_expression(
                expression, {}, environment, error);
        return value ? value->integer_value()
                     : std::optional<std::int64_t>{};
      };
  return materialize_systemverilog_container_type(
      type, source, evaluate,
      [&](std::string code,
          std::string message,
          frontend::SourceSpan span) {
        const bool bound_failure =
            code == "FSIM-ELAB-SVCONTAINER-004"
            || code == "FSIM-ELAB-SVCONTAINER-020";
        report(
            bound_failure ? "FSIM-ELAB-SVPORT-002"
                          : "FSIM-ELAB-SVPORT-001",
            std::move(message), std::move(span));
      });
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
  if (design_.roots_.size() == 1 && path == active_root_) {
    design_.container_by_name_.emplace(declaration.name, id);
  }
  return id;
}

std::optional<ContainerObjectId>
HierarchyBuilder::connect_container_port(
    const frontend::SignalDeclaration& port,
    const frontend::PortConnection& connection,
    const std::string& path,
    const SignalMap& parent_signals,
    const ContainerMap& parent_containers,
    const std::unordered_set<std::string>&
        parent_read_only_containers,
    const bool cross_language) {
  const auto expected =
      container_port_type(port.type, port.span, {});
  if (!expected) {
    return std::nullopt;
  }
  if (cross_language) {
    return connect_cross_language_container_port(
        port, connection, path, parent_signals, *expected);
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
      && !(port.type.vhdl_array
           && port.type.vhdl_array->flat_width)
      && (simple_type_name == "bit_vector"
          || simple_type_name == "std_logic_vector"
          || simple_type_name == "std_ulogic_vector")) {
    report(
        "FSIM-ELAB-VHARRAY-005",
        "VHDL array port '" + path + "." + port.name
            + "' requires a concrete index constraint",
        source);
    return;
  }
  const auto unsupported_domain =
      [](const frontend::ValueDomain domain) {
        return domain == frontend::ValueDomain::Unknown;
      };
  const bool scalar_boundary =
      port.type.systemverilog_scalar
          != frontend::SystemVerilogScalarKind::None
      || actual.systemverilog_scalar
          != frontend::SystemVerilogScalarKind::None;
  if (scalar_boundary) {
    if (cross_language
        || port.type.systemverilog_scalar != actual.systemverilog_scalar) {
      report(
          "FSIM-ELAB-BIND-019",
          "real/time boundary '" + path + "." + port.name
              + "' requires an exact same-language scalar profile",
          source);
    }
    return;
  }
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
        "aggregate boundary '" + path + "." + port.name
            + "' requires the same nominal aggregate type",
        source);
    return;
  }
  const bool port_array = port.type.vhdl_array.has_value();
  const bool actual_array = static_cast<bool>(actual.vhdl_array);
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
  if (!cross_language && port_array && actual_array) {
    const auto& formal = *port.type.vhdl_array;
    const auto& connected = *actual.vhdl_array;
    if (!vhdl_array_shape_matches(formal, connected, true)) {
      report(
          "FSIM-ELAB-BIND-031",
          "VHDL array rank, element subtype, bounds, direction, or "
          "flattened stride differs on boundary '" + path + "."
              + port.name + "'",
          source);
      return;
    }
  }
  const bool port_enumeration =
      !port.type.enumeration_literals.empty();
  const bool actual_enumeration =
      !actual.enumeration_literals.empty();
  if (cross_language && (port_enumeration || actual_enumeration)) {
    report(
        "FSIM-ELAB-BIND-052",
        "enumeration boundary '" + path + "." + port.name
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
        "enumeration boundary '" + path + "." + port.name
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
  const bool boolean_involved =
      port.type.domain == frontend::ValueDomain::Boolean
      || actual.source_domain == frontend::ValueDomain::Boolean;
  const bool integer_involved =
      port.type.domain == frontend::ValueDomain::Integer
      || actual.source_domain == frontend::ValueDomain::Integer;
  const bool boolean_boundary =
      cross_language && width == 1 && actual.width == 1
      && ((port.type.domain == frontend::ValueDomain::Boolean
           && (actual.source_domain == frontend::ValueDomain::Bit2
               || actual.source_domain == frontend::ValueDomain::Logic4))
          || (actual.source_domain == frontend::ValueDomain::Boolean
              && (port.type.domain == frontend::ValueDomain::Bit2
                  || port.type.domain == frontend::ValueDomain::Logic4)));
  const bool integer_boundary =
      cross_language && width == 32 && actual.width == 32
      && port.type.is_signed && actual.is_signed
      && ((port.type.domain == frontend::ValueDomain::Integer
           && (actual.source_domain == frontend::ValueDomain::Bit2
               || actual.source_domain == frontend::ValueDomain::Logic4))
          || (actual.source_domain == frontend::ValueDomain::Integer
              && (port.type.domain == frontend::ValueDomain::Bit2
                  || port.type.domain == frontend::ValueDomain::Logic4)));
  const bool adaptable_direction =
      port.direction == frontend::PortDirection::Input
      || port.direction == frontend::PortDirection::Output
      || port.direction == frontend::PortDirection::Buffer;
  const bool adaptable_width =
      cross_language
      && !boolean_involved && !integer_involved
      && adaptable_direction;
  if (width != actual.width && !adaptable_width) {
    report(
        "FSIM-ELAB-BIND-020",
        "width mismatch on '" + path + "." + port.name + "': "
            + std::to_string(width) + " versus "
            + std::to_string(actual.width),
        source);
  }
  if (!cross_language
      && port.type.packed_range
      && actual.packed_range
      && (port.type.packed_range->left
              != actual.packed_range->left
          || port.type.packed_range->right
              != actual.packed_range->right
          || port.type.packed_range->descending
              != actual.packed_range->descending)) {
    report(
        "FSIM-ELAB-BIND-031",
        "VHDL packed-array bounds or direction differ on boundary '"
            + path + "." + port.name + "'",
        source);
  }
  const bool adaptable_signedness =
      cross_language
      && !boolean_involved && !integer_involved
      && adaptable_direction;
  if (port.type.is_signed != actual.is_signed && width > 1
      && !adaptable_signedness) {
    report(
        "FSIM-ELAB-BIND-021",
        "signedness mismatch on '" + path + "." + port.name + "'",
        source);
  }
  if (integer_involved) {
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
    bool compatible = integer_boundary && adaptable_direction;
    if (!cross_language) {
      const auto port_bounds = bounds(port.type.integer_range);
      const auto actual_bounds = bounds(actual.integer_range);
      const auto contains =
          [](const auto& outer, const auto& inner) {
            return outer.first <= inner.first
                && outer.second >= inner.second;
          };
      compatible =
          port.direction == frontend::PortDirection::Input
              ? contains(port_bounds, actual_bounds)
          : port.direction == frontend::PortDirection::Output
              ? contains(actual_bounds, port_bounds)
              : port_bounds == actual_bounds;
    }
    if (!compatible) {
      report(
          "FSIM-ELAB-BIND-051",
          "integer subtype ranges on boundary '" + path + "."
              + port.name + "' cannot guarantee a range-safe "
                "32-bit signed conversion",
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
  const bool checked_boolean_loss =
      boolean_boundary
      && adaptable_direction;
  const bool checked_integer_loss =
      integer_boundary && adaptable_direction;
  if (lossy && !checked_boolean_loss && !checked_integer_loss) {
    report(
        "FSIM-ELAB-BIND-022",
        "implicit lossy conversion into a 2-state boundary at '"
            + path + "." + port.name + "' is forbidden",
        source);
  }
}

HierarchyBuilder::PortAliases HierarchyBuilder::connect_instance(
    const frontend::Instance& instance,
    DesignUnit& target,
    const std::string& path,
    const SignalMap& parent_signals,
    const StringMap& parent_strings,
    const std::unordered_set<StringObjectId>&
        parent_read_only_strings,
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
      parent_strings,
      parent_read_only_strings,
      parent_containers,
      parent_read_only_containers,
      binding,
      cross_language,
      target.language == frontend::Language::Vhdl2008,
      &target);
}

frontend::SignalDeclaration
HierarchyBuilder::external_port_declaration(
    const ExternalPort& port) {
  frontend::SignalDeclaration declaration;
  declaration.name = port.name;
  declaration.type = port.type;
  declaration.direction = port.direction;
  declaration.is_port = true;
  return declaration;
}

}  // namespace fsim::elaboration
