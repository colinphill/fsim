// SPDX-License-Identifier: Apache-2.0
//
// The shapes of VHDL composite values (IEEE 1076-2008 5.3.2.2, 5.3.3): the
// index range of each dimension of an array and the constraints of the
// elements and record elements below it. A shape is composed from the object
// an expression names (its declared subtype, the actual a formal or port takes
// its bounds from, an alias's object) and the selections applied to it.
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>
#include <unordered_set>

namespace fsim::elaboration {
namespace {

    using semantic::vhdl::ElementConstraint;
    using semantic::vhdl::RangeConstraint;
    using semantic::vhdl::RangeKind;

    // FSIM_VHDL_SHAPE_TRACE prints composed shapes; read once, as shape
    // queries run for most VHDL expressions.
    bool vhdl_shape_trace()
    {
        static const bool enabled
            = std::getenv("FSIM_VHDL_SHAPE_TRACE") != nullptr;
        return enabled;
    }

    template <typename Range>
    [[nodiscard]] RangeConstraint index_constraint(const Range& range)
    {
        RangeConstraint constraint;
        constraint.kind = RangeKind::array_index;
        constraint.left = range.left;
        constraint.right = range.right;
        constraint.descending = range.descending;
        constraint.null = range.descending ? range.left < range.right
                                           : range.left > range.right;
        return constraint;
    }

    // The constraints of `owner` addressed to `step` and below, with that
    // step removed.
    [[nodiscard]] std::vector<ElementConstraint> constraints_below(
        const std::vector<ElementConstraint>& owner, const std::string_view step)
    {
        std::vector<ElementConstraint> result;
        for (const auto& constraint : owner) {
            if (constraint.path.empty()
                || !same_hir_identifier(constraint.path.front(), step, true)) {
                continue;
            }
            ElementConstraint below = constraint;
            below.path.erase(below.path.begin());
            result.push_back(std::move(below));
        }
        return result;
    }

    // `shape`'s levels addressed by `path` (prefix first).
    template <typename Shape>
    void append_constraints(std::vector<ElementConstraint>& result,
        const std::vector<std::string>& path, const Shape& shape)
    {
        if (!shape.ranges.empty()
            && std::ranges::all_of(shape.ranges,
                [](const auto& range) { return range.has_value(); })) {
            ElementConstraint level;
            level.path = path;
            for (const auto& range : shape.ranges) {
                level.constraints.push_back(index_constraint(*range));
            }
            result.push_back(std::move(level));
        }
        for (const auto& constraint : shape.element_constraints) {
            ElementConstraint nested = constraint;
            nested.path.insert(nested.path.begin(), path.begin(), path.end());
            result.push_back(std::move(nested));
        }
    }

    // The shape a signal's elaborated layout records.
    template <typename Shape>
    [[nodiscard]] Shape metadata_shape(const PackedTypeMetadata& type)
    {
        using Range = typename decltype(Shape::ranges)::value_type::value_type;
        Shape result;
        if (type.vhdl_array) {
            for (const auto& dimension : type.vhdl_array->dimensions) {
                result.ranges.push_back(dimension.range
                        ? std::optional { Range { dimension.range->left,
                              dimension.range->right,
                              dimension.range->descending } }
                        : std::nullopt);
            }
            if (type.vhdl_array->element_types.size() == 1U) {
                append_constraints(result.element_constraints, { "" },
                    metadata_shape<Shape>(
                        type.vhdl_array->element_types.front()));
            }
            return result;
        }
        for (const auto& member : type.packed_members) {
            if (member.nested_types.size() == 1U) {
                append_constraints(result.element_constraints, { member.name },
                    metadata_shape<Shape>(member.nested_types.front()));
            }
        }
        return result;
    }

    // Fills what `shape` leaves open from `facts`.
    template <typename Shape>
    void complete_shape(Shape& shape, const Shape& facts)
    {
        if (shape.ranges.empty()) {
            shape.ranges = facts.ranges;
        } else if (facts.ranges.size() == shape.ranges.size()) {
            for (std::size_t dimension { }; dimension < shape.ranges.size();
                ++dimension) {
                if (!shape.ranges[dimension]) {
                    shape.ranges[dimension] = facts.ranges[dimension];
                }
            }
        }
        for (const auto& constraint : facts.element_constraints) {
            if (std::ranges::none_of(shape.element_constraints,
                    [&](const ElementConstraint& existing) {
                        return existing.path == constraint.path;
                    })) {
                shape.element_constraints.push_back(constraint);
            }
        }
    }

} // namespace

bool Lowerer::HirVhdlShape::complete() const noexcept
{
    return std::ranges::all_of(
        ranges, [](const auto& range) { return range.has_value(); });
}

bool Lowerer::HirVhdlShape::null() const noexcept
{
    return std::ranges::any_of(ranges, [](const auto& range) {
        return range
            && range->left != range->right
            && range->descending != (range->left > range->right);
    });
}

std::optional<Lowerer::HirVhdlShape> Lowerer::hir_vhdl_subtype_shape(
    const semantic::vhdl::SubtypeIndication& declared,
    const semantic::ScopeId scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    // A subtype that a package instance declares reads that instance's
    // generics (IEEE 1076-2008 4.9).
    const SizeRestoreGuard instance_frames { hir_generic_binding_frames_ };
    if (declared.type_mark.spelling.find('.') != std::string::npos) {
        semantic::vhdl::Name mark;
        mark.spelling = declared.type_mark.spelling;
        if (const auto member
            = hir_resolver().resolve_vhdl_package_members(mark, scope)
                  .unique();
            member && member->package_instance
            && !member->generic_bindings.empty()) {
            hir_generic_binding_frames_.push_back(member->generic_bindings);
        }
    }
    const auto subtype = hir_effective_vhdl_subtype(declared).value_or(declared);
    // The number of index ranges: the array type's dimensions, one for a
    // predefined vector (IEEE 1076-2008 16.3), none for a scalar or record.
    const auto chain = hir_vhdl_subtype_chain(subtype, scope);
    const semantic::vhdl::TypeDefinition* array { };
    if (chain.definition != nullptr
        && chain.definition->form == semantic::vhdl::TypeForm::array) {
        array = chain.definition;
    } else if (subtype.type_mark.target.valid()) {
        const auto* base = semantic::vhdl_base_type_definition(
            *specialized_hir_unit_, subtype.type_mark.target);
        if (base != nullptr && base->form == semantic::vhdl::TypeForm::array) {
            array = base;
        }
    }
    const auto constrained = std::ranges::count_if(subtype.constraints,
        [](const RangeConstraint& range) {
            return range.kind == RangeKind::array_index;
        });
    // Every predefined array type is one-dimensional, including NUMERIC_STD's
    // SIGNED and UNSIGNED, which are known by their names rather than by a
    // retained type record (IEEE 1076-2008 16.8.5.1).
    const bool predefined_vector = array == nullptr
        && (chain.builtin != semantic::vhdl::BuiltinTypeIdentity::none
            || subtype.builtin_type
                != semantic::vhdl::BuiltinTypeIdentity::none
            || hir_vhdl_array_type_family(subtype).has_value());
    std::size_t dimensions = array != nullptr ? array->array_dimensions.size()
        : predefined_vector
            && constrained <= 1
        ? 1U
        : static_cast<std::size_t>(constrained);
    dimensions = std::max(dimensions, static_cast<std::size_t>(constrained));
    HirVhdlShape result;
    result.ranges.resize(dimensions);
    std::size_t dimension { };
    for (const auto& constraint : subtype.constraints) {
        if (constraint.kind != RangeKind::array_index) {
            continue;
        }
        if (dimension >= dimensions) {
            break;
        }
        if (const auto range = hir_vhdl_concrete_range(constraint, scope)) {
            result.ranges[dimension]
                = HirPackedRange { *range->left, *range->right,
                      range->descending };
        }
        ++dimension;
    }
    if (array != nullptr) {
        for (std::size_t index { };
            index < dimensions && index < array->array_dimensions.size();
            ++index) {
            const auto& declared_dimension = array->array_dimensions[index];
            if (result.ranges[index] || !declared_dimension.constraint) {
                continue;
            }
            if (const auto range = hir_vhdl_concrete_range(
                    *declared_dimension.constraint, scope)) {
                result.ranges[index] = HirPackedRange { *range->left,
                    *range->right, range->descending };
            }
        }
    }
    for (const auto& constraint : subtype.element_constraints) {
        ElementConstraint folded;
        folded.path = constraint.path;
        bool static_bounds = !constraint.constraints.empty();
        for (const auto& range : constraint.constraints) {
            if (range.kind != RangeKind::array_index) {
                folded.constraints.push_back(range);
                continue;
            }
            const auto concrete = hir_vhdl_concrete_range(range, scope);
            if (!concrete) {
                static_bounds = false;
                break;
            }
            folded.constraints.push_back(*concrete);
        }
        if (static_bounds) {
            result.element_constraints.push_back(std::move(folded));
        }
    }
    // The constraints the element subtypes of the type declare apply
    // below the object's own (IEEE 1076-2008 5.3.2.2, 5.3.3).
    if (hir_vhdl_shape_depth_ >= 32U) {
        return result;
    }
    ++hir_vhdl_shape_depth_;
    struct DepthGuard {
        std::size_t& depth;
        ~DepthGuard() { --depth; }
    } guard { hir_vhdl_shape_depth_ };
    std::vector<ElementConstraint> declared_below;
    const auto append_element = [&](const std::string& step,
                                    const semantic::vhdl::SubtypeIndication&
                                        element) {
        if (const auto shape = hir_vhdl_subtype_shape(element, scope)) {
            append_constraints(declared_below, { step }, *shape);
        }
    };
    if (array != nullptr && array->element_subtype) {
        append_element(std::string { }, *array->element_subtype);
    } else if (chain.definition != nullptr
        && chain.definition->form == semantic::vhdl::TypeForm::record) {
        for (const auto& element : chain.definition->record_elements) {
            append_element(element.name, element.subtype);
        }
    }
    for (auto& constraint : declared_below) {
        // An object's constraint of a level overrides the declared one.
        if (std::ranges::none_of(result.element_constraints,
                [&](const ElementConstraint& existing) {
                    return existing.path == constraint.path;
                })) {
            result.element_constraints.push_back(std::move(constraint));
        }
    }
    return result;
}

void Lowerer::apply_hir_vhdl_shape(semantic::vhdl::SubtypeIndication& subtype,
    const HirVhdlShape& shape, const semantic::ScopeId scope) const
{
    // Levels the subtype already constrains keep their constraints.
    const auto declared = hir_vhdl_subtype_shape(subtype, scope);
    const auto declared_level = [&](const std::vector<std::string>& path) {
        return declared
            && std::ranges::any_of(declared->element_constraints,
                [&](const ElementConstraint& candidate) {
                    return candidate.path == path;
                });
    };
    std::size_t dimension { };
    bool changed { };
    for (auto& constraint : subtype.constraints) {
        if (constraint.kind != RangeKind::array_index) {
            continue;
        }
        if (dimension < shape.ranges.size() && shape.ranges[dimension]
            && (!constraint.left || !constraint.right)) {
            const auto range = index_constraint(*shape.ranges[dimension]);
            constraint.left = range.left;
            constraint.right = range.right;
            constraint.descending = range.descending;
            constraint.null = range.null;
            constraint.direction_from_bounds = false;
            changed = true;
        }
        ++dimension;
    }
    if (dimension == 0U && !shape.ranges.empty() && shape.complete()) {
        for (const auto& range : shape.ranges) {
            subtype.constraints.push_back(index_constraint(*range));
        }
        subtype.unconstrained = false;
        changed = true;
    }
    for (const auto& constraint : shape.element_constraints) {
        auto existing = std::ranges::find_if(subtype.element_constraints,
            [&](const ElementConstraint& candidate) {
                return candidate.path == constraint.path;
            });
        if (existing == subtype.element_constraints.end()) {
            if (declared_level(constraint.path)) {
                continue;
            }
            subtype.element_constraints.push_back(constraint);
            changed = true;
        } else if (std::ranges::any_of(existing->constraints,
                       [](const RangeConstraint& range) {
                           return range.kind == RangeKind::array_index
                               && (!range.left || !range.right);
                       })) {
            *existing = constraint;
            changed = true;
        }
    }
    if (changed) {
        subtype.executable_width.reset();
        if (auto effective = hir_effective_vhdl_subtype(subtype)) {
            subtype = std::move(*effective);
        }
    }
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_external_subtype(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::call
        || expression->vhdl->text != "@vhdl-external"
        || expression->vhdl->operands.size() != 2U) {
        return std::nullopt;
    }
    constexpr auto subtype_prefix = std::string_view { "@vhdl-subtype:" };
    const auto subtype_expression = specialized_hir_unit_->find_expression(
        expression->vhdl->operands.back());
    if (!subtype_expression || subtype_expression->vhdl == nullptr
        || !subtype_expression->vhdl->text.starts_with(subtype_prefix)) {
        return std::nullopt;
    }
    semantic::vhdl::SubtypeIndication declared;
    declared.type_mark.spelling
        = subtype_expression->vhdl->text.substr(subtype_prefix.size());
    auto subtype = hir_effective_vhdl_subtype(declared);
    const auto signal = hir_vhdl_external_signal_lookup(expression_id);
    if (!subtype || !signal) {
        return subtype;
    }
    // The constraint written in the external name has the bounds of the
    // object denoted, which its elaborated layout records.
    const auto& info = design_.signal_info_[*signal];
    PackedTypeMetadata layout;
    layout.vhdl_array = info.vhdl_array;
    layout.packed_members = info.packed_members;
    auto shape = metadata_shape<HirVhdlShape>(layout);
    if (!info.vhdl_array && info.packed_members.empty() && info.packed_range
        && hir_vhdl_array_type_family(*subtype)) {
        shape.ranges.push_back(HirPackedRange { info.packed_range->left,
            info.packed_range->right, info.packed_range->descending });
    }
    apply_hir_vhdl_shape(*subtype, shape, expression->vhdl->scope);
    return subtype;
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_return_identifier_context(
    const semantic::ExpressionId value,
    const semantic::vhdl::SubtypeIndication& target) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto callee = hir_referenced_declaration(value);
    const auto function = callee
        ? specialized_hir_unit_->find_declaration(*callee)
        : std::nullopt;
    if (!function || function->vhdl == nullptr
        || function->vhdl->form != semantic::vhdl::DeclarationForm::function
        || !function->vhdl->callable
        || !function->vhdl->callable->return_identifier) {
        return std::nullopt;
    }
    auto effective = hir_effective_vhdl_subtype(target);
    if (!effective || effective->unconstrained
        || !effective->executable_width) {
        return std::nullopt;
    }
    return effective;
}

bool Lowerer::hir_vhdl_contextual_result_local(
    const semantic::DeclarationId declaration_id) const
{
    if (!active_hir_callable_
        || *active_hir_callable_ >= hir_callable_frames_.size()
        || specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto declaration
        = specialized_hir_unit_->find_declaration(declaration_id);
    const auto callable = specialized_hir_unit_->find_declaration(
        hir_callable_frames_[*active_hir_callable_].declaration);
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->subtype || !callable
        || callable->vhdl == nullptr || !callable->vhdl->callable
        || !callable->vhdl->callable->return_identifier) {
        return false;
    }
    const auto return_identifier = *callable->vhdl->callable->return_identifier;
    const auto result_declaration
        = specialized_hir_unit_->find_declaration(return_identifier);
    if (result_declaration && result_declaration->vhdl != nullptr
        && simple_name_of(declaration->vhdl->subtype->type_mark.spelling)
            == result_declaration->vhdl->name) {
        return true;
    }
    auto type_id = declaration->vhdl->subtype->type_mark.target;
    std::unordered_set<std::uint32_t> visited;
    while (type_id.valid() && visited.insert(type_id.value()).second) {
        const auto type = specialized_hir_unit_->find_type(type_id);
        if (!type || type->vhdl == nullptr) {
            return false;
        }
        if (type->vhdl->declaration == return_identifier) {
            return true;
        }
        type_id = type->vhdl->base.type_mark.target;
    }
    return false;
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_bound_formal_subtype(
    const semantic::DeclarationId formal) const
{
    for (auto frame = hir_generic_binding_frames_.rbegin();
         frame != hir_generic_binding_frames_.rend(); ++frame) {
        const auto bound = std::ranges::find(*frame, formal,
            &semantic::CompiledActualBinding::formal);
        if (bound == frame->end()) {
            continue;
        }
        const auto declaration = specialized_hir_unit_ != nullptr
            ? specialized_hir_unit_->find_declaration(formal)
            : std::nullopt;
        // Only an object's binding carries its actual's subtype; a generic
        // type's carries the type it denotes.
        if (!bound->vhdl_type || !declaration || declaration->vhdl == nullptr
            || declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::generic_type) {
            return std::nullopt;
        }
        return hir_effective_vhdl_subtype(*bound->vhdl_type)
            .value_or(*bound->vhdl_type);
    }
    return std::nullopt;
}

std::optional<Lowerer::HirVhdlShape> Lowerer::hir_vhdl_object_facts(
    const semantic::DeclarationId declaration,
    const std::size_t dimensions) const
{
    HirVhdlShape object;
    // An object of the return identifier's subtype has the call's result
    // subtype, record element constraints included (IEEE 1076-2019 4.2.1).
    if (hir_vhdl_contextual_result_local(declaration)) {
        const auto& result_subtype
            = hir_callable_frames_[*active_hir_callable_].vhdl_result_subtype;
        if (result_subtype && !result_subtype->element_constraints.empty()) {
            object.element_constraints = result_subtype->element_constraints;
            return object;
        }
    }
    // A formal of the running subprogram takes its bounds from its actual
    // (IEEE 1076-2008 4.2.2.2).
    bool formal { };
    for (std::size_t dimension { }; dimension < dimensions; ++dimension) {
        const auto range
            = hir_vhdl_callable_formal_range(declaration, dimension);
        if (!range) {
            break;
        }
        formal = true;
        object.ranges.resize(dimensions);
        object.ranges[dimension] = *range;
    }
    if (const auto* constraints
        = hir_vhdl_callable_formal_element_constraints(declaration)) {
        formal = true;
        object.element_constraints = *constraints;
    }
    // A formal a shape proof binds to a call's actual has that actual's
    // subtype (IEEE 1076-2008 4.2.2.2).
    if (!formal) {
        if (const auto bound = hir_vhdl_bound_formal_subtype(declaration)) {
            auto shape = hir_vhdl_subtype_shape(*bound, hir_process_scope_);
            if (shape && shape->complete()
                && (!shape->ranges.empty()
                    || !shape->element_constraints.empty())) {
                return shape;
            }
        }
    }
    // An object alias whose subtype indication leaves index ranges or
    // element bounds open takes them from the aliased object
    // (IEEE 1076-2008 6.6.2).
    if (!formal) {
        if (const auto aliased = hir_vhdl_alias_object(declaration)) {
            // A formal bound to its call's actual has that actual's shape.
            std::optional<HirVhdlShape> shape;
            for (auto frame = hir_generic_binding_frames_.rbegin();
                 !shape && frame != hir_generic_binding_frames_.rend();
                 ++frame) {
                const auto bound = std::ranges::find(*frame, *aliased,
                    &semantic::CompiledActualBinding::formal);
                if (bound != frame->end()) {
                    if (bound->expression) {
                        shape = hir_vhdl_shape(*bound->expression);
                    }
                    break;
                }
            }
            if (!shape) {
                shape = hir_vhdl_object_shape(*aliased, hir_process_scope_);
            }
            if (shape) {
                return shape;
            }
        }
    }
    // A generic constant of an unconstrained array subtype has the shape
    // of the actual its instance binds (IEEE 1076-2008 6.5.6.2); a literal
    // or aggregate actual takes the generic's subtype as its context.
    if (!formal) {
        for (auto frame = hir_generic_binding_frames_.rbegin();
             frame != hir_generic_binding_frames_.rend(); ++frame) {
            const auto bound = std::ranges::find(*frame, declaration,
                &semantic::CompiledActualBinding::formal);
            if (bound == frame->end()) {
                continue;
            }
            const auto declared = specialized_hir_unit_ != nullptr
                ? specialized_hir_unit_->find_declaration(declaration)
                : std::nullopt;
            if (bound->expression && declared && declared->vhdl != nullptr
                && declared->vhdl->form
                    == semantic::vhdl::DeclarationForm::generic_constant
                && declared->vhdl->subtype) {
                auto shape = hir_vhdl_contextual_shape(*bound->expression,
                    *declared->vhdl->subtype, declared->vhdl->scope);
                if (shape && shape->complete()) {
                    return shape;
                }
            }
            break;
        }
    }
    if (!formal) {
        // A signal or port: the layout elaboration gave its occurrence
        // (IEEE 1076-2008 6.5.6.3, 14.4.2.5).
        const auto binding
            = hir_runtime_binding(declaration, hir_process_scope_, false);
        if (binding && binding->signal && !binding->signal_selection
            && *binding->signal < design_.signal_info_.size()) {
            const auto& info = design_.signal_info_[*binding->signal];
            PackedTypeMetadata layout;
            layout.vhdl_array = info.vhdl_array;
            layout.packed_members = info.packed_members;
            object = metadata_shape<HirVhdlShape>(layout);
        }
    }
    // A constant whose subtype leaves index ranges open takes them from its
    // initial value (IEEE 1076-2008 6.4.2.2), as do signals and variables
    // in VHDL-2019 (6.4.2.3, 6.4.2.4).
    if (!formal
        && (!object.complete() || object.ranges.empty()
            || object.element_constraints.empty())) {
        const auto declared = specialized_hir_unit_ != nullptr
            ? specialized_hir_unit_->find_declaration(declaration)
            : std::nullopt;
        using Form = semantic::vhdl::DeclarationForm;
        if (declared && declared->vhdl != nullptr
            && (declared->vhdl->form == Form::constant
                || (vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019
                    && (declared->vhdl->form == Form::signal
                        || declared->vhdl->form == Form::variable)))
            && declared->vhdl->initializer && declared->vhdl->subtype) {
            const auto subtype_shape = hir_vhdl_subtype_shape(
                *declared->vhdl->subtype, declared->vhdl->scope);
            // Bounded index ranges over an element of open bounds
            // (`t_slv_array(0 to 3)` of std_logic_vector) leave the width
            // to the initial value too.
            const bool open_element = subtype_shape
                && subtype_shape->complete() && !subtype_shape->ranges.empty()
                && subtype_shape->element_constraints.empty()
                && object.element_constraints.empty()
                && !vhdl_subtype_width(*declared->vhdl->subtype);
            // A record subtype that leaves element bounds open takes them
            // from the initial value too (IEEE 1076-2019 6.4.2.2).
            const auto effective = hir_effective_vhdl_subtype(
                *declared->vhdl->subtype);
            const bool open_record = effective
                && hir_vhdl_partially_constrained_record(*effective);
            if (!subtype_shape || !subtype_shape->complete() || open_element
                || open_record) {
                auto initial = hir_vhdl_shape(*declared->vhdl->initializer);
                // A literal or aggregate has the declared subtype as its
                // context (9.3.2, 9.3.3.3).
                if ((!initial || !initial->complete())
                    && declared->vhdl->form != Form::constant) {
                    if (const auto initialized
                        = hir_vhdl_initialized_object_subtype(
                            *declared->vhdl->initializer,
                            *declared->vhdl->subtype,
                            declared->vhdl->scope)) {
                        initial = hir_vhdl_subtype_shape(
                            *initialized, declared->vhdl->scope);
                    }
                }
                // A record value's shape is its element constraints,
                // whatever the rank of the member being shaped.
                if (initial && initial->complete()
                    && (initial->ranges.size() == dimensions
                        || (open_record && initial->ranges.empty()
                            && !initial->element_constraints.empty()))) {
                    if (object.ranges.empty()) {
                        object = std::move(*initial);
                    } else {
                        complete_shape(object, *initial);
                    }
                }
            }
        }
    }
    return object;
}

std::optional<Lowerer::HirVhdlShape> Lowerer::hir_vhdl_object_shape(
    const semantic::DeclarationId declaration_id,
    const semantic::ScopeId scope) const
{
    const auto declaration = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_declaration(declaration_id)
        : std::nullopt;
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->subtype) {
        return std::nullopt;
    }
    auto result = hir_vhdl_subtype_shape(*declaration->vhdl->subtype, scope);
    const auto facts = hir_vhdl_object_facts(
        declaration_id, result ? result->ranges.size() : 1U);
    if (!result) {
        return facts;
    }
    if (facts) {
        complete_shape(*result, *facts);
    }
    return result;
}

bool Lowerer::hir_vhdl_null_value(
    const semantic::ExpressionId expression_id) const
{
    const auto value = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!value || value->vhdl == nullptr) {
        return false;
    }
    if (value->vhdl->kind == semantic::vhdl::ExpressionKind::string_literal) {
        return value->vhdl->text == "\"\"";
    }
    // An aggregate whose named choices are all static null ranges has no
    // elements (IEEE 1076-2008 9.3.3.3).
    if (value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
        && !value->vhdl->associations.empty()
        && std::ranges::all_of(value->vhdl->associations,
            [&](const semantic::vhdl::AggregateAssociation& association) {
                return !association.positional()
                    && !association.choices.empty()
                    && std::ranges::all_of(association.choices,
                        [&](const semantic::ExpressionId choice_id) {
                            const auto choice
                                = specialized_hir_unit_->find_expression(
                                    choice_id);
                            if (!choice || choice->vhdl == nullptr
                                || choice->vhdl->kind
                                    != semantic::vhdl::ExpressionKind::binary
                                || choice->vhdl->operands.size() != 2U
                                || (choice->vhdl->text != "to"
                                    && choice->vhdl->text != "downto")) {
                                return false;
                            }
                            const auto left = hir_constant_integer(
                                choice->vhdl->operands[0]);
                            const auto right = hir_constant_integer(
                                choice->vhdl->operands[1]);
                            return left && right
                                && (choice->vhdl->text == "to"
                                        ? *left > *right
                                        : *left < *right);
                        });
            })) {
        return true;
    }
    const auto shape = hir_vhdl_shape(expression_id);
    return shape && shape->null();
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_concatenation_subtype(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    // A user-declared "&" has its own result subtype.
    if (source.referenced_name && source.referenced_name->selected) {
        return std::nullopt;
    }
    // The array operands share the result's base type; an element operand
    // has the element type (IEEE 1076-2008 9.2.5).
    struct ArrayOperand {
        semantic::vhdl::SubtypeIndication subtype;
        semantic::TypeId root;
        semantic::TypeId element_root;
    };
    std::vector<ArrayOperand> operands;
    // A string literal or aggregate takes its type from this context
    // (IEEE 1076-2008 9.3.2, 9.3.3.1); only the other operands fix it.
    const auto context_typed = [&](const semantic::ExpressionId operand) {
        const auto value = specialized_hir_unit_->find_expression(operand);
        return value && value->vhdl != nullptr
            && (value->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal
                || value->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate);
    };
    const bool typed_operand = std::ranges::any_of(source.operands,
        [&](const semantic::ExpressionId operand) {
            return !context_typed(operand);
        });
    for (const auto operand : source.operands) {
        if (typed_operand && context_typed(operand)) {
            continue;
        }
        const auto subtype = hir_vhdl_expression_subtype(operand);
        if (!subtype || !subtype->type_mark.target.valid()) {
            continue;
        }
        const auto operand_root = semantic::vhdl_root_type(
            *specialized_hir_unit_, subtype->type_mark.target);
        const auto definition = operand_root.valid()
            ? specialized_hir_unit_->find_type(operand_root)
            : std::nullopt;
        if (!definition || definition->vhdl == nullptr
            || definition->vhdl->form != semantic::vhdl::TypeForm::array) {
            continue;
        }
        const auto element_root = definition->vhdl->element_subtype
                && definition->vhdl->element_subtype->type_mark.target.valid()
            ? semantic::vhdl_root_type(*specialized_hir_unit_,
                  definition->vhdl->element_subtype->type_mark.target)
            : semantic::TypeId { };
        operands.push_back({ *subtype, operand_root, element_root });
    }
    // The result's type is the one every array-typed operand either has or
    // is an element of; an array of arrays takes element operands that are
    // arrays themselves (`rows & x"00"`).
    std::optional<semantic::vhdl::SubtypeIndication> array;
    semantic::TypeId root;
    std::size_t arrays { };
    for (const auto& candidate : operands) {
        const bool fits = std::ranges::all_of(operands,
            [&](const ArrayOperand& operand) {
                return operand.root == candidate.root
                    || (candidate.element_root.valid()
                        && operand.root == candidate.element_root);
            });
        if (!fits) {
            continue;
        }
        if (array && root != candidate.root) {
            return std::nullopt;
        }
        if (!array) {
            array = candidate.subtype;
            root = candidate.root;
        }
    }
    if (!array) {
        return std::nullopt;
    }
    arrays = static_cast<std::size_t>(std::ranges::count_if(operands,
        [&](const ArrayOperand& operand) { return operand.root == root; }));
    const auto context_operands = typed_operand
        ? std::ranges::count_if(source.operands, context_typed)
        : 0;
    if (arrays == 0U
        || operands.size() + static_cast<std::size_t>(context_operands)
            < 2U) {
        return std::nullopt;
    }
    array->type_mark.target = root;
    array->type_mark.spelling.clear();
    if (const auto type = specialized_hir_unit_->find_type(root);
        type && type->vhdl != nullptr) {
        if (const auto declaration = specialized_hir_unit_->find_declaration(
                type->vhdl->declaration);
            declaration && declaration->vhdl != nullptr) {
            array->type_mark.spelling = declaration->vhdl->name;
        }
    }
    std::erase_if(array->constraints, [](const RangeConstraint& range) {
        return range.kind == RangeKind::array_index;
    });
    array->unconstrained = true;
    array->executable_width.reset();
    return array;
}

// A shape depends on what the expression's subtype does and on the unrolled
// loops around it; it is kept like the subtype (see
// hir_vhdl_expression_subtype).
std::optional<Lowerer::HirVhdlShape> Lowerer::hir_vhdl_shape(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr || hir_vhdl_shape_depth_ >= 32U) {
        return std::nullopt;
    }
    auto key = hir_vhdl_query_key(expression_id, hir_process_scope_);
    if (key) {
        if (const auto found = hir_vhdl_shape_cache_.find(*key);
            found != hir_vhdl_shape_cache_.end()) {
            return found->second;
        }
    }
    const auto fallback_calls = hir_boundary_fallback_calls_;
    auto result = compute_hir_vhdl_shape(expression_id);
    constexpr std::size_t maximum_entries { 1U << 17U };
    if (key && result && hir_boundary_fallback_calls_ == fallback_calls
        && hir_vhdl_shape_cache_.size() < maximum_entries
        && hir_vhdl_expression_subtype_cacheable()) {
        try {
            hir_vhdl_shape_cache_.emplace(std::move(*key), *result);
        } catch (const std::bad_alloc&) {
        }
    }
    return result;
}

std::optional<Lowerer::HirVhdlShape> Lowerer::compute_hir_vhdl_shape(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    ++hir_vhdl_shape_depth_;
    struct DepthGuard {
        std::size_t& depth;
        ~DepthGuard() { --depth; }
    } guard { hir_vhdl_shape_depth_ };
    const auto& source = *expression->vhdl;
    const auto scope = source.scope;
    using Kind = semantic::vhdl::ExpressionKind;

    // What the expression's subtype declares.
    std::optional<HirVhdlShape> result;
    const auto declared_subtype = hir_vhdl_expression_subtype(expression_id);
    if (declared_subtype) {
        result = hir_vhdl_subtype_shape(*declared_subtype, scope);
    }
    // What the object and the selections from it supply.
    std::optional<HirVhdlShape> facts;
    const bool concatenation
        = (source.kind == Kind::concatenation && source.operands.size() >= 2U)
        || (source.kind == Kind::binary && source.text == "&"
            && source.operands.size() == 2U);
    // A function call, with or without actuals, has its result's shape
    // (IEEE 1076-2008 10.13).
    const bool function_call
        = (source.kind == Kind::call
              && !source.text.starts_with('@')
              && source.text.find('\'') == std::string::npos
              && !source.text.starts_with(
                  semantic::vhdl::qualified_expression_prefix))
        || (source.kind == Kind::name && hir_vhdl_function_name(expression_id));
    const auto function_result = function_call
        ? hir_vhdl_function_result_shape(expression_id, hir_process_scope_)
        : std::nullopt;
    // A partially constrained record result has the element bounds of the
    // values its return statements give (IEEE 1076-2008 10.13), an
    // operator a user function overloads included (4.5.2).
    const bool operator_call
        = source.kind == Kind::binary || source.kind == Kind::unary;
    const auto record_result
        = ((function_call && !function_result) || operator_call)
            && declared_subtype
            && hir_vhdl_partially_constrained_record(
                hir_effective_vhdl_subtype(*declared_subtype)
                    .value_or(*declared_subtype))
        ? hir_vhdl_returned_result_shape(
              expression_id, hir_process_scope_, std::nullopt, true)
        : std::nullopt;
    if (function_result
        && (function_result->width != 0U || function_result->null_range)) {
        HirVhdlShape called;
        if (function_result->ranges.empty()) {
            called.ranges.push_back(function_result->range);
        } else {
            called.ranges.assign(function_result->ranges.begin(),
                function_result->ranges.end());
        }
        called.element_constraints = function_result->element_constraints;
        result = std::move(called);
    } else if (record_result && record_result->record) {
        HirVhdlShape called;
        called.element_constraints = record_result->element_constraints;
        result = std::move(called);
    } else if (source.kind == Kind::name) {
        if (const auto target = hir_vhdl_alias_target_expression(
                expression_id)) {
            // An object alias denotes the object (IEEE 1076-2008 6.6.2).
            facts = hir_vhdl_shape(*target);
        } else if (const auto declaration
            = hir_referenced_declaration(expression_id)) {
            const auto dimensions = result ? result->ranges.size() : 1U;
            facts = hir_vhdl_object_facts(*declaration, dimensions);
            const auto declared = specialized_hir_unit_->find_declaration(
                *declaration);
            // A composite generic constant of a package instance, or one
            // left to its default, has the shape of that actual (IEEE
            // 1076-2008 6.5.6.2, 4.9); so does an unconstrained subprogram
            // formal of a call whose binding is in scope (4.2.2.2).
            using Form = semantic::vhdl::DeclarationForm;
            if ((!facts || !facts->complete() || facts->ranges.empty())
                && declared
                && declared->vhdl != nullptr
                && (declared->vhdl->form == Form::generic_constant
                    || declared->vhdl->form == Form::port)
                && declared->vhdl->subtype) {
                if (const auto actual = hir_generic_actual(expression_id)) {
                    if (auto shape = hir_vhdl_contextual_shape(*actual,
                            *declared->vhdl->subtype, declared->vhdl->scope);
                        shape && shape->complete()) {
                        facts = std::move(shape);
                    }
                }
            }
            // `obj.m1.m2`: the members of a record object, selected by name
            // (IEEE 1076-2008 8.3).
            if (source.text.find('.') != std::string::npos && declared
                && declared->vhdl != nullptr && declared->vhdl->subtype) {
                std::vector<std::string_view> segments;
                for (std::string_view rest { source.text };;) {
                    const auto separator = rest.find('.');
                    segments.push_back(rest.substr(0U, separator));
                    if (separator == std::string_view::npos) {
                        break;
                    }
                    rest.remove_prefix(separator + 1U);
                }
                const auto root = std::ranges::find_if(segments,
                    [&](const std::string_view segment) {
                        return same_hir_identifier(
                            segment, declared->vhdl->name, true);
                    });
                std::optional<HirVhdlShape> member;
                if (root != segments.end() && std::next(root) != segments.end()) {
                    member = hir_vhdl_subtype_shape(
                        *declared->vhdl->subtype, scope);
                    if (member && facts) {
                        complete_shape(*member, *facts);
                    } else if (!member) {
                        // An open subtype (a contextual result's) leaves
                        // the shape to the object's facts.
                        member = facts;
                    }
                    for (auto step = std::next(root);
                        member && step != segments.end(); ++step) {
                        HirVhdlShape element;
                        for (const auto& constraint : constraints_below(
                                 member->element_constraints, *step)) {
                            if (constraint.path.empty()) {
                                for (const auto& range :
                                    constraint.constraints) {
                                    if (range.kind == RangeKind::array_index
                                        && range.left && range.right) {
                                        element.ranges.push_back(
                                            HirPackedRange { *range.left,
                                                *range.right,
                                                range.descending });
                                    }
                                }
                            } else {
                                element.element_constraints.push_back(
                                    constraint);
                            }
                        }
                        member = std::move(element);
                    }
                }
                // A selected name ending at the object itself (`pkg.obj`)
                // keeps the object's facts.
                if (root == segments.end()
                    || std::next(root) != segments.end()) {
                    facts = std::move(member);
                }
            }
        }
    } else if (const auto range_index = source.kind == Kind::index
                   && source.operands.size() == 2U
                ? specialized_hir_unit_->find_expression(source.operands[1])
                : std::nullopt;
        range_index && range_index->vhdl != nullptr
        && range_index->vhdl->kind == Kind::call
        && range_index->vhdl->operands.size() == 1U
        && (range_index->vhdl->text == "'range"
            || range_index->vhdl->text == "'reverse_range")) {
        // `a(b'range)` is the slice of `a` by that range (8.5).
        if (const auto bounds = hir_vhdl_shape(
                range_index->vhdl->operands.front());
            bounds && bounds->ranges.size() == 1U && bounds->ranges.front()) {
            auto range = *bounds->ranges.front();
            if (range_index->vhdl->text == "'reverse_range") {
                range = HirPackedRange { range.right, range.left,
                    !range.descending };
            }
            HirVhdlShape slice;
            if (const auto prefix = hir_vhdl_shape(source.operands.front())) {
                slice = *prefix;
            }
            slice.ranges.assign(1U, range);
            // The expression's declared subtype is not the element's.
            result = slice;
            facts = std::move(slice);
        }
    } else if (source.kind == Kind::index && source.operands.size() >= 2U) {
        if (const auto prefix = hir_vhdl_shape(source.operands.front())) {
            HirVhdlShape element;
            for (const auto& constraint :
                constraints_below(prefix->element_constraints, "")) {
                if (constraint.path.empty()) {
                    for (const auto& range : constraint.constraints) {
                        if (range.kind == RangeKind::array_index
                            && range.left && range.right) {
                            element.ranges.push_back(HirPackedRange {
                                *range.left, *range.right,
                                range.descending });
                        }
                    }
                } else {
                    element.element_constraints.push_back(constraint);
                }
            }
            facts = std::move(element);
        }
    } else if (source.kind == Kind::call && source.operands.size() == 1U
        && (source.text.starts_with("@vhdl-member:")
            || source.text == "'element")) {
        const auto step = source.text == "'element"
            ? std::string_view { }
            : std::string_view { source.text }.substr(
                  std::string_view { "@vhdl-member:" }.size());
        if (const auto prefix = hir_vhdl_shape(source.operands.front())) {
            HirVhdlShape element;
            for (const auto& constraint :
                constraints_below(prefix->element_constraints, step)) {
                if (constraint.path.empty()) {
                    for (const auto& range : constraint.constraints) {
                        if (range.kind == RangeKind::array_index
                            && range.left && range.right) {
                            element.ranges.push_back(HirPackedRange {
                                *range.left, *range.right,
                                range.descending });
                        }
                    }
                } else {
                    element.element_constraints.push_back(constraint);
                }
            }
            facts = std::move(element);
        }
    } else if (source.kind == Kind::slice && !source.operands.empty()) {
        if (const auto prefix = hir_vhdl_shape(source.operands.front())) {
            HirVhdlShape slice = *prefix;
            if (!slice.ranges.empty()) {
                slice.ranges.front() = hir_expression_range(
                    expression_id, hir_process_scope_);
                // A slice with static bounds has their range (8.5).
                const auto left = source.operands.size() == 3U
                    ? hir_constant_integer(source.operands[1])
                    : std::nullopt;
                const auto right = source.operands.size() == 3U
                    ? hir_constant_integer(source.operands[2])
                    : std::nullopt;
                if (!slice.ranges.front() && left && right
                    && (source.text == "to" || source.text == "downto")) {
                    slice.ranges.front() = HirPackedRange { *left, *right,
                        source.text == "downto" };
                }
            }
            facts = std::move(slice);
            // The declared subtype is the prefix's; the slice has its own
            // index range, unknown until its bounds are (8.5).
            if (result && result->ranges.size() == 1U
                && facts->ranges.size() == 1U) {
                result->ranges.front() = facts->ranges.front();
            }
        }
    } else if (source.kind == Kind::call && source.operands.size() == 1U
        && source.text.starts_with(
            semantic::vhdl::qualified_expression_prefix)) {
        facts = hir_vhdl_shape(source.operands.front());
        // An aggregate or literal operand has the type mark as its context
        // (IEEE 1076-2008 9.3.5).
        const auto operand = specialized_hir_unit_->find_expression(
            source.operands.front());
        if ((!facts || !facts->complete()) && operand
            && operand->vhdl != nullptr
            && (operand->vhdl->kind == Kind::aggregate
                || operand->vhdl->kind == Kind::string_literal)) {
            if (const auto declared
                = hir_vhdl_expression_subtype(expression_id)) {
                // A record whose element bounds are open takes them from
                // the values its elements associate (9.3.3.2).
                const auto record = hir_effective_vhdl_subtype(*declared)
                                        .value_or(*declared);
                facts = operand->vhdl->kind == Kind::aggregate
                        && hir_vhdl_partially_constrained_record(record)
                    ? hir_vhdl_record_value_shape(
                          source.operands.front(), record, scope)
                    : hir_vhdl_contextual_shape(
                          source.operands.front(), *declared, scope);
            }
        }
    } else if (concatenation && !result) {
        // The predefined "&" yields its array operands' type (9.2.5); the
        // range is fixed below from the index subtype.
        if (const auto array = hir_vhdl_concatenation_subtype(expression_id)) {
            result = hir_vhdl_subtype_shape(*array, scope);
            if (result && result->ranges.size() == 1U) {
                result->ranges.front().reset();
                for (const auto operand : source.operands) {
                    const auto operand_subtype
                        = hir_vhdl_expression_subtype(operand);
                    const auto operand_shape = operand_subtype
                            && operand_subtype->type_mark.target
                                == array->type_mark.target
                        ? hir_vhdl_shape(operand)
                        : std::nullopt;
                    if (operand_shape
                        && !operand_shape->element_constraints.empty()) {
                        result->element_constraints
                            = operand_shape->element_constraints;
                        break;
                    }
                }
            } else {
                result.reset();
            }
        }
    }
    if (!result) {
        result = std::move(facts);
    } else if (facts) {
        complete_shape(*result, *facts);
    }
    // An IEEE fixed-point operation or conversion has the index range its
    // package defines for the result (IEEE 1076-2019 G.4).
    if ((!result || (result->ranges.size() == 1U && !result->ranges.front()))
        && (source.kind == Kind::binary
            || (source.kind == Kind::call
                && ((declared_subtype
                        && hir_vhdl_fixed_subtype(
                            hir_effective_vhdl_subtype(*declared_subtype)
                                .value_or(*declared_subtype)))
                    || is_hir_vhdl_fixed_function(expression_id))))) {
        if (const auto fixed = hir_vhdl_fixed_range(expression_id)) {
            HirVhdlShape operation;
            operation.ranges.push_back(
                HirPackedRange { fixed->left, fixed->right, true });
            result = std::move(operation);
        }
    }
    // A one-dimensional value whose range the expression itself proves.
    if (result && result->ranges.size() == 1U && !result->ranges.front()) {
        result->ranges.front()
            = hir_expression_range(expression_id, hir_process_scope_);
    }
    // A literal or aggregate whose context leaves the range open starts at
    // the index subtype's 'LEFT (IEEE 1076-2008 9.3.2, 9.3.3.3), as does a
    // concatenation (9.2.5).
    if (result && result->ranges.size() == 1U && !result->ranges.front()
        && (source.kind == Kind::string_literal
            || source.kind == Kind::aggregate || concatenation)) {
        if (auto declared = concatenation
                ? hir_vhdl_concatenation_subtype(expression_id)
                : hir_vhdl_expression_subtype(expression_id)) {
            // The operands' element constraints give the element length
            // the range is counted in (9.2.5).
            if (declared->element_constraints.empty()) {
                declared->element_constraints = result->element_constraints;
            }
            const auto width = hir_expression_width(
                expression_id, hir_process_scope_, &*declared);
            if (width && *width != 0U) {
                semantic::vhdl::Declaration inferred;
                inferred.scope = scope;
                inferred.form = semantic::vhdl::DeclarationForm::constant;
                inferred.subtype = *declared;
                std::erase_if(inferred.subtype->constraints,
                    [](const RangeConstraint& range) {
                        return range.kind == RangeKind::array_index;
                    });
                inferred.subtype->unconstrained = true;
                inferred.initializer = expression_id;
                result->ranges.front() = hir_vhdl_unconstrained_constant_range(
                    inferred, *width, hir_process_scope_);
            }
        }
    }
    // FSIM_VHDL_SHAPE_TRACE prints each composed shape.
    if (vhdl_shape_trace()) {
        std::cerr << "[fsim-vhdl-shape] expression=" << expression_id.value()
                  << " kind=" << static_cast<int>(source.kind) << " text="
                  << source.text << " facts=" << facts.has_value();
        if (result) {
            std::cerr << " ranges=";
            for (const auto& range : result->ranges) {
                if (range) {
                    std::cerr << range->left << (range->descending ? "d" : "a")
                              << range->right << ',';
                } else {
                    std::cerr << "?,";
                }
            }
            std::cerr << " elements=" << result->element_constraints.size();
        } else {
            std::cerr << " none";
        }
        std::cerr << '\n';
    }
    return result;
}

std::optional<std::size_t> Lowerer::hir_vhdl_positional_aggregate_bits(
    const semantic::ExpressionId aggregate_id,
    const semantic::vhdl::SubtypeIndication& context,
    const semantic::ScopeId scope) const
{
    // A positional aggregate has one element per association, or the
    // elements of an association of the aggregate's own type (IEEE
    // 1076-2008 9.3.3.1, 9.3.3.3).
    const auto aggregate = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(aggregate_id)
        : std::nullopt;
    if (!aggregate || aggregate->vhdl == nullptr
        || aggregate->vhdl->kind != semantic::vhdl::ExpressionKind::aggregate
        || aggregate->vhdl->associations.empty()
        || !semantic::vhdl::positional_aggregate(*aggregate->vhdl)
        || !context.type_mark.target.valid()) {
        return std::nullopt;
    }
    const auto* array = semantic::vhdl_base_type_definition(
        *specialized_hir_unit_, context.type_mark.target);
    if (array == nullptr || array->form != semantic::vhdl::TypeForm::array
        || array->array_dimensions.size() != 1U || !array->element_subtype) {
        return std::nullopt;
    }
    const auto element = hir_effective_vhdl_subtype(
        semantic::vhdl::constrained_element_subtype(
            context, "", *array->element_subtype));
    const auto element_width = element ? vhdl_subtype_width(*element)
                                       : std::nullopt;
    if (!element_width || *element_width == 0U) {
        return std::nullopt;
    }
    const auto root = semantic::vhdl_root_type(
        *specialized_hir_unit_, context.type_mark.target);
    std::size_t count { };
    for (const auto& association : aggregate->vhdl->associations) {
        const auto value_subtype = vhdl_standard_ >= frontend::VhdlStandard::Vhdl2008
            ? hir_vhdl_expression_subtype(association.value)
            : std::nullopt;
        const bool segment = value_subtype
            && value_subtype->type_mark.target.valid()
            && semantic::vhdl_root_type(*specialized_hir_unit_,
                   value_subtype->type_mark.target) == root;
        if (!segment) {
            ++count;
            continue;
        }
        const auto value_shape = hir_vhdl_shape(association.value);
        if (!value_shape || value_shape->ranges.size() != 1U
            || !value_shape->ranges.front()) {
            return std::nullopt;
        }
        const auto& range = *value_shape->ranges.front();
        if (!value_shape->null()) {
            count += static_cast<std::size_t>(
                support::index_distance(range.left, range.right) + 1U);
        }
    }
    (void)scope;
    return count * *element_width;
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_initialized_object_subtype(
    const semantic::ExpressionId initializer,
    const semantic::vhdl::SubtypeIndication& declared,
    const semantic::ScopeId scope) const
{
    [[maybe_unused]] const HirEffectiveVhdlSubtypeCacheScope cache_scope {
        *this
    };
    // A record subtype that leaves element bounds open takes them from the
    // value (IEEE 1076-2019 6.4.2.2).
    if (const auto record = hir_effective_vhdl_subtype(declared).value_or(
            declared);
        hir_vhdl_partially_constrained_record(record)) {
        const auto shape = hir_vhdl_record_value_shape(
            initializer, record, scope);
        if (!shape || !shape->ranges.empty()
            || shape->element_constraints.empty()) {
            return std::nullopt;
        }
        auto subtype = declared;
        apply_hir_vhdl_shape(subtype, *shape, scope);
        const auto width = vhdl_subtype_width(subtype);
        return width && *width != 0U ? std::optional { std::move(subtype) }
                                     : std::nullopt;
    }
    auto shape = hir_vhdl_shape(initializer);
    // A literal or aggregate has the declared subtype as its context; with
    // its range open it starts at the index subtype's 'LEFT (IEEE 1076-2008
    // 9.3.2, 9.3.3.3).
    const auto value = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(initializer)
        : std::nullopt;
    if ((!shape || !shape->complete()) && value && value->vhdl != nullptr
        && (value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
            || value->vhdl->kind
                == semantic::vhdl::ExpressionKind::string_literal)) {
        const auto open = hir_vhdl_subtype_shape(declared, scope);
        const bool aggregate = value->vhdl->kind
            == semantic::vhdl::ExpressionKind::aggregate;
        // A named aggregate without OTHERS spans its choices.
        const auto named = aggregate && open && open->ranges.size() == 1U
                && !open->ranges.front()
            ? hir_vhdl_named_aggregate_bounds(*value->vhdl)
            : std::nullopt;
        auto width = named ? std::nullopt
                           : hir_expression_width(
                                 initializer, hir_process_scope_, &declared);
        if (aggregate && !named && (!width || *width == 0U)) {
            width = hir_vhdl_positional_aggregate_width(initializer, declared);
        }
        if (aggregate && !named && (!width || *width == 0U)) {
            width = hir_vhdl_positional_aggregate_bits(
                initializer, declared, scope);
        }
        if (vhdl_shape_trace()) {
            std::cerr << "[fsim-vhdl-shape] initialized expression="
                      << initializer.value() << " open="
                      << (open ? static_cast<int>(open->ranges.size()) : -1)
                      << " mark=" << declared.type_mark.spelling << "/"
                      << declared.type_mark.target.valid() << "/"
                      << static_cast<int>(hir_vhdl_subtype_chain(
                             declared, scope).builtin)
                      << " named=" << named.has_value() << " width="
                      << (width ? static_cast<long long>(*width) : -1LL)
                      << '\n';
        }
        if (named) {
            HirVhdlShape contextual;
            contextual.ranges.push_back(*named);
            shape = std::move(contextual);
        } else if (open && open->ranges.size() == 1U && !open->ranges.front()
            && width && *width != 0U) {
            semantic::vhdl::Declaration inferred;
            inferred.scope = scope;
            inferred.form = semantic::vhdl::DeclarationForm::constant;
            inferred.subtype = declared;
            inferred.initializer = initializer;
            if (const auto range = hir_vhdl_unconstrained_constant_range(
                    inferred, *width, hir_process_scope_)) {
                HirVhdlShape contextual;
                contextual.ranges.push_back(*range);
                shape = std::move(contextual);
            }
        }
    }
    if (!shape || shape->ranges.empty() || !shape->complete()) {
        return std::nullopt;
    }
    // An aggregate whose element subtype is open takes the element bounds
    // from its element values (IEEE 1076-2008 9.3.3.3).
    if (value && value->vhdl != nullptr
        && value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
        && !value->vhdl->associations.empty()
        && std::ranges::none_of(shape->element_constraints,
            [](const ElementConstraint& constraint) {
                return constraint.path == std::vector<std::string> { "" };
            })
        && declared.type_mark.target.valid()) {
        const auto* base = semantic::vhdl_base_type_definition(
            *specialized_hir_unit_, declared.type_mark.target);
        if (base != nullptr && base->form == semantic::vhdl::TypeForm::array
            && base->element_subtype) {
            const auto element = hir_vhdl_chain_element_subtype(
                declared, *base->element_subtype);
            const auto element_shape = hir_vhdl_subtype_shape(element, scope);
            if (element_shape && !element_shape->ranges.empty()
                && !element_shape->complete()) {
                const auto element_subtype
                    = hir_vhdl_initialized_object_subtype(
                        value->vhdl->associations.front().value, element,
                        scope);
                const auto inferred = element_subtype
                    ? hir_vhdl_subtype_shape(*element_subtype, scope)
                    : std::nullopt;
                if (!inferred || !inferred->complete()) {
                    return std::nullopt;
                }
                append_constraints(shape->element_constraints, { "" },
                    *inferred);
            }
        }
    }
    auto subtype = declared;
    apply_hir_vhdl_shape(subtype, *shape, scope);
    const auto completed = hir_vhdl_subtype_shape(subtype, scope);
    if (!completed || !completed->complete()
        || (!vhdl_subtype_width(subtype) && !completed->null())) {
        return std::nullopt;
    }
    return subtype;
}

std::optional<Lowerer::HirVhdlShape> Lowerer::hir_vhdl_contextual_shape(
    const semantic::ExpressionId expression_id,
    const semantic::vhdl::SubtypeIndication& context,
    const semantic::ScopeId scope) const
{
    auto shape = hir_vhdl_shape(expression_id);
    if (shape && shape->complete()) {
        return shape;
    }
    // A literal or aggregate takes its type from the context; an open
    // index range comes from its own choices or element count (IEEE
    // 1076-2008 9.3.2, 9.3.3.3).
    const auto value = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    // A qualified expression whose type mark leaves the bounds open has its
    // operand's (IEEE 1076-2008 9.3.5).
    if (value && value->vhdl != nullptr
        && value->vhdl->kind == semantic::vhdl::ExpressionKind::call
        && value->vhdl->text.starts_with("@vhdl-qualified:")
        && value->vhdl->operands.size() == 1U) {
        auto operand = hir_vhdl_contextual_shape(
            value->vhdl->operands.front(), context, scope);
        return operand && operand->complete() ? operand : shape;
    }
    if (!value || value->vhdl == nullptr
        || (value->vhdl->kind != semantic::vhdl::ExpressionKind::aggregate
            && value->vhdl->kind
                != semantic::vhdl::ExpressionKind::string_literal)) {
        return shape;
    }
    const auto subtype = hir_vhdl_initialized_object_subtype(
        expression_id, context, scope);
    auto contextual = subtype ? hir_vhdl_subtype_shape(*subtype, scope)
                              : std::nullopt;
    if (vhdl_shape_trace()) {
        std::cerr << "[fsim-vhdl-shape] contextual expression="
                  << expression_id.value() << " context="
                  << context.type_mark.spelling << " subtype="
                  << subtype.has_value() << " complete="
                  << (contextual && contextual->complete()) << '\n';
    }
    return contextual && contextual->complete() ? contextual : shape;
}

std::optional<Lowerer::HirConstantSelection> Lowerer::hir_vhdl_value_slice(
    const semantic::ExpressionId expression_id) const
{
    // The bits of a static slice within its prefix value: the prefix's range
    // from its shape, each element its share of the prefix width (8.5).
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::slice
        || expression->vhdl->operands.size() != 3U
        || (expression->vhdl->text != "to"
            && expression->vhdl->text != "downto")) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto left = hir_constant_integer(source.operands[1]);
    const auto right = hir_constant_integer(source.operands[2]);
    const auto prefix = hir_vhdl_shape(source.operands[0]);
    const auto prefix_width
        = hir_expression_width(source.operands[0], hir_process_scope_);
    if (!left || !right || !prefix || prefix->ranges.size() != 1U
        || !prefix->ranges.front() || !prefix_width || *prefix_width == 0U) {
        return std::nullopt;
    }
    const auto& range = *prefix->ranges.front();
    const auto descending = source.text == "downto";
    const auto count = range.descending
        ? range.left - range.right + 1
        : range.right - range.left + 1;
    const auto length = descending ? *left - *right + 1 : *right - *left + 1;
    const auto low = std::min(range.left, range.right);
    const auto high = std::max(range.left, range.right);
    if (count <= 0 || length <= 0 || descending != range.descending
        || *left < low || *left > high || *right < low || *right > high
        || *prefix_width % static_cast<std::size_t>(count) != 0U) {
        return std::nullopt;
    }
    const auto element = *prefix_width / static_cast<std::size_t>(count);
    // Bit 0 holds the element at the prefix's right bound.
    const auto right_position = range.descending ? *right - range.right
                                                 : range.right - *right;
    return HirConstantSelection {
        static_cast<std::size_t>(right_position) * element,
        static_cast<std::size_t>(length) * element,
    };
}

bool Lowerer::hir_vhdl_shapes_differ(const semantic::ExpressionId left_id,
    const semantic::ExpressionId right_id) const
{
    const auto left = hir_vhdl_shape(left_id);
    const auto right = hir_vhdl_shape(right_id);
    if (!left || !right || left->ranges.empty()
        || left->ranges.size() != right->ranges.size() || !left->complete()
        || !right->complete()) {
        return false;
    }
    const auto length = [](const auto& range) {
        return range.descending ? range.left - range.right + 1
                                : range.right - range.left + 1;
    };
    const auto lengths_differ = [&](const auto& a, const auto& b) {
        return std::max<std::int64_t>(length(a), 0)
            != std::max<std::int64_t>(length(b), 0);
    };
    for (std::size_t dimension { }; dimension < left->ranges.size();
        ++dimension) {
        if (lengths_differ(*left->ranges[dimension],
                *right->ranges[dimension])) {
            return true;
        }
    }
    // Every element of an array has the same shape; compare the levels both
    // sides constrain.
    for (const auto& constraint : left->element_constraints) {
        const auto other = std::ranges::find(right->element_constraints,
            constraint.path, &ElementConstraint::path);
        if (other == right->element_constraints.end()
            || other->constraints.size() != constraint.constraints.size()) {
            continue;
        }
        for (std::size_t index { }; index < constraint.constraints.size();
            ++index) {
            const auto& a = constraint.constraints[index];
            const auto& b = other->constraints[index];
            if (a.kind == RangeKind::array_index
                && b.kind == RangeKind::array_index && a.left && a.right
                && b.left && b.right
                && lengths_differ(
                    HirPackedRange { *a.left, *a.right, a.descending },
                    HirPackedRange { *b.left, *b.right, b.descending })) {
                return true;
            }
        }
    }
    return false;
}

bool Lowerer::hir_vhdl_context_sized(
    const semantic::vhdl::SubtypeIndication& result) const
{
    // An unconstrained array result, or a record result an element of which
    // leaves its bounds open, takes its subtype from the call's context
    // (IEEE 1076-2019 4.2.1, 14.4.2.5).
    return result.unconstrained
        || hir_vhdl_partially_constrained_record(result);
}

bool Lowerer::hir_vhdl_partially_constrained_record(
    const semantic::vhdl::SubtypeIndication& subtype) const
{
    if (specialized_hir_unit_ == nullptr || subtype.executable_width
        || !subtype.type_mark.target.valid()) {
        return false;
    }
    const auto* definition = semantic::vhdl_base_type_definition(
        *specialized_hir_unit_, subtype.type_mark.target);
    return definition != nullptr
        && definition->form == semantic::vhdl::TypeForm::record;
}

std::optional<Lowerer::HirVhdlShape> Lowerer::hir_vhdl_record_value_shape(
    const semantic::ExpressionId value,
    const semantic::vhdl::SubtypeIndication& record,
    const semantic::ScopeId scope,
    const bool targets) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(value)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    // `T'(aggregate)` with T leaving element bounds open is shaped as its
    // aggregate (IEEE 1076-2008 9.3.5).
    if (expression->vhdl->kind == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->operands.size() == 1U
        && expression->vhdl->text.starts_with(
            semantic::vhdl::qualified_expression_prefix)) {
        const auto operand = specialized_hir_unit_->find_expression(
            expression->vhdl->operands.front());
        // The type mark alone says whether bounds stay open; the
        // expression's own subtype may carry a placeholder width.
        auto qualifier = hir_vhdl_expression_subtype(value);
        if (qualifier) {
            semantic::vhdl::SubtypeIndication mark;
            mark.type_mark = qualifier->type_mark;
            qualifier = hir_effective_vhdl_subtype(mark).value_or(mark);
        }
        if (operand && operand->vhdl != nullptr
            && operand->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
            && qualifier
            && hir_vhdl_partially_constrained_record(*qualifier)) {
            return hir_vhdl_record_value_shape(
                expression->vhdl->operands.front(), record, scope, targets);
        }
    }
    const auto& aggregate = *expression->vhdl;
    if (aggregate.kind != semantic::vhdl::ExpressionKind::aggregate) {
        return hir_vhdl_shape(value);
    }
    const auto effective = hir_effective_vhdl_subtype(record).value_or(
        record);
    const auto* definition = effective.type_mark.target.valid()
        ? semantic::vhdl_base_type_definition(
              *specialized_hir_unit_, effective.type_mark.target)
        : nullptr;
    if (definition == nullptr
        || definition->form != semantic::vhdl::TypeForm::record) {
        return std::nullopt;
    }
    HirVhdlShape result;
    std::size_t position { };
    for (const auto& association : aggregate.associations) {
        std::size_t index { position++ };
        if (!association.positional()) {
            const auto choice = association.choices.size() == 1U
                ? specialized_hir_unit_->find_expression(
                      association.choices.front())
                : std::nullopt;
            if (!choice || choice->vhdl == nullptr
                || choice->vhdl->kind
                    != semantic::vhdl::ExpressionKind::name) {
                return std::nullopt;
            }
            const auto found = std::ranges::find_if(
                definition->record_elements,
                [&](const semantic::vhdl::RecordElement& element) {
                    return equal_ignoring_ascii_case(
                        element.name, choice->vhdl->text);
                });
            if (found == definition->record_elements.end()) {
                return std::nullopt;
            }
            index = static_cast<std::size_t>(std::distance(
                definition->record_elements.begin(), found));
        }
        if (index >= definition->record_elements.size()) {
            return std::nullopt;
        }
        const auto& element = definition->record_elements[index];
        const auto element_subtype
            = semantic::vhdl::constrained_element_subtype(
                effective, element.name, element.subtype);
        const auto element_effective = hir_effective_vhdl_subtype(
            element_subtype).value_or(element_subtype);
        // A level the type declares keeps its constraint.
        if (!hir_vhdl_context_sized(element_effective)) {
            continue;
        }
        // A target element is an object with its own shape; a value
        // element takes its open bounds from itself (9.3.3.2).
        const auto child = hir_vhdl_partially_constrained_record(
                               element_effective)
            ? hir_vhdl_record_value_shape(
                  association.value, element_subtype, scope, targets)
            : targets ? hir_vhdl_shape(association.value)
                      : hir_vhdl_contextual_shape(
                            association.value, element_subtype, scope);
        if (!child) {
            return std::nullopt;
        }
        append_constraints(result.element_constraints, { element.name },
            *child);
    }
    return result;
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_aggregate_target_subtype(const semantic::ExpressionId target,
    const semantic::vhdl::SubtypeIndication& type,
    const semantic::ScopeId scope) const
{
    // The shape of a target: an object's own, or for a record aggregate the
    // shapes of the targets its elements associate (9.3.3.1, 9.3.3.2).
    const auto shape = hir_vhdl_record_value_shape(target, type, scope, true);
    if (!shape) {
        return std::nullopt;
    }
    auto subtype = type;
    apply_hir_vhdl_shape(subtype, *shape, scope);
    const auto width = vhdl_subtype_width(subtype);
    return width && *width != 0U ? std::optional { std::move(subtype) }
                                 : std::nullopt;
}

} // namespace fsim::elaboration
