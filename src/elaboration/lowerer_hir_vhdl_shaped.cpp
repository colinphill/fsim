// SPDX-License-Identifier: Apache-2.0
// VHDL array objects whose index ranges are known only when they are
// elaborated at run time (IEEE 1076-2008 5.3.2.2, 14.4.2.5), such as a
// subprogram's `variable r : int3d(1 to x, 1 to y, 1 to z)`. Such an object
// lives in a container register holding its shape and one packed payload;
// see runtime::simir::VhdlShaped.
#include "lowerer_internal.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <utility>

namespace fsim::elaboration {
namespace {

// Names why a declaration could not take a run-time shape, under
// FSIM_HIR_LOWER_TRACE_FAILURES.
bool shaped_rejected(const std::string_view reason)
{
    if (elaboration_detail::trace_lowering_failures()) {
        std::cerr << "[fsim-hir-lower] vhdl-shaped-rejected " << reason
                  << '\n';
    }
    return false;
}

// The code of a character literal (`'X'`, IEEE 1076-2008 15.6), which a
// CHARACTER element takes even where the literal could be a logic value.
std::optional<std::uint8_t> character_literal_code(
    const semantic::vhdl::Expression& expression)
{
    const auto& text = expression.text;
    if ((expression.kind != semantic::vhdl::ExpressionKind::logic_literal
            && expression.kind != semantic::vhdl::ExpressionKind::name)
        || text.size() != 3U || text.front() != '\'' || text.back() != '\'') {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(text[1]);
}

} // namespace

semantic::vhdl::SubtypeIndication Lowerer::hir_vhdl_chain_element_subtype(
    const semantic::vhdl::SubtypeIndication& owner,
    semantic::vhdl::SubtypeIndication element,
    const std::string_view step) const
{
    std::vector<const semantic::vhdl::SubtypeIndication*> owners;
    std::vector<semantic::vhdl::SubtypeIndication> bases;
    bases.reserve(32U);
    const semantic::vhdl::SubtypeIndication* current = &owner;
    std::unordered_set<std::uint32_t> visited;
    for (std::size_t depth { }; depth < 32U && current != nullptr; ++depth) {
        if (!current->element_constraints.empty()) {
            owners.push_back(current);
        }
        if (specialized_hir_unit_ == nullptr
            || !current->type_mark.target.valid()
            || !visited.insert(current->type_mark.target.value()).second) {
            break;
        }
        const auto type = specialized_hir_unit_->find_type(
            current->type_mark.target);
        if (!type || type->vhdl == nullptr
            || (type->vhdl->form != semantic::vhdl::TypeForm::subtype
                && type->vhdl->form != semantic::vhdl::TypeForm::alias)) {
            break;
        }
        bases.push_back(type->vhdl->base);
        current = &bases.back();
    }
    for (auto next = owners.rbegin(); next != owners.rend(); ++next) {
        element = semantic::vhdl::constrained_element_subtype(
            **next, step, std::move(element));
    }
    return element;
}

std::optional<std::size_t> Lowerer::hir_vhdl_symbolic_slice_length(
    const semantic::ExpressionId slice_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto slice = specialized_hir_unit_->find_expression(slice_id);
    if (!slice || slice->vhdl == nullptr
        || slice->vhdl->kind != semantic::vhdl::ExpressionKind::slice
        || slice->vhdl->operands.size() != 3U) {
        return std::nullopt;
    }
    // Two expressions are the same when their trees match.
    const auto same = [&](const auto& self, const semantic::ExpressionId left,
                          const semantic::ExpressionId right,
                          const std::size_t depth) -> bool {
        if (left == right) {
            return true;
        }
        const auto a = specialized_hir_unit_->find_expression(left);
        const auto b = specialized_hir_unit_->find_expression(right);
        if (depth > 16U || !a || !b || a->vhdl == nullptr || b->vhdl == nullptr
            || a->vhdl->kind != b->vhdl->kind || a->vhdl->text != b->vhdl->text
            || a->vhdl->operands.size() != b->vhdl->operands.size()
            || hir_referenced_declaration(left)
                != hir_referenced_declaration(right)) {
            return false;
        }
        for (std::size_t index { }; index < a->vhdl->operands.size(); ++index) {
            if (!self(self, a->vhdl->operands[index],
                    b->vhdl->operands[index], depth + 1U)) {
                return false;
            }
        }
        return true;
    };
    // `base + c`, `base - c`, `c + base` or a constant `c`.
    struct Linear {
        std::optional<semantic::ExpressionId> base;
        std::int64_t constant { };
    };
    const auto linear = [&](const semantic::ExpressionId id) -> Linear {
        if (const auto constant = hir_constant_integer(id)) {
            return { std::nullopt, *constant };
        }
        const auto node = specialized_hir_unit_->find_expression(id);
        if (node && node->vhdl != nullptr
            && node->vhdl->kind == semantic::vhdl::ExpressionKind::binary
            && node->vhdl->operands.size() == 2U
            && (node->vhdl->text == "+" || node->vhdl->text == "-")) {
            const auto right = hir_constant_integer(node->vhdl->operands[1]);
            if (right) {
                return { node->vhdl->operands[0],
                    node->vhdl->text == "+" ? *right : -*right };
            }
            const auto left = hir_constant_integer(node->vhdl->operands[0]);
            if (left && node->vhdl->text == "+") {
                return { node->vhdl->operands[1], *left };
            }
        }
        return { id, 0 };
    };
    const auto first = linear(slice->vhdl->operands[1]);
    const auto last = linear(slice->vhdl->operands[2]);
    if (first.base.has_value() != last.base.has_value()
        || (first.base && !same(same, *first.base, *last.base, 0U))) {
        return std::nullopt;
    }
    const auto distance = slice->vhdl->text == "downto"
        ? first.constant - last.constant
        : last.constant - first.constant;
    return distance >= 0
        ? std::optional { static_cast<std::size_t>(distance) + 1U }
        : std::optional<std::size_t> { 0U };
}

std::optional<std::size_t> Lowerer::hir_vhdl_vector_element_width(
    const semantic::vhdl::SubtypeIndication& subtype) const
{
    using Builtin = semantic::vhdl::BuiltinTypeIdentity;
    auto current = subtype;
    std::unordered_set<std::uint32_t> visited;
    for (std::size_t depth { }; depth < 32U; ++depth) {
        switch (current.builtin_type) {
        case Builtin::std_standard_bit_vector:
        case Builtin::std_standard_boolean_vector:
        case Builtin::ieee_std_logic_1164_std_logic_vector:
        case Builtin::ieee_std_logic_1164_std_ulogic_vector:
            return 1U;
        case Builtin::std_standard_integer_vector:
            return static_cast<std::size_t>(
                frontend::vhdl_predefined_integer_storage_width(
                    vhdl_standard_));
        case Builtin::std_standard_string:
            return 8U;
        default:
            break;
        }
        if (specialized_hir_unit_ == nullptr
            || !current.type_mark.target.valid()
            || !visited.insert(current.type_mark.target.value()).second) {
            return std::nullopt;
        }
        const auto type = specialized_hir_unit_->find_type(
            current.type_mark.target);
        if (!type || type->vhdl == nullptr) {
            return std::nullopt;
        }
        if (type->vhdl->form == semantic::vhdl::TypeForm::array) {
            if (type->vhdl->array_dimensions.size() != 1U
                || !type->vhdl->element_subtype) {
                return std::nullopt;
            }
            const auto element = hir_effective_vhdl_subtype(
                hir_vhdl_chain_element_subtype(
                    subtype, *type->vhdl->element_subtype));
            return element && element->executable_width
                    && *element->executable_width != 0U
                ? std::optional { static_cast<std::size_t>(
                      *element->executable_width) }
                : std::nullopt;
        }
        if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
            && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
            return std::nullopt;
        }
        current = type->vhdl->base;
    }
    return std::nullopt;
}

std::optional<Lowerer::HirVhdlShapedProfile>
Lowerer::hir_vhdl_shaped_profile(
    const semantic::vhdl::SubtypeIndication& subtype,
    const bool open_array, const bool string_elements) const
{
    if (specialized_hir_unit_ == nullptr) {
        return (shaped_rejected("profile check 1"), std::nullopt);
    }
    HirVhdlShapedProfile profile;
    profile.subtype = subtype;
    // The first index constraints met along the subtype chain bound the
    // object; the array type supplies the element subtype.
    const semantic::vhdl::TypeDefinition* array { };
    auto builtin = semantic::vhdl::BuiltinTypeIdentity::none;
    auto current = subtype;
    std::unordered_set<std::uint32_t> visited;
    for (std::size_t depth { }; depth < 32U; ++depth) {
        if (profile.constraints.empty()) {
            for (const auto& constraint : current.constraints) {
                if (constraint.kind == semantic::vhdl::RangeKind::array_index) {
                    profile.constraints.push_back(constraint);
                }
            }
        }
        if (current.builtin_type != semantic::vhdl::BuiltinTypeIdentity::none) {
            builtin = current.builtin_type;
            break;
        }
        if (!current.type_mark.target.valid()
            || !visited.insert(current.type_mark.target.value()).second) {
            break;
        }
        const auto type = specialized_hir_unit_->find_type(
            current.type_mark.target);
        if (!type || type->vhdl == nullptr) {
            break;
        }
        if (type->vhdl->form == semantic::vhdl::TypeForm::array) {
            array = type->vhdl;
            // A constrained array type definition bounds its objects
            // itself (`type t is array (1 to q'length) of bit`, 5.3.2.1).
            if (profile.constraints.empty() && !open_array
                && !array->array_dimensions.empty()
                && std::ranges::all_of(array->array_dimensions,
                    [](const auto& dimension) {
                        return !dimension.unconstrained
                            && dimension.constraint.has_value();
                    })) {
                for (const auto& dimension : array->array_dimensions) {
                    auto constraint = *dimension.constraint;
                    constraint.kind = semantic::vhdl::RangeKind::array_index;
                    profile.constraints.push_back(std::move(constraint));
                }
            }
            break;
        }
        if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
            && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
            break;
        }
        current = type->vhdl->base;
    }
    using Builtin = semantic::vhdl::BuiltinTypeIdentity;
    const bool builtin_array = builtin == Builtin::std_standard_bit_vector
        || builtin == Builtin::std_standard_boolean_vector
        || builtin == Builtin::std_standard_integer_vector
        || builtin == Builtin::ieee_std_logic_1164_std_logic_vector
        || builtin == Builtin::ieee_std_logic_1164_std_ulogic_vector
        || (string_elements && builtin == Builtin::std_standard_string);
    // An unconstrained array type: each object supplies its own bounds,
    // one range per index (5.3.2.2).
    if (open_array) {
        if (!profile.constraints.empty()
            || (array != nullptr
                && std::ranges::any_of(array->array_dimensions,
                    [](const auto& dimension) {
                        return !dimension.unconstrained;
                    }))) {
            return (shaped_rejected("open profile constraints"), std::nullopt);
        }
        const auto rank = array != nullptr
            ? array->array_dimensions.size() : std::size_t { 1U };
        profile.constraints.resize(rank);
        for (auto& constraint : profile.constraints) {
            constraint.kind = semantic::vhdl::RangeKind::array_index;
        }
    }
    if ((array == nullptr && !builtin_array) || profile.constraints.empty()
        || (array != nullptr
            && (!array->element_subtype
                || profile.constraints.size()
                    != array->array_dimensions.size()))
        || (array == nullptr && profile.constraints.size() != 1U)) {
        return (shaped_rejected("profile check 2: " + subtype.type_mark.spelling
                    + " array=" + std::to_string(array != nullptr)
                    + " builtin=" + std::to_string(builtin_array)
                    + " constraints=" + std::to_string(profile.constraints.size())
                    + " raw=" + std::to_string(subtype.constraints.size())),
            std::nullopt);
    }
    // Only a range that does not fold needs a run-time shape.
    if (!open_array && std::ranges::all_of(profile.constraints,
            [&](const semantic::vhdl::RangeConstraint& constraint) {
                return (constraint.left
                           || (constraint.left_expression
                               && hir_constant_integer(
                                   *constraint.left_expression)))
                    && (constraint.right
                        || (constraint.right_expression
                            && hir_constant_integer(
                                *constraint.right_expression)));
            })) {
        return (shaped_rejected("profile check 3"), std::nullopt);
    }
    if (array == nullptr) {
        // The predefined one-dimensional arrays (IEEE 1076-2008 5.3.2.4,
        // 5.3.2.3, 16.7): BIT, BOOLEAN, INTEGER, CHARACTER and STD_ULOGIC
        // elements.
        const bool integer = builtin == Builtin::std_standard_integer_vector;
        const bool logic = builtin == Builtin::ieee_std_logic_1164_std_logic_vector
            || builtin == Builtin::ieee_std_logic_1164_std_ulogic_vector;
        if (builtin == Builtin::std_standard_string) {
            profile.element_width = 8U;
            profile.element_domain = frontend::ValueDomain::String;
            // CHARACTER'LEFT is NUL (16.3).
            profile.element_default = PackedLogic4(8U, Logic4::zero);
            profile.type.element_nominal_type
                = "@builtin:std.standard.character";
        } else {
            profile.element_width = integer
                ? static_cast<std::size_t>(
                      frontend::vhdl_predefined_integer_storage_width(
                          vhdl_standard_))
                : 1U;
            profile.element_domain = integer ? frontend::ValueDomain::Integer
                : logic ? frontend::ValueDomain::Logic9
                : builtin == Builtin::std_standard_boolean_vector
                ? frontend::ValueDomain::Boolean
                : frontend::ValueDomain::Bit2;
            profile.element_signed = integer;
            // INTEGER'LEFT: the most negative value of its storage width.
            auto integer_left
                = PackedLogic4(profile.element_width, Logic4::zero);
            if (integer) {
                integer_left.set(profile.element_width - 1U, Logic4::one);
            }
            profile.element_default = integer
                ? integer_left
                : logic ? PackedLogic4::from_logic9_msb_string("U")
                : PackedLogic4(1U, Logic4::zero);
        }
        // The predefined element type, named for images (16.2.2).
        profile.element_subtype.type_mark.spelling
            = builtin == Builtin::std_standard_string ? "character"
            : integer ? "integer"
            : logic ? "std_ulogic"
            : builtin == Builtin::std_standard_boolean_vector ? "boolean"
            : "bit";
        profile.element_subtype.executable_width = profile.element_width;
        profile.element_subtype.domain = integer
            ? semantic::vhdl::ValueDomain::integer
            : logic ? semantic::vhdl::ValueDomain::logic9
            : builtin == Builtin::std_standard_boolean_vector
            ? semantic::vhdl::ValueDomain::boolean
            : semantic::vhdl::ValueDomain::bit2;
        profile.element_subtype.signed_value = integer;
    } else {
        const auto declared_element = hir_vhdl_chain_element_subtype(
            subtype, *array->element_subtype);
        // An element of an unconstrained array type takes its index ranges
        // from each object as well (5.3.2.2): an object's shape is its own
        // ranges then its element's, and `v(i)(j)` names a leaf (8.4).
        if (open_array) {
            if (auto nested = hir_vhdl_shaped_profile(declared_element, true)) {
                nested->subtype = subtype;
                nested->outer_rank = profile.constraints.size();
                nested->constraints.insert(nested->constraints.begin(),
                    profile.constraints.begin(), profile.constraints.end());
                nested->type.vhdl_rank = static_cast<std::uint32_t>(
                    nested->constraints.size());
                return nested;
            }
        }
        const auto element = hir_effective_vhdl_subtype(declared_element);
        if (!element || !element->executable_width
            || *element->executable_width == 0U
            || *element->executable_width
                > std::numeric_limits<std::uint32_t>::max()
            || element->domain == semantic::vhdl::ValueDomain::string
            || element->domain == semantic::vhdl::ValueDomain::unknown) {
            return (shaped_rejected("profile check 4"), std::nullopt);
        }
        profile.element_subtype = *element;
        profile.element_width
            = static_cast<std::size_t>(*element->executable_width);
        profile.element_domain = vhdl_value_domain(element->domain);
        profile.element_signed = element->signed_value;
        // Each element starts at its subtype's default (6.4.2.3).
        auto initial = hir_vhdl_default_value(
            *element, profile.element_width, 0U, true);
        // An array of STD_ULOGIC or BIT elements defaults every element to
        // its leftmost value, 'U' or '0' (6.4.2.3).
        if (!initial && profile.element_domain == frontend::ValueDomain::Logic9) {
            initial = PackedLogic4::from_logic9_msb_string(
                std::string(profile.element_width, 'U'));
        } else if (!initial
            && (profile.element_domain == frontend::ValueDomain::Bit2
                || profile.element_domain == frontend::ValueDomain::Boolean)
            && element->type_mark.target.valid() == false) {
            initial = PackedLogic4(profile.element_width, Logic4::zero);
        }
        if (!initial || initial->width() != profile.element_width) {
            return (shaped_rejected("profile check 5"), std::nullopt);
        }
        profile.element_default = std::move(*initial);
        // An array element with static index ranges can be indexed in turn
        // (`v(i)(j)`, 8.4).
        std::uint64_t leaves { 1U };
        for (const auto& constraint : element->constraints) {
            if (constraint.kind != semantic::vhdl::RangeKind::array_index) {
                continue;
            }
            const auto left = constraint.left ? constraint.left
                : constraint.left_expression
                ? hir_constant_integer(*constraint.left_expression)
                : std::nullopt;
            const auto right = constraint.right ? constraint.right
                : constraint.right_expression
                ? hir_constant_integer(*constraint.right_expression)
                : std::nullopt;
            if (!left || !right) {
                profile.inner.clear();
                break;
            }
            VhdlShapeRange range { *left, *right, constraint.descending };
            const auto length = range.descending
                ? (*left >= *right ? *left - *right + 1 : 0)
                : (*right >= *left ? *right - *left + 1 : 0);
            leaves *= static_cast<std::uint64_t>(length);
            profile.inner.push_back(range);
        }
        if (!profile.inner.empty() && leaves != 0U
            && profile.element_width % leaves == 0U) {
            profile.leaf_width = static_cast<std::size_t>(
                profile.element_width / leaves);
            profile.leaf_domain = profile.element_domain;
            profile.leaf_signed = false;
        } else {
            profile.inner.clear();
        }
    }
    profile.type.element_kind = ContainerElementKind::Packed;
    profile.type.element_width = static_cast<std::uint32_t>(profile.element_width);
    profile.type.two_state
        = profile.element_domain != frontend::ValueDomain::Logic4
        && profile.element_domain != frontend::ValueDomain::Logic9;
    profile.type.signed_elements = profile.element_signed;
    profile.type.vhdl_shaped = true;
    profile.type.vhdl_rank = static_cast<std::uint32_t>(
        profile.constraints.size());
    return profile;
}

bool Lowerer::lower_hir_vhdl_shaped_declaration(
    const semantic::DeclarationId declaration_id)
{
    const auto declaration = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_declaration(declaration_id)
        : std::nullopt;
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->subtype
        || (declaration->vhdl->form != semantic::vhdl::DeclarationForm::variable
            && declaration->vhdl->form
                != semantic::vhdl::DeclarationForm::constant)) {
        return false;
    }
    auto profile = hir_vhdl_shaped_profile(*declaration->vhdl->subtype);
    if (!profile) {
        return shaped_rejected("profile " + declaration->vhdl->name);
    }
    // The bounds are evaluated when the declaration is elaborated (IEEE
    // 1076-2008 14.4.2.5).
    std::vector<RegisterId> bounds;
    std::vector<std::uint8_t> directions;
    const auto bound = [&](const std::optional<std::int64_t> constant,
                           const std::optional<semantic::ExpressionId> source)
        -> std::optional<RegisterId> {
        if (source) {
            const auto width = hir_expression_width(
                *source, hir_process_scope_).value_or(32U);
            return lower_hir_expression(*source, width);
        }
        if (constant) {
            return load_integer(*constant, 64U, frontend::ValueDomain::Integer);
        }
        return std::nullopt;
    };
    for (const auto& constraint : profile->constraints) {
        const auto left = bound(constraint.left, constraint.left_expression);
        const auto right = bound(constraint.right, constraint.right_expression);
        if (!left || !right) {
            return shaped_rejected("bounds " + declaration->vhdl->name);
        }
        bounds.push_back(*left);
        bounds.push_back(*right);
        // `X'range` takes X's direction (IEEE 1076-2008 5.3.2.2).
        const bool descending = constraint.direction_from_bounds
            ? hir_vhdl_range_direction(constraint, hir_process_scope_)
                  .value_or(constraint.descending)
            : constraint.descending;
        directions.push_back(descending ? 1U : 0U);
    }
    const auto fill = load_constant(profile->element_default,
        profile->element_domain);
    HirVhdlShapedLocal local;
    local.container = allocate_container_register(profile->type);
    local.profile = std::move(*profile);
    process_.operations.emplace_back(VhdlShaped {
        VhdlShapedOperation::create,
        local.container,
        { },
        fill,
        std::move(bounds),
        std::move(directions),
        local.profile.type,
        0U,
        VhdlShapedAttribute::length,
        0U,
        { },
    });
    const auto container = local.container;
    hir_vhdl_shaped_locals_.insert_or_assign(
        declaration_id.value(), std::move(local));
    if (declaration->vhdl->initializer) {
        const auto assigned = lower_hir_vhdl_shaped_whole_assignment(
            container,
            hir_vhdl_shaped_locals_.at(declaration_id.value()).profile,
            *declaration->vhdl->initializer);
        if (!assigned) {
            hir_vhdl_shaped_locals_.erase(declaration_id.value());
            return shaped_rejected("initializer " + declaration->vhdl->name);
        }
    }
    return true;
}

const Lowerer::HirVhdlShapedLocal* Lowerer::hir_vhdl_shaped_name(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_shaped_active()) {
        return nullptr;
    }
    if (const auto* view = hir_vhdl_shaped_heap_view(expression_id, false)) {
        return view;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::name) {
        return nullptr;
    }
    const auto declaration = hir_referenced_declaration(expression_id);
    if (!declaration) {
        return nullptr;
    }
    const auto found = hir_vhdl_shaped_locals_.find(declaration->value());
    return found == hir_vhdl_shaped_locals_.end() ? nullptr : &found->second;
}

const Lowerer::HirVhdlShapedLocal* Lowerer::hir_vhdl_shaped_element(
    const semantic::ExpressionId expression_id) const
{
    return hir_vhdl_shaped_element_selectors(expression_id).first;
}

std::pair<const Lowerer::HirVhdlShapedLocal*,
    std::span<const semantic::ExpressionId>>
Lowerer::hir_vhdl_shaped_element_selectors(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_shaped_active()) {
        return { };
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return { };
    }
    const auto& source = *expression->vhdl;
    const HirVhdlShapedLocal* local { };
    std::span<const semantic::ExpressionId> selectors;
    if (source.kind == semantic::vhdl::ExpressionKind::index
        && source.operands.size() >= 2U) {
        local = hir_vhdl_shaped_prefix(source.operands.front());
        selectors = std::span { source.operands }.subspan(1U);
    } else if (source.kind == semantic::vhdl::ExpressionKind::call
        && !source.operands.empty()
        && std::ranges::all_of(source.argument_names,
            [](const std::string& name) { return name.empty(); })) {
        // `r(i, j)` parsed as a call: the name is the prefix and the
        // operands are its index expressions.
        if (const auto declaration = hir_referenced_declaration(expression_id)) {
            const auto found = hir_vhdl_shaped_locals_.find(
                declaration->value());
            if (found != hir_vhdl_shaped_locals_.end()) {
                local = &found->second;
                selectors = std::span { source.operands };
            }
        }
        // `p.e(i)`: an array element of a designated record.
        if (local == nullptr && source.text.find('.') != std::string::npos) {
            if (const auto path = hir_vhdl_shaped_record_name_path(
                    expression_id, source.text)) {
                local = hir_vhdl_shaped_member_view(expression_id, *path);
                selectors = std::span { source.operands };
            }
        }
    }
    if (local == nullptr
        || selectors.size() != local->profile.constraints.size()) {
        return { };
    }
    return { local, selectors };
}

std::optional<Lowerer::HirVhdlShapedAttributeUse>
Lowerer::hir_vhdl_shaped_attribute(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::call
        || expression->vhdl->operands.empty()
        || expression->vhdl->operands.size() > 2U) {
        return std::nullopt;
    }
    const auto& text = expression->vhdl->text;
    std::optional<VhdlShapedAttribute> attribute;
    if (text == "'left") {
        attribute = VhdlShapedAttribute::left;
    } else if (text == "'right") {
        attribute = VhdlShapedAttribute::right;
    } else if (text == "'low") {
        attribute = VhdlShapedAttribute::low;
    } else if (text == "'high") {
        attribute = VhdlShapedAttribute::high;
    } else if (text == "'length") {
        attribute = VhdlShapedAttribute::length;
    } else if (text == "'ascending") {
        attribute = VhdlShapedAttribute::ascending;
    }
    if (!attribute) {
        return std::nullopt;
    }
    const auto* local = hir_vhdl_shaped_prefix(expression->vhdl->operands.front());
    // A row of an array of open arrays has the ranges after the array's
    // own (5.3.2.2).
    std::size_t first { };
    if (local == nullptr) {
        if (const auto row = hir_vhdl_shaped_row(
                expression->vhdl->operands.front())) {
            local = row->local;
            first = local->profile.outer_rank;
        }
    }
    if (local == nullptr) {
        return std::nullopt;
    }
    std::uint32_t dimension { };
    if (expression->vhdl->operands.size() == 2U) {
        const auto selected = hir_constant_integer(expression->vhdl->operands[1]);
        if (!selected || *selected < 1
            || static_cast<std::size_t>(*selected)
                > local->profile.constraints.size() - first) {
            return std::nullopt;
        }
        dimension = static_cast<std::uint32_t>(*selected - 1);
    }
    return HirVhdlShapedAttributeUse { local, *attribute,
        static_cast<std::uint32_t>(dimension + first) };
}

std::optional<Lowerer::HirVhdlShapedAccess> Lowerer::hir_vhdl_shaped_access(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    if (const auto [local, selectors]
        = hir_vhdl_shaped_element_selectors(expression_id);
        local != nullptr) {
        return HirVhdlShapedAccess { local,
            { selectors.begin(), selectors.end() }, false };
    }
    // `v(i)(j)`: an index of an array element (8.4).
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::index
        || expression->vhdl->operands.size() < 2U) {
        return std::nullopt;
    }
    // A leaf of an array of unconstrained arrays: both indexes select in
    // the one flattened shape.
    if (const auto row = specialized_hir_unit_->find_expression(
            expression->vhdl->operands.front());
        row && row->vhdl != nullptr
        && row->vhdl->kind == semantic::vhdl::ExpressionKind::index
        && row->vhdl->operands.size() >= 2U) {
        const auto* local = hir_vhdl_shaped_prefix(row->vhdl->operands.front());
        if (local != nullptr && local->profile.outer_rank != 0U
            && local->profile.outer_rank == row->vhdl->operands.size() - 1U
            && local->profile.constraints.size()
                == local->profile.outer_rank
                    + expression->vhdl->operands.size() - 1U) {
            HirVhdlShapedAccess access { local, { }, false };
            access.selectors.assign(row->vhdl->operands.begin() + 1,
                row->vhdl->operands.end());
            access.selectors.insert(access.selectors.end(),
                expression->vhdl->operands.begin() + 1,
                expression->vhdl->operands.end());
            return access;
        }
    }
    auto element = hir_vhdl_shaped_access(expression->vhdl->operands.front());
    if (!element || element->leaf
        || element->local->profile.inner.size()
            != expression->vhdl->operands.size() - 1U) {
        return std::nullopt;
    }
    element->selectors.insert(element->selectors.end(),
        expression->vhdl->operands.begin() + 1,
        expression->vhdl->operands.end());
    element->leaf = true;
    return element;
}

std::optional<VhdlShaped> Lowerer::lower_hir_vhdl_shaped_access(
    const HirVhdlShapedAccess& access, const VhdlShapedOperation operation)
{
    VhdlShaped result;
    result.operation = operation;
    result.target = access.local->container;
    result.source = access.local->container;
    for (const auto selector : access.selectors) {
        const auto lowered = lower_hir_expression(selector,
            hir_expression_width(selector, hir_process_scope_).value_or(32U));
        if (!lowered) {
            return std::nullopt;
        }
        result.operands.push_back(*lowered);
    }
    if (access.leaf) {
        result.inner = access.local->profile.inner;
        result.width = static_cast<std::uint32_t>(
            access.local->profile.leaf_width);
    }
    return result;
}

std::optional<Lowerer::HirVhdlShapedAccess> Lowerer::hir_vhdl_shaped_row(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->operands.empty()) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const HirVhdlShapedLocal* local { };
    std::span<const semantic::ExpressionId> selectors;
    if (source.kind == semantic::vhdl::ExpressionKind::index
        && source.operands.size() >= 2U) {
        local = hir_vhdl_shaped_prefix(source.operands.front());
        selectors = std::span { source.operands }.subspan(1U);
    } else if (source.kind == semantic::vhdl::ExpressionKind::call
        && source.text.find('.') != std::string::npos
        && std::ranges::all_of(source.argument_names,
            [](const std::string& name) { return name.empty(); })) {
        // `p.e(i)` parsed as a call.
        if (const auto path = hir_vhdl_shaped_record_name_path(
                expression_id, source.text)) {
            local = hir_vhdl_shaped_member_view(expression_id, *path);
            selectors = std::span { source.operands };
        }
    }
    if (local == nullptr || local->profile.outer_rank == 0U
        || selectors.size() != local->profile.outer_rank
        || selectors.size() >= local->profile.constraints.size()) {
        return std::nullopt;
    }
    return HirVhdlShapedAccess { local,
        { selectors.begin(), selectors.end() }, false, true };
}

Lowerer::HirVhdlShapedProfile Lowerer::hir_vhdl_shaped_row_profile(
    const HirVhdlShapedProfile& profile) const
{
    auto row = profile;
    row.constraints.erase(row.constraints.begin(),
        row.constraints.begin()
            + static_cast<std::ptrdiff_t>(profile.outer_rank));
    row.outer_rank = 0U;
    row.type.vhdl_rank = static_cast<std::uint32_t>(row.constraints.size());
    const auto* array = profile.subtype.type_mark.target.valid()
        ? semantic::vhdl_base_type_definition(
              *specialized_hir_unit_, profile.subtype.type_mark.target)
        : nullptr;
    if (array != nullptr && array->element_subtype) {
        row.subtype = hir_vhdl_chain_element_subtype(
            profile.subtype, *array->element_subtype);
    }
    return row;
}

std::optional<StringRegisterId> Lowerer::lower_hir_vhdl_shaped_image(
    const semantic::ExpressionId expression_id, const bool characters)
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    const auto profile = hir_vhdl_shaped_value_profile(expression_id);
    if (!profile || profile->constraints.size() != 1U) {
        return std::nullopt;
    }
    const bool character_elements
        = profile->element_domain == frontend::ValueDomain::String;
    const bool bit_elements = profile->element_width == 1U
        && (profile->element_domain == frontend::ValueDomain::Bit2
            || profile->element_domain == frontend::ValueDomain::Logic4
            || profile->element_domain == frontend::ValueDomain::Logic9);
    if (!character_elements && (characters || !bit_elements)) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto value = lower_hir_vhdl_shaped_container(expression_id,
        expression && expression->vhdl != nullptr
            ? hir_source_span(expression->vhdl->source)
            : frontend::SourceSpan { });
    if (!value) {
        return std::nullopt;
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(FormatContainerPattern {
        destination, value->first });
    return destination;
}

std::optional<Lowerer::HirVhdlShapedSlice> Lowerer::hir_vhdl_shaped_slice(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != semantic::vhdl::ExpressionKind::slice
        || expression->vhdl->operands.size() != 3U
        || (expression->vhdl->text != "to"
            && expression->vhdl->text != "downto")) {
        return std::nullopt;
    }
    const auto* local = hir_vhdl_shaped_prefix(expression->vhdl->operands[0]);
    if (local == nullptr || local->profile.constraints.size() != 1U) {
        return std::nullopt;
    }
    return HirVhdlShapedSlice { local, expression->vhdl->operands[1],
        expression->vhdl->operands[2], expression->vhdl->text == "downto" };
}

std::optional<VhdlShaped> Lowerer::lower_hir_vhdl_shaped_slice(
    const HirVhdlShapedSlice& slice, const VhdlShapedOperation operation)
{
    VhdlShaped result;
    result.operation = operation;
    result.target = slice.local->container;
    result.source = slice.local->container;
    for (const auto bound : { slice.first, slice.last }) {
        const auto lowered = lower_hir_expression(bound,
            hir_expression_width(bound, hir_process_scope_).value_or(32U));
        if (!lowered) {
            return std::nullopt;
        }
        result.operands.push_back(*lowered);
    }
    result.directions.push_back(slice.descending ? 1U : 0U);
    return result;
}

std::optional<std::size_t> Lowerer::hir_vhdl_shaped_width(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    if (const auto access = hir_vhdl_shaped_access(expression_id)) {
        return access->leaf ? access->local->profile.leaf_width
                            : access->local->profile.element_width;
    }
    if (const auto path = hir_vhdl_shaped_record_path(expression_id);
        path && path->member != nullptr && !path->member->shaped) {
        return path->member->width;
    }
    if (const auto comparison = specialized_hir_unit_->find_expression(
            expression_id);
        comparison && comparison->vhdl != nullptr
        && comparison->vhdl->kind == semantic::vhdl::ExpressionKind::binary
        && comparison->vhdl->operands.size() == 2U
        && (comparison->vhdl->text == "=" || comparison->vhdl->text == "/=")
        && (hir_vhdl_shaped_value_profile(comparison->vhdl->operands[0])
            || hir_vhdl_shaped_value_profile(comparison->vhdl->operands[1]))) {
        return 1U;
    }
    if (const auto use = hir_vhdl_shaped_attribute(expression_id)) {
        return use->attribute == VhdlShapedAttribute::ascending
            ? 1U
            : static_cast<std::size_t>(
                  frontend::vhdl_predefined_integer_storage_width(
                      vhdl_standard_));
    }
    // A slice whose bounds fold, or differ by a constant (8.5).
    if (const auto slice = hir_vhdl_shaped_slice(expression_id)) {
        const auto first = hir_constant_integer(slice->first);
        const auto last = hir_constant_integer(slice->last);
        std::optional<std::size_t> length;
        if (first && last) {
            const auto low = slice->descending ? *last : *first;
            const auto high = slice->descending ? *first : *last;
            length = high < low ? 0U
                                : static_cast<std::size_t>(high - low) + 1U;
        } else {
            length = hir_vhdl_symbolic_slice_length(expression_id);
        }
        if (length && *length != 0U) {
            return *length * slice->local->profile.element_width;
        }
    }
    return std::nullopt;
}

std::optional<frontend::ValueDomain> Lowerer::hir_vhdl_shaped_domain(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    if (const auto access = hir_vhdl_shaped_access(expression_id)) {
        return access->leaf ? access->local->profile.leaf_domain
                            : access->local->profile.element_domain;
    }
    if (const auto path = hir_vhdl_shaped_record_path(expression_id);
        path && path->member != nullptr && !path->member->shaped) {
        return path->member->domain;
    }
    if (const auto use = hir_vhdl_shaped_attribute(expression_id)) {
        return use->attribute == VhdlShapedAttribute::ascending
            ? frontend::ValueDomain::Boolean
            : frontend::ValueDomain::Integer;
    }
    if (const auto slice = hir_vhdl_shaped_slice(expression_id)) {
        return slice->local->profile.element_domain;
    }
    if (const auto comparison = specialized_hir_unit_->find_expression(
            expression_id);
        comparison && comparison->vhdl != nullptr
        && comparison->vhdl->kind == semantic::vhdl::ExpressionKind::binary
        && comparison->vhdl->operands.size() == 2U
        && (comparison->vhdl->text == "=" || comparison->vhdl->text == "/=")
        && (hir_vhdl_shaped_value_profile(comparison->vhdl->operands[0])
            || hir_vhdl_shaped_value_profile(comparison->vhdl->operands[1]))) {
        return frontend::ValueDomain::Boolean;
    }
    return std::nullopt;
}

std::optional<std::optional<RegisterId>>
Lowerer::lower_hir_vhdl_shaped_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    const auto span = [&] {
        const auto expression = specialized_hir_unit_->find_expression(
            expression_id);
        return expression && expression->vhdl != nullptr
            ? hir_source_span(expression->vhdl->source)
            : frontend::SourceSpan { };
    };
    // Array equality with a value of run-time length: equal lengths and
    // elements (9.2.3).
    if (const auto comparison = specialized_hir_unit_->find_expression(
            expression_id);
        comparison && comparison->vhdl != nullptr
        && comparison->vhdl->kind == semantic::vhdl::ExpressionKind::binary
        && comparison->vhdl->operands.size() == 2U
        && (comparison->vhdl->text == "=" || comparison->vhdl->text == "/=")) {
        const auto& operands = comparison->vhdl->operands;
        const bool lhs_shaped = hir_vhdl_shaped_value_profile(operands[0]).has_value();
        const bool rhs_shaped = hir_vhdl_shaped_value_profile(operands[1]).has_value();
        if (lhs_shaped || rhs_shaped) {
            const auto shaped = lhs_shaped ? operands[0] : operands[1];
            const auto other = lhs_shaped ? operands[1] : operands[0];
            const auto lhs = lower_hir_vhdl_shaped_container(shaped, span());
            if (!lhs) {
                return std::optional<RegisterId> { };
            }
            const auto equal = allocate_register(1U, frontend::ValueDomain::Boolean);
            VhdlShaped operation {
                VhdlShapedOperation::equal,
                { },
                lhs->first,
                equal,
                { },
                { },
                { },
                0U,
                VhdlShapedAttribute::length,
                1U,
                { },
            };
            if (lhs_shaped && rhs_shaped) {
                const auto rhs = lower_hir_vhdl_shaped_container(other, span());
                if (!rhs) {
                    return std::optional<RegisterId> { };
                }
                operation.target = rhs->first;
            } else {
                const auto packed = lower_hir_vhdl_shaped_value(
                    other, lhs->second, std::nullopt);
                if (!packed) {
                    return std::optional<RegisterId> { };
                }
                operation.operation = VhdlShapedOperation::equal_packed;
                operation.operands.push_back(*packed);
            }
            process_.operations.emplace_back(std::move(operation));
            if (comparison->vhdl->text == "=") {
                return std::optional { equal };
            }
            const auto different = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(UnaryNot { different, equal });
            return std::optional { different };
        }
    }
    // A static element, or a whole record, of a designated record.
    if (const auto path = hir_vhdl_shaped_record_path(expression_id);
        path && (!path->shaped || !path->shaped->members.empty())) {
        return std::optional { lower_hir_vhdl_shaped_record_read(
            expression_id, *path, expected_width, span()) };
    }
    if (auto access = hir_vhdl_shaped_access(expression_id)) {
        const auto bound = bind_hir_vhdl_shaped(*access->local, span());
        if (!bound) {
            return std::optional<RegisterId> { };
        }
        access->local = &bound->local;
        const auto& profile = access->local->profile;
        const auto width = access->leaf ? profile.leaf_width
                                        : profile.element_width;
        const auto domain = access->leaf ? profile.leaf_domain
                                         : profile.element_domain;
        const auto signed_value = access->leaf ? profile.leaf_signed
                                               : profile.element_signed;
        auto operation = lower_hir_vhdl_shaped_access(
            *access, VhdlShapedOperation::element_read);
        if (!operation) {
            return std::optional<RegisterId> { };
        }
        const auto destination = allocate_register(width, domain);
        operation->value = destination;
        operation->width = static_cast<std::uint32_t>(width);
        process_.operations.emplace_back(std::move(*operation));
        return std::optional { expected_width != 0U && expected_width != width
                ? resize_register(destination, expected_width, signed_value)
                : destination };
    }
    if (const auto use = hir_vhdl_shaped_attribute(expression_id)) {
        const auto bound = bind_hir_vhdl_shaped(*use->local, span());
        if (!bound) {
            return std::optional<RegisterId> { };
        }
        const bool boolean = use->attribute == VhdlShapedAttribute::ascending;
        const auto width = boolean ? 1U
            : expected_width != 0U ? expected_width
            : static_cast<std::size_t>(
                  frontend::vhdl_predefined_integer_storage_width(
                      vhdl_standard_));
        const auto destination = allocate_register(width,
            boolean ? frontend::ValueDomain::Boolean
                    : frontend::ValueDomain::Integer);
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::query,
            { },
            bound->local.container,
            destination,
            { },
            { },
            { },
            use->dimension,
            use->attribute,
            static_cast<std::uint32_t>(width),
            { },
        });
        return std::optional { destination };
    }
    // A slice of a one-dimensional object in a context of known length
    // (8.5).
    if (auto slice = hir_vhdl_shaped_slice(expression_id);
        slice && expected_width != 0U) {
        const auto bound = bind_hir_vhdl_shaped(*slice->local, span());
        if (!bound) {
            return std::optional<RegisterId> { };
        }
        slice->local = &bound->local;
        auto operation = lower_hir_vhdl_shaped_slice(
            *slice, VhdlShapedOperation::slice_read);
        if (!operation) {
            return std::optional<RegisterId> { };
        }
        const auto destination = allocate_register(
            expected_width, slice->local->profile.element_domain);
        operation->value = destination;
        operation->width = static_cast<std::uint32_t>(expected_width);
        process_.operations.emplace_back(std::move(*operation));
        return std::optional { destination };
    }
    // A concatenation with a shaped operand in a context of known length.
    if (expected_width != 0U && hir_vhdl_shaped_name(expression_id) == nullptr
        && hir_vhdl_shaped_value_profile(expression_id)) {
        const auto value = lower_hir_vhdl_shaped_container(expression_id, span());
        if (!value) {
            return std::optional<RegisterId> { };
        }
        const auto destination = allocate_register(
            expected_width, value->second.element_domain);
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::to_packed,
            { },
            value->first,
            destination,
            { },
            { },
            { },
            0U,
            VhdlShapedAttribute::length,
            static_cast<std::uint32_t>(expected_width),
            { },
        });
        return std::optional { destination };
    }
    // The whole object in a context of known length (14.7.3.4).
    if (const auto* local = hir_vhdl_shaped_name(expression_id);
        local != nullptr && expected_width != 0U) {
        const auto bound = bind_hir_vhdl_shaped(*local, span());
        if (!bound) {
            return std::optional<RegisterId> { };
        }
        const auto destination = allocate_register(
            expected_width, local->profile.element_domain);
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::to_packed,
            { },
            bound->local.container,
            destination,
            { },
            { },
            { },
            0U,
            VhdlShapedAttribute::length,
            static_cast<std::uint32_t>(expected_width),
            { },
        });
        return std::optional { destination };
    }
    return std::nullopt;
}

bool Lowerer::lower_hir_vhdl_shaped_whole_assignment(
    const ContainerRegisterId target,
    const HirVhdlShapedProfile& profile,
    const semantic::ExpressionId value_id)
{
    const auto value = specialized_hir_unit_->find_expression(value_id);
    // Another shaped object: the target keeps its bounds (10.6.2.1).
    if (const auto* source = hir_vhdl_shaped_name(value_id)) {
        const auto bound = bind_hir_vhdl_shaped(*source,
            value && value->vhdl != nullptr
                ? hir_source_span(value->vhdl->source)
                : frontend::SourceSpan { });
        if (!bound) {
            return false;
        }
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::assign,
            target,
            bound->local.container,
            { },
            { },
            { },
            { },
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        });
        return true;
    }
    // `(others => e)` takes the target's bounds (9.3.3.3): the target is
    // refilled in its own shape.
    if (value && value->vhdl != nullptr
        && value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
        && value->vhdl->associations.size() == 1U
        && value->vhdl->associations.front().choice_spelling == "others") {
        auto element = lower_hir_expression(
            value->vhdl->associations.front().value, profile.element_width);
        if (!element) {
            return false;
        }
        if (register_width(*element) != profile.element_width) {
            element = resize_register(
                *element, profile.element_width, profile.element_signed);
        }
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::create,
            target,
            { },
            *element,
            { },
            { },
            profile.type,
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        });
        return true;
    }
    // An aggregate of several dimensions has its own ranges; each must have
    // the target's length (10.6.2.1).
    if (value && value->vhdl != nullptr
        && value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
        && profile.constraints.size() != 1U) {
        const auto source = lower_hir_vhdl_shaped_materialize(
            profile, value_id, hir_source_span(value->vhdl->source));
        if (!source) {
            return false;
        }
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::assign,
            target,
            *source,
            { },
            { },
            { },
            { },
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        });
        return true;
    }
    // A value of statically known length (10.6.2.1).
    const auto lowered = lower_hir_vhdl_shaped_value(value_id, profile,
        std::nullopt);
    if (!lowered) {
        return false;
    }
    process_.operations.emplace_back(VhdlShaped {
        VhdlShapedOperation::from_packed,
        target,
        { },
        *lowered,
        { },
        { },
        { },
        0U,
        VhdlShapedAttribute::length,
        0U,
        { },
    });
    return true;
}

std::optional<bool> Lowerer::lower_hir_vhdl_shaped_assignment(
    const semantic::ExpressionId target_id,
    const semantic::ExpressionId value_id)
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    const auto target_expression = specialized_hir_unit_->find_expression(
        target_id);
    const auto span = target_expression && target_expression->vhdl != nullptr
        ? hir_source_span(target_expression->vhdl->source)
        : frontend::SourceSpan { };
    if (const auto path = hir_vhdl_shaped_record_path(target_id);
        path && (!path->shaped || !path->shaped->members.empty())) {
        return lower_hir_vhdl_shaped_record_write(*path, value_id, span);
    }
    if (auto access = hir_vhdl_shaped_access(target_id)) {
        const auto bound = bind_hir_vhdl_shaped(*access->local, span);
        if (!bound) {
            return false;
        }
        access->local = &bound->local;
        const auto& profile = access->local->profile;
        const auto width = access->leaf ? profile.leaf_width
                                        : profile.element_width;
        const auto signed_value = access->leaf ? profile.leaf_signed
                                               : profile.element_signed;
        auto operation = lower_hir_vhdl_shaped_access(
            *access, VhdlShapedOperation::element_write);
        if (!operation) {
            return false;
        }
        const auto literal = specialized_hir_unit_->find_expression(value_id);
        const auto code = profile.element_domain == frontend::ValueDomain::String
                && !access->leaf && literal && literal->vhdl != nullptr
            ? character_literal_code(*literal->vhdl)
            : std::nullopt;
        auto value = code
            ? std::optional { load_constant(unsigned_value(*code, 8U),
                  frontend::ValueDomain::String) }
            : lower_hir_expression(value_id, width);
        if (!value) {
            return false;
        }
        if (register_width(*value) != width) {
            value = resize_register(*value, width, signed_value);
        }
        operation->value = *value;
        process_.operations.emplace_back(std::move(*operation));
        store_hir_vhdl_shaped(*bound);
        return true;
    }
    // A row of an array of open arrays takes a value of its length.
    if (auto row = hir_vhdl_shaped_row(target_id)) {
        const auto bound = bind_hir_vhdl_shaped(*row->local, span);
        if (!bound) {
            return false;
        }
        row->local = &bound->local;
        auto operation = lower_hir_vhdl_shaped_access(
            *row, VhdlShapedOperation::element_write);
        const auto value = operation
            ? lower_hir_vhdl_shaped_value(value_id,
                  hir_vhdl_shaped_row_profile(bound->local.profile),
                  std::nullopt)
            : std::nullopt;
        if (!value) {
            return false;
        }
        operation->value = *value;
        process_.operations.emplace_back(std::move(*operation));
        store_hir_vhdl_shaped(*bound);
        return true;
    }
    if (auto slice = hir_vhdl_shaped_slice(target_id)) {
        const auto bound = bind_hir_vhdl_shaped(*slice->local, span);
        if (!bound) {
            return false;
        }
        slice->local = &bound->local;
        auto operation = lower_hir_vhdl_shaped_slice(
            *slice, VhdlShapedOperation::slice_write);
        if (!operation) {
            return false;
        }
        const auto value = lower_hir_vhdl_shaped_value(value_id,
            slice->local->profile, hir_vhdl_shaped_width(target_id));
        if (!value) {
            return false;
        }
        operation->value = *value;
        process_.operations.emplace_back(std::move(*operation));
        store_hir_vhdl_shaped(*bound);
        return true;
    }
    if (const auto* local = hir_vhdl_shaped_name(target_id)) {
        const auto bound = bind_hir_vhdl_shaped(*local, span);
        if (!bound
            || !lower_hir_vhdl_shaped_whole_assignment(
                bound->local.container, bound->local.profile, value_id)) {
            return false;
        }
        store_hir_vhdl_shaped(*bound);
        return true;
    }
    return std::nullopt;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_shaped_value(
    const semantic::ExpressionId value_id,
    const HirVhdlShapedProfile& profile,
    std::optional<std::size_t> width)
{
    const auto value = specialized_hir_unit_->find_expression(value_id);
    const bool positional_aggregate = value && value->vhdl != nullptr
        && value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
        && !value->vhdl->associations.empty()
        && std::ranges::all_of(value->vhdl->associations,
            [](const auto& association) { return association.positional(); });
    // A positional aggregate has as many elements as associations
    // (9.3.3.3), a string literal as many as characters (9.3.2).
    if (!width && positional_aggregate) {
        width = value->vhdl->associations.size() * profile.element_width;
    }
    if (!width && value && value->vhdl != nullptr
        && value->vhdl->kind == semantic::vhdl::ExpressionKind::string_literal
        && value->vhdl->decoded_string) {
        width = value->vhdl->decoded_string->size() * profile.element_width;
    }
    if (!width || *width == 0U) {
        width = hir_expression_width(value_id, hir_process_scope_);
    }
    if (!width || *width == 0U || *width % profile.element_width != 0U) {
        return std::nullopt;
    }
    // An aggregate or literal takes the target's type; its bounds convert
    // to the target's when assigned (10.6.2.1), so a 0-based range of the
    // right length serves as its context.
    const bool contextual = value && value->vhdl != nullptr
        && (value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
            || value->vhdl->kind == semantic::vhdl::ExpressionKind::string_literal);
    if (contextual && profile.constraints.size() == 1U) {
        auto context = profile.subtype;
        semantic::vhdl::RangeConstraint range;
        range.kind = semantic::vhdl::RangeKind::array_index;
        range.left = 0;
        range.right = static_cast<std::int64_t>(
            *width / profile.element_width) - 1;
        std::erase_if(context.constraints, [](const auto& constraint) {
            return constraint.kind == semantic::vhdl::RangeKind::array_index;
        });
        context.constraints.insert(context.constraints.begin(), range);
        context.unconstrained = false;
        context.executable_width = *width;
        const auto lowered
            = value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
            ? lower_hir_vhdl_aggregate(value_id, *width, &context)
            : lower_hir_expression(value_id, *width,
                  frontend::SystemVerilogScalarKind::None, &context);
        return lowered ? std::optional { conform_hir_vhdl_shaped_domain(
                             *lowered, profile.element_domain) }
                       : std::nullopt;
    }
    const auto lowered = lower_hir_expression(value_id, *width);
    return lowered ? std::optional { conform_hir_vhdl_shaped_domain(
                         *lowered, profile.element_domain) }
                   : std::nullopt;
}

std::optional<Lowerer::HirVhdlShapedProfile>
Lowerer::hir_vhdl_shaped_value_profile(
    const semantic::ExpressionId expression_id) const
{
    if (!hir_vhdl_shaped_active()) {
        return std::nullopt;
    }
    if (const auto* local = hir_vhdl_shaped_name(expression_id)) {
        return local->profile;
    }
    if (const auto row = hir_vhdl_shaped_row(expression_id)) {
        return hir_vhdl_shaped_row_profile(row->local->profile);
    }
    const auto expression = specialized_hir_unit_->find_expression(expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && source.text.starts_with(semantic::vhdl::qualified_expression_prefix)
        && source.operands.size() == 1U) {
        return hir_vhdl_shaped_value_profile(source.operands.front());
    }
    // `(l to r => e)` with bounds known only at run time, in a context that
    // gives its type.
    if (hir_vhdl_shaped_context_profile_
        && source.kind == semantic::vhdl::ExpressionKind::aggregate
        && hir_vhdl_single_dynamic_range_aggregate(source)) {
        const auto choice = specialized_hir_unit_->find_expression(
            source.associations.front().choices.front());
        if (choice && choice->vhdl != nullptr
            && choice->vhdl->kind == semantic::vhdl::ExpressionKind::binary
            && choice->vhdl->operands.size() == 2U) {
            return hir_vhdl_shaped_context_profile_;
        }
    }
    if ((source.kind == semantic::vhdl::ExpressionKind::binary
            && source.text == "&" && source.operands.size() == 2U)
        || (source.kind == semantic::vhdl::ExpressionKind::concatenation
            && !source.operands.empty())) {
        for (const auto operand : source.operands) {
            if (auto profile = hir_vhdl_shaped_value_profile(operand);
                profile && profile->constraints.size() == 1U) {
                return profile;
            }
        }
    }
    return std::nullopt;
}

Lowerer::HirPackedRange Lowerer::hir_vhdl_shaped_packed_range(
    const HirVhdlShapedProfile& profile,
    const semantic::ExpressionId value_id,
    const std::size_t width) const
{
    const auto value = specialized_hir_unit_->find_expression(value_id);
    semantic::vhdl::Declaration inferred_value;
    inferred_value.scope = value && value->vhdl != nullptr
        ? value->vhdl->scope : semantic::ScopeId { };
    inferred_value.form = semantic::vhdl::DeclarationForm::constant;
    inferred_value.subtype = profile.subtype;
    std::erase_if(inferred_value.subtype->constraints, [](const auto& constraint) {
        return constraint.kind == semantic::vhdl::RangeKind::array_index;
    });
    inferred_value.subtype->unconstrained = true;
    inferred_value.initializer = value_id;
    if (const auto range = hir_vhdl_unconstrained_constant_range(
            inferred_value, width, hir_process_scope_)) {
        return *range;
    }
    const auto count = profile.element_width == 0U
        ? std::size_t { } : width / profile.element_width;
    return HirPackedRange { 0, static_cast<std::int64_t>(count) - 1, false };
}

std::optional<std::pair<ContainerRegisterId, Lowerer::HirVhdlShapedProfile>>
Lowerer::lower_hir_vhdl_shaped_container(
    const semantic::ExpressionId expression_id,
    const frontend::SourceSpan& span)
{
    if (const auto* local = hir_vhdl_shaped_name(expression_id)) {
        auto bound = bind_hir_vhdl_shaped(*local, span);
        if (!bound) {
            return std::nullopt;
        }
        return std::pair { bound->local.container, bound->local.profile };
    }
    if (auto row = hir_vhdl_shaped_row(expression_id)) {
        const auto bound = bind_hir_vhdl_shaped(*row->local, span);
        if (!bound) {
            return std::nullopt;
        }
        row->local = &bound->local;
        auto operation = lower_hir_vhdl_shaped_access(
            *row, VhdlShapedOperation::element_read);
        if (!operation) {
            return std::nullopt;
        }
        auto profile = hir_vhdl_shaped_row_profile(bound->local.profile);
        const auto result = allocate_container_register(profile.type);
        operation->target = result;
        process_.operations.emplace_back(std::move(*operation));
        return std::pair { result, std::move(profile) };
    }
    const auto expression = specialized_hir_unit_->find_expression(expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && source.text.starts_with(semantic::vhdl::qualified_expression_prefix)
        && source.operands.size() == 1U) {
        return lower_hir_vhdl_shaped_container(source.operands.front(), span);
    }
    if (source.kind == semantic::vhdl::ExpressionKind::aggregate) {
        // `(l to r => e)`: every element of the range is e (9.3.3.3).
        auto profile = hir_vhdl_shaped_value_profile(expression_id);
        const auto choice = profile
            ? specialized_hir_unit_->find_expression(
                  source.associations.front().choices.front())
            : std::nullopt;
        if (!choice || choice->vhdl == nullptr
            || choice->vhdl->operands.size() != 2U) {
            return std::nullopt;
        }
        std::vector<RegisterId> bounds;
        for (const auto bound : choice->vhdl->operands) {
            const auto lowered = lower_hir_expression(bound,
                hir_expression_width(bound, hir_process_scope_).value_or(32U));
            if (!lowered) {
                return std::nullopt;
            }
            bounds.push_back(*lowered);
        }
        auto element = lower_hir_expression(
            source.associations.front().value, profile->element_width);
        if (!element) {
            return std::nullopt;
        }
        if (register_width(*element) != profile->element_width) {
            element = resize_register(*element, profile->element_width,
                profile->element_signed);
        }
        const auto result = allocate_container_register(profile->type);
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::create,
            result,
            { },
            conform_hir_vhdl_shaped_domain(*element, profile->element_domain),
            std::move(bounds),
            { static_cast<std::uint8_t>(choice->vhdl->text == "downto" ? 1U : 0U) },
            profile->type,
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        });
        return std::pair { result, std::move(*profile) };
    }
    auto profile = hir_vhdl_shaped_value_profile(expression_id);
    const bool concatenation = (source.kind == semantic::vhdl::ExpressionKind::binary
            && source.text == "&" && source.operands.size() == 2U)
        || (source.kind == semantic::vhdl::ExpressionKind::concatenation
            && !source.operands.empty());
    if (!profile || !concatenation) {
        return std::nullopt;
    }
    // An operand of the element type contributes one element (9.2.5).
    const auto element_operand = [&](const semantic::ExpressionId operand) {
        const auto node = specialized_hir_unit_->find_expression(operand);
        if (node && node->vhdl != nullptr
            && (node->vhdl->kind == semantic::vhdl::ExpressionKind::logic_literal
                || node->vhdl->kind == semantic::vhdl::ExpressionKind::integer_literal
                || node->vhdl->kind == semantic::vhdl::ExpressionKind::boolean_literal
                || (node->vhdl->kind == semantic::vhdl::ExpressionKind::name
                    && node->vhdl->text.size() == 3U
                    && node->vhdl->text.front() == '\''
                    && node->vhdl->text.back() == '\''))) {
            return true;
        }
        const auto subtype = hir_vhdl_expression_subtype(operand);
        if (!subtype) {
            return false;
        }
        auto type_id = subtype->type_mark.target;
        for (std::size_t depth { }; depth < 32U && type_id.valid(); ++depth) {
            const auto type = specialized_hir_unit_->find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                return false;
            }
            if (type->vhdl->form == semantic::vhdl::TypeForm::array) {
                return false;
            }
            if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
                && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
                return true;
            }
            type_id = type->vhdl->base.type_mark.target;
        }
        return subtype->builtin_type == semantic::vhdl::BuiltinTypeIdentity::none
            && subtype->executable_width
            && *subtype->executable_width == profile->element_width;
    };
    const auto packed_operand = [&](const semantic::ExpressionId operand)
        -> std::optional<RegisterId> {
        if (element_operand(operand)) {
            auto value = lower_hir_expression(operand, profile->element_width);
            if (value && register_width(*value) != profile->element_width) {
                value = resize_register(*value, profile->element_width,
                    profile->element_signed);
            }
            return value ? std::optional { conform_hir_vhdl_shaped_domain(
                               *value, profile->element_domain) }
                         : std::nullopt;
        }
        return lower_hir_vhdl_shaped_value(operand, *profile, std::nullopt);
    };
    // The left operand starts the result, which then takes each further
    // operand's elements (9.2.5).
    const auto& operands = source.operands;
    const auto result = allocate_container_register(profile->type);
    if (hir_vhdl_shaped_value_profile(operands.front())) {
        const auto left = lower_hir_vhdl_shaped_container(operands.front(), span);
        if (!left) {
            return std::nullopt;
        }
        process_.operations.emplace_back(CopyContainerRegister {
            result, left->first });
    } else {
        const auto left = packed_operand(operands.front());
        if (!left) {
            return std::nullopt;
        }
        const auto width = register_width(*left);
        const auto range = element_operand(operands.front())
            ? hir_vhdl_shaped_packed_range(*profile, operands.front(), 0U)
            : hir_vhdl_shaped_packed_range(*profile, operands.front(), width);
        const auto count = static_cast<std::int64_t>(width / profile->element_width);
        const auto right_bound = range.descending ? range.left - (count - 1)
                                                  : range.left + (count - 1);
        const auto fill = load_constant(profile->element_default,
            profile->element_domain);
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::create,
            result,
            { },
            fill,
            { load_integer(range.left, 64U, frontend::ValueDomain::Integer),
                load_integer(right_bound, 64U, frontend::ValueDomain::Integer) },
            { static_cast<std::uint8_t>(range.descending ? 1U : 0U) },
            profile->type,
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        });
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::from_packed,
            result,
            { },
            *left,
            { },
            { },
            { },
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        });
    }
    for (std::size_t index = 1U; index < operands.size(); ++index) {
        VhdlShaped append {
            VhdlShapedOperation::append,
            result,
            { },
            { },
            { },
            { },
            { },
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        };
        if (hir_vhdl_shaped_value_profile(operands[index])) {
            const auto right = lower_hir_vhdl_shaped_container(operands[index], span);
            if (!right) {
                return std::nullopt;
            }
            append.source = right->first;
        } else {
            const auto right = packed_operand(operands[index]);
            if (!right) {
                return std::nullopt;
            }
            append.operation = VhdlShapedOperation::append_packed;
            append.operands.push_back(*right);
        }
        process_.operations.emplace_back(std::move(append));
    }
    return std::pair { result, std::move(*profile) };
}

RegisterId Lowerer::conform_hir_vhdl_shaped_domain(
    const RegisterId value, const frontend::ValueDomain domain)
{
    // A shaped payload holds its elements in one representation, so an
    // operand in another (a literal typed as STD_ULOGIC, say) is converted.
    if (domain == frontend::ValueDomain::Unknown
        || register_domain(value) == domain) {
        return value;
    }
    if (is_two_state_domain(domain)
        && !is_two_state_domain(register_domain(value))) {
        const auto converted = convert_to_two_state(value);
        if (register_domain(converted) == domain) {
            return converted;
        }
        const auto relabeled = allocate_register(register_width(converted), domain);
        process_.operations.emplace_back(CopyRegister { relabeled, converted });
        return relabeled;
    }
    const auto converted = allocate_register(register_width(value), domain);
    process_.operations.emplace_back(CopyRegister { converted, value });
    return converted;
}

std::optional<Lowerer::HirVhdlShapedProfile>
Lowerer::hir_vhdl_shaped_open_profile(
    const semantic::vhdl::SubtypeIndication& subtype) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    // The array type itself, without the index constraints of subtypes.
    auto current = subtype;
    std::unordered_set<std::uint32_t> visited;
    for (std::size_t depth { }; depth < 32U; ++depth) {
        if (current.builtin_type != semantic::vhdl::BuiltinTypeIdentity::none) {
            semantic::vhdl::SubtypeIndication open;
            open.type_mark = current.type_mark;
            open.builtin_type = current.builtin_type;
            open.domain = current.domain;
            open.unconstrained = true;
            return hir_vhdl_shaped_profile(open, true);
        }
        if (!current.type_mark.target.valid()
            || !visited.insert(current.type_mark.target.value()).second) {
            return std::nullopt;
        }
        const auto type = specialized_hir_unit_->find_type(current.type_mark.target);
        if (!type || type->vhdl == nullptr) {
            return std::nullopt;
        }
        if (type->vhdl->form == semantic::vhdl::TypeForm::array) {
            semantic::vhdl::SubtypeIndication open;
            open.type_mark = current.type_mark;
            open.domain = current.domain;
            open.unconstrained = true;
            return hir_vhdl_shaped_profile(open, true);
        }
        if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
            && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
            return std::nullopt;
        }
        current = type->vhdl->base;
    }
    return std::nullopt;
}

bool Lowerer::hir_vhdl_elaborated_constant(
    const semantic::vhdl::Declaration& declaration) const
{
    if (declaration.form != semantic::vhdl::DeclarationForm::constant
        || !declaration.subtype || !declaration.initializer) {
        return false;
    }
    // Bounds known only at run time: a shaped object (5.3.2.2).
    if (hir_vhdl_shaped_profile(*declaration.subtype)) {
        return true;
    }
    const auto effective = hir_effective_vhdl_subtype(*declaration.subtype);
    // A STRING value that does not fold (`constant s : string := d(n)`) is
    // computed once, when the declaration is elaborated (14.4.2.5), not at
    // each read.
    if (effective && effective->domain == semantic::vhdl::ValueDomain::string
        && !specialized_hir_unit_->evaluate_string_expression(
            *declaration.initializer)) {
        return true;
    }
    // An integer value that does not fold (`constant len : integer :=
    // p'length`) keeps the value it had when the declaration was elaborated,
    // whatever it was computed from changes later.
    // Only a scalar: records and arrays keep their own paths.
    const bool composite = [&] {
        auto type_id = declaration.subtype->type_mark.target;
        for (std::size_t depth { }; depth < 32U && type_id.valid(); ++depth) {
            const auto type = specialized_hir_unit_->find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                return true;
            }
            if (type->vhdl->form == semantic::vhdl::TypeForm::array
                || type->vhdl->form == semantic::vhdl::TypeForm::record) {
                return true;
            }
            if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
                && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
                return false;
            }
            type_id = type->vhdl->base.type_mark.target;
        }
        return false;
    }();
    return effective && !composite
        && effective->domain == semantic::vhdl::ValueDomain::integer
        && std::ranges::none_of(effective->constraints,
            [](const semantic::vhdl::RangeConstraint& constraint) {
                return constraint.kind
                    == semantic::vhdl::RangeKind::array_index;
            })
        && !hir_constant_integer(*declaration.initializer);
}

bool Lowerer::hir_vhdl_shaped_active() const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    if (!hir_vhdl_shaped_locals_.empty() || hir_vhdl_shaped_context_profile_) {
        return true;
    }
    if (hir_vhdl_shaped_active_unit_ != specialized_hir_unit_) {
        hir_vhdl_shaped_active_unit_ = specialized_hir_unit_;
        hir_vhdl_shaped_active_types_ = false;
        for (const auto& stored : specialized_hir_unit_->design().vhdl_hir.types()) {
            const auto type = specialized_hir_unit_->find_type(stored.id);
            if (type && type->vhdl != nullptr
                && type->vhdl->form == semantic::vhdl::TypeForm::access
                && hir_vhdl_shaped_heap_profile(*type->vhdl)) {
                hir_vhdl_shaped_active_types_ = true;
                break;
            }
        }
        shaped_rejected(hir_vhdl_shaped_active_types_
                ? "active: shaped access type"
                : "inactive: no shaped access type");
    }
    return hir_vhdl_shaped_active_types_;
}

std::optional<Lowerer::HirVhdlShapedProfile>
Lowerer::hir_vhdl_shaped_heap_profile(
    const semantic::vhdl::TypeDefinition& access_type) const
{
    if (const auto found = hir_vhdl_shaped_heap_profiles_.find(
            access_type.id.value());
        found != hir_vhdl_shaped_heap_profiles_.end()) {
        return found->second;
    }
    std::optional<HirVhdlShapedProfile> profile;
    if (access_type.designated_subtype) {
        const auto designated = hir_effective_vhdl_subtype(
            *access_type.designated_subtype)
            .value_or(*access_type.designated_subtype);
        // STRING objects keep their own lengths in string storage.
        if (designated.domain != semantic::vhdl::ValueDomain::string) {
            profile = hir_vhdl_shaped_record_profile(
                *access_type.designated_subtype);
            if (!profile) {
                profile = hir_vhdl_shaped_profile(
                    *access_type.designated_subtype, true);
            }
        }
    }
    hir_vhdl_shaped_heap_profiles_.emplace(access_type.id.value(), profile);
    return profile;
}

const Lowerer::HirVhdlShapedLocal* Lowerer::hir_vhdl_shaped_heap_view(
    const semantic::ExpressionId expression_id, const bool prefix) const
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return nullptr;
    }
    const auto& source = *expression->vhdl;
    // An array element of a designated record (`p.e`, IEEE 1076-2008 8.3).
    if (const auto path = hir_vhdl_shaped_record_path(expression_id)) {
        return hir_vhdl_shaped_member_view(expression_id, *path);
    }
    std::optional<semantic::ExpressionId> handle;
    if (source.kind == semantic::vhdl::ExpressionKind::call
        && source.text == "@vhdl-dereference"
        && source.operands.size() == 1U) {
        handle = source.operands.front();
    } else if (prefix && source.kind == semantic::vhdl::ExpressionKind::name) {
        handle = expression_id;
    } else {
        return nullptr;
    }
    // A prefix's expression subtype may already be the designated one; its
    // declaration names the access type.
    const semantic::vhdl::TypeDefinition* type { };
    if (const auto subtype = hir_vhdl_expression_subtype(*handle)) {
        type = hir_vhdl_access_type_definition(*subtype);
    }
    for (const auto declaration :
        { hir_target_declaration(*handle), hir_referenced_declaration(*handle) }) {
        if (type != nullptr || !declaration) {
            continue;
        }
        const auto found = specialized_hir_unit_->find_declaration(*declaration);
        if (found && found->vhdl != nullptr && found->vhdl->subtype) {
            type = hir_vhdl_access_type_definition(*found->vhdl->subtype);
        }
    }
    if (type == nullptr) {
        {
        const auto handle_expression = specialized_hir_unit_->find_expression(*handle);
        return (shaped_rejected("heap view: no access type for '"
                    + (handle_expression && handle_expression->vhdl != nullptr
                            ? handle_expression->vhdl->text : std::string { })
                    + "'"),
            nullptr);
    }
    }
    auto profile = hir_vhdl_shaped_heap_profile(*type);
    // A designated record is reached through its elements.
    if (!profile || !profile->members.empty()) {
        return nullptr;
    }
    auto& view = hir_vhdl_shaped_heap_views_[handle->value()];
    view.profile = std::move(*profile);
    view.heap_handle = *handle;
    view.access_type = type;
    return &view;
}

const Lowerer::HirVhdlShapedLocal* Lowerer::hir_vhdl_shaped_prefix(
    const semantic::ExpressionId expression_id) const
{
    if (const auto* local = hir_vhdl_shaped_name(expression_id)) {
        return local;
    }
    return hir_vhdl_shaped_heap_view(expression_id, true);
}

std::optional<Lowerer::HirVhdlShapedBound> Lowerer::bind_hir_vhdl_shaped(
    const HirVhdlShapedLocal& local, const frontend::SourceSpan& span)
{
    HirVhdlShapedBound bound { local, std::nullopt, { } };
    if ((!local.heap_handle && !local.heap_handle_declaration)
        || local.access_type == nullptr) {
        return bound;
    }
    auto* heap = hir_vhdl_access_heap(*local.access_type, span);
    if (heap == nullptr || !heap->shaped) {
        return std::nullopt;
    }
    HirVhdlShapedRecordPath path;
    path.handle = local.heap_handle;
    path.handle_declaration = local.heap_handle_declaration;
    const auto handle = lower_hir_vhdl_shaped_handle(path, *heap, span);
    if (!handle) {
        return std::nullopt;
    }
    bound.local.container = allocate_container_register(local.profile.type);
    bound.handle = *handle;
    bound.heap_objects = heap->objects;
    process_.operations.emplace_back(ContainerElementRead {
        bound.local.container, heap->objects, *handle, false,
        local.heap_members });
    return bound;
}

void Lowerer::store_hir_vhdl_shaped(const HirVhdlShapedBound& bound)
{
    if (bound.handle) {
        process_.operations.emplace_back(ContainerElementWrite {
            bound.heap_objects, *bound.handle, bound.local.container, false,
            bound.local.heap_members });
    }
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_shaped_allocator(
    const HirVhdlAccessHeap& heap,
    const std::span<const semantic::vhdl::RangeConstraint> constraints,
    const std::optional<semantic::ExpressionId> initializer,
    const std::optional<semantic::vhdl::SubtypeIndication>& allocated,
    const frontend::SourceSpan& span)
{
    const auto& profile = *heap.shaped;
    // A record: `new T` takes its open elements' ranges from T's element
    // constraints, `new T'(v)` from v (9.3.7).
    if (!profile.members.empty()) {
        const auto container = initializer
            ? lower_hir_vhdl_shaped_materialize(profile, *initializer, span)
            : lower_hir_vhdl_shaped_create(profile,
                  allocated.value_or(profile.subtype));
        if (!container) {
            return (shaped_rejected("record allocator"), std::nullopt);
        }
        return issue_hir_vhdl_access_handle(
            heap, span, [&](const RegisterId handle) {
                process_.operations.emplace_back(ContainerElementWrite {
                    heap.objects, handle, *container, false, { } });
            });
    }
    const auto container = allocate_container_register(profile.type);
    const auto fill = load_constant(profile.element_default,
        profile.element_domain);
    const auto create = [&](std::vector<RegisterId> bounds,
                            std::vector<std::uint8_t> directions) {
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::create,
            container,
            { },
            fill,
            std::move(bounds),
            std::move(directions),
            profile.type,
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        });
    };
    const auto bound_register = [&](const std::optional<std::int64_t> constant,
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
    if (!initializer) {
        // `new T(r)`: the allocator's index constraint (9.3.7).
        if (constraints.size() != profile.constraints.size()) {
            return (shaped_rejected("allocator constraints"), std::nullopt);
        }
        std::vector<RegisterId> bounds;
        std::vector<std::uint8_t> directions;
        for (const auto& constraint : constraints) {
            const auto left = bound_register(
                constraint.left ? constraint.left : hir_range_left(constraint),
                constraint.left_expression);
            const auto right = bound_register(
                constraint.right ? constraint.right : hir_range_right(constraint),
                constraint.right_expression);
            if (!left || !right) {
                return (shaped_rejected("allocator bounds"), std::nullopt);
            }
            bounds.push_back(*left);
            bounds.push_back(*right);
            const bool descending = constraint.direction_from_bounds
                ? hir_vhdl_range_direction(constraint, hir_process_scope_)
                      .value_or(constraint.descending)
                : constraint.descending;
            directions.push_back(descending ? 1U : 0U);
        }
        create(std::move(bounds), std::move(directions));
    } else {
        // `new T'(v)`: the value supplies the bounds (9.3.7).
        auto value_id = *initializer;
        for (std::size_t depth { }; depth < 16U; ++depth) {
            const auto qualified = specialized_hir_unit_->find_expression(
                value_id);
            if (!qualified || qualified->vhdl == nullptr
                || qualified->vhdl->kind != semantic::vhdl::ExpressionKind::call
                || !qualified->vhdl->text.starts_with(
                    semantic::vhdl::qualified_expression_prefix)
                || qualified->vhdl->operands.size() != 1U) {
                break;
            }
            value_id = qualified->vhdl->operands.front();
        }
        if (const auto* source = hir_vhdl_shaped_name(value_id)) {
            const auto bound = bind_hir_vhdl_shaped(*source, span);
            if (!bound) {
                return std::nullopt;
            }
            return issue_hir_vhdl_access_handle(
                heap, span, [&](const RegisterId handle) {
                    process_.operations.emplace_back(ContainerElementWrite {
                        heap.objects, handle, bound->local.container, false,
                        { } });
                });
        }
        // An array of unconstrained arrays: the value's element subtype
        // supplies the element's index ranges (5.3.2.2).
        std::vector<HirPackedRange> inner;
        if (profile.outer_rank == 1U) {
            const auto shape = hir_vhdl_shape(value_id);
            if (!shape) {
                return (shaped_rejected("allocator element shape"),
                    std::nullopt);
            }
            const auto element = std::ranges::find_if(
                shape->element_constraints,
                [](const semantic::vhdl::ElementConstraint& constraint) {
                    return constraint.path.size() == 1U
                        && constraint.path.front().empty();
                });
            if (element == shape->element_constraints.end()) {
                return (shaped_rejected("allocator element shape"),
                    std::nullopt);
            }
            for (const auto& constraint : element->constraints) {
                if (constraint.kind != semantic::vhdl::RangeKind::array_index) {
                    continue;
                }
                const auto left = constraint.left ? constraint.left
                    : constraint.left_expression
                    ? hir_constant_integer(*constraint.left_expression)
                    : std::nullopt;
                const auto right = constraint.right ? constraint.right
                    : constraint.right_expression
                    ? hir_constant_integer(*constraint.right_expression)
                    : std::nullopt;
                if (!left || !right) {
                    return (shaped_rejected("allocator element bounds"),
                        std::nullopt);
                }
                inner.push_back({ *left, *right, constraint.descending });
            }
        }
        if (profile.constraints.size() != 1U + inner.size()) {
            return (shaped_rejected("allocator value rank"), std::nullopt);
        }
        const auto value_source = specialized_hir_unit_->find_expression(value_id);
        auto width = hir_expression_width(*initializer, hir_process_scope_,
            &heap.designated_subtype);
        semantic::vhdl::Declaration inferred_value;
        inferred_value.scope = value_source && value_source->vhdl != nullptr
            ? value_source->vhdl->scope : semantic::ScopeId { };
        inferred_value.form = semantic::vhdl::DeclarationForm::constant;
        inferred_value.subtype = heap.designated_subtype;
        inferred_value.subtype->constraints.clear();
        inferred_value.subtype->unconstrained = true;
        inferred_value.initializer = value_id;
        std::size_t proven_width { };
        const auto range = hir_vhdl_unconstrained_constant_range(
            inferred_value, width.value_or(0U), hir_process_scope_,
            &proven_width);
        if (proven_width != 0U) {
            width = proven_width;
        }
        if (!range || !width || *width == 0U
            || *width % profile.element_width != 0U) {
            return (shaped_rejected("allocator value range"), std::nullopt);
        }
        const auto lowered = lower_hir_vhdl_shaped_value(value_id, profile, width);
        if (!lowered) {
            return std::nullopt;
        }
        std::vector<RegisterId> bounds;
        std::vector<std::uint8_t> directions;
        inner.insert(inner.begin(),
            HirPackedRange { range->left, range->right, range->descending });
        for (const auto& dimension : inner) {
            bounds.push_back(load_integer(
                dimension.left, 64U, frontend::ValueDomain::Integer));
            bounds.push_back(load_integer(
                dimension.right, 64U, frontend::ValueDomain::Integer));
            directions.push_back(dimension.descending ? 1U : 0U);
        }
        create(std::move(bounds), std::move(directions));
        process_.operations.emplace_back(VhdlShaped {
            VhdlShapedOperation::from_packed,
            container,
            { },
            *lowered,
            { },
            { },
            { },
            0U,
            VhdlShapedAttribute::length,
            0U,
            { },
        });
    }
    return issue_hir_vhdl_access_handle(
        heap, span, [&](const RegisterId handle) {
            process_.operations.emplace_back(ContainerElementWrite {
                heap.objects, handle, container, false, { } });
        });
}

} // namespace fsim::elaboration
