// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
namespace {

bool same_vhdl_name(
    const std::string_view left,
    const std::string_view right)
{
    return std::ranges::equal(
        left, right, [](const char lhs, const char rhs) {
            return std::tolower(static_cast<unsigned char>(lhs))
                == std::tolower(static_cast<unsigned char>(rhs));
        });
}

SourceLocation hir_vhdl_source_location(
    const frontend::SourceSpan& span)
{
    return SourceLocation {
        span.source_name.str(),
        static_cast<std::uint32_t>(span.begin.line),
        static_cast<std::uint32_t>(span.begin.column),
    };
}

const semantic::vhdl::TypeDefinition* hir_vhdl_named_type(
    const semantic::SpecializedHirUnit& unit,
    const semantic::vhdl::SubtypeIndication& subtype,
    const semantic::vhdl::TypeForm required)
{
    auto type_id = subtype.type_mark.target;
    if (!type_id.valid() && !subtype.type_mark.spelling.empty()) {
        const semantic::vhdl::TypeDefinition* selected = nullptr;
        for (const auto& stored : unit.design().vhdl_hir.types()) {
            const auto candidate = unit.find_type(stored.id);
            if (!candidate || candidate->vhdl == nullptr
                || !same_vhdl_name(
                    candidate->vhdl->name,
                    subtype.type_mark.spelling)) {
                continue;
            }
            if (candidate->vhdl->form == required) {
                if (selected != nullptr
                    && selected->id != candidate->vhdl->id) {
                    return nullptr;
                }
                selected = candidate->vhdl;
            }
        }
        return selected;
    }
    std::unordered_set<std::uint32_t> visited;
    while (type_id.valid() && visited.insert(type_id.value()).second) {
        const auto type = unit.find_type(type_id);
        if (!type || type->vhdl == nullptr) {
            return nullptr;
        }
        if (type->vhdl->form == required) {
            return type->vhdl;
        }
        if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
            && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
            return nullptr;
        }
        type_id = type->vhdl->base.type_mark.target;
    }
    return nullptr;
}

std::optional<std::size_t> hir_vhdl_subtype_width(
    const semantic::vhdl::SubtypeIndication& subtype)
{
    if (subtype.executable_width && *subtype.executable_width != 0U
        && *subtype.executable_width
            <= std::numeric_limits<std::size_t>::max()) {
        return static_cast<std::size_t>(*subtype.executable_width);
    }
    if (subtype.domain == semantic::vhdl::ValueDomain::integer
        && subtype.integer_storage_width != 0U) {
        return subtype.integer_storage_width;
    }
    return std::nullopt;
}

frontend::ValueDomain hir_vhdl_domain(
    const semantic::vhdl::ValueDomain domain)
{
    switch (domain) {
    case semantic::vhdl::ValueDomain::bit2:
        return frontend::ValueDomain::Bit2;
    case semantic::vhdl::ValueDomain::logic4:
        return frontend::ValueDomain::Logic4;
    case semantic::vhdl::ValueDomain::logic9:
        return frontend::ValueDomain::Logic9;
    case semantic::vhdl::ValueDomain::boolean:
        return frontend::ValueDomain::Boolean;
    case semantic::vhdl::ValueDomain::integer:
        return frontend::ValueDomain::Integer;
    case semantic::vhdl::ValueDomain::string:
        return frontend::ValueDomain::String;
    case semantic::vhdl::ValueDomain::unknown:
        break;
    }
    return frontend::ValueDomain::Unknown;
}

} // namespace

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
    const frontend::SourceSpan& span)
{
    if (!type.designated_subtype) {
        report(
            "FSIM-ELAB-VHACCESS-005",
            "a VHDL access operation requires one resolved designated subtype",
            span);
        return nullptr;
    }
    const auto designated = hir_effective_vhdl_subtype(
        *type.designated_subtype).value_or(*type.designated_subtype);
    const auto width = hir_vhdl_subtype_width(designated);
    if (!width || *width == 0U
        || *width > std::numeric_limits<std::uint32_t>::max()
        || designated.domain == semantic::vhdl::ValueDomain::string) {
        report(
            "FSIM-ELAB-VHACCESS-006",
            "a bounded VHDL allocator requires a concrete nonempty packed designated subtype",
            span);
        return nullptr;
    }
    if (designated.domain == semantic::vhdl::ValueDomain::logic9) {
        report(
            "FSIM-ELAB-VHACCESS-007",
            "nine-state designated objects require the later access-object state extension",
            span);
        return nullptr;
    }
    if (const auto found = hir_vhdl_access_heaps_.find(type.id.value());
        found != hir_vhdl_access_heaps_.end()) {
        return &found->second;
    }
    ContainerType objects_type;
    objects_type.element_width = static_cast<std::uint32_t>(*width);
    objects_type.two_state = is_two_state_domain(
        hir_vhdl_domain(designated.domain));
    objects_type.signed_elements = designated.signed_value;
    objects_type.associative = true;
    objects_type.index_width = 32U;
    objects_type.two_state_indices = true;
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

std::optional<RegisterId> Lowerer::lower_hir_vhdl_access_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
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
        return subtype
            ? hir_vhdl_named_type(
                  *specialized_hir_unit_, *subtype,
                  semantic::vhdl::TypeForm::access)
            : nullptr;
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
        const semantic::vhdl::TypeDefinition* selected = nullptr;
        for (const auto& stored :
            specialized_hir_unit_->design().vhdl_hir.types()) {
            const auto candidate = specialized_hir_unit_->find_type(
                stored.id);
            if (!candidate || candidate->vhdl == nullptr
                || candidate->vhdl->form
                    != semantic::vhdl::TypeForm::access
                || !candidate->vhdl->designated_subtype
                || !same_vhdl_name(
                    candidate->vhdl->designated_subtype
                        ->type_mark.spelling,
                    designated_name)) {
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
        const auto* lhs_type = access_type(source.operands[0]);
        const auto* rhs_type = access_type(source.operands[1]);
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
        const auto result = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant { result, unsigned_value(0U, 32U) });
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
        auto* heap = hir_vhdl_access_heap(*type, span);
        if (heap == nullptr) {
            return std::nullopt;
        }
        const auto width = hir_vhdl_subtype_width(
            heap->designated_subtype);
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
            value = allocate_register(
                *width,
                hir_vhdl_domain(heap->designated_subtype.domain));
            process_.operations.emplace_back(LoadConstant {
                *value, unsigned_value(0U, *width) });
        }
        if (!value) {
            report(
                "FSIM-ELAB-VHACCESS-012",
                "a qualified VHDL allocator requires exactly one initial value",
                span);
            return std::nullopt;
        }
        const auto designated_domain = hir_vhdl_domain(
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
        const auto issued_size = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ContainerSize {
            issued_size, heap->issued_handles });
        const auto live_size = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ContainerSize {
            live_size, heap->objects });
        const auto maximum = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            maximum,
            unsigned_value(heap->maximum_live_objects, 32U) });
        const auto within_limit = allocate_register(
            1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_unsigned,
            within_limit,
            live_size,
            maximum });
        process_.operations.emplace_back(Assert {
            within_limit,
            "VHDL access allocation exceeded its owning-storage limit",
            AssertionSeverity::failure,
            hir_vhdl_source_location(span),
        });
        const auto one = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant { one, unsigned_value(1U, 32U) });
        const auto handle = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_unsigned, handle, issued_size, one });
        process_.operations.emplace_back(ContainerWrite {
            heap->objects, handle, *value, false });
        const auto token = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant { token, unsigned_value(1U, 1U) });
        process_.operations.emplace_back(PushContainer {
            heap->issued_handles, token, false, std::nullopt });
        return handle;
    }
    if (source.text != "@vhdl-dereference"
        || source.operands.size() != 1U) {
        return std::nullopt;
    }
    const auto* type = access_type(source.operands.front());
    auto* heap = type != nullptr
        ? hir_vhdl_access_heap(*type, span)
        : nullptr;
    const auto handle = lower_hir_expression(
        source.operands.front(), 32U);
    if (heap == nullptr || !handle) {
        return std::nullopt;
    }
    const auto live_result = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ContainerExists {
        live_result, heap->objects, *handle, false });
    const auto live = resize_register(live_result, 1U, false);
    process_.operations.emplace_back(Assert {
        live,
        "VHDL access dereference used a null, stale, or foreign handle",
        AssertionSeverity::failure,
        hir_vhdl_source_location(span),
    });
    const auto width = hir_vhdl_subtype_width(heap->designated_subtype);
    const auto result = allocate_register(
        *width, hir_vhdl_domain(heap->designated_subtype.domain));
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
    const auto zero = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(
        LoadConstant { zero, unsigned_value(0U, 32U) });
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
        && same_vhdl_name(
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
    return target->vhdl->kind
            == semantic::vhdl::ExpressionKind::name
        && subtype
        && hir_vhdl_named_type(
            *specialized_hir_unit_, *subtype,
            semantic::vhdl::TypeForm::access)
            != nullptr;
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
    if (target->vhdl->kind
        == semantic::vhdl::ExpressionKind::name) {
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
            if (assigned_type == nullptr
                || assigned_type->id != type->id) {
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
        auto* heap = type != nullptr
            ? hir_vhdl_access_heap(
                  *type, hir_source_span(target->vhdl->source))
            : nullptr;
        const auto assigned = lower_hir_expression(
            *statement->vhdl->value, 32U);
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
    const auto member = hir_vhdl_member_selection(
        *statement->vhdl->target);
    const auto dereference_id = member && member->root.valid()
        ? member->root
        : *statement->vhdl->target;
    const auto dereference = specialized_hir_unit_->find_expression(
        dereference_id);
    if (!dereference || dereference->vhdl == nullptr
        || dereference->vhdl->kind
            != semantic::vhdl::ExpressionKind::call
        || dereference->vhdl->text != "@vhdl-dereference"
        || dereference->vhdl->operands.size() != 1U) {
        return false;
    }
    const auto prefix = dereference->vhdl->operands.front();
    const auto subtype = hir_vhdl_expression_subtype(prefix);
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
        *type, hir_source_span(target->vhdl->source));
    const auto designated = hir_effective_vhdl_subtype(
        *type->designated_subtype).value_or(*type->designated_subtype);
    const auto width = hir_vhdl_subtype_width(designated);
    const auto assignment_width = member
        ? std::optional { member->width }
        : width;
    const auto handle = lower_hir_expression(prefix, 32U);
    const auto input = specialized_hir_unit_->find_expression(
        *statement->vhdl->value);
    auto value = !assignment_width
        ? std::optional<RegisterId> { }
        : input && input->vhdl != nullptr
                && input->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate
                && !member
        ? lower_hir_vhdl_aggregate(
              *statement->vhdl->value, *width, &designated)
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
        : hir_vhdl_domain(designated.domain);
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
    const auto live_result = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ContainerExists {
        live_result, heap->objects, *handle, false });
    const auto live = resize_register(live_result, 1U, false);
    process_.operations.emplace_back(Assert {
        live,
        "VHDL access dereference used a null, stale, or foreign handle",
        AssertionSeverity::failure,
        hir_vhdl_source_location(hir_source_span(target->vhdl->source)),
    });
    if (member) {
        if (member->offset > *width
            || member->width > *width - member->offset
            || member->offset
                > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const auto current = allocate_register(
            *width, hir_vhdl_domain(designated.domain));
        process_.operations.emplace_back(ContainerRead {
            current, heap->objects, *handle, false });
        const auto updated = allocate_register(
            *width, hir_vhdl_domain(designated.domain));
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
    auto heap = hir_vhdl_access_heaps_.find(type->id.value());
    if (heap == hir_vhdl_access_heaps_.end()) {
        return false;
    }
    const auto released = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(CopyRegister {
        released, *binding->local });
    if (type->deallocate_releases_storage) {
        process_.operations.emplace_back(DeleteContainer {
            heap->second.objects, released, false });
    }
    process_.operations.emplace_back(LoadConstant {
        *binding->local, unsigned_value(0U, 32U) });
    emit_hir_vhdl_access_reclamation(
        *type, heap->second, released);
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

std::optional<RegisterId> Lowerer::lower_hir_vhdl_physical_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->operands.size() != 1U
        || (expected_width != 32U && expected_width != 64U)) {
        return std::nullopt;
    }
    constexpr std::string_view prefix { "@vhdl-physical:" };
    const auto unit_name = std::string_view {
        expression->vhdl->text
    }.substr(prefix.size());
    std::optional<std::int64_t> scale;
    for (const auto& stored :
        specialized_hir_unit_->design().vhdl_hir.types()) {
        const auto type = specialized_hir_unit_->find_type(stored.id);
        if (!type || type->vhdl == nullptr
            || type->vhdl->form
                != semantic::vhdl::TypeForm::physical) {
            continue;
        }
        const auto width = hir_vhdl_subtype_width(type->vhdl->base);
        if (width && *width != expected_width) {
            continue;
        }
        const auto unit = std::ranges::find_if(
            type->vhdl->physical_units,
            [&](const semantic::vhdl::PhysicalUnit& candidate) {
                return same_vhdl_name(candidate.name, unit_name);
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
        const auto unit_scale = resolved_scale->second;
        if (scale && *scale != unit_scale) {
            return std::nullopt;
        }
        scale = unit_scale;
    }
    const auto magnitude
        = specialized_hir_unit_->evaluate_integral_expression(
            expression->vhdl->operands.front());
    if (!scale || !magnitude) {
        report(
            "FSIM-ELAB-VHPHYSICAL-006",
            "invalid physical literal unit or magnitude",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    }
    if (*scale <= 0
        || (*magnitude > 0
            && *magnitude
                > std::numeric_limits<std::int64_t>::max() / *scale)
        || (*magnitude < 0
            && *magnitude
                < std::numeric_limits<std::int64_t>::min() / *scale)) {
        report(
            "FSIM-ELAB-VHPHYSICAL-006",
            "physical literal exceeds its bounded runtime representation",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    }
    const auto value = *magnitude * *scale;
    if (expected_width == 32U
        && (value < std::numeric_limits<std::int32_t>::min()
            || value > std::numeric_limits<std::int32_t>::max())) {
        report(
            "FSIM-ELAB-VHPHYSICAL-006",
            "physical literal exceeds its bounded runtime representation",
            hir_source_span(expression->vhdl->source));
        return std::nullopt;
    }
    const auto result = allocate_register(
        expected_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant {
        result,
        unsigned_value(
            static_cast<std::uint64_t>(value), expected_width),
    });
    return result;
}

} // namespace fsim::elaboration
