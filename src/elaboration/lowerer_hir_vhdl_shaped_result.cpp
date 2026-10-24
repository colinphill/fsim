// SPDX-License-Identifier: Apache-2.0
// VHDL function results whose index ranges are known only at run time: the
// result has the bounds of the value the function returns (IEEE 1076-2008
// 4.2.1), so such a call yields a shaped value (see
// lowerer_hir_vhdl_shaped.cpp) whose attributes and range are read when the
// call is evaluated.
#include "lowerer_internal.hpp"

#include <optional>
#include <string_view>
#include <utility>

namespace fsim::elaboration {
namespace {

using semantic::vhdl::ExpressionKind;

std::optional<VhdlShapedAttribute> shaped_attribute(const std::string_view text)
{
    if (text == "'left") {
        return VhdlShapedAttribute::left;
    }
    if (text == "'right") {
        return VhdlShapedAttribute::right;
    }
    if (text == "'low") {
        return VhdlShapedAttribute::low;
    }
    if (text == "'high") {
        return VhdlShapedAttribute::high;
    }
    if (text == "'length") {
        return VhdlShapedAttribute::length;
    }
    if (text == "'ascending") {
        return VhdlShapedAttribute::ascending;
    }
    return std::nullopt;
}

} // namespace

std::optional<Lowerer::HirVhdlShapedProfile>
Lowerer::hir_vhdl_shaped_call_profile(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    // An operator a user function overloads is a call of it too.
    if (!expression || expression->vhdl == nullptr
        || !expression->vhdl->referenced_name
        || (expression->vhdl->kind != ExpressionKind::call
            && expression->vhdl->kind != ExpressionKind::unary
            && expression->vhdl->kind != ExpressionKind::binary)
        || expression->vhdl->text.starts_with('\'')
        || expression->vhdl->text.starts_with('@')
        || expression->vhdl->text.starts_with(
            semantic::vhdl::qualified_expression_prefix)) {
        return std::nullopt;
    }
    const auto resolution = resolve_hir_vhdl_function_call(
        expression_id, hir_process_scope_, 0U);
    const auto declaration = resolution
        ? specialized_hir_unit_->find_declaration(resolution->body)
        : std::nullopt;
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->callable
        || !declaration->vhdl->callable->function
        || !declaration->vhdl->callable->return_type
        || declaration->vhdl->callable->return_identifier) {
        return std::nullopt;
    }
    hir_generic_binding_frames_.push_back(resolution->generic_bindings);
    struct GenericScope {
        decltype(hir_generic_binding_frames_)& frames;
        ~GenericScope() { frames.pop_back(); }
    } generic_scope { hir_generic_binding_frames_ };
    const auto declared = hir_effective_vhdl_subtype(
        *declaration->vhdl->callable->return_type);
    if (!declared || !declared->unconstrained) {
        return std::nullopt;
    }
    auto profile = hir_vhdl_shaped_open_profile(*declared);
    if (!profile || profile->constraints.size() != 1U
        || profile->outer_rank != 0U || !profile->members.empty()
        || hir_vhdl_function_result_shape(
            expression_id, hir_process_scope_, resolution->body)) {
        return std::nullopt;
    }
    return profile;
}

std::optional<ContainerRegisterId> Lowerer::lower_hir_vhdl_shaped_call(
    const semantic::ExpressionId expression_id,
    const HirVhdlShapedProfile& profile)
{
    hir_vhdl_shaped_call_request_ = std::pair { expression_id.value(), profile };
    const auto value = lower_hir_function_call_value(expression_id, 0U);
    hir_vhdl_shaped_call_request_.reset();
    if (!value || !value->container || value->container->type != profile.type) {
        return std::nullopt;
    }
    return value->container->value;
}

bool Lowerer::lower_hir_vhdl_shaped_return(
    const std::size_t frame, const semantic::ExpressionId value)
{
    const auto profile = *hir_callable_frames_[frame].vhdl_shaped_result;
    const auto result = hir_callable_frames_[frame].container_result;
    const auto expression = specialized_hir_unit_->find_expression(value);
    if (!result || !expression || expression->vhdl == nullptr) {
        return false;
    }
    const auto returned = lower_hir_vhdl_shaped_materialize(
        profile, value, hir_source_span(expression->vhdl->source));
    if (!returned) {
        return false;
    }
    process_.operations.emplace_back(CopyContainerRegister {
        *result, *returned });
    hir_callable_frames_[frame].return_jumps.push_back(
        static_cast<InstructionIndex>(process_.operations.size()));
    process_.operations.emplace_back(Jump { });
    return true;
}

std::optional<std::optional<RegisterId>>
Lowerer::lower_hir_vhdl_shaped_range_use(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto span = hir_source_span(source.source);
    // A call prefix is lowered syntactically first: most expressions are
    // neither form.
    const auto call_prefix = [&](const semantic::ExpressionId prefix) {
        const auto node = specialized_hir_unit_->find_expression(prefix);
        return node && node->vhdl != nullptr && node->vhdl->referenced_name
            && (node->vhdl->kind == ExpressionKind::call
                || node->vhdl->kind == ExpressionKind::unary
                || node->vhdl->kind == ExpressionKind::binary);
    };
    // The dimension an attribute's optional argument selects.
    const auto dimension = [&](const semantic::vhdl::Expression& attribute)
        -> std::optional<std::uint32_t> {
        if (attribute.operands.size() < 2U) {
            return 0U;
        }
        const auto selected = hir_constant_integer(attribute.operands[1]);
        return selected && *selected == 1 ? std::optional { 0U } : std::nullopt;
    };
    const auto query = [&](const ContainerRegisterId container,
                           const VhdlShapedAttribute attribute,
                           const std::uint32_t selected,
                           const std::size_t width) {
        const bool boolean = attribute == VhdlShapedAttribute::ascending;
        const auto destination = allocate_register(boolean ? 1U : width,
            boolean ? frontend::ValueDomain::Boolean
                    : frontend::ValueDomain::Integer);
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::query,
            { },
            container,
            destination,
            { },
            { },
            { },
            selected,
            attribute,
            static_cast<std::uint32_t>(boolean ? 1U : width),
            { },
        });
        return destination;
    };
    // `f(x)'left`: the call is evaluated and its result's range read.
    if (source.kind == ExpressionKind::call && !source.operands.empty()
        && source.operands.size() <= 2U) {
        const auto attribute = shaped_attribute(source.text);
        if (!attribute || !call_prefix(source.operands.front())) {
            return std::nullopt;
        }
        const auto profile = hir_vhdl_shaped_call_profile(
            source.operands.front());
        const auto selected = dimension(source);
        if (!profile || !selected) {
            return std::nullopt;
        }
        const auto container = lower_hir_vhdl_shaped_call(
            source.operands.front(), *profile);
        if (!container) {
            return std::optional<RegisterId> { };
        }
        const auto width = expected_width != 0U ? expected_width
            : static_cast<std::size_t>(
                  frontend::vhdl_predefined_integer_storage_width(
                      vhdl_standard_));
        return std::optional { query(*container, *attribute, *selected, width) };
    }
    // `a(r'range)` where r's range is known only at run time: the slice of
    // the packed array a between r's bounds (8.5).
    if (source.kind != ExpressionKind::index || source.operands.size() != 2U) {
        return std::nullopt;
    }
    const auto selector = specialized_hir_unit_->find_expression(
        source.operands[1]);
    if (!selector || selector->vhdl == nullptr
        || selector->vhdl->kind != ExpressionKind::call
        || selector->vhdl->operands.empty()
        || (selector->vhdl->text != "'range"
            && selector->vhdl->text != "'reverse_range")) {
        return std::nullopt;
    }
    const auto range_prefix = selector->vhdl->operands.front();
    const auto selected = dimension(*selector->vhdl);
    if (!selected) {
        return std::nullopt;
    }
    std::optional<ContainerRegisterId> container;
    if (call_prefix(range_prefix)) {
        const auto profile = hir_vhdl_shaped_call_profile(range_prefix);
        if (!profile) {
            return std::nullopt;
        }
        if (hir_vhdl_shaped_active()
            && hir_vhdl_shaped_prefix(source.operands.front()) != nullptr) {
            return std::nullopt;
        }
        container = lower_hir_vhdl_shaped_call(range_prefix, *profile);
    } else if (hir_vhdl_shaped_active()) {
        const auto* local = hir_vhdl_shaped_prefix(range_prefix);
        if (local == nullptr
            || hir_vhdl_shaped_prefix(source.operands.front()) != nullptr) {
            return std::nullopt;
        }
        const auto bound = bind_hir_vhdl_shaped(*local, span);
        if (!bound) {
            return std::optional<RegisterId> { };
        }
        container = bound->local.container;
    } else {
        return std::nullopt;
    }
    const auto range = hir_expression_range(
        source.operands.front(), hir_process_scope_);
    const auto source_width = hir_expression_width(
        source.operands.front(), hir_process_scope_);
    if (!container || !range || !source_width || *source_width == 0U
        || expected_width == 0U) {
        report("FSIM-ELAB-VHSLICE-001",
            "a slice by a range known only at run time requires a packed "
            "prefix of concrete range and a context of known length",
            span);
        return std::optional<RegisterId> { };
    }
    // The slice's direction is the prefix's (8.5).
    const bool reverse = selector->vhdl->text == "'reverse_range";
    const auto ascending = resize_register(
        query(*container, VhdlShapedAttribute::ascending, *selected, 1U), 32U,
        false);
    const std::int64_t required = range->descending == reverse ? 1 : 0;
    process_.operations.emplace_back(IntegerCheck {
        ascending, required, required });
    const auto left = query(*container,
        reverse ? VhdlShapedAttribute::right : VhdlShapedAttribute::left,
        *selected, 64U);
    const auto right = query(*container,
        reverse ? VhdlShapedAttribute::left : VhdlShapedAttribute::right,
        *selected, 64U);
    const auto part = lower_hir_vhdl_dynamic_part(
        *range, left, right, *source_width, expected_width);
    const auto input = part
        ? lower_hir_expression(source.operands.front(), *source_width)
        : std::nullopt;
    if (!input) {
        return std::optional<RegisterId> { };
    }
    const auto destination = allocate_register(
        expected_width, register_domain(*input));
    process_.operations.emplace_back(DynamicPartSelect {
        destination,
        *input,
        part->base,
        part->left,
        part->right,
        part->width,
        part->increasing,
        part->source_descending,
        is_two_state_domain(register_domain(*input)),
        part->base_offset,
    });
    return std::optional { destination };
}

} // namespace fsim::elaboration
