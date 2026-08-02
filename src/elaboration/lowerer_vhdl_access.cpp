// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

SourceLocation access_source_location(
    const frontend::SourceSpan& span) {
  return SourceLocation{
      span.source_name,
      static_cast<std::uint32_t>(span.begin.line),
      static_cast<std::uint32_t>(span.begin.column)};
}

std::string access_identity(const frontend::Type& type) {
  return !type.nominal_type.empty()
      ? type.nominal_type
      : !type.vhdl_type_declaration.empty()
          ? type.vhdl_type_declaration
          : type.spelling;
}

}  // namespace

std::optional<ContainerRegisterId> Lowerer::vhdl_access_heap(
    const frontend::Type& access_type,
    const frontend::SourceSpan& span) {
  if (!access_type.vhdl_access
      || access_type.vhdl_access->designated_types.size() != 1) {
    report(
        "FSIM-ELAB-VHACCESS-005",
        "a VHDL access operation requires one resolved designated subtype",
        span);
    return std::nullopt;
  }
  const auto& access = *access_type.vhdl_access;
  const auto& designated = access.designated_types.front();
  const auto width = designated.width();
  if (!width || *width == 0
      || *width > std::numeric_limits<std::uint32_t>::max()
      || designated.domain == frontend::ValueDomain::String
      || designated.systemverilog_container
      || designated.vhdl_protected) {
    report(
        "FSIM-ELAB-VHACCESS-006",
        "a bounded VHDL allocator requires a concrete nonempty packed "
        "designated subtype",
        span);
    return std::nullopt;
  }
  if (designated.domain == frontend::ValueDomain::Logic9) {
    report(
        "FSIM-ELAB-VHACCESS-007",
        "nine-state designated objects require the later access-object "
        "state extension",
        span);
    return std::nullopt;
  }
  const auto identity = access_identity(access_type);
  if (const auto found = vhdl_access_heaps_.find(identity);
      found != vhdl_access_heaps_.end()) {
    return found->second;
  }
  ContainerType heap_type;
  heap_type.element_width = static_cast<std::uint32_t>(*width);
  heap_type.two_state = is_two_state_domain(designated.domain);
  heap_type.signed_elements = designated.is_signed;
  heap_type.queue = true;
  heap_type.maximum_elements = access.maximum_objects;
  heap_type.element_nominal_type = designated.nominal_type;
  const auto heap = allocate_container_register(heap_type);
  vhdl_access_heaps_.emplace(identity, heap);
  return heap;
}

std::optional<RegisterId> Lowerer::vhdl_access_index(
    const Expression& access_expression,
    const frontend::Type& access_type) {
  if (!access_type.vhdl_access) {
    return std::nullopt;
  }
  const auto handle = lower_expression(
      access_expression,
      access_type.vhdl_access->handle_width,
      &access_type);
  if (!handle) {
    return std::nullopt;
  }
  const auto one = allocate_register(
      access_type.vhdl_access->handle_width,
      frontend::ValueDomain::Bit2);
  process_.operations.emplace_back(LoadConstant{
      one,
      unsigned_value(
          1, access_type.vhdl_access->handle_width)});
  const auto index = allocate_register(
      access_type.vhdl_access->handle_width,
      frontend::ValueDomain::Bit2);
  process_.operations.emplace_back(Binary{
      BinaryOperator::subtract_unsigned,
      index,
      *handle,
      one});
  return index;
}

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_access_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type) {
  if (language_ != frontend::Language::Vhdl2008) {
    return {};
  }
  if (expression.kind == ExpressionKind::Binary
      && expression.operands.size() == 2) {
    const auto lhs_type =
        vhdl_expression_type(expression.operands[0]);
    const auto rhs_type =
        vhdl_expression_type(expression.operands[1]);
    const auto lhs_access = lhs_type && lhs_type->vhdl_access;
    const auto rhs_access = rhs_type && rhs_type->vhdl_access;
    const auto lhs_null = expression.operands[0].kind
            == ExpressionKind::Call
        && expression.operands[0].text == "@vhdl-null";
    const auto rhs_null = expression.operands[1].kind
            == ExpressionKind::Call
        && expression.operands[1].text == "@vhdl-null";
    if (!lhs_access && !rhs_access) {
      return {};
    }
    if (expression.text != "=" && expression.text != "/=") {
      report(
          "FSIM-ELAB-VHACCESS-017",
          "VHDL access values support only equality and inequality",
          expression.span);
      return std::nullopt;
    }
    const auto* context = lhs_access
        ? &*lhs_type : rhs_access ? &*rhs_type : nullptr;
    if (context == nullptr
        || (!lhs_access && !lhs_null)
        || (!rhs_access && !rhs_null)) {
      report(
          "FSIM-ELAB-VHACCESS-018",
          "VHDL access equality requires compatible access values or null",
          expression.span);
      return std::nullopt;
    }
    if (lhs_access && rhs_access
        && access_identity(*lhs_type)
            != access_identity(*rhs_type)) {
      report(
          "FSIM-ELAB-VHACCESS-019",
          "VHDL access equality requires the same nominal access type",
          expression.span);
      return std::nullopt;
    }
    const auto width = context->vhdl_access->handle_width;
    const auto lhs = lower_expression(
        expression.operands[0], width, context);
    const auto rhs = lower_expression(
        expression.operands[1], width, context);
    if (!lhs || !rhs) {
      return std::nullopt;
    }
    const auto equal = allocate_register(
        1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary{
        BinaryOperator::case_equal, equal, *lhs, *rhs});
    if (expression.text == "=") {
      return equal;
    }
    const auto different = allocate_register(
        1, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(UnaryNot{
        different, equal});
    return different;
  }
  if (expression.kind != ExpressionKind::Call) {
    return {};
  }
  if (expression.text == "@vhdl-null") {
    if (expected_type == nullptr || !expected_type->vhdl_access) {
      report(
          "FSIM-ELAB-VHACCESS-008",
          "the VHDL null access value requires an access-type context",
          expression.span);
      return std::nullopt;
    }
    const auto width = expected_type->vhdl_access->handle_width;
    if (expected_width != width) {
      report(
          "FSIM-ELAB-VHACCESS-009",
          "the VHDL access context has an inconsistent handle width",
          expression.span);
      return std::nullopt;
    }
    const auto result = allocate_register(
        width, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        result, unsigned_value(0, width)});
    return result;
  }
  if (expression.text == "@vhdl-new"
      || expression.text == "@vhdl-new-qualified") {
    if (expected_type == nullptr || !expected_type->vhdl_access
        || expected_type->vhdl_access->designated_types.size() != 1
        || expression.operands.empty()) {
      report(
          "FSIM-ELAB-VHACCESS-010",
          "a VHDL allocator requires a resolved access-type context",
          expression.span);
      return std::nullopt;
    }
    const auto& access = *expected_type->vhdl_access;
    const auto& designated = access.designated_types.front();
    constexpr std::string_view subtype_prefix{"@vhdl-subtype:"};
    const auto& subtype = expression.operands.front();
    if (subtype.kind != ExpressionKind::Identifier
        || !subtype.text.starts_with(subtype_prefix)
        || subtype.text.substr(subtype_prefix.size())
            != designated.spelling) {
      report(
          "FSIM-ELAB-VHACCESS-011",
          "an allocator subtype must match its access type's designated "
          "subtype",
          subtype.span);
      return std::nullopt;
    }
    const auto designated_width = designated.width();
    const auto heap = vhdl_access_heap(*expected_type, expression.span);
    if (!designated_width || !heap) {
      return std::nullopt;
    }
    std::optional<RegisterId> value;
    if (expression.text == "@vhdl-new-qualified") {
      if (expression.operands.size() != 2) {
        report(
            "FSIM-ELAB-VHACCESS-012",
            "a qualified VHDL allocator requires exactly one initial value",
            expression.span);
        return std::nullopt;
      }
      value = lower_expression(
          expression.operands[1], *designated_width, &designated);
    } else {
      const auto initialized = allocate_register(
          *designated_width, designated.domain);
      process_.operations.emplace_back(LoadConstant{
          initialized,
          default_packed_value(designated, *designated_width)});
      value = initialized;
    }
    if (!value || register_width(*value) != *designated_width) {
      if (value) {
        report(
            "FSIM-ELAB-VHACCESS-013",
            "a VHDL allocator initializer has the wrong designated width",
            expression.span);
      }
      return std::nullopt;
    }

    const auto size = allocate_register(
        access.handle_width, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ContainerSize{size, *heap});
    const auto maximum = allocate_register(
        access.handle_width, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        maximum,
        unsigned_value(access.maximum_objects, access.handle_width)});
    const auto available = allocate_register(
        1, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary{
        BinaryOperator::less_unsigned,
        available,
        size,
        maximum});
    process_.operations.emplace_back(Assert{
        available,
        "VHDL access allocation exceeded the bounded object limit",
        AssertionSeverity::failure,
        access_source_location(expression.span)});
    process_.operations.emplace_back(PushContainer{
        *heap, *value, false, std::nullopt});

    const auto one = allocate_register(
        access.handle_width, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        one, unsigned_value(1, access.handle_width)});
    const auto handle = allocate_register(
        access.handle_width, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary{
        BinaryOperator::add_unsigned,
        handle,
        size,
        one});
    return handle;
  }
  if (expression.text != "@vhdl-dereference"
      || expression.operands.size() != 1) {
    return {};
  }
  const auto access_type =
      vhdl_expression_type(expression.operands.front());
  if (!access_type || !access_type->vhdl_access
      || access_type->vhdl_access->designated_types.size() != 1) {
    report(
        "FSIM-ELAB-VHACCESS-014",
        "a VHDL dereference requires an access-typed prefix",
        expression.span);
    return std::nullopt;
  }
  const auto& designated =
      access_type->vhdl_access->designated_types.front();
  const auto width = designated.width();
  const auto heap = vhdl_access_heap(*access_type, expression.span);
  const auto index = vhdl_access_index(
      expression.operands.front(), *access_type);
  if (!width || !heap || !index) {
    return std::nullopt;
  }
  const auto result = allocate_register(*width, designated.domain);
  process_.operations.emplace_back(ContainerRead{
      result, *heap, *index, false});
  return result;
}

bool Lowerer::lower_vhdl_access_assignment(
    const Statement& statement) {
  if (language_ != frontend::Language::Vhdl2008
      || statement.target.kind != ExpressionKind::Call
      || statement.target.text != "@vhdl-dereference") {
    return false;
  }
  if (statement.target.operands.size() != 1) {
    report(
        "FSIM-ELAB-VHACCESS-014",
        "a VHDL dereference target requires one access-typed prefix",
        statement.target.span);
    return true;
  }
  if (statement.assignment_kind != AssignmentKind::Blocking
      || statement.procedural_assignment_control
          != frontend::ProceduralAssignmentControl::None) {
    report(
        "FSIM-ELAB-VHACCESS-015",
        "a dereferenced access object requires a time-free variable "
        "assignment",
        statement.span);
    return true;
  }
  const auto access_type =
      vhdl_expression_type(statement.target.operands.front());
  if (!access_type || !access_type->vhdl_access
      || access_type->vhdl_access->designated_types.size() != 1) {
    report(
        "FSIM-ELAB-VHACCESS-014",
        "a VHDL dereference target requires an access-typed prefix",
        statement.target.span);
    return true;
  }
  const auto& designated =
      access_type->vhdl_access->designated_types.front();
  const auto width = designated.width();
  const auto heap = vhdl_access_heap(
      *access_type, statement.target.span);
  const auto index = vhdl_access_index(
      statement.target.operands.front(), *access_type);
  if (!width || !heap || !index) {
    return true;
  }
  if (!validate_vhdl_composite_assignment(
          &designated, statement.value)) {
    return true;
  }
  const auto value = lower_expression(
      statement.value, *width, &designated);
  if (!value) {
    return true;
  }
  if (register_width(*value) != *width) {
    report(
        "FSIM-ELAB-VHACCESS-016",
        "a dereferenced assignment has the wrong designated width",
        statement.value.span);
    return true;
  }
  process_.operations.emplace_back(ContainerWrite{
      *heap, *index, *value, false});
  return true;
}

}  // namespace fsim::elaboration
