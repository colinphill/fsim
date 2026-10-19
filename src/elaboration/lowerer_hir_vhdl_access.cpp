// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
namespace {

bool declared_textio_line_type(
    const semantic::SpecializedHirUnit& unit,
    const semantic::vhdl::TypeDefinition& type)
{
    if (!type.declaration.valid()) {
        return false;
    }
    const auto declaration = unit.find_declaration(type.declaration);
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->scope.valid()) {
        return false;
    }
    return unit.design().vhdl_scope_in_package(
        declaration->vhdl->scope, "std", "textio");
}

bool compiler_owned_textio_line_type(
    const semantic::SpecializedHirUnit& unit,
    const semantic::vhdl::SubtypeIndication& subtype,
    const semantic::ScopeId use_scope,
    const semantic::vhdl::TypeDefinition* type)
{
    auto spelling = std::string_view { subtype.type_mark.spelling };
    spelling = simple_name_of(spelling);
    if (!equal_ignoring_ascii_case(spelling, "line")) {
        return false;
    }
    if (type != nullptr) {
        if (type->form != semantic::vhdl::TypeForm::access
            || !equal_ignoring_ascii_case(type->name, "line")) {
            return false;
        }
        if (type->declaration.valid()) {
            return declared_textio_line_type(unit, *type);
        }
    } else if (subtype.type_mark.target.valid()) {
        return false;
    }

    // STD.TEXTIO.LINE is compiler-owned but declaration-free in the fallback
    // environment. Require exact package visibility and compiler access-type
    // metadata so a declaration-backed user type named LINE cannot match.
    const semantic::CompiledDesignResolver resolver { unit };
    return resolver.vhdl_builtin_type_visible(
        subtype.type_mark.spelling, use_scope);
}
const semantic::vhdl::TypeDefinition* hir_vhdl_named_type(
    const semantic::SpecializedHirUnit& unit,
    const semantic::vhdl::SubtypeIndication& subtype,
    const semantic::vhdl::TypeForm required)
{
    const auto named_type = [&](std::string_view spelling)
        -> const semantic::vhdl::TypeDefinition* {
        spelling = simple_name_of(spelling);
        if (spelling.empty()) {
            return nullptr;
        }
        const semantic::vhdl::TypeDefinition* selected = nullptr;
        for (const auto& stored : unit.design().vhdl_hir.types()) {
            const auto candidate = unit.find_type(stored.id);
            if (!candidate || candidate->vhdl == nullptr
                || !equal_ignoring_ascii_case(
                    candidate->vhdl->name, spelling)) {
                continue;
            }
            if (selected != nullptr
                && selected->id != candidate->vhdl->id) {
                return nullptr;
            }
            selected = candidate->vhdl;
        }
        return selected;
    };

    auto type_id = subtype.type_mark.target;
    auto spelling = std::string_view { subtype.type_mark.spelling };
    std::unordered_set<std::uint32_t> visited;
    while (true) {
        if (!type_id.valid()) {
            const auto candidate = named_type(spelling);
            if (candidate == nullptr) {
                return nullptr;
            }
            type_id = candidate->id;
        }
        if (!visited.insert(type_id.value()).second) {
            return nullptr;
        }
        const auto type = unit.find_type(type_id);
        if (!type || type->vhdl == nullptr) {
            const auto candidate = named_type(spelling);
            if (candidate == nullptr || candidate->id == type_id) {
                return nullptr;
            }
            type_id = candidate->id;
            continue;
        }
        if (type->vhdl->form == required) {
            return type->vhdl;
        }
        if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
            && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
            return nullptr;
        }
        spelling = type->vhdl->base.type_mark.spelling;
        type_id = type->vhdl->base.type_mark.target;
    }
}

const semantic::vhdl::TypeDefinition* hir_vhdl_array_type(
    const semantic::SpecializedHirUnit& unit,
    semantic::TypeId type_id)
{
    const auto* definition = semantic::vhdl_base_type_definition(unit, type_id);
    return definition != nullptr
            && definition->form == semantic::vhdl::TypeForm::array
        ? definition
        : nullptr;
}

} // namespace

const semantic::vhdl::TypeDefinition*
Lowerer::hir_vhdl_access_type_definition(
    const semantic::vhdl::SubtypeIndication& subtype) const
{
    return specialized_hir_unit_ == nullptr
        ? nullptr
        : hir_vhdl_named_type(
              *specialized_hir_unit_, subtype,
              semantic::vhdl::TypeForm::access);
}

RegisterId Lowerer::issue_hir_vhdl_access_handle(
    const HirVhdlAccessHeap& heap, const frontend::SourceSpan& span,
    const std::function<void(RegisterId)>& store)
{
    const auto issued_size = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ContainerSize {
        issued_size, heap.issued_handles });
    const auto live_size = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ContainerSize {
        live_size, heap.objects });
    const auto maximum
        = load_constant(unsigned_value(heap.maximum_live_objects, 32U),
            frontend::ValueDomain::Bit2);
    const auto within_limit = allocate_register(
        1U, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary {
        BinaryOperator::less_unsigned, within_limit, live_size, maximum });
    process_.operations.emplace_back(Assert {
        within_limit,
        "VHDL access allocation exceeded its owning-storage limit",
        AssertionSeverity::failure,
        runtime_source_location(span),
    });
    // Handles are never reused: the next one is one past every handle
    // issued so far, so a stale handle stays detectable.
    const auto one
        = load_constant(unsigned_value(1U, 32U), frontend::ValueDomain::Bit2);
    const auto handle = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        BinaryOperator::add_unsigned, handle, issued_size, one });
    store(handle);
    const auto token
        = load_constant(unsigned_value(1U, 1U), frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(PushContainer {
        heap.issued_handles, token, false, std::nullopt });
    return handle;
}

void Lowerer::emit_hir_vhdl_access_live_check(const HirVhdlAccessHeap& heap,
    const RegisterId handle, const frontend::SourceSpan& span)
{
    const auto live_result = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ContainerExists {
        live_result, heap.objects, handle, false });
    const auto live = resize_register(live_result, 1U, false);
    process_.operations.emplace_back(Assert {
        live,
        "VHDL access dereference used a null, stale, or foreign handle",
        AssertionSeverity::failure,
        runtime_source_location(span),
    });
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_designated_subtype(
    const semantic::vhdl::SubtypeIndication& access_subtype) const
{
    const auto* type = hir_vhdl_access_type_definition(access_subtype);
    return type != nullptr ? hir_vhdl_access_shape_subtype(*type)
                           : std::nullopt;
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_access_shape_subtype(
    const semantic::vhdl::TypeDefinition& type) const
{
    // The heap fixes an open designated array's shape from its allocators
    // (IEEE 1076-2008 5.4.2); a STRING object keeps its own length.
    if (const auto heap = hir_vhdl_access_heaps_.find(type.id.value());
        heap != hir_vhdl_access_heaps_.end()
        && heap->second.designated_subtype.domain
            != semantic::vhdl::ValueDomain::string) {
        return heap->second.designated_subtype;
    }
    return type.designated_subtype
        ? hir_effective_vhdl_subtype(*type.designated_subtype)
        : std::nullopt;
}

bool Lowerer::is_hir_vhdl_access_expression(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return false;
    }
    const auto& source = *expression->vhdl;
    if (source.kind == semantic::vhdl::ExpressionKind::call) {
        return source.text == "@vhdl-null"
            || source.text == "@vhdl-new"
            || source.text == "@vhdl-new-qualified"
            || source.text == "@vhdl-dereference";
    }
    if (source.kind != semantic::vhdl::ExpressionKind::binary
        || source.operands.size() != 2U
        || (source.text != "=" && source.text != "/=")) {
        return false;
    }
    return std::ranges::any_of(
        source.operands, [&](const semantic::ExpressionId operand_id) {
            const auto operand = specialized_hir_unit_->find_expression(
                operand_id);
            if (operand && operand->vhdl != nullptr
                && operand->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call
                && operand->vhdl->text == "@vhdl-null") {
                return true;
            }
            const auto subtype = hir_vhdl_expression_subtype(operand_id);
            return subtype
                && hir_vhdl_named_type(
                    *specialized_hir_unit_, *subtype,
                    semantic::vhdl::TypeForm::access)
                    != nullptr;
        });
}

Lowerer::HirVhdlAccessHeap* Lowerer::hir_vhdl_access_heap(
    const semantic::vhdl::TypeDefinition& type,
    const frontend::SourceSpan& span,
    const semantic::vhdl::SubtypeIndication* const access_subtype,
    const std::optional<semantic::ExpressionId> initializer,
    const std::span<const semantic::vhdl::RangeConstraint>
        allocator_constraints)
{
    if (!type.designated_subtype) {
        report(
            "FSIM-ELAB-VHACCESS-005",
            "a VHDL access operation requires one resolved designated subtype",
            span);
        return nullptr;
    }

    auto designated = hir_effective_vhdl_subtype(
        *type.designated_subtype).value_or(*type.designated_subtype);
    const auto* array = hir_vhdl_array_type(
        *specialized_hir_unit_, designated.type_mark.target);

    const bool string_payload
        = designated.domain == semantic::vhdl::ValueDomain::string;
    if (string_payload && !hir_vhdl_predefined_string_type(designated)) {
        report(
            "FSIM-ELAB-VHACCESS-024",
            "only the predefined VHDL STRING designated subtype has string storage",
            span);
        return nullptr;
    }

    const auto normalize_range = [&](semantic::vhdl::RangeConstraint range)
        -> std::optional<semantic::vhdl::RangeConstraint> {
        if (range.kind != semantic::vhdl::RangeKind::array_index) {
            return std::nullopt;
        }
        const auto left = hir_range_left(range);
        const auto right = hir_range_right(range);
        const auto direction = range.direction_from_bounds
            ? hir_vhdl_range_direction(range, hir_process_scope_)
            : std::optional { range.descending };
        if (!left || !right || !direction) {
            return std::nullopt;
        }
        range.left = *left;
        range.right = *right;
        range.left_expression.reset();
        range.right_expression.reset();
        range.descending = *direction;
        range.direction_from_bounds = false;
        range.null = *direction ? *left < *right : *left > *right;
        return range;
    };
    const auto index_range = [&](const auto& constraints)
        -> std::optional<semantic::vhdl::RangeConstraint> {
        const auto found = std::ranges::find_if(
            constraints, [](const semantic::vhdl::RangeConstraint& range) {
                return range.kind
                    == semantic::vhdl::RangeKind::array_index;
            });
        return found == constraints.end()
            ? std::nullopt : normalize_range(*found);
    };
    const auto same_range = [](
        const semantic::vhdl::RangeConstraint& left,
        const semantic::vhdl::RangeConstraint& right) {
        return left.left == right.left && left.right == right.right
            && left.descending == right.descending
            && left.null == right.null;
    };

    // A fixed multidimensional array has one packed storage value only when
    // every declared dimension and its scalar leaf produce a complete layout.
    // Keep open/ragged array-of-array allocation on the existing rejection
    // path; it needs per-object recursive shape metadata.
    std::vector<semantic::vhdl::RangeConstraint>
        fixed_multidimensional_ranges;
    std::optional<std::uint64_t> fixed_multidimensional_element_count;
    if (array != nullptr && array->array_dimensions.size() > 1U) {
        const auto element = array->element_subtype
            ? hir_effective_vhdl_subtype(*array->element_subtype)
            : std::nullopt;
        const auto element_width = element
            ? vhdl_subtype_width(*element)
            : std::nullopt;
        const auto* nested_array = element
                && element->type_mark.target.valid()
            ? hir_vhdl_array_type(
                  *specialized_hir_unit_, element->type_mark.target)
            : nullptr;
        auto index_constraints = std::vector<
            semantic::vhdl::RangeConstraint> { };
        for (const auto& constraint : designated.constraints) {
            if (constraint.kind
                == semantic::vhdl::RangeKind::array_index) {
                index_constraints.push_back(constraint);
            }
        }
        bool fixed_shape = array->element_subtype.has_value()
            && element.has_value() && nested_array == nullptr && element_width
            && *element_width != 0U
            && element->domain != semantic::vhdl::ValueDomain::unknown
            && element->domain != semantic::vhdl::ValueDomain::string
            && element->domain != semantic::vhdl::ValueDomain::logic9
            && index_constraints.size()
                <= array->array_dimensions.size();
        std::uint64_t element_count { 1U };
        if (fixed_shape) {
            fixed_multidimensional_ranges.reserve(
                array->array_dimensions.size());
            for (std::size_t dimension { };
                 dimension < array->array_dimensions.size(); ++dimension) {
                const auto& array_dimension
                    = array->array_dimensions[dimension];
                if (array_dimension.unconstrained) {
                    fixed_shape = false;
                    break;
                }
                const auto* source_range
                    = dimension < index_constraints.size()
                    ? &index_constraints[dimension]
                    : array_dimension.constraint
                    ? &*array_dimension.constraint
                    : nullptr;
                const auto range = source_range != nullptr
                    ? normalize_range(*source_range)
                    : std::nullopt;
                if (!range || !range->left || !range->right
                    || range->null) {
                    fixed_shape = false;
                    break;
                }
                const auto distance = index_distance(
                    *range->left, *range->right);
                if (distance
                        == std::numeric_limits<std::uint64_t>::max()
                    || distance + 1U == 0U
                    || element_count
                        > std::numeric_limits<std::uint64_t>::max()
                            / (distance + 1U)) {
                    fixed_shape = false;
                    break;
                }
                element_count *= distance + 1U;
                fixed_multidimensional_ranges.push_back(*range);
            }
        }
        const auto designated_width = vhdl_subtype_width(designated);
        if (fixed_shape && designated_width
            && element_count
                <= std::numeric_limits<std::uint64_t>::max()
                    / *element_width
            && element_count * *element_width == *designated_width) {
            fixed_multidimensional_element_count = element_count;
        } else {
            fixed_multidimensional_ranges.clear();
            report(
                "FSIM-ELAB-VHACCESS-024",
                "a multidimensional VHDL access object requires one fully constrained packed layout",
                span);
            return nullptr;
        }
    }

    std::vector<semantic::vhdl::RangeConstraint>
        normalized_allocator_ranges;
    normalized_allocator_ranges.reserve(allocator_constraints.size());
    for (const auto& allocator_range : allocator_constraints) {
        const auto normalized = normalize_range(allocator_range);
        if (!normalized) {
            report(
                "FSIM-ELAB-VHACCESS-006",
                "an explicit VHDL allocator range requires static array-index bounds",
                span);
            return nullptr;
        }
        normalized_allocator_ranges.push_back(*normalized);
    }

    const auto access_context = access_subtype != nullptr
        ? hir_effective_vhdl_subtype(*access_subtype)
        : std::nullopt;
    const semantic::vhdl::TypeDefinition* contextual_access_type
        = access_context
        ? hir_vhdl_named_type(
              *specialized_hir_unit_, *access_context,
              semantic::vhdl::TypeForm::access)
        : nullptr;
    if (access_context != std::nullopt
        && (contextual_access_type == nullptr
            || contextual_access_type->id != type.id)) {
        report(
            "FSIM-ELAB-VHACCESS-023",
            "the allocator's access subtype does not identify its resolved access type",
            span);
        return nullptr;
    }

    auto declared_range = index_range(designated.constraints);
    auto context_range = access_context
        ? index_range(access_context->constraints)
        : std::nullopt;
    if (!declared_range && array != nullptr
        && !array->array_dimensions.empty()
        && array->array_dimensions.front().constraint) {
        declared_range = normalize_range(
            *array->array_dimensions.front().constraint);
    }
    if (declared_range && context_range
        && !same_range(*declared_range, *context_range)) {
        report(
            "FSIM-ELAB-VHACCESS-023",
            "a single access type cannot use multiple fixed designated-object shapes in this representation",
            span);
        return nullptr;
    }
    auto object_range = declared_range ? declared_range : context_range;
    if (!fixed_multidimensional_ranges.empty() && !object_range) {
        object_range = fixed_multidimensional_ranges.front();
    }
    if (!normalized_allocator_ranges.empty()) {
        const bool predefined_bit_vector
            = !designated.type_mark.target.valid()
            && designated.builtin_type
                == semantic::vhdl::BuiltinTypeIdentity::
                    std_standard_bit_vector;
        const bool one_dimensional_array = predefined_bit_vector
            || string_payload
            || (array != nullptr && array->array_dimensions.size() == 1U);
        if (!one_dimensional_array
            || normalized_allocator_ranges.size() != 1U) {
            report(
                "FSIM-ELAB-VHACCESS-006",
                "explicit allocator bounds require one index range of a one-dimensional array",
                span);
            return nullptr;
        }
        const auto& allocator_range = normalized_allocator_ranges.front();
        const auto integer_index_base
            = frontend::vhdl_predefined_integer_range(
                vhdl_standard_, "integer");
        const auto natural_index_subtype
            = frontend::vhdl_predefined_integer_range(
                vhdl_standard_, "natural");
        const bool endpoints_in_integer_base
            = allocator_range.left && allocator_range.right
            && integer_index_base.contains(*allocator_range.left)
            && integer_index_base.contains(*allocator_range.right);
        const bool endpoints_in_natural
            = allocator_range.left && allocator_range.right
            && natural_index_subtype.contains(*allocator_range.left)
            && natural_index_subtype.contains(*allocator_range.right);
        const auto positive_index_subtype
            = frontend::vhdl_predefined_integer_range(
                vhdl_standard_, "positive");
        const bool endpoints_in_positive
            = allocator_range.left && allocator_range.right
            && positive_index_subtype.contains(*allocator_range.left)
            && positive_index_subtype.contains(*allocator_range.right);
        // BIT_VECTOR is indexed by NATURAL and STRING by POSITIVE (16.3).
        if ((predefined_bit_vector || string_payload)
            && (!endpoints_in_integer_base
                || (!allocator_range.null
                    && !(string_payload ? endpoints_in_positive
                                        : endpoints_in_natural)))) {
            report(
                "FSIM-ELAB-VHACCESS-023",
                "the BIT_VECTOR allocator bounds are outside its predefined NATURAL index subtype",
                span);
            return nullptr;
        }
        if (object_range && !same_range(*object_range, allocator_range)) {
            report(
                "FSIM-ELAB-VHACCESS-023",
                "explicit VHDL allocator bounds do not match the designated subtype",
                span);
            return nullptr;
        }
        object_range = allocator_range;
    }

    const bool open_array = designated.unconstrained
        || (array != nullptr
            && !array->array_dimensions.empty()
            && array->array_dimensions.front().unconstrained);
    std::optional<std::size_t> initializer_width;
    std::optional<semantic::vhdl::RangeConstraint> value_range;
    if (initializer) {
        const auto source = specialized_hir_unit_->find_expression(*initializer);
        initializer_width = hir_expression_width(
            *initializer, hir_process_scope_, &designated);
        if (open_array && source && source->vhdl != nullptr) {
            auto inference_expression = *initializer;
            auto inference_source = source;
            // The allocator operand is a qualified expression such as
            // BIT_VECTOR'("1111"). Its positional value supplies bounds
            // using the designated array's index subtype; do not ask the
            // constant folder to erase that aggregate/value shape first.
            for (std::size_t depth = 0U; depth < 16U; ++depth) {
                if (!inference_source || inference_source->vhdl == nullptr
                    || inference_source->vhdl->kind
                        != semantic::vhdl::ExpressionKind::call
                    || !inference_source->vhdl->text.starts_with(
                        semantic::vhdl::qualified_expression_prefix)
                    || inference_source->vhdl->operands.size() != 1U) {
                    break;
                }
                const auto qualified_subtype
                    = hir_vhdl_expression_subtype(inference_expression);
                const auto* qualified_array = qualified_subtype
                    ? hir_vhdl_named_type(
                          *specialized_hir_unit_, *qualified_subtype,
                          semantic::vhdl::TypeForm::array)
                    : nullptr;
                const auto* designated_array = hir_vhdl_named_type(
                    *specialized_hir_unit_, designated,
                    semantic::vhdl::TypeForm::array);
                if (qualified_array == nullptr
                    || designated_array == nullptr
                    || qualified_array->id != designated_array->id) {
                    break;
                }
                inference_expression
                    = inference_source->vhdl->operands.front();
                inference_source = specialized_hir_unit_->find_expression(
                    inference_expression);
            }
            if (inference_source && inference_source->vhdl != nullptr) {
                auto inference_subtype = *type.designated_subtype;
                inference_subtype.constraints.clear();
                inference_subtype.unconstrained = true;
                semantic::vhdl::Declaration inferred_value;
                inferred_value.scope = inference_source->vhdl->scope;
                inferred_value.form
                    = semantic::vhdl::DeclarationForm::constant;
                inferred_value.subtype = inference_subtype;
                inferred_value.initializer = inference_expression;
                std::size_t proven_width { };
                if (const auto inferred = hir_vhdl_unconstrained_constant_range(
                        inferred_value,
                        initializer_width.value_or(0U),
                        hir_process_scope_,
                        &proven_width)) {
                    if (proven_width != 0U) {
                        initializer_width = proven_width;
                    }
                    semantic::vhdl::RangeConstraint range;
                    range.kind = semantic::vhdl::RangeKind::array_index;
                    range.left = inferred->left;
                    range.right = inferred->right;
                    range.descending = inferred->descending;
                    range.source = inference_source->vhdl->source;
                    range.null = false;
                    value_range = std::move(range);
                }
            }
        }
    }

    // A STRING value's literal or constant initializer gives its range
    // (9.3.7, 5.3.2.2).
    if (string_payload && !object_range && !value_range && initializer) {
        auto value_id = *initializer;
        for (std::size_t depth = 0U; depth < 16U; ++depth) {
            const auto qualified = specialized_hir_unit_->find_expression(
                value_id);
            if (!qualified || qualified->vhdl == nullptr
                || qualified->vhdl->kind
                    != semantic::vhdl::ExpressionKind::call
                || !qualified->vhdl->text.starts_with(
                    semantic::vhdl::qualified_expression_prefix)
                || qualified->vhdl->operands.size() != 1U) {
                break;
            }
            value_id = qualified->vhdl->operands.front();
        }
        const auto value = specialized_hir_unit_->find_expression(value_id);
        std::optional<HirPackedRange> literal_range;
        if (value && value->vhdl != nullptr
            && value->vhdl->kind
                == semantic::vhdl::ExpressionKind::string_literal
            && value->vhdl->decoded_string) {
            literal_range = hir_vhdl_string_literal_range(
                *value->vhdl->decoded_string);
        } else if (value && value->vhdl != nullptr
            && value->vhdl->kind == semantic::vhdl::ExpressionKind::name) {
            literal_range = hir_vhdl_constant_string_literal_range(value_id);
        }
        if (literal_range) {
            semantic::vhdl::RangeConstraint range;
            range.kind = semantic::vhdl::RangeKind::array_index;
            range.left = literal_range->left;
            range.right = literal_range->right;
            range.descending = literal_range->descending;
            range.null = literal_range->descending
                ? literal_range->left < literal_range->right
                : literal_range->left > literal_range->right;
            value_range = std::move(range);
        }
    }
    if (object_range && value_range
        && !same_range(*object_range, *value_range)) {
        report(
            "FSIM-ELAB-VHACCESS-023",
            "the qualified allocator value does not satisfy the fixed access-subtype bounds",
            span);
        return nullptr;
    }
    if (!object_range && value_range) {
        object_range = value_range;
    }
    // `new T'(a.all)` with `a` of this access type copies an object of the
    // heap's own shape.
    const bool same_heap_initializer = !object_range && initializer && [&] {
        auto value_id = *initializer;
        for (std::size_t depth = 0U; depth < 16U; ++depth) {
            const auto qualified = specialized_hir_unit_->find_expression(
                value_id);
            if (!qualified || qualified->vhdl == nullptr
                || qualified->vhdl->kind
                    != semantic::vhdl::ExpressionKind::call
                || !qualified->vhdl->text.starts_with(
                    semantic::vhdl::qualified_expression_prefix)
                || qualified->vhdl->operands.size() != 1U) {
                break;
            }
            value_id = qualified->vhdl->operands.front();
        }
        const auto value = specialized_hir_unit_->find_expression(value_id);
        if (!value || value->vhdl == nullptr
            || value->vhdl->kind != semantic::vhdl::ExpressionKind::call
            || (value->vhdl->text != "@vhdl-dereference"
                && value->vhdl->text != "@vhdl-member:all")
            || value->vhdl->operands.size() != 1U) {
            return false;
        }
        const auto prefix_subtype = hir_vhdl_expression_subtype(
            value->vhdl->operands.front());
        const auto* prefix_type = prefix_subtype
            ? hir_vhdl_access_type_definition(*prefix_subtype)
            : nullptr;
        return prefix_type != nullptr && prefix_type->id == type.id;
    }();
    if (!object_range && (!initializer || same_heap_initializer)) {
        if (const auto found = hir_vhdl_access_heaps_.find(type.id.value());
            found != hir_vhdl_access_heaps_.end()) {
            return &found->second;
        }
    }
    // STRING objects keep their own lengths in string storage.
    if (open_array && !object_range && !string_payload) {
        report(
            "FSIM-ELAB-VHACCESS-006",
            "an unconstrained VHDL access object requires one statically resolved rank-one index range",
            span);
        return nullptr;
    }
    if (object_range) {
        const auto current = index_range(designated.constraints);
        if (current) {
            const auto found = std::ranges::find_if(
                designated.constraints,
                [](const semantic::vhdl::RangeConstraint& range) {
                    return range.kind
                        == semantic::vhdl::RangeKind::array_index;
                });
            *found = *object_range;
        } else {
            designated.constraints.insert(
                designated.constraints.begin(), *object_range);
        }
        designated.unconstrained = false;
    }
    if (!fixed_multidimensional_ranges.empty()) {
        std::size_t dimension { };
        for (auto& constraint : designated.constraints) {
            if (constraint.kind
                != semantic::vhdl::RangeKind::array_index) {
                continue;
            }
            if (dimension >= fixed_multidimensional_ranges.size()) {
                break;
            }
            constraint = fixed_multidimensional_ranges[dimension++];
        }
        while (dimension < fixed_multidimensional_ranges.size()) {
            designated.constraints.push_back(
                fixed_multidimensional_ranges[dimension++]);
        }
        designated.unconstrained = false;
    }

    if (designated.domain == semantic::vhdl::ValueDomain::logic9) {
        report(
            "FSIM-ELAB-VHACCESS-007",
            "nine-state designated objects require the later access-object state extension",
            span);
        return nullptr;
    }

    std::optional<std::size_t> width = vhdl_subtype_width(designated);
    const bool predefined_bit_vector
        = !designated.type_mark.target.valid()
        && designated.builtin_type
            == semantic::vhdl::BuiltinTypeIdentity::
                std_standard_bit_vector;
    // An open one-dimensional array object is as wide as its range's
    // elements (5.3.2.2).
    std::optional<std::size_t> open_element_width;
    if (predefined_bit_vector) {
        open_element_width = 1U;
    } else if (!string_payload && open_array && array != nullptr
        && array->array_dimensions.size() == 1U && array->element_subtype) {
        const auto element = hir_effective_vhdl_subtype(
            *array->element_subtype);
        open_element_width = element ? vhdl_subtype_width(*element)
                                     : std::nullopt;
    }
    if (open_element_width && *open_element_width != 0U && open_array
        && object_range && object_range->left && object_range->right) {
        const auto distance = index_distance(
            *object_range->left, *object_range->right);
        if (!object_range->null
            && distance == std::numeric_limits<std::uint64_t>::max()) {
            report(
                "FSIM-ELAB-VHACCESS-006",
                "the VHDL BIT_VECTOR access-object length exceeds the supported bound",
                span);
            return nullptr;
        }
        const auto extent = object_range->null
            ? std::uint64_t { } : distance + 1U;
        if (extent > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-VHACCESS-006",
                "the VHDL BIT_VECTOR access-object length exceeds the supported bound",
                span);
            return nullptr;
        }
        if (extent > std::numeric_limits<std::uint32_t>::max()
                / *open_element_width) {
            report(
                "FSIM-ELAB-VHACCESS-006",
                "the VHDL access-object width exceeds the supported bound",
                span);
            return nullptr;
        }
        width = static_cast<std::size_t>(extent) * *open_element_width;
        designated.executable_width = *width;
    }
    if (!string_payload && (open_array || !width) && initializer_width) {
        width = initializer_width;
        designated.executable_width = *width;
    }
    if (!string_payload && (!width || *width == 0U
        || *width > std::numeric_limits<std::uint32_t>::max())) {
        report(
            "FSIM-ELAB-VHACCESS-006",
            "a bounded VHDL allocator requires a concrete nonempty packed designated subtype",
            span);
        return nullptr;
    }

    if (object_range && !object_range->null && !string_payload
        && array != nullptr && array->element_subtype) {
        const auto element = hir_effective_vhdl_subtype(
            *array->element_subtype);
        const auto element_width = element
            ? vhdl_subtype_width(*element)
            : std::nullopt;
        const auto distance = index_distance(
            *object_range->left, *object_range->right);
        if (distance == std::numeric_limits<std::uint64_t>::max()) {
            report(
                "FSIM-ELAB-VHACCESS-023",
                "the fixed VHDL access-object bounds exceed the supported extent",
                span);
            return nullptr;
        }
        const auto count = fixed_multidimensional_element_count
            ? *fixed_multidimensional_element_count
            : distance + 1U;
        if (element_width && *element_width != 0U
            && count <= std::numeric_limits<std::size_t>::max()
                    / *element_width
            && count * *element_width != *width) {
            report(
                "FSIM-ELAB-VHACCESS-023",
                "the fixed VHDL access-object bounds do not match its packed width",
                span);
            return nullptr;
        }
    }

    if (const auto found = hir_vhdl_access_heaps_.find(type.id.value());
        found != hir_vhdl_access_heaps_.end()) {
        const auto existing_range = index_range(
            found->second.designated_subtype.constraints);
        if (!string_payload && object_range && existing_range
            && !same_range(*object_range, *existing_range)) {
            report(
                "FSIM-ELAB-VHACCESS-023",
                "this access type is already represented with different fixed bounds",
                span);
            return nullptr;
        }
        if (!fixed_multidimensional_ranges.empty()) {
            std::vector<semantic::vhdl::RangeConstraint>
                existing_multidimensional_ranges;
            for (const auto& constraint :
                found->second.designated_subtype.constraints) {
                if (constraint.kind
                    != semantic::vhdl::RangeKind::array_index) {
                    continue;
                }
                const auto normalized = normalize_range(constraint);
                if (!normalized) {
                    existing_multidimensional_ranges.clear();
                    break;
                }
                existing_multidimensional_ranges.push_back(*normalized);
            }
            bool same_multidimensional_shape
                = existing_multidimensional_ranges.size()
                == fixed_multidimensional_ranges.size();
            for (std::size_t dimension { };
                 same_multidimensional_shape
                     && dimension < fixed_multidimensional_ranges.size();
                 ++dimension) {
                same_multidimensional_shape = same_range(
                    existing_multidimensional_ranges[dimension],
                    fixed_multidimensional_ranges[dimension]);
            }
            if (!same_multidimensional_shape) {
                report(
                    "FSIM-ELAB-VHACCESS-023",
                    "this access type is already represented with different multidimensional bounds",
                    span);
                return nullptr;
            }
        }
        const auto existing_width
            = vhdl_subtype_width(found->second.designated_subtype);
        if (!string_payload && width && existing_width
            && *width != *existing_width) {
            report(
                "FSIM-ELAB-VHACCESS-023",
                "this access type is already represented with a different packed width",
                span);
            return nullptr;
        }
        return &found->second;
    }

    ContainerType objects_type;
    objects_type.element_kind = string_payload
        ? ContainerElementKind::String : ContainerElementKind::Packed;
    objects_type.element_width = string_payload
        ? 1U : static_cast<std::uint32_t>(*width);
    objects_type.two_state = string_payload
        || is_two_state_domain(vhdl_value_domain(designated.domain));
    objects_type.signed_elements = designated.signed_value;
    objects_type.associative = true;
    objects_type.index_width = 32U;
    objects_type.two_state_indices = true;
    objects_type.element_nominal_type = designated.type_mark.spelling;
    const auto maximum = std::min<std::uint64_t>(
        maximum_container_elements(objects_type),
        type.maximum_objects);
    const auto objects = allocate_container_register(objects_type);
    ContainerType ledger_type;
    ledger_type.element_width = 1U;
    ledger_type.two_state = true;
    ledger_type.queue = true;
    ledger_type.maximum_elements = std::min<std::uint64_t>(
        maximum_container_elements(ledger_type),
        std::numeric_limits<std::uint32_t>::max());
    const auto ledger = allocate_container_register(ledger_type);
    const auto [inserted, unused] = hir_vhdl_access_heaps_.emplace(
        type.id.value(), HirVhdlAccessHeap {
            objects, ledger, maximum, designated });
    (void)unused;
    return &inserted->second;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_access_object(
    const RegisterId handle,
    const semantic::vhdl::SubtypeIndication& access_subtype,
    const frontend::SourceSpan& span)
{
    const auto* type = hir_vhdl_named_type(
        *specialized_hir_unit_, access_subtype,
        semantic::vhdl::TypeForm::access);
    if (type == nullptr || !type->designated_subtype) {
        return std::nullopt;
    }
    auto* heap = hir_vhdl_access_heap(
        *type, span, &access_subtype);
    if (heap == nullptr
        || heap->designated_subtype.domain
            == semantic::vhdl::ValueDomain::string) {
        return std::nullopt;
    }
    const auto width = vhdl_subtype_width(heap->designated_subtype);
    if (!width || *width == 0U) {
        return std::nullopt;
    }
    emit_hir_vhdl_access_live_check(
        *heap, handle, span);
    const auto result = allocate_register(
        *width, vhdl_value_domain(heap->designated_subtype.domain));
    process_.operations.emplace_back(ContainerRead {
        result, heap->objects, handle, false });
    return result;
}

std::optional<StringRegisterId>
Lowerer::lower_hir_vhdl_access_string_object(
    const semantic::ExpressionId dereference_id)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(dereference_id)
        : std::nullopt;
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::call
        || (expression->vhdl->text != "@vhdl-dereference"
            && expression->vhdl->text != "@vhdl-member:all")
        || expression->vhdl->operands.size() != 1U) {
        return std::nullopt;
    }
    return lower_hir_vhdl_access_string_handle(
        expression->vhdl->operands.front(),
        hir_source_span(expression->vhdl->source));
}

std::optional<StringRegisterId>
Lowerer::lower_hir_vhdl_access_string_handle(
    const semantic::ExpressionId prefix, const frontend::SourceSpan& span)
{
    const auto access_subtype = hir_vhdl_expression_subtype(prefix);
    const auto* type = access_subtype
        ? hir_vhdl_access_type_definition(*access_subtype)
        : nullptr;
    const auto designated = type != nullptr && type->designated_subtype
        ? hir_effective_vhdl_subtype(*type->designated_subtype)
        : std::nullopt;
    if (type == nullptr || !designated
        || designated->domain != semantic::vhdl::ValueDomain::string
        || !hir_vhdl_predefined_string_type(*designated)) {
        return std::nullopt;
    }
    auto* heap = hir_vhdl_access_heap(
        *type, span, &*access_subtype);
    const auto handle = lower_hir_expression(prefix, 32U);
    if (heap == nullptr || !handle
        || heap->designated_subtype.domain
            != semantic::vhdl::ValueDomain::string) {
        return std::nullopt;
    }
    emit_hir_vhdl_access_live_check(
        *heap, *handle, span);
    const auto result = allocate_string_register();
    process_.operations.emplace_back(ContainerStringRead {
        result, heap->objects, *handle, false, false, false, { } });
    return result;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_access_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width,
    const semantic::vhdl::SubtypeIndication* expected_access_subtype,
    const bool implicit_dereference)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto span = hir_source_span(source.source);
    const auto access_type = [&](const semantic::ExpressionId value)
        -> const semantic::vhdl::TypeDefinition* {
        const auto subtype = hir_vhdl_expression_subtype(value);
        if (!subtype) {
            return nullptr;
        }
        // STD.TEXTIO.LINE is reached through its access definition.
        const auto* named = hir_vhdl_named_type(*specialized_hir_unit_,
            *subtype, semantic::vhdl::TypeForm::access);
        return named != nullptr ? named
                                : hir_vhdl_access_type_definition(*subtype);
    };
    const auto allocated_type = [&]()
        -> const semantic::vhdl::TypeDefinition* {
        if (source.operands.empty()) {
            return nullptr;
        }
        const auto subtype_expression
            = specialized_hir_unit_->find_expression(
                source.operands.front());
        constexpr std::string_view prefix { "@vhdl-subtype:" };
        if (!subtype_expression
            || subtype_expression->vhdl == nullptr
            || !subtype_expression->vhdl->text.starts_with(prefix)) {
            return nullptr;
        }
        const auto designated_name = std::string_view {
            subtype_expression->vhdl->text
        }.substr(prefix.size());
        const auto resolver = hir_resolver();
        using BuiltinType = semantic::vhdl::BuiltinTypeIdentity;
        using ScalarType = semantic::CompiledVhdlStandardScalarIdentity;
        struct NominalTypeIdentity {
            std::optional<semantic::TypeId> type;
            BuiltinType builtin { BuiltinType::none };
            ScalarType standard_scalar { ScalarType::none };
        };
        const auto nominal_base_type = [&](semantic::TypeId type_id)
            -> std::optional<NominalTypeIdentity> {
            std::unordered_set<std::uint32_t> visited;
            while (type_id.valid()) {
                if (!visited.insert(type_id.value()).second) {
                    return std::nullopt;
                }
                const auto type = specialized_hir_unit_->find_type(type_id);
                const auto& semantic_types
                    = specialized_hir_unit_->design().semantics.types();
                const auto semantic_type = std::ranges::find(
                    semantic_types, type_id, &semantic::Type::id);
                if (!type || type->vhdl == nullptr
                    || semantic_type == semantic_types.end()) {
                    return std::nullopt;
                }
                const auto& definition = *type->vhdl;
                if (definition.form
                        != semantic::vhdl::TypeForm::subtype
                    && definition.form
                        != semantic::vhdl::TypeForm::alias) {
                    return NominalTypeIdentity { type_id,
                        BuiltinType::none, ScalarType::none };
                }
                if (definition.form
                        == semantic::vhdl::TypeForm::subtype
                    && semantic_type->kind
                        == semantic::TypeKind::declaration) {
                    // A user-declared scalar integer type is HIR-subtype
                    // shaped but remains a distinct nominal declaration.
                    return NominalTypeIdentity {
                        type_id, BuiltinType::none, ScalarType::none
                    };
                }
                auto base_type = definition.base.type_mark.target;
                if (!base_type.valid()) {
                    const auto declaration
                        = specialized_hir_unit_->find_declaration(
                            definition.declaration);
                    if (!declaration || declaration->vhdl == nullptr) {
                        return std::nullopt;
                    }
                    const auto resolved = resolver.resolve_vhdl_type_identity(
                        definition.base.type_mark.spelling,
                        declaration->vhdl->scope);
                    if (resolved.status
                        != semantic::CompiledResolutionStatus::unique) {
                        return std::nullopt;
                    }
                    if (resolved.builtin_type != BuiltinType::none) {
                        return NominalTypeIdentity { std::nullopt,
                            resolved.builtin_type, ScalarType::none };
                    }
                    if (resolved.standard_scalar_type != ScalarType::none) {
                        return NominalTypeIdentity { std::nullopt,
                            BuiltinType::none,
                            resolved.standard_scalar_type };
                    }
                    if (!resolved.type) {
                        return std::nullopt;
                    }
                    base_type = *resolved.type;
                }
                type_id = base_type;
            }
            return std::nullopt;
        };
        const auto access_declaration_scope = [&](const auto& definition)
            -> std::optional<semantic::ScopeId> {
            const auto declaration = specialized_hir_unit_->find_declaration(
                definition.declaration);
            if (!declaration || declaration->vhdl == nullptr) {
                return std::nullopt;
            }
            return declaration->vhdl->scope;
        };
        const auto allocated_designated_type
            = resolver.resolve_vhdl_type_identity(
                designated_name, source.scope);
        const auto same_designated_base_type = [&](
            const semantic::vhdl::SubtypeIndication& candidate,
            const semantic::ScopeId candidate_scope) {
            if (allocated_designated_type.status
                != semantic::CompiledResolutionStatus::unique) {
                return false;
            }
            const auto allocated_identity
                = allocated_designated_type.builtin_type
                    != BuiltinType::none
                ? std::optional<NominalTypeIdentity> {
                    NominalTypeIdentity { std::nullopt,
                        allocated_designated_type.builtin_type,
                        ScalarType::none }
                }
                : allocated_designated_type.standard_scalar_type
                        != ScalarType::none
                ? std::optional<NominalTypeIdentity> {
                    NominalTypeIdentity { std::nullopt,
                        BuiltinType::none,
                        allocated_designated_type.standard_scalar_type }
                }
                : allocated_designated_type.type
                ? nominal_base_type(*allocated_designated_type.type)
                : std::nullopt;
            if (!allocated_identity) {
                return false;
            }
            auto candidate_identity = candidate.builtin_type
                    != BuiltinType::none
                ? std::optional<NominalTypeIdentity> {
                    NominalTypeIdentity { std::nullopt,
                        candidate.builtin_type, ScalarType::none }
                }
                : std::nullopt;
            auto candidate_type = candidate.type_mark.target;
            if (!candidate_identity && !candidate_type.valid()) {
                const auto resolved = resolver.resolve_vhdl_type_identity(
                    candidate.type_mark.spelling, candidate_scope);
                if (resolved.status
                    != semantic::CompiledResolutionStatus::unique) {
                    return false;
                }
                if (resolved.builtin_type != BuiltinType::none) {
                    candidate_identity = NominalTypeIdentity {
                        std::nullopt, resolved.builtin_type,
                        ScalarType::none };
                } else if (resolved.standard_scalar_type
                    != ScalarType::none) {
                    candidate_identity = NominalTypeIdentity {
                        std::nullopt, BuiltinType::none,
                        resolved.standard_scalar_type };
                } else if (resolved.type) {
                    candidate_type = *resolved.type;
                }
            }
            // Subtype and alias declarations preserve the base type identity,
            // while declared types remain nominal even when their HIR form is
            // subtype-shaped (for example, a user-defined integer type).
            if (!candidate_identity && candidate_type.valid()) {
                candidate_identity = nominal_base_type(candidate_type);
            }
            return candidate_identity
                && candidate_identity->builtin
                    == allocated_identity->builtin
                && candidate_identity->standard_scalar
                    == allocated_identity->standard_scalar
                && candidate_identity->type == allocated_identity->type;
        };
        const auto* contextual_type = expected_access_subtype != nullptr
            ? hir_vhdl_named_type(
                  *specialized_hir_unit_, *expected_access_subtype,
                  semantic::vhdl::TypeForm::access)
            : nullptr;
        if (contextual_type != nullptr
            && contextual_type->designated_subtype) {
            const auto candidate_scope
                = access_declaration_scope(*contextual_type);
            if (candidate_scope
                && same_designated_base_type(
                    *contextual_type->designated_subtype,
                    *candidate_scope)) {
                return contextual_type;
            }
        }
        const semantic::vhdl::TypeDefinition* selected = nullptr;
        for (const auto& stored :
            specialized_hir_unit_->design().vhdl_hir.types()) {
            const auto candidate = specialized_hir_unit_->find_type(
                stored.id);
            if (!candidate || candidate->vhdl == nullptr
                || candidate->vhdl->form
                    != semantic::vhdl::TypeForm::access
                || !candidate->vhdl->designated_subtype) {
                continue;
            }
            const auto candidate_scope
                = access_declaration_scope(*candidate->vhdl);
            if (!candidate_scope
                || !same_designated_base_type(
                    *candidate->vhdl->designated_subtype,
                    *candidate_scope)) {
                continue;
            }
            if (selected != nullptr
                && selected->id != candidate->vhdl->id) {
                return nullptr;
            }
            selected = candidate->vhdl;
        }
        return selected;
    };
    if (source.kind == semantic::vhdl::ExpressionKind::binary
        && source.operands.size() == 2U
        && (source.text == "=" || source.text == "/=")) {
        // `null` takes the other operand's access type (IEEE 1076-2008
        // 9.3.2).
        const auto null_literal = [&](const semantic::ExpressionId id) {
            const auto operand = specialized_hir_unit_->find_expression(id);
            return operand && operand->vhdl != nullptr
                && operand->vhdl->text == "@vhdl-null";
        };
        // A STD.TEXTIO LINE is held as its string; null is the empty
        // value (IEEE 1076-2008 16.4).
        const auto textio_line = [&](const semantic::ExpressionId id) {
            const auto subtype = hir_vhdl_expression_subtype(id);
            return subtype
                && compiler_owned_textio_line_type(*specialized_hir_unit_,
                    *subtype, hir_process_scope_,
                    hir_vhdl_named_type(*specialized_hir_unit_, *subtype,
                        semantic::vhdl::TypeForm::access));
        };
        for (std::size_t side { }; side < 2U; ++side) {
            if (!null_literal(source.operands[1U - side])
                || !textio_line(source.operands[side])) {
                continue;
            }
            const auto line = lower_hir_string_expression(
                source.operands[side]);
            if (!line) {
                return std::nullopt;
            }
            const auto length = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(StringLength { length, *line });
            const auto zero = load_constant(
                unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
            const auto empty = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal, empty, length, zero });
            if (source.text == "=") {
                return empty;
            }
            const auto present = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(UnaryNot { present, empty });
            return present;
        }
        const auto* lhs_type = null_literal(source.operands[0])
            ? nullptr : access_type(source.operands[0]);
        const auto* rhs_type = null_literal(source.operands[1])
            ? nullptr : access_type(source.operands[1]);
        const auto* type = lhs_type != nullptr ? lhs_type : rhs_type;
        if (type == nullptr || (lhs_type != nullptr && rhs_type != nullptr
                && lhs_type->id != rhs_type->id)) {
            report(
                "FSIM-ELAB-VHACCESS-019",
                "VHDL access equality requires the same nominal access type",
                span);
            return std::nullopt;
        }
        const auto lhs = lower_hir_expression(source.operands[0], 32U);
        const auto rhs = lower_hir_expression(source.operands[1], 32U);
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        const auto equal = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, equal, *lhs, *rhs });
        if (source.text == "=") {
            return equal;
        }
        const auto different = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(UnaryNot { different, equal });
        return different;
    }
    if (source.text == "@vhdl-null") {
        if (expected_width != 32U) {
            report(
                "FSIM-ELAB-VHACCESS-009",
                "the VHDL access context has an inconsistent handle width",
                span);
            return std::nullopt;
        }
        const auto result = load_constant(
            unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
        return result;
    }
    if (source.text == "@vhdl-new"
        || source.text == "@vhdl-new-qualified") {
        const auto* type = allocated_type();
        if (type == nullptr || expected_width != 32U) {
            report(
                "FSIM-ELAB-VHACCESS-010",
                "a VHDL allocator requires a resolved access-type context",
                span);
            return std::nullopt;
        }
        std::vector<semantic::vhdl::RangeConstraint> allocator_constraints;
        const auto allocator_subtype = !source.operands.empty()
            ? specialized_hir_unit_->find_expression(
                  source.operands.front())
            : std::nullopt;
        const auto designated_subtype = type->designated_subtype
            ? hir_effective_vhdl_subtype(*type->designated_subtype)
            : std::nullopt;
        const bool predefined_bit_vector
            = designated_subtype
            && !designated_subtype->type_mark.target.valid()
            && designated_subtype->builtin_type
                == semantic::vhdl::BuiltinTypeIdentity::
                    std_standard_bit_vector;
        // An index constraint of a one-dimensional array allocator
        // (IEEE 1076-2008 9.3.7).
        const auto* allocated_array = designated_subtype
            ? hir_vhdl_named_type(*specialized_hir_unit_,
                  *designated_subtype, semantic::vhdl::TypeForm::array)
            : nullptr;
        const bool one_dimensional_array = predefined_bit_vector
            || (designated_subtype
                && designated_subtype->domain
                    == semantic::vhdl::ValueDomain::string)
            || (allocated_array != nullptr
                && allocated_array->array_dimensions.size() == 1U);
        if (one_dimensional_array && allocator_subtype
            && allocator_subtype->vhdl != nullptr) {
            for (const auto bound_id : allocator_subtype->vhdl->operands) {
                const auto bound = specialized_hir_unit_->find_expression(
                    bound_id);
                // A range attribute (`x'range`) or a discrete subtype name
                // also constrains the index (5.3.2.1).
                const bool explicit_range = bound && bound->vhdl != nullptr
                    && bound->vhdl->kind
                        == semantic::vhdl::ExpressionKind::binary
                    && (bound->vhdl->text == "to"
                        || bound->vhdl->text == "downto")
                    && bound->vhdl->operands.size() == 2U;
                if (!explicit_range && bound && bound->vhdl != nullptr) {
                    auto discrete = [&]() -> std::optional<HirPackedRange> {
                        if (const auto attribute = hir_vhdl_attribute_profile(
                                bound_id, hir_process_scope_);
                            attribute && attribute->discrete_range) {
                            return attribute->discrete_range;
                        }
                        return hir_expression_range(
                            bound_id, hir_process_scope_);
                    }();
                    if (discrete) {
                        semantic::vhdl::RangeConstraint range;
                        range.kind = semantic::vhdl::RangeKind::array_index;
                        range.left = discrete->left;
                        range.right = discrete->right;
                        range.descending = discrete->descending;
                        range.source = bound->vhdl->source;
                        allocator_constraints.push_back(std::move(range));
                        continue;
                    }
                }
                if (!explicit_range) {
                    report(
                        "FSIM-ELAB-VHACCESS-006",
                        "an explicit array allocator range requires one static to/downto constraint",
                        span);
                    return std::nullopt;
                }
                semantic::vhdl::RangeConstraint range;
                range.kind = semantic::vhdl::RangeKind::array_index;
                range.left_expression = bound->vhdl->operands[0];
                range.right_expression = bound->vhdl->operands[1];
                range.descending = bound->vhdl->text == "downto";
                // A range attribute or subtype name keeps its endpoints as
                // 'LEFT and 'RIGHT; their values give its direction.
                const auto bound_attribute = [&](
                    const semantic::ExpressionId id) -> std::string {
                    const auto endpoint
                        = specialized_hir_unit_->find_expression(id);
                    return endpoint && endpoint->vhdl != nullptr
                            && endpoint->vhdl->kind
                                == semantic::vhdl::ExpressionKind::call
                        ? endpoint->vhdl->text
                        : std::string { };
                };
                const auto left_attribute
                    = bound_attribute(bound->vhdl->operands[0]);
                const auto right_attribute
                    = bound_attribute(bound->vhdl->operands[1]);
                range.direction_from_bounds
                    = (left_attribute == "'left"
                          && right_attribute == "'right")
                    || (left_attribute == "'right"
                        && right_attribute == "'left");
                range.source = bound->vhdl->source;
                allocator_constraints.push_back(std::move(range));
            }
            if (!allocator_constraints.empty()
                && allocator_constraints.size() != 1U) {
                report(
                    "FSIM-ELAB-VHACCESS-006",
                    "a one-dimensional array allocator requires one index range",
                    span);
                return std::nullopt;
            }
        }
        const auto allocator_initializer
            = source.text == "@vhdl-new-qualified"
                    && source.operands.size() == 2U
            ? std::optional { source.operands[1] }
            : std::nullopt;
        auto* heap = hir_vhdl_access_heap(
            *type, span, expected_access_subtype, allocator_initializer,
            allocator_constraints);
        if (heap == nullptr) {
            return std::nullopt;
        }
        if (heap->designated_subtype.domain
            == semantic::vhdl::ValueDomain::string) {
            // Each STRING object has its own length. Its index range is
            // the allocator's constraint or the designated subtype's; an
            // unconstrained qualified value defines its own (9.3.7).
            const semantic::vhdl::RangeConstraint* object_range = nullptr;
            if (!allocator_constraints.empty()) {
                object_range = &allocator_constraints.front();
            } else if (designated_subtype) {
                const auto found = std::ranges::find_if(
                    designated_subtype->constraints,
                    [](const semantic::vhdl::RangeConstraint& constraint) {
                        return constraint.kind
                            == semantic::vhdl::RangeKind::array_index;
                    });
                if (found != designated_subtype->constraints.end()) {
                    object_range = &*found;
                }
            }
            std::optional<std::uint64_t> extent;
            if (object_range != nullptr) {
                const auto left = hir_range_left(*object_range);
                const auto right = hir_range_right(*object_range);
                if (!left || !right) {
                    report(
                        "FSIM-ELAB-VHACCESS-006",
                        "a VHDL STRING allocator requires concrete index bounds",
                        span);
                    return std::nullopt;
                }
                const bool descending = object_range->direction_from_bounds
                    ? *left > *right
                    : object_range->descending;
                const bool null_range = descending
                    ? *left < *right : *left > *right;
                extent = null_range
                    ? std::uint64_t { }
                    : index_distance(*left, *right) + 1U;
                if (*extent > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-VHACCESS-006",
                        "the VHDL STRING access-object length exceeds the supported bound",
                        span);
                    return std::nullopt;
                }
            }
            const bool qualified = source.text == "@vhdl-new-qualified"
                && source.operands.size() == 2U;
            if (!qualified) {
                if (!extent) {
                    report(
                        "FSIM-ELAB-VHACCESS-006",
                        "a VHDL STRING allocator requires an index range or a qualified initial value",
                        span);
                    return std::nullopt;
                }
                // The default value repeats CHARACTER'LEFT (6.4.2.3).
                const auto initial = allocate_string_register();
                process_.operations.emplace_back(LoadStringConstant {
                    initial,
                    std::string(static_cast<std::size_t>(*extent), '\0') });
                return issue_hir_vhdl_access_handle(
                    *heap, span, [&](const RegisterId handle) {
                        process_.operations.emplace_back(ContainerStringWrite {
                            heap->objects, handle, initial, false, false,
                            false, { } });
                    });
            }
            const auto text = lower_hir_string_expression(
                source.operands[1],
                designated_subtype ? &*designated_subtype
                                   : &heap->designated_subtype);
            if (!text) {
                report(
                    "FSIM-ELAB-VHACCESS-012",
                    "a VHDL STRING allocator requires one executable STRING value",
                    span);
                return std::nullopt;
            }
            if (!extent) {
                return issue_hir_vhdl_access_handle(
                    *heap, span, [&](const RegisterId handle) {
                        process_.operations.emplace_back(ContainerStringWrite {
                            heap->objects, handle, *text, false, false,
                            false, { } });
                    });
            }
            const auto actual_length = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                StringLength { actual_length, *text });
            const auto required_length = load_constant(
                unsigned_value(*extent, 32U), frontend::ValueDomain::Bit2);
            const auto correct_length = allocate_register(
                1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                BinaryOperator::equal,
                correct_length,
                actual_length,
                required_length,
            });
            process_.operations.emplace_back(Assert {
                correct_length,
                "VHDL STRING allocator value does not satisfy its designated range",
                AssertionSeverity::failure,
                runtime_source_location(span),
            });
            return issue_hir_vhdl_access_handle(
                *heap, span, [&](const RegisterId handle) {
                    process_.operations.emplace_back(ContainerStringWrite {
                        heap->objects, handle, *text, false, false, false,
                        { } });
                });
        }
        const auto width = vhdl_subtype_width(
            heap->designated_subtype);
        if (!width || *width == 0U) {
            return std::nullopt;
        }
        std::optional<RegisterId> value;
        if (source.text == "@vhdl-new-qualified"
            && source.operands.size() == 2U) {
            const auto initial = specialized_hir_unit_->find_expression(
                source.operands[1]);
            value = initial && initial->vhdl != nullptr
                    && initial->vhdl->kind
                        == semantic::vhdl::ExpressionKind::aggregate
                ? lower_hir_vhdl_aggregate(
                      source.operands[1], *width,
                      &heap->designated_subtype)
                : lower_hir_expression(source.operands[1], *width);
        } else if (source.text == "@vhdl-new"
            && source.operands.size() == 1U) {
            // An allocator without an initial value creates an object with
            // its subtype's default value (IEEE 1076-2008 9.3.7, 6.4.2.3).
            value = allocate_register(
                *width,
                vhdl_value_domain(heap->designated_subtype.domain));
            const auto initial = hir_vhdl_default_value(
                heap->designated_subtype, *width, 0U, true);
            process_.operations.emplace_back(LoadConstant {
                *value, initial ? *initial : unsigned_value(0U, *width) });
        }
        if (!value) {
            report(
                "FSIM-ELAB-VHACCESS-012",
                "a qualified VHDL allocator requires exactly one initial value",
                span);
            return std::nullopt;
        }
        const auto designated_domain = vhdl_value_domain(
            heap->designated_subtype.domain);
        if (register_domain(*value) != designated_domain) {
            if (is_two_state_domain(designated_domain)
                && !is_two_state_domain(register_domain(*value))) {
                value = convert_to_two_state(*value);
            } else {
                const auto converted = allocate_register(
                    *width, designated_domain);
                process_.operations.emplace_back(CopyRegister {
                    converted, *value });
                value = converted;
            }
        }
        return issue_hir_vhdl_access_handle(
            *heap, span, [&](const RegisterId handle) {
                process_.operations.emplace_back(ContainerWrite {
                    heap->objects, handle, *value, false });
            });
    }
    if (implicit_dereference) {
        const auto subtype = expected_access_subtype != nullptr
            ? std::optional { *expected_access_subtype }
            : hir_vhdl_expression_subtype(expression_id);
        const auto handle = lower_hir_expression(expression_id, 32U);
        if (!subtype || !handle) {
            return std::nullopt;
        }
        return lower_hir_vhdl_access_object(
            *handle, *subtype, span);
    }
    if (source.text != "@vhdl-dereference"
        || source.operands.size() != 1U) {
        return std::nullopt;
    }
    const auto* type = access_type(source.operands.front());
    const auto source_access_subtype = hir_vhdl_expression_subtype(
        source.operands.front());
    auto* heap = type != nullptr
        ? hir_vhdl_access_heap(
              *type, span,
              source_access_subtype ? &*source_access_subtype : nullptr)
        : nullptr;
    const auto handle = lower_hir_expression(
        source.operands.front(), 32U);
    if (heap == nullptr || !handle) {
        return std::nullopt;
    }
    emit_hir_vhdl_access_live_check(
        *heap, *handle, span);
    if (heap->designated_subtype.domain
        == semantic::vhdl::ValueDomain::string) {
        return std::nullopt;
    }
    const auto width = vhdl_subtype_width(heap->designated_subtype);
    if (!width || *width == 0U) {
        return std::nullopt;
    }
    const auto result = allocate_register(
        *width, vhdl_value_domain(heap->designated_subtype.domain));
    process_.operations.emplace_back(ContainerRead {
        result, heap->objects, *handle, false });
    return result;
}

void Lowerer::emit_hir_vhdl_access_reclamation(
    const semantic::vhdl::TypeDefinition& type,
    const HirVhdlAccessHeap& heap,
    const RegisterId candidate)
{
    if (!type.reclaim_when_unreachable) {
        return;
    }
    const auto zero
        = load_constant(unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
    auto reachable = allocate_register(
        1U, frontend::ValueDomain::Boolean);
    process_.operations.emplace_back(Binary {
        BinaryOperator::case_equal, reachable, candidate, zero });
    for (const auto& [declaration_value, root] : hir_local_registers_) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            semantic::DeclarationId::from_index(declaration_value));
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            continue;
        }
        const auto* root_type = hir_vhdl_named_type(
            *specialized_hir_unit_, *declaration->vhdl->subtype,
            semantic::vhdl::TypeForm::access);
        if (root_type == nullptr || root_type->id != type.id) {
            continue;
        }
        const auto equal = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, equal, candidate, root });
        const auto combined = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::bit_or, combined, reachable, equal });
        reachable = combined;
    }
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch {
        reachable, 0U, 0U, UnknownBranchPolicy::error });
    const auto reclaim = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(DeleteContainer {
        heap.objects, candidate, false });
    const auto finish = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[branch] = Branch {
        reachable, finish, reclaim, UnknownBranchPolicy::error };
}

bool Lowerer::is_hir_vhdl_access_deallocation(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    return statement && statement->vhdl != nullptr
        && statement->vhdl->kind
            == semantic::vhdl::StatementKind::procedure_call
        && equal_ignoring_ascii_case(
            statement->vhdl->procedure.canonical.empty()
                ? statement->vhdl->procedure.spelling
                : statement->vhdl->procedure.canonical,
            "deallocate")
        && statement->vhdl->procedure_arguments.size() == 1U;
}

bool Lowerer::is_hir_vhdl_access_assignment(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::variable_assignment
        || !statement->vhdl->target || !statement->vhdl->value) {
        return false;
    }
    const auto target = specialized_hir_unit_->find_expression(
        *statement->vhdl->target);
    if (!target || target->vhdl == nullptr) {
        return false;
    }
    if (target->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && target->vhdl->text == "@vhdl-dereference") {
        return true;
    }
    const auto member = hir_vhdl_member_selection(
        *statement->vhdl->target);
    if (member && member->access_object_root) {
        return true;
    }
    // An element of an object designated by an access value (`p(i)`,
    // IEEE 1076-2008 8.4 implicit dereference); its shape is known once
    // the heap exists.
    if ((target->vhdl->kind == semantic::vhdl::ExpressionKind::index
            || target->vhdl->kind == semantic::vhdl::ExpressionKind::call)
        && !target->vhdl->text.starts_with('@')
        && !target->vhdl->operands.empty()) {
        // The prefix's expression subtype is already the designated one;
        // its declaration names the access type.
        const auto prefix_declaration = hir_target_declaration(
            target->vhdl->operands.front());
        const auto declaration = prefix_declaration
            ? specialized_hir_unit_->find_declaration(*prefix_declaration)
            : std::nullopt;
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->subtype
            && hir_vhdl_named_type(*specialized_hir_unit_,
                   *declaration->vhdl->subtype,
                   semantic::vhdl::TypeForm::access)
                != nullptr) {
            return true;
        }
    }
    const auto member_root = member && member->root.valid()
        ? specialized_hir_unit_->find_expression(member->root)
        : std::nullopt;
    if (member_root && member_root->vhdl != nullptr
        && member_root->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && member_root->vhdl->text == "@vhdl-dereference"
        && member_root->vhdl->operands.size() == 1U) {
        return true;
    }
    const auto subtype = hir_vhdl_expression_subtype(
        *statement->vhdl->target);
    // An access-typed record element (`h.p := q`) is an ordinary element
    // write of the record's handle bits.
    if (target->vhdl->kind
            != semantic::vhdl::ExpressionKind::name
        || member || !subtype) {
        return false;
    }
    const auto* access_type = hir_vhdl_named_type(
        *specialized_hir_unit_, *subtype,
        semantic::vhdl::TypeForm::access);
    if (access_type != nullptr) {
        return true;
    }
    // The compiler-owned STD.TEXTIO.LINE access type has no retained HIR
    // type declaration. Select the string-specialized path only for its
    // allocator/null assignments, and only when that exact standard member
    // is visible in the current scope.
    const auto value = specialized_hir_unit_->find_expression(
        *statement->vhdl->value);
    const bool supported_line_value = value && value->vhdl != nullptr
        && value->vhdl->kind == semantic::vhdl::ExpressionKind::call
        && (value->vhdl->text == "@vhdl-new-qualified"
            || value->vhdl->text == "@vhdl-null");
    return supported_line_value
        && compiler_owned_textio_line_type(
            *specialized_hir_unit_, *subtype,
            hir_process_scope_, access_type);
}

bool Lowerer::reject_hir_vhdl_access_signal_escape(
    const semantic::StatementId statement_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::signal_assignment
        || !statement->vhdl->target) {
        return false;
    }
    const auto& source = *statement->vhdl;
    const auto value = source.waveform.empty()
        ? source.value
        : std::optional { source.waveform.front().value };
    if (!value) {
        return false;
    }
    const auto target_subtype = hir_vhdl_expression_subtype(
        *source.target);
    if (!target_subtype
        || hir_vhdl_named_type(
               *specialized_hir_unit_, *target_subtype,
               semantic::vhdl::TypeForm::access)
            == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        *value);
    if (expression && expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text == "@vhdl-null") {
        return false;
    }
    report(
        "FSIM-ELAB-VHACCESS-021",
        "a nonnull process-local VHDL access handle cannot escape "
        "through a signal",
        expression && expression->vhdl != nullptr
            ? hir_source_span(expression->vhdl->source)
            : hir_source_span(source.source));
    return true;
}

std::optional<bool> Lowerer::lower_hir_vhdl_access_element_assignment(
    const semantic::ExpressionId target_id,
    const semantic::ExpressionId value_id)
{
    // An element of an array object an access value designates (`p(i)`,
    // IEEE 1076-2008 8.4 implicit dereference, 10.6.2.1).
    const auto target = specialized_hir_unit_->find_expression(target_id);
    if (!target || target->vhdl == nullptr
        || (target->vhdl->kind != semantic::vhdl::ExpressionKind::index
            && (target->vhdl->kind != semantic::vhdl::ExpressionKind::call
                || target->vhdl->text.starts_with('@')))
        || target->vhdl->operands.size() != 2U) {
        return std::nullopt;
    }
    const auto prefix = target->vhdl->operands.front();
    const auto prefix_declaration = hir_target_declaration(prefix);
    const auto declaration = prefix_declaration
        ? specialized_hir_unit_->find_declaration(*prefix_declaration)
        : std::nullopt;
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->subtype) {
        return std::nullopt;
    }
    const auto& access_subtype = *declaration->vhdl->subtype;
    const auto* type = hir_vhdl_named_type(*specialized_hir_unit_,
        access_subtype, semantic::vhdl::TypeForm::access);
    if (type == nullptr) {
        return std::nullopt;
    }
    const auto span = hir_source_span(target->vhdl->source);
    auto* heap = hir_vhdl_access_heap(*type, span, &access_subtype);
    if (heap == nullptr
        || heap->designated_subtype.domain
            == semantic::vhdl::ValueDomain::string) {
        return false;
    }
    const auto& designated = heap->designated_subtype;
    const auto width = vhdl_subtype_width(designated);
    const auto range = std::ranges::find_if(designated.constraints,
        [](const semantic::vhdl::RangeConstraint& constraint) {
            return constraint.kind == semantic::vhdl::RangeKind::array_index;
        });
    const auto* array = designated.type_mark.target.valid()
        ? hir_vhdl_array_type(
              *specialized_hir_unit_, designated.type_mark.target)
        : nullptr;
    const auto element = array != nullptr && array->element_subtype
        ? hir_effective_vhdl_subtype(*array->element_subtype)
        : std::nullopt;
    if (!width || range == designated.constraints.end() || !element
        || array->array_dimensions.size() != 1U) {
        return false;
    }
    const auto left = hir_range_left(*range);
    const auto right = hir_range_right(*range);
    const auto selected = hir_constant_integer(target->vhdl->operands[1]);
    if (!left || !right || !selected
        || *selected < std::min(*left, *right)
        || *selected > std::max(*left, *right)) {
        return false;
    }
    const auto count = index_distance(*left, *right) + 1U;
    if (count == 0U || *width % count != 0U) {
        return false;
    }
    // The right bound's element is the least significant (5.3.2.1).
    const auto element_width = *width / static_cast<std::size_t>(count);
    const auto offset = index_distance(*selected, *right) * element_width;
    if (offset > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    // The prefix names the access variable; its value is the handle.
    const auto binding = hir_runtime_binding(
        *prefix_declaration, hir_process_scope_, true);
    const auto handle = binding ? binding->local : std::nullopt;
    auto value = lower_hir_expression(value_id, element_width);
    if (!handle || !value) {
        return false;
    }
    if (register_width(*value) != element_width) {
        value = resize_register(*value, element_width, element->signed_value);
    }
    const auto element_domain = vhdl_value_domain(element->domain);
    if (register_domain(*value) != element_domain) {
        const auto converted = allocate_register(element_width, element_domain);
        process_.operations.emplace_back(CopyRegister { converted, *value });
        value = converted;
    }
    emit_hir_vhdl_access_live_check(*heap, *handle, span);
    const auto object_domain = vhdl_value_domain(designated.domain);
    const auto current = allocate_register(*width, object_domain);
    process_.operations.emplace_back(ContainerRead {
        current, heap->objects, *handle, false });
    const auto updated = allocate_register(*width, object_domain);
    process_.operations.emplace_back(Insert {
        updated, current, *value, static_cast<std::uint32_t>(offset) });
    process_.operations.emplace_back(ContainerWrite {
        heap->objects, *handle, updated, false });
    return true;
}

bool Lowerer::lower_hir_vhdl_access_assignment(
    const semantic::StatementId statement_id)
{
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || !statement->vhdl->target || !statement->vhdl->value) {
        return false;
    }
    const auto target = specialized_hir_unit_->find_expression(
        *statement->vhdl->target);
    if (!target || target->vhdl == nullptr) {
        return false;
    }
    if (const auto element = lower_hir_vhdl_access_element_assignment(
            *statement->vhdl->target, *statement->vhdl->value)) {
        return *element;
    }
    const auto line_declaration = hir_target_declaration(
        *statement->vhdl->target);
    const auto line_subtype = hir_vhdl_expression_subtype(
        *statement->vhdl->target);
    const auto* line_type = line_subtype
        ? hir_vhdl_named_type(
              *specialized_hir_unit_, *line_subtype,
              semantic::vhdl::TypeForm::access)
        : nullptr;
    const auto line_binding = line_declaration
        ? hir_string_binding(*line_declaration, hir_process_scope_, true)
        : std::nullopt;
    const auto assigned_line = specialized_hir_unit_->find_expression(
        *statement->vhdl->value);
    if (line_subtype
        && compiler_owned_textio_line_type(
            *specialized_hir_unit_, *line_subtype,
            hir_process_scope_, line_type)
        && line_binding && line_binding->kind == HirStringBindingKind::local
        && line_binding->local && assigned_line
        && assigned_line->vhdl != nullptr
        && assigned_line->vhdl->kind
            == semantic::vhdl::ExpressionKind::call) {
        if (assigned_line->vhdl->text == "@vhdl-null") {
            process_.operations.emplace_back(LoadStringConstant {
                *line_binding->local, std::string { } });
            return true;
        }
        if (assigned_line->vhdl->text == "@vhdl-new-qualified"
            && assigned_line->vhdl->operands.size() == 2U) {
            const auto initial = lower_hir_string_expression(
                assigned_line->vhdl->operands[1]);
            if (!initial) {
                return false;
            }
            process_.operations.emplace_back(CopyStringRegister {
                *line_binding->local, *initial });
            return true;
        }
        // `new string(1 to n)`: a STRING of n CHARACTER'LEFT (NUL)
        // elements (IEEE 1076-2008 9.3.7, 6.4.2.3).
        if (assigned_line->vhdl->text == "@vhdl-new"
            && assigned_line->vhdl->operands.size() == 1U) {
            const auto subtype = specialized_hir_unit_->find_expression(
                assigned_line->vhdl->operands.front());
            const auto bound = subtype && subtype->vhdl != nullptr
                    && subtype->vhdl->operands.size() == 1U
                ? specialized_hir_unit_->find_expression(
                      subtype->vhdl->operands.front())
                : std::nullopt;
            if (bound && bound->vhdl != nullptr
                && bound->vhdl->kind == semantic::vhdl::ExpressionKind::binary
                && (bound->vhdl->text == "to" || bound->vhdl->text == "downto")
                && bound->vhdl->operands.size() == 2U) {
                const auto left = hir_constant_integer(bound->vhdl->operands[0]);
                const auto right
                    = hir_constant_integer(bound->vhdl->operands[1]);
                if (left && right) {
                    const auto descending = bound->vhdl->text == "downto";
                    const auto count = descending
                        ? (*left >= *right ? *left - *right + 1 : 0)
                        : (*right >= *left ? *right - *left + 1 : 0);
                    if (count >= 0 && count <= (std::int64_t { 1 } << 24)) {
                        process_.operations.emplace_back(LoadStringConstant {
                            *line_binding->local,
                            std::string(static_cast<std::size_t>(count),
                                '\0') });
                        return true;
                    }
                }
            }
        }
    }
    const auto member = hir_vhdl_member_selection(
        *statement->vhdl->target);
    if (target->vhdl->kind
            == semantic::vhdl::ExpressionKind::name
        && !(member && member->access_object_root)) {
        const auto declaration_id = hir_target_declaration(
            *statement->vhdl->target);
        const auto binding = declaration_id
            ? hir_runtime_binding(
                  *declaration_id, hir_process_scope_, true)
            : std::nullopt;
        const auto subtype = hir_vhdl_expression_subtype(
            *statement->vhdl->target);
        const auto* type = subtype
            ? hir_vhdl_named_type(
                  *specialized_hir_unit_, *subtype,
                  semantic::vhdl::TypeForm::access)
            : nullptr;
        const auto assigned_expression
            = specialized_hir_unit_->find_expression(
                *statement->vhdl->value);
        const auto contextual_access_value
            = assigned_expression
                && assigned_expression->vhdl != nullptr
                && assigned_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call;
        if (type != nullptr && !contextual_access_value) {
            const auto assigned_subtype = hir_vhdl_expression_subtype(
                *statement->vhdl->value);
            const auto* assigned_type = assigned_subtype
                ? hir_vhdl_named_type(
                      *specialized_hir_unit_, *assigned_subtype,
                      semantic::vhdl::TypeForm::access)
                : nullptr;
            if (assigned_type != nullptr
                && assigned_type->id != type->id) {
                report(
                    "FSIM-ELAB-VHACCESS-020",
                    "VHDL access assignment requires the same nominal "
                    "access type or null",
                    assigned_expression
                            && assigned_expression->vhdl != nullptr
                        ? hir_source_span(
                              assigned_expression->vhdl->source)
                        : hir_source_span(statement->vhdl->source));
                return true;
            }
        }
        const auto contextual_allocator
            = assigned_expression && assigned_expression->vhdl != nullptr
            && assigned_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
            && (assigned_expression->vhdl->text == "@vhdl-new"
                || assigned_expression->vhdl->text
                    == "@vhdl-new-qualified");
        const auto assigned = contextual_allocator
            ? lower_hir_vhdl_access_expression(
                  *statement->vhdl->value, 32U,
                  subtype ? &*subtype : nullptr)
            : lower_hir_expression(*statement->vhdl->value, 32U);
        auto* heap = type != nullptr
            ? hir_vhdl_access_heap(
                  *type, hir_source_span(target->vhdl->source),
                  subtype ? &*subtype : nullptr)
            : nullptr;
        if (!binding || !binding->local || type == nullptr
            || heap == nullptr || !assigned) {
            return false;
        }
        const auto released = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(CopyRegister {
            released, *binding->local });
        process_.operations.emplace_back(CopyRegister {
            *binding->local, *assigned });
        emit_hir_vhdl_access_reclamation(
            *type, *heap, released);
        return true;
    }
    const bool implicit_access_member
        = member && member->access_object_root;
    const auto dereference_id = member && member->root.valid()
        ? member->root
        : *statement->vhdl->target;
    const auto dereference = specialized_hir_unit_->find_expression(
        dereference_id);
    if (!implicit_access_member
        && (!dereference || dereference->vhdl == nullptr
            || dereference->vhdl->kind
                != semantic::vhdl::ExpressionKind::call
            || dereference->vhdl->text != "@vhdl-dereference"
            || dereference->vhdl->operands.size() != 1U)) {
        return false;
    }
    std::optional<semantic::vhdl::SubtypeIndication> subtype;
    std::optional<RegisterId> handle;
    if (implicit_access_member) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            member->declaration);
        const auto binding = hir_runtime_binding(
            member->declaration, hir_process_scope_, true);
        const auto declaration_subtype
            = declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->subtype
            ? declaration->vhdl->subtype
            : std::nullopt;
        subtype = member->root.valid()
            ? hir_vhdl_expression_subtype(member->root)
            : declaration_subtype;
        handle = member->root.valid()
            ? lower_hir_expression(member->root, 32U)
            : binding && binding->local
                ? binding->local
                : std::nullopt;
    } else {
        const auto prefix = dereference->vhdl->operands.front();
        subtype = hir_vhdl_expression_subtype(prefix);
        // `l.all := "..."` on a STD.TEXTIO LINE held as a string replaces
        // the designated STRING (IEEE 1076-2008 10.6.2.1, 16.4).
        const auto textio_declaration = hir_target_declaration(prefix);
        const auto textio_binding = textio_declaration
            ? hir_string_binding(
                  *textio_declaration, hir_process_scope_, true)
            : std::nullopt;
        if (!member && subtype
            && compiler_owned_textio_line_type(*specialized_hir_unit_,
                *subtype, hir_process_scope_,
                hir_vhdl_named_type(*specialized_hir_unit_, *subtype,
                    semantic::vhdl::TypeForm::access))
            && textio_binding
            && textio_binding->kind == HirStringBindingKind::local
            && textio_binding->local) {
            const auto text = lower_hir_string_expression(
                *statement->vhdl->value);
            if (!text) {
                return false;
            }
            process_.operations.emplace_back(CopyStringRegister {
                *textio_binding->local, *text });
            return true;
        }
        handle = lower_hir_expression(prefix, 32U);
    }
    const auto* type = subtype
        ? hir_vhdl_named_type(
              *specialized_hir_unit_, *subtype,
              semantic::vhdl::TypeForm::access)
        : nullptr;
    if (type == nullptr || !type->designated_subtype) {
        report(
            "FSIM-ELAB-VHACCESS-014",
            "a VHDL dereference target requires an access-typed prefix",
            hir_source_span(target->vhdl->source));
        return true;
    }
    auto* heap = hir_vhdl_access_heap(
        *type, hir_source_span(target->vhdl->source),
        subtype ? &*subtype : nullptr);
    // A whole STRING object takes a value of its own length (10.6.2.1).
    if (heap != nullptr && !member && handle
        && heap->designated_subtype.domain
            == semantic::vhdl::ValueDomain::string) {
        const auto type_designated = hir_effective_vhdl_subtype(
            *type->designated_subtype);
        const auto text = lower_hir_string_expression(
            *statement->vhdl->value,
            type_designated ? &*type_designated : nullptr);
        if (!text) {
            return false;
        }
        const auto span = hir_source_span(target->vhdl->source);
        emit_hir_vhdl_access_live_check(*heap, *handle, span);
        const auto current = allocate_string_register();
        process_.operations.emplace_back(ContainerStringRead {
            current, heap->objects, *handle, false, false, false, { } });
        const auto current_length = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            StringLength { current_length, current });
        const auto value_length = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            StringLength { value_length, *text });
        const auto same_length = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::equal, same_length, current_length,
            value_length });
        process_.operations.emplace_back(Assert {
            same_length,
            "VHDL STRING object assignment value does not have the object's length",
            AssertionSeverity::failure,
            runtime_source_location(span),
        });
        process_.operations.emplace_back(ContainerStringWrite {
            heap->objects, *handle, *text, false, false, false, { } });
        return true;
    }
    // The object's shape is the heap's designated subtype (5.4.2).
    const auto designated = heap != nullptr
        ? heap->designated_subtype
        : hir_effective_vhdl_subtype(*type->designated_subtype)
              .value_or(*type->designated_subtype);
    const auto width = vhdl_subtype_width(designated);
    const auto assignment_width = member
        ? std::optional { member->width }
        : width;
    const auto* access_member_type = member
        ? hir_vhdl_named_type(
              *specialized_hir_unit_, member->subtype,
              semantic::vhdl::TypeForm::access)
        : nullptr;
    const auto input = specialized_hir_unit_->find_expression(
        *statement->vhdl->value);
    const bool contextual_allocator = access_member_type != nullptr
        && input && input->vhdl != nullptr
        && input->vhdl->kind == semantic::vhdl::ExpressionKind::call
        && (input->vhdl->text == "@vhdl-new"
            || input->vhdl->text == "@vhdl-new-qualified");
    const bool null_assignment = input && input->vhdl != nullptr
        && input->vhdl->kind == semantic::vhdl::ExpressionKind::call
        && input->vhdl->text == "@vhdl-null";
    if (access_member_type != nullptr
        && !contextual_allocator && !null_assignment) {
        const auto assigned_subtype = hir_vhdl_expression_subtype(
            *statement->vhdl->value);
        const auto* assigned_type = assigned_subtype
            ? hir_vhdl_named_type(
                  *specialized_hir_unit_, *assigned_subtype,
                  semantic::vhdl::TypeForm::access)
            : nullptr;
        if (assigned_type != nullptr
            && assigned_type->id != access_member_type->id) {
            report(
                "FSIM-ELAB-VHACCESS-020",
                "VHDL access assignment requires the same nominal "
                "access type or null",
                input && input->vhdl != nullptr
                    ? hir_source_span(input->vhdl->source)
                    : hir_source_span(statement->vhdl->source));
            return true;
        }
    }
    auto value = !assignment_width
        ? std::optional<RegisterId> { }
        : input && input->vhdl != nullptr
                && input->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate
                && !member
        ? lower_hir_vhdl_aggregate(
              *statement->vhdl->value, *width, &designated)
        : contextual_allocator
        ? lower_hir_vhdl_access_expression(
              *statement->vhdl->value, *assignment_width,
              &member->subtype)
        : lower_hir_expression(
              *statement->vhdl->value, *assignment_width);
    if (heap == nullptr || !width || !assignment_width
        || !handle || !value) {
        return false;
    }
    if (register_width(*value) != *assignment_width) {
        value = resize_register(
            *value, *assignment_width,
            member ? member->signed_value : designated.signed_value);
    }
    const auto assignment_domain = member
        ? member->domain
        : vhdl_value_domain(designated.domain);
    if (register_domain(*value) != assignment_domain) {
        if (is_two_state_domain(assignment_domain)
            && !is_two_state_domain(register_domain(*value))) {
            value = convert_to_two_state(*value);
        } else {
            const auto converted = allocate_register(
                *assignment_width, assignment_domain);
            process_.operations.emplace_back(CopyRegister {
                converted, *value });
            value = converted;
        }
    }
    emit_hir_vhdl_access_live_check(
        *heap, *handle, hir_source_span(target->vhdl->source));
    if (member) {
        if (member->offset > *width
            || member->width > *width - member->offset
            || member->offset
                > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const auto current = allocate_register(
            *width, vhdl_value_domain(designated.domain));
        process_.operations.emplace_back(ContainerRead {
            current, heap->objects, *handle, false });
        const auto updated = allocate_register(
            *width, vhdl_value_domain(designated.domain));
        process_.operations.emplace_back(Insert {
            updated,
            current,
            *value,
            static_cast<std::uint32_t>(member->offset),
        });
        value = updated;
    }
    process_.operations.emplace_back(ContainerWrite {
        heap->objects, *handle, *value, false });
    return true;
}

bool Lowerer::lower_hir_vhdl_access_deallocation(
    const semantic::StatementId statement_id)
{
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->procedure_arguments.size() != 1U) {
        return false;
    }
    const auto actual = statement->vhdl->procedure_arguments.front().actual;
    const auto declaration_id = hir_target_declaration(actual);
    if (const auto line = declaration_id
            ? hir_string_binding(*declaration_id, hir_process_scope_, true)
            : std::nullopt;
        line && line->local) {
        // DEALLOCATE of a std.textio LINE leaves it null (IEEE 1076-2008
        // 5.4.3); a line is held as a runtime string, so it becomes empty.
        process_.operations.emplace_back(
            LoadStringConstant { *line->local, std::string { } });
        return true;
    }
    const auto binding = declaration_id
        ? hir_runtime_binding(*declaration_id, hir_process_scope_, true)
        : std::nullopt;
    const auto subtype = hir_vhdl_expression_subtype(actual);
    const auto* type = subtype
        ? hir_vhdl_named_type(
              *specialized_hir_unit_, *subtype,
              semantic::vhdl::TypeForm::access)
        : nullptr;
    if (!binding || !binding->local || type == nullptr
        || !type->designated_subtype) {
        report(
            "FSIM-ELAB-VHACCESS-022",
            "VHDL access deallocation requires a writable access variable",
            hir_source_span(statement->vhdl->source));
        return true;
    }
    // Deallocating a null handle is legal before any allocation has occurred
    // (and process declarations may allocate later in source order). Create
    // the empty heap here so the same runtime container is shared with those
    // later allocations.
    auto* heap = hir_vhdl_access_heap(
        *type, hir_source_span(statement->vhdl->source), &*subtype);
    if (heap == nullptr) {
        return false;
    }
    const auto released = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(CopyRegister {
        released, *binding->local });
    if (type->deallocate_releases_storage) {
        process_.operations.emplace_back(DeleteContainer {
            heap->objects, released, false });
    }
    process_.operations.emplace_back(LoadConstant {
        *binding->local, unsigned_value(0U, 32U) });
    emit_hir_vhdl_access_reclamation(
        *type, *heap, released);
    return true;
}

bool Lowerer::is_hir_vhdl_physical_expression(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    return expression && expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->text.starts_with("@vhdl-physical:");
}

std::optional<Lowerer::HirVhdlPhysicalUnit> Lowerer::hir_vhdl_physical_unit(
    const std::string_view unit_name,
    const std::size_t expected_width,
    const semantic::ScopeId source_scope) const
{
    // The scale of a unit of a physical type in its primary unit, and the
    // type's width (IEEE 1076-2008 5.2.4). Without an expected width the
    // unit selects its type. Resolve the unit in the source expression's
    // lexical scope before using its type-specific scale.
    if (!source_scope.valid()) {
        return std::nullopt;
    }
    const auto resolver = hir_resolver();
    semantic::vhdl::Name name;
    name.spelling = std::string { unit_name };
    name.canonical = name.spelling;
    const auto selected_resolution = resolver.resolve_vhdl(
        name, source_scope,
        [&](const semantic::CompiledDeclarationView& candidate) {
            return candidate.vhdl != nullptr && std::ranges::any_of(
                specialized_hir_unit_->design().vhdl_hir.types(),
                [&](const semantic::vhdl::TypeDefinition& stored) {
                    const auto type = specialized_hir_unit_->find_type(
                        stored.id);
                    return type && type->vhdl != nullptr
                        && type->vhdl->form
                            == semantic::vhdl::TypeForm::physical
                        && std::ranges::any_of(
                            type->vhdl->physical_units,
                            [&](const semantic::vhdl::PhysicalUnit& unit) {
                                return unit.declaration == candidate.vhdl->id
                                    && equal_ignoring_ascii_case(
                                        unit.name, unit_name);
                            });
            });
        });
    const auto selected_unit = selected_resolution.unique();
    if (!selected_unit) {
        if (selected_resolution.status
                != semantic::CompiledResolutionStatus::not_found
            || resolver.resolve_vhdl(
                    name, source_scope,
                    [](const semantic::CompiledDeclarationView&) {
                        return true;
                    }).status
                != semantic::CompiledResolutionStatus::not_found) {
            return std::nullopt;
        }
        const auto builtin_unit = std::ranges::find(
            elaboration_detail::vhdl_time_units, unit_name,
            &std::pair<std::string_view, std::uint64_t>::first);
        if (builtin_unit != elaboration_detail::vhdl_time_units.end()
            && (expected_width == 0U || expected_width == 64U)) {
            return HirVhdlPhysicalUnit {
                64U, static_cast<std::int64_t>(builtin_unit->second) };
        }
        return std::nullopt;
    }
    std::optional<semantic::TypeId> selected_type;
    for (const auto& stored :
        specialized_hir_unit_->design().vhdl_hir.types()) {
        const auto type = specialized_hir_unit_->find_type(stored.id);
        if (!type || type->vhdl == nullptr
            || type->vhdl->form != semantic::vhdl::TypeForm::physical
            || std::ranges::none_of(
                type->vhdl->physical_units,
                [&](const semantic::vhdl::PhysicalUnit& unit) {
                    return unit.declaration == *selected_unit;
                })) {
            continue;
        }
        if (selected_type && *selected_type != stored.id) {
            return std::nullopt;
        }
        selected_type = stored.id;
    }
    if (!selected_type) {
        return std::nullopt;
    }
    std::optional<HirVhdlPhysicalUnit> result;
    for (const auto& stored :
        specialized_hir_unit_->design().vhdl_hir.types()) {
        if (stored.id != *selected_type) {
            continue;
        }
        const auto type = specialized_hir_unit_->find_type(stored.id);
        if (!type || type->vhdl == nullptr
            || type->vhdl->form
                != semantic::vhdl::TypeForm::physical) {
            continue;
        }
        const auto width = vhdl_subtype_width(type->vhdl->base);
        if (expected_width != 0U && width && *width != expected_width) {
            continue;
        }
        const auto unit = std::ranges::find_if(
            type->vhdl->physical_units,
            [&](const semantic::vhdl::PhysicalUnit& candidate) {
                return equal_ignoring_ascii_case(candidate.name, unit_name);
            });
        if (unit == type->vhdl->physical_units.end()) {
            continue;
        }
        std::unordered_map<std::string, std::int64_t> scales;
        for (const auto& candidate : type->vhdl->physical_units) {
            std::optional<std::int64_t> candidate_scale
                = candidate.scale_factor;
            if (!candidate_scale && !candidate.scale) {
                candidate_scale = 1;
            }
            if (!candidate_scale && candidate.scale) {
                const auto scale_expression
                    = specialized_hir_unit_->find_expression(
                        *candidate.scale);
                constexpr std::string_view physical_prefix {
                    "@vhdl-physical:"
                };
                if (scale_expression
                    && scale_expression->vhdl != nullptr
                    && scale_expression->vhdl->text.starts_with(
                        physical_prefix)
                    && scale_expression->vhdl->operands.size() == 1U) {
                    const auto referenced = std::string {
                        std::string_view { scale_expression->vhdl->text }
                            .substr(physical_prefix.size())
                    };
                    const auto prior = scales.find(referenced);
                    const auto multiplier = specialized_hir_unit_
                        ->evaluate_integral_expression(
                            scale_expression->vhdl->operands.front());
                    if (prior != scales.end() && multiplier
                        && *multiplier >= 0
                        && (*multiplier == 0
                            || prior->second
                                <= std::numeric_limits<std::int64_t>::max()
                                    / *multiplier)) {
                        candidate_scale = prior->second * *multiplier;
                    }
                }
            }
            if (!candidate_scale) {
                break;
            }
            scales.insert_or_assign(candidate.name, *candidate_scale);
        }
        const auto resolved_scale = scales.find(unit->name);
        if (resolved_scale == scales.end()) {
            continue;
        }
        // Without a context width, a physical type is as wide as an
        // integer type with its range (IEEE 1076-2008 5.2.4.1).
        const auto range_width = [&]() -> std::size_t {
            auto constraints = type->vhdl->base.constraints;
            if (type->vhdl->scalar_range) {
                constraints.insert(constraints.begin(),
                    *type->vhdl->scalar_range);
            }
            for (const auto& constraint : constraints) {
                const auto left = constraint.left ? constraint.left
                    : constraint.left_expression
                    ? specialized_hir_unit_->evaluate_integral_expression(
                          *constraint.left_expression)
                    : std::nullopt;
                const auto right = constraint.right ? constraint.right
                    : constraint.right_expression
                    ? specialized_hir_unit_->evaluate_integral_expression(
                          *constraint.right_expression)
                    : std::nullopt;
                if (left && right) {
                    const auto fits = [](const std::int64_t value) {
                        return value >= std::numeric_limits<std::int32_t>::min()
                            && value <= std::numeric_limits<std::int32_t>::max();
                    };
                    return fits(*left) && fits(*right) ? 32U : 64U;
                }
            }
            return 0U;
        };
        const auto resolved_width = expected_width != 0U
            ? expected_width
            : width ? *width
                    : range_width();
        if (result
            && (result->scale != resolved_scale->second
                || result->width != resolved_width)) {
            return std::nullopt;
        }
        result = HirVhdlPhysicalUnit {
            resolved_width, resolved_scale->second };
    }
    return result;
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_physical_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->operands.size() != 1U
        || (expected_width != 0U && expected_width != 32U
            && expected_width != 64U)) {
        return std::nullopt;
    }
    constexpr std::string_view prefix { "@vhdl-physical:" };
    const auto unit = hir_vhdl_physical_unit(
        std::string_view { expression->vhdl->text }.substr(prefix.size()),
        expected_width, expression->vhdl->scope);
    std::optional<std::int64_t> value;
    const auto magnitude_expression = specialized_hir_unit_->find_expression(
        expression->vhdl->operands.front());
    if (unit && unit->scale > 0 && magnitude_expression
        && magnitude_expression->vhdl != nullptr
        && magnitude_expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::real_literal) {
        const auto rounded = semantic::detail::decimal_physical_magnitude(
            magnitude_expression->vhdl->text,
            static_cast<std::uint64_t>(unit->scale));
        if (rounded
            && *rounded <= static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            // decimal_physical_magnitude already multiplies by the unit's
            // scale and rounds to the primary unit.
            value = static_cast<std::int64_t>(*rounded);
        }
    } else {
        const auto magnitude
            = specialized_hir_unit_->evaluate_integral_expression(
                expression->vhdl->operands.front());
        if (unit && magnitude) {
            const auto scale = unit->scale;
            if (scale > 0
                && !(*magnitude > 0
                    && *magnitude
                        > std::numeric_limits<std::int64_t>::max() / scale)
                && !(*magnitude < 0
                    && *magnitude
                        < std::numeric_limits<std::int64_t>::min() / scale)) {
                value = *magnitude * scale;
            }
        }
    }
    if (!unit || !value) {
        report(
            "FSIM-ELAB-VHPHYSICAL-006",
            "invalid physical literal unit or magnitude",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    }
    const auto width = unit->width != 0U ? unit->width : std::size_t { 64U };
    if (width == 32U
        && (*value < std::numeric_limits<std::int32_t>::min()
            || *value > std::numeric_limits<std::int32_t>::max())) {
        report(
            "FSIM-ELAB-VHPHYSICAL-006",
            "physical literal exceeds its bounded runtime representation",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    }
    const auto result = load_constant(
        unsigned_value(static_cast<std::uint64_t>(*value), width),
        frontend::ValueDomain::Integer);
    return result;
}

} // namespace fsim::elaboration
