// SPDX-License-Identifier: Apache-2.0
// VHDL records with an element whose index ranges each object supplies
// (IEEE 1076-2008 5.3.3), designated by access values. An object is an
// aggregate container with one member per element in declaration order: a
// packed value for an element of static layout, and a shaped container (see
// lowerer_hir_vhdl_shaped.cpp) for an open array or another such record.
#include "lowerer_internal.hpp"

#include "fsim/support/vhdl_identifier.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace fsim::elaboration {
namespace {

constexpr std::string_view member_prefix { "@vhdl-member:" };

// Names why a shaped record operation was not lowered, under
// FSIM_HIR_LOWER_TRACE_FAILURES.
template <typename Result>
Result record_rejected(const std::string_view reason, Result result)
{
    if (std::getenv("FSIM_HIR_LOWER_TRACE_FAILURES") != nullptr) {
        std::cerr << "[fsim-hir-lower] vhdl-shaped-record-rejected " << reason
                  << '\n';
    }
    return result;
}

// An access handle or a static element of unknown value domain is held in
// two-state bits.
frontend::ValueDomain packed_member_domain(const frontend::ValueDomain domain)
{
    return domain == frontend::ValueDomain::Unknown
        ? frontend::ValueDomain::Bit2
        : domain;
}

} // namespace

// Selects element `name` of the record `path` denotes (IEEE 1076-2008 8.3).
bool Lowerer::select_hir_vhdl_shaped_record_element(
    HirVhdlShapedRecordPath& path, const std::string_view name)
{
    if (!path.shaped || path.shaped->members.empty()) {
        return false;
    }
    const auto& members = path.shaped->members;
    for (std::size_t index { }; index < members.size(); ++index) {
        if (same_hir_identifier(members[index].name, name, true)) {
            path.members.push_back(static_cast<std::uint32_t>(index));
            path.owner = path.shaped;
            path.member = &members[index];
            path.shaped = members[index].shaped;
            return true;
        }
    }
    return false;
}

std::optional<Lowerer::HirVhdlShapedProfile>
Lowerer::hir_vhdl_shaped_record_profile(
    const semantic::vhdl::SubtypeIndication& subtype) const
{
    if (specialized_hir_unit_ == nullptr || !subtype.type_mark.target.valid()) {
        return std::nullopt;
    }
    const auto* definition = semantic::vhdl_base_type_definition(
        *specialized_hir_unit_, subtype.type_mark.target);
    if (definition == nullptr
        || definition->form != semantic::vhdl::TypeForm::record
        || definition->record_elements.empty()) {
        return std::nullopt;
    }
    HirVhdlShapedProfile profile;
    profile.subtype = subtype;
    ContainerType heap;
    heap.element_kind = ContainerElementKind::Aggregate;
    heap.element_width = 0U;
    heap.associative = true;
    heap.index_width = 32U;
    heap.two_state_indices = true;
    heap.element_nominal_type = subtype.type_mark.spelling;
    bool shaped { };
    for (const auto& element : definition->record_elements) {
        HirVhdlShapedMember member;
        member.name = element.name;
        member.subtype = hir_vhdl_chain_element_subtype(
            subtype, element.subtype, element.name);
        const auto effective = hir_effective_vhdl_subtype(member.subtype);
        const auto width = effective ? vhdl_subtype_width(*effective)
                                     : std::nullopt;
        ContainerType member_type;
        if (width && *width <= std::numeric_limits<std::uint32_t>::max()) {
            member.width = *width;
            member.domain = packed_member_domain(
                vhdl_value_domain(effective->domain));
            member.signed_value = effective->signed_value;
            // Each element starts at its subtype's default (6.4.2.3).
            const auto initial = hir_vhdl_default_value(
                *effective, *width, 0U, true);
            member.initial = initial && initial->width() == *width
                ? *initial
                : member.domain == frontend::ValueDomain::Logic9
                ? PackedLogic4::from_logic9_msb_string(std::string(*width, 'U'))
                : PackedLogic4(*width, Logic4::zero);
            member_type.element_kind = ContainerElementKind::Packed;
            member_type.element_width = static_cast<std::uint32_t>(*width);
            member_type.two_state = member.domain != frontend::ValueDomain::Logic4
                && member.domain != frontend::ValueDomain::Logic9;
            member_type.signed_elements = member.signed_value;
            member_type.fixed = true;
            member_type.dimensions = { { 0, 0 } };
        } else {
            auto nested = hir_vhdl_shaped_record_profile(member.subtype);
            if (!nested) {
                nested = hir_vhdl_shaped_profile(member.subtype, true, true);
            }
            if (!nested) {
                return std::nullopt;
            }
            member_type = nested->type;
            member.shaped = std::make_shared<const HirVhdlShapedProfile>(
                std::move(*nested));
            shaped = true;
        }
        heap.element_types.push_back(std::move(member_type));
        heap.member_names.push_back(element.name);
        profile.members.push_back(std::move(member));
    }
    const auto value_type = shaped
        ? runtime::simir::nested_element_container_type(heap)
        : std::nullopt;
    if (!value_type) {
        return std::nullopt;
    }
    profile.type = *value_type;
    profile.heap_type = std::move(heap);
    return profile;
}

std::optional<Lowerer::HirVhdlShapedRecordPath>
Lowerer::hir_vhdl_shaped_record_name_path(
    const semantic::ExpressionId expression_id,
    const std::string_view text) const
{
    const auto declaration_id = hir_referenced_declaration(expression_id);
    const auto declaration = declaration_id
        ? specialized_hir_unit_->find_declaration(*declaration_id)
        : std::nullopt;
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->subtype) {
        return std::nullopt;
    }
    const auto* access = hir_vhdl_access_type_definition(
        *declaration->vhdl->subtype);
    auto root = access != nullptr ? hir_vhdl_shaped_heap_profile(*access)
                                  : std::nullopt;
    if (!root || root->members.empty()) {
        return std::nullopt;
    }
    // The element names follow the object's own name (8.3, 8.1).
    const auto parts = support::split_vhdl_selected_name(text);
    std::size_t first { parts.size() };
    for (std::size_t part { }; part + 1U < parts.size(); ++part) {
        if (same_hir_identifier(parts[part], declaration->vhdl->name, true)) {
            first = part + 1U;
            break;
        }
    }
    if (first >= parts.size()) {
        return std::nullopt;
    }
    HirVhdlShapedRecordPath path;
    path.handle_declaration = *declaration_id;
    path.access_type = access;
    path.shaped = std::make_shared<const HirVhdlShapedProfile>(
        std::move(*root));
    for (auto part = first; part < parts.size(); ++part) {
        if (!select_hir_vhdl_shaped_record_element(path, parts[part])) {
            return std::nullopt;
        }
    }
    return path;
}

std::optional<Lowerer::HirVhdlShapedRecordPath>
Lowerer::hir_vhdl_shaped_record_path(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr || !hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::name
        && source.text.find('.') != std::string::npos) {
        return hir_vhdl_shaped_record_name_path(expression_id, source.text);
    }
    if (source.kind != semantic::vhdl::ExpressionKind::call
        || source.operands.size() != 1U) {
        return std::nullopt;
    }
    // The object an access value designates (`p.all`, or `p` as the prefix
    // of a selected name, 8.1).
    const auto designated = [&](const semantic::ExpressionId handle)
        -> std::optional<HirVhdlShapedRecordPath> {
        const auto subtype = hir_vhdl_expression_subtype(handle);
        const auto* access = subtype
            ? hir_vhdl_access_type_definition(*subtype)
            : nullptr;
        auto root = access != nullptr ? hir_vhdl_shaped_heap_profile(*access)
                                      : std::nullopt;
        if (!root || root->members.empty()) {
            return std::nullopt;
        }
        HirVhdlShapedRecordPath path;
        path.handle = handle;
        path.access_type = access;
        path.shaped = std::make_shared<const HirVhdlShapedProfile>(
            std::move(*root));
        return path;
    };
    if (source.text == "@vhdl-dereference") {
        return designated(source.operands.front());
    }
    if (!source.text.starts_with(member_prefix)) {
        return std::nullopt;
    }
    auto path = hir_vhdl_shaped_record_path(source.operands.front());
    if (!path || !path->shaped) {
        path = designated(source.operands.front());
    }
    if (!path
        || !select_hir_vhdl_shaped_record_element(*path,
            std::string_view { source.text }.substr(member_prefix.size()))) {
        return std::nullopt;
    }
    return path;
}

const Lowerer::HirVhdlShapedLocal* Lowerer::hir_vhdl_shaped_member_view(
    const semantic::ExpressionId expression_id,
    const HirVhdlShapedRecordPath& path) const
{
    if (!path.shaped || !path.shaped->members.empty()) {
        return nullptr;
    }
    auto& view = hir_vhdl_shaped_member_views_[expression_id.value()];
    view.profile = *path.shaped;
    view.heap_handle = path.handle;
    view.heap_handle_declaration = path.handle_declaration;
    view.access_type = path.access_type;
    view.heap_members = path.members;
    return &view;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_shaped_handle(
    const HirVhdlShapedRecordPath& path,
    const HirVhdlAccessHeap& heap,
    const frontend::SourceSpan& span)
{
    std::optional<RegisterId> handle;
    if (path.handle) {
        handle = lower_hir_expression(*path.handle, 32U);
    } else if (path.handle_declaration) {
        const auto binding = hir_runtime_binding(
            *path.handle_declaration, hir_process_scope_, true);
        if (binding && binding->kind == HirRuntimeBindingKind::local
            && binding->local) {
            handle = binding->local;
        }
    }
    if (!handle) {
        return record_rejected("handle", std::optional<RegisterId> { });
    }
    emit_hir_vhdl_access_live_check(heap, *handle, span);
    return handle;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_shaped_record_read(
    const semantic::ExpressionId expression_id,
    const HirVhdlShapedRecordPath& path,
    const std::size_t expected_width,
    const frontend::SourceSpan& span)
{
    auto* heap = hir_vhdl_access_heap(*path.access_type, span);
    if (heap == nullptr || !heap->shaped) {
        return std::nullopt;
    }
    if (!path.shaped && path.member == nullptr) {
        return std::nullopt;
    }
    // A whole record needs its context's length (14.7.3.4).
    if (path.shaped && expected_width == 0U) {
        return record_rejected("record read width",
            std::optional<RegisterId> { });
    }
    const auto handle = lower_hir_vhdl_shaped_handle(path, *heap, span);
    if (!handle) {
        return std::nullopt;
    }
    if (!path.shaped) {
        const auto& member = *path.member;
        const auto value = allocate_register(member.width, member.domain);
        process_.operations.emplace_back(ContainerAggregateRead {
            value, heap->objects, *handle, path.members, false });
        return expected_width != 0U && expected_width != member.width
            ? resize_register(value, expected_width, member.signed_value)
            : value;
    }
    const auto record = allocate_container_register(path.shaped->type);
    process_.operations.emplace_back(ContainerElementRead {
        record, heap->objects, *handle, false, path.members });
    const auto value = allocate_register(expected_width,
        packed_member_domain(hir_expression_domain(
            expression_id, hir_process_scope_)
                .value_or(frontend::ValueDomain::Unknown)));
    process_.operations.emplace_back(VhdlShaped {
        VhdlShapedOperation::to_packed,
        { },
        record,
        value,
        { },
        { },
        { },
        0U,
        VhdlShapedAttribute::length,
        static_cast<std::uint32_t>(expected_width),
        { },
    });
    return value;
}

bool Lowerer::lower_hir_vhdl_shaped_record_write(
    const HirVhdlShapedRecordPath& path,
    const semantic::ExpressionId value_id,
    const frontend::SourceSpan& span)
{
    auto* heap = hir_vhdl_access_heap(*path.access_type, span);
    if (heap == nullptr || !heap->shaped
        || (!path.shaped && path.member == nullptr)) {
        return false;
    }
    const auto handle = lower_hir_vhdl_shaped_handle(path, *heap, span);
    if (!handle) {
        return false;
    }
    if (!path.shaped) {
        const auto& member = *path.member;
        auto value = lower_hir_expression(value_id, member.width);
        if (!value) {
            return false;
        }
        if (register_width(*value) != member.width) {
            value = resize_register(*value, member.width, member.signed_value);
        }
        process_.operations.emplace_back(ContainerAggregateWrite {
            heap->objects, *handle, *value, path.members, false });
        return true;
    }
    // A whole record keeps its elements' bounds; the value must match them
    // (10.6.2.1).
    const auto width = hir_expression_width(value_id, hir_process_scope_);
    if (!width || *width == 0U) {
        return record_rejected("record write width", false);
    }
    const auto value = lower_hir_expression(value_id, *width);
    if (!value) {
        return false;
    }
    const auto record = allocate_container_register(path.shaped->type);
    process_.operations.emplace_back(ContainerElementRead {
        record, heap->objects, *handle, false, path.members });
    process_.operations.emplace_back(VhdlShaped {
        VhdlShapedOperation::from_packed,
        record,
        { },
        *value,
        { },
        { },
        { },
        0U,
        VhdlShapedAttribute::length,
        0U,
        { },
    });
    process_.operations.emplace_back(ContainerElementWrite {
        heap->objects, *handle, record, false, path.members });
    return true;
}

std::optional<ContainerRegisterId> Lowerer::lower_hir_vhdl_shaped_create(
    const HirVhdlShapedProfile& profile,
    const semantic::vhdl::SubtypeIndication& shape)
{
    const auto container = allocate_container_register(profile.type);
    if (!profile.members.empty()) {
        // The index register is ignored for an aggregate value.
        const auto at = load_integer(0, 32U, frontend::ValueDomain::Bit2);
        for (std::size_t index { }; index < profile.members.size(); ++index) {
            const auto& member = profile.members[index];
            const std::vector<std::uint32_t> path {
                static_cast<std::uint32_t>(index)
            };
            if (!member.shaped) {
                process_.operations.emplace_back(ContainerAggregateWrite {
                    container, at, load_constant(member.initial, member.domain),
                    path, false });
                continue;
            }
            const auto nested = lower_hir_vhdl_shaped_create(*member.shaped,
                hir_vhdl_chain_element_subtype(
                    shape, member.subtype, member.name));
            if (!nested) {
                return std::nullopt;
            }
            process_.operations.emplace_back(ContainerElementWrite {
                container, at, *nested, false, path });
        }
        return container;
    }
    // An array takes the index ranges met first along its subtype chain,
    // then, for an array of open arrays, its element's (5.3.2.2).
    const auto index_ranges = [&](const semantic::vhdl::SubtypeIndication& subtype) {
        std::vector<semantic::vhdl::RangeConstraint> ranges;
        auto current = subtype;
        std::unordered_set<std::uint32_t> visited;
        for (std::size_t depth { }; depth < 32U; ++depth) {
            for (const auto& constraint : current.constraints) {
                if (constraint.kind == semantic::vhdl::RangeKind::array_index) {
                    ranges.push_back(constraint);
                }
            }
            if (!ranges.empty() || !current.type_mark.target.valid()
                || !visited.insert(current.type_mark.target.value()).second) {
                break;
            }
            const auto type = specialized_hir_unit_->find_type(
                current.type_mark.target);
            if (!type || type->vhdl == nullptr
                || (type->vhdl->form != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form != semantic::vhdl::TypeForm::alias)) {
                break;
            }
            current = type->vhdl->base;
        }
        return ranges;
    };
    auto ranges = index_ranges(shape);
    if (profile.outer_rank != 0U) {
        const auto* array = shape.type_mark.target.valid()
            ? semantic::vhdl_base_type_definition(
                  *specialized_hir_unit_, shape.type_mark.target)
            : nullptr;
        if (array == nullptr || !array->element_subtype) {
            return record_rejected("create element",
                std::optional<ContainerRegisterId> { });
        }
        const auto inner = index_ranges(
            hir_vhdl_chain_element_subtype(shape, *array->element_subtype));
        ranges.insert(ranges.end(), inner.begin(), inner.end());
    }
    if (ranges.size() != profile.constraints.size()) {
        return record_rejected("create ranges",
            std::optional<ContainerRegisterId> { });
    }
    std::vector<RegisterId> bounds;
    std::vector<std::uint8_t> directions;
    const auto bound = [&](const std::optional<std::int64_t> constant,
                           const std::optional<semantic::ExpressionId> source)
        -> std::optional<RegisterId> {
        if (constant) {
            return load_integer(*constant, 64U, frontend::ValueDomain::Integer);
        }
        if (source) {
            const auto width = hir_expression_width(
                *source, hir_process_scope_).value_or(32U);
            return lower_hir_expression(*source, width);
        }
        return std::nullopt;
    };
    for (const auto& range : ranges) {
        const auto left = bound(range.left, range.left_expression);
        const auto right = bound(range.right, range.right_expression);
        if (!left || !right) {
            return record_rejected("create bounds",
                std::optional<ContainerRegisterId> { });
        }
        bounds.push_back(*left);
        bounds.push_back(*right);
        const bool descending = range.direction_from_bounds
            ? hir_vhdl_range_direction(range, hir_process_scope_)
                  .value_or(range.descending)
            : range.descending;
        directions.push_back(descending ? 1U : 0U);
    }
    process_.operations.emplace_back(VhdlShaped {
        VhdlShapedOperation::create,
        container,
        { },
        load_constant(profile.element_default, profile.element_domain),
        std::move(bounds),
        std::move(directions),
        profile.type,
        0U,
        VhdlShapedAttribute::length,
        0U,
        { },
    });
    return container;
}

std::optional<ContainerRegisterId> Lowerer::lower_hir_vhdl_shaped_materialize(
    const HirVhdlShapedProfile& profile,
    const semantic::ExpressionId value_id,
    const frontend::SourceSpan& span)
{
    // A qualified expression denotes its operand's value (9.3.5).
    auto value = value_id;
    for (std::size_t depth { }; depth < 16U; ++depth) {
        const auto qualified = specialized_hir_unit_->find_expression(value);
        if (!qualified || qualified->vhdl == nullptr
            || qualified->vhdl->kind != semantic::vhdl::ExpressionKind::call
            || !qualified->vhdl->text.starts_with(
                semantic::vhdl::qualified_expression_prefix)
            || qualified->vhdl->operands.size() != 1U) {
            break;
        }
        value = qualified->vhdl->operands.front();
    }
    const auto source = specialized_hir_unit_->find_expression(value);
    if (!source || source->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto from_packed = [&](const ContainerRegisterId container,
                                 const RegisterId packed) {
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::from_packed,
            container,
            { },
            packed,
            { },
            { },
            { },
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        });
    };
    if (!profile.members.empty()) {
        // A record aggregate: each element's value supplies its ranges
        // (9.3.3.2).
        if (source->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate) {
            const auto& members = profile.members;
            std::vector<std::optional<semantic::ExpressionId>> values(
                members.size());
            std::size_t position { };
            const auto member_index = [&](const std::string_view name)
                -> std::optional<std::size_t> {
                for (std::size_t index { }; index < members.size(); ++index) {
                    if (same_hir_identifier(members[index].name, name, true)) {
                        return index;
                    }
                }
                return std::nullopt;
            };
            for (const auto& association : source->vhdl->associations) {
                if (association.positional()) {
                    if (position >= values.size()) {
                        return std::nullopt;
                    }
                    values[position++] = association.value;
                    continue;
                }
                if (support::equal_ignoring_ascii_case(
                        association.choice_spelling, "others")) {
                    for (auto& entry : values) {
                        if (!entry) {
                            entry = association.value;
                        }
                    }
                    continue;
                }
                std::vector<std::string> names;
                for (const auto choice_id : association.choices) {
                    const auto choice
                        = specialized_hir_unit_->find_expression(choice_id);
                    if (!choice || choice->vhdl == nullptr
                        || choice->vhdl->kind
                            != semantic::vhdl::ExpressionKind::name) {
                        return std::nullopt;
                    }
                    names.push_back(choice->vhdl->text);
                }
                if (names.empty()) {
                    names.push_back(association.choice_spelling);
                }
                for (const auto& name : names) {
                    const auto index = member_index(name);
                    if (!index) {
                        return record_rejected("aggregate choice " + name,
                            std::optional<ContainerRegisterId> { });
                    }
                    values[*index] = association.value;
                }
            }
            const auto container = allocate_container_register(profile.type);
            const auto at = load_integer(0, 32U, frontend::ValueDomain::Bit2);
            for (std::size_t index { }; index < members.size(); ++index) {
                const auto& member = members[index];
                if (!values[index]) {
                    return record_rejected("aggregate element " + member.name,
                        std::optional<ContainerRegisterId> { });
                }
                const std::vector<std::uint32_t> path {
                    static_cast<std::uint32_t>(index)
                };
                if (member.shaped) {
                    const auto nested = lower_hir_vhdl_shaped_materialize(
                        *member.shaped, *values[index], span);
                    if (!nested) {
                        return std::nullopt;
                    }
                    process_.operations.emplace_back(ContainerElementWrite {
                        container, at, *nested, false, path });
                    continue;
                }
                const auto element = specialized_hir_unit_->find_expression(
                    *values[index]);
                const auto context = hir_effective_vhdl_subtype(member.subtype)
                    .value_or(member.subtype);
                auto lowered = element && element->vhdl != nullptr
                        && element->vhdl->kind
                            == semantic::vhdl::ExpressionKind::aggregate
                    ? lower_hir_vhdl_aggregate(
                          *values[index], member.width, &context)
                    : lower_hir_expression(*values[index], member.width);
                if (!lowered) {
                    return std::nullopt;
                }
                if (register_width(*lowered) != member.width) {
                    lowered = resize_register(
                        *lowered, member.width, member.signed_value);
                }
                process_.operations.emplace_back(ContainerAggregateWrite {
                    container, at, *lowered, path, false });
            }
            return container;
        }
        // A designated record: its value, ranges included.
        if (const auto path = hir_vhdl_shaped_record_path(value);
            path && path->shaped && path->shaped->type == profile.type) {
            auto* heap = hir_vhdl_access_heap(*path->access_type, span);
            const auto handle = heap != nullptr
                ? lower_hir_vhdl_shaped_handle(*path, *heap, span)
                : std::nullopt;
            if (!handle) {
                return std::nullopt;
            }
            const auto container = allocate_container_register(profile.type);
            process_.operations.emplace_back(ContainerElementRead {
                container, heap->objects, *handle, false, path->members });
            return container;
        }
        // A packed record value: its own shape gives the open elements'
        // ranges (1076-2019 6.4.2.2).
        const auto shape = hir_vhdl_record_value_shape(
            value, profile.subtype, source->vhdl->scope);
        const auto width = hir_expression_width(value, hir_process_scope_);
        if (!shape || !width || *width == 0U) {
            return record_rejected("record value shape",
                std::optional<ContainerRegisterId> { });
        }
        auto shaped_subtype = profile.subtype;
        apply_hir_vhdl_shape(shaped_subtype, *shape, source->vhdl->scope);
        const auto container = lower_hir_vhdl_shaped_create(
            profile, shaped_subtype);
        const auto packed = container
            ? lower_hir_expression(value, *width)
            : std::nullopt;
        if (!packed) {
            return std::nullopt;
        }
        from_packed(*container, *packed);
        return container;
    }
    // An array: another shaped value, or a packed one with its own ranges.
    if (hir_vhdl_shaped_value_profile(value)) {
        const auto shaped = lower_hir_vhdl_shaped_container(value, span);
        if (!shaped || shaped->second.type != profile.type) {
            return std::nullopt;
        }
        const auto container = allocate_container_register(profile.type);
        process_.operations.emplace_back(CopyContainerRegister {
            container, shaped->first });
        return container;
    }
    // A literal or aggregate takes its open ranges from itself (9.3.2,
    // 9.3.3.3).
    const bool contextual
        = source->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
        || source->vhdl->kind == semantic::vhdl::ExpressionKind::string_literal;
    if (const auto shape = contextual
            ? hir_vhdl_contextual_shape(
                  value, profile.subtype, source->vhdl->scope)
            : hir_vhdl_shape(value);
        shape && shape->complete()
        && (profile.outer_rank != 0U
            || shape->ranges.size() == profile.constraints.size())) {
        auto shaped_subtype = profile.subtype;
        apply_hir_vhdl_shape(shaped_subtype, *shape, source->vhdl->scope);
        const auto effective = hir_effective_vhdl_subtype(shaped_subtype)
            .value_or(shaped_subtype);
        const auto width = vhdl_subtype_width(effective);
        const auto container = width
            ? lower_hir_vhdl_shaped_create(profile, shaped_subtype)
            : std::nullopt;
        auto packed = !container ? std::nullopt
            : source->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
            ? lower_hir_vhdl_aggregate(value, *width, &effective)
            : lower_hir_expression(value, *width);
        if (!packed || register_width(*packed) != *width) {
            return record_rejected("array value",
                std::optional<ContainerRegisterId> { });
        }
        from_packed(*container,
            conform_hir_vhdl_shaped_domain(*packed, profile.element_domain));
        return container;
    }
    // A one-dimensional literal or aggregate: its own index range (9.3.2,
    // 9.3.3.3).
    if (profile.constraints.size() != 1U) {
        return record_rejected("array value shape",
            std::optional<ContainerRegisterId> { });
    }
    const auto packed = lower_hir_vhdl_shaped_value(value, profile, std::nullopt);
    if (!packed) {
        return std::nullopt;
    }
    const auto range = hir_vhdl_shaped_packed_range(
        profile, value, register_width(*packed));
    const auto container = allocate_container_register(profile.type);
    process_.operations.emplace_back(VhdlShaped {
        VhdlShapedOperation::create,
        container,
        { },
        load_constant(profile.element_default, profile.element_domain),
        {
            load_integer(range.left, 64U, frontend::ValueDomain::Integer),
            load_integer(range.right, 64U, frontend::ValueDomain::Integer),
        },
        { static_cast<std::uint8_t>(range.descending ? 1U : 0U) },
        profile.type,
        0U,
        VhdlShapedAttribute::length,
        0U,
        { },
    });
    from_packed(container, *packed);
    return container;
}

} // namespace fsim::elaboration
