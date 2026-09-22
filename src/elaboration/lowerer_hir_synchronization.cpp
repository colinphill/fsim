// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
namespace {

[[nodiscard]] bool synchronization_type(
    const semantic::sv::TypeReference* type,
    const std::string_view spelling)
{
    if (type == nullptr) {
        return false;
    }
    auto name = std::string_view { type->target.spelling };
    if (const auto separator = name.rfind("::");
        separator != std::string_view::npos) {
        name.remove_prefix(separator + 2U);
    }
    return name == spelling;
}

[[nodiscard]] const semantic::sv::TypeReference* mailbox_element_type(
    const semantic::sv::TypeReference* type)
{
    if (!synchronization_type(type, "mailbox")
        || type->interface_parameter_actuals.size() != 1U) {
        return nullptr;
    }
    const auto& actual = type->interface_parameter_actuals.front();
    return actual.kind == semantic::sv::ActualKind::type && actual.type
        ? &*actual.type
        : nullptr;
}

} // namespace

std::optional<Lowerer::HirSynchronizationTaskProfile>
Lowerer::hir_synchronization_task_profile(
    const semantic::sv::Statement& statement,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr
        || statement.kind != semantic::sv::StatementKind::task_call) {
        return std::nullopt;
    }
    const auto separator = statement.task.spelling.rfind('.');
    if (separator == std::string::npos || separator == 0U
        || separator + 1U == statement.task.spelling.size()) {
        return std::nullopt;
    }
    const auto receiver_name = std::string_view {
        statement.task.spelling
    }.substr(0U, separator);
    const auto method = std::string_view {
        statement.task.spelling
    }.substr(separator + 1U);
    const auto supported_method = method == "put" || method == "get"
        || method == "peek" || method == "try_put"
        || method == "try_get" || method == "try_peek";
    if (!supported_method) {
        return std::nullopt;
    }
    const auto receiver = semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    }.resolve_systemverilog(receiver_name, process_scope, { }, true)
         .unique();
    const auto declaration = receiver
        ? specialized_hir_unit_->find_declaration(*receiver)
        : std::nullopt;
    const auto* type = declaration
            && declaration->systemverilog != nullptr
            && declaration->systemverilog->type
        ? &*declaration->systemverilog->type
        : nullptr;
    const bool mailbox = synchronization_type(type, "mailbox");
    if (!mailbox && !synchronization_type(type, "semaphore")) {
        return std::nullopt;
    }
    return HirSynchronizationTaskProfile {
        *receiver, method, mailbox
    };
}

bool Lowerer::lower_hir_synchronization_statement(
    const semantic::sv::Statement& statement)
{
    using namespace runtime::simir;
    const auto profile = hir_synchronization_task_profile(
        statement, hir_process_scope_);
    if (!profile || specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        profile->receiver);
    const auto binding = hir_runtime_binding(
        profile->receiver, hir_process_scope_, true);
    const auto* receiver_type = declaration
            && declaration->systemverilog != nullptr
            && declaration->systemverilog->type
        ? &*declaration->systemverilog->type
        : nullptr;
    if (!binding || binding->width != 64U
        || (!binding->local && !binding->signal)) {
        report(
            "FSIM-ELAB-SVSYNC-001",
            "mailbox/semaphore method requires a compatible writable "
            "64-bit receiver",
            hir_source_span(statement.source));
        return true;
    }
    const auto receiver = [&]() {
        if (binding->local) {
            return *binding->local;
        }
        const auto value = allocate_register(
            binding->width, binding->domain);
        process_.operations.emplace_back(ReadSignal {
            value, *binding->signal,
            sample_concurrent_assertion_reads_
                ? SignalReadKind::sampled
                : SignalReadKind::current,
        });
        implicit_signal_dependencies_.push_back(*binding->signal);
        return value;
    }();
    const auto arguments_valid = std::ranges::all_of(
        statement.task_arguments,
        [](const semantic::sv::TaskAssociation& argument) {
            return !argument.formal && argument.actual;
        });
    if (!arguments_valid) {
        report(
            profile->mailbox
                ? "FSIM-ELAB-SVSYNC-003"
                : "FSIM-ELAB-SVSYNC-004",
            "mailbox/semaphore methods require positional value arguments",
            hir_source_span(statement.source));
        return true;
    }

    if (profile->mailbox) {
        const auto* element = mailbox_element_type(receiver_type);
        const auto width = element
            ? hir_systemverilog_type_width(*element)
            : std::nullopt;
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-SVSYNC-002",
                "mailbox operations require exactly one finite positive "
                "packed element type",
                hir_source_span(statement.source));
            return true;
        }
        if ((profile->method != "put" && profile->method != "get"
                && profile->method != "peek")
            || statement.task_arguments.size() != 1U) {
            report(
                "FSIM-ELAB-SVSYNC-003",
                "mailbox task methods require put, get, or peek with one "
                "positional argument",
                hir_source_span(statement.source));
            return true;
        }
        const auto argument = *statement.task_arguments.front().actual;
        if (profile->method == "put") {
            auto value = lower_hir_expression(argument, *width);
            if (!value) {
                return false;
            }
            if (register_width(*value) != *width) {
                value = resize_register(
                    *value, *width, hir_expression_signed(argument));
            }
            process_.operations.emplace_back(MailboxPut {
                receiver, *value, static_cast<std::uint32_t>(*width),
                std::nullopt,
            });
            return true;
        }
        const auto domain = hir_expression_domain(
            argument, hir_process_scope_)
                                .value_or(frontend::ValueDomain::Logic4);
        const auto value = allocate_register(*width, domain);
        process_.operations.emplace_back(MailboxGet {
            receiver, value, static_cast<std::uint32_t>(*width),
            std::nullopt, profile->method == "peek",
        });
        return lower_hir_packed_copy_out(argument, value);
    }

    if ((profile->method != "put" && profile->method != "get")
        || statement.task_arguments.size() > 1U) {
        report(
            "FSIM-ELAB-SVSYNC-004",
            "semaphore task methods require get or put with zero or one "
            "key-count argument",
            hir_source_span(statement.source));
        return true;
    }
    const auto keys = statement.task_arguments.empty()
        ? std::optional { allocate_register(
              32U, frontend::ValueDomain::Integer) }
        : lower_hir_expression(
              *statement.task_arguments.front().actual, 32U);
    if (!keys) {
        return false;
    }
    if (statement.task_arguments.empty()) {
        process_.operations.emplace_back(LoadConstant {
            *keys, PackedLogic4::from_aval_bval(32U, 1U, 0U) });
    }
    if (profile->method == "put") {
        process_.operations.emplace_back(SemaphorePut {
            receiver, *keys });
    } else {
        process_.operations.emplace_back(SemaphoreGet {
            receiver, *keys, std::nullopt });
    }
    return true;
}

Lowerer::HirSynchronizationAttempt
Lowerer::lower_hir_synchronization_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width,
    const semantic::sv::TypeReference* expected_type)
{
    using namespace runtime::simir;
    if (language_ != frontend::Language::SystemVerilog2017
        || specialized_hir_unit_ == nullptr) {
        return { };
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return { };
    }
    const auto& source = *expression->systemverilog;
    if (source.kind != semantic::sv::ExpressionKind::call) {
        return { };
    }

    if (source.text.starts_with("@sv-sync-new:")) {
        const auto kind = std::string_view { source.text }.substr(13U);
        const bool mailbox = kind == "mailbox"
            && synchronization_type(expected_type, "mailbox");
        const bool semaphore = kind == "semaphore"
            && synchronization_type(expected_type, "semaphore");
        if ((!mailbox && !semaphore) || expected_width != 64U
            || source.operands.size() > 1U) {
            report(
                "FSIM-ELAB-SVSYNC-001",
                "mailbox/semaphore construction requires a compatible "
                "64-bit destination and zero or one count argument",
                hir_source_span(source.source));
            return { true, false, std::nullopt };
        }
        const auto count = source.operands.empty()
            ? std::optional { allocate_register(
                  32U, frontend::ValueDomain::Integer) }
            : lower_hir_expression(source.operands.front(), 32U);
        if (!count) {
            return { true, false, std::nullopt };
        }
        if (source.operands.empty()) {
            process_.operations.emplace_back(LoadConstant {
                *count, PackedLogic4::from_aval_bval(32U, 0U, 0U) });
        }
        const auto destination = allocate_register(
            64U, frontend::ValueDomain::Bit2);
        if (mailbox) {
            const auto* element = mailbox_element_type(expected_type);
            const auto width = element
                ? hir_systemverilog_type_width(*element)
                : std::nullopt;
            if (!width || *width == 0U
                || *width > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-SVSYNC-002",
                    "typed mailbox construction requires exactly one finite "
                    "positive packed element type",
                    hir_source_span(source.source));
                return { true, false, std::nullopt };
            }
            process_.operations.emplace_back(MailboxCreate {
                destination, *count, static_cast<std::uint32_t>(*width) });
        } else {
            process_.operations.emplace_back(
                SemaphoreCreate { destination, *count });
        }
        return { true, true, destination };
    }

    if (!source.text.starts_with('.') || source.operands.empty()) {
        return { };
    }
    const auto receiver_declaration = hir_target_declaration(
        source.operands.front());
    const auto declaration = receiver_declaration
        ? specialized_hir_unit_->find_declaration(*receiver_declaration)
        : std::nullopt;
    const auto* receiver_type = declaration
            && declaration->systemverilog != nullptr
            && declaration->systemverilog->type
        ? &*declaration->systemverilog->type
        : nullptr;
    const bool mailbox = synchronization_type(receiver_type, "mailbox");
    const bool semaphore = synchronization_type(receiver_type, "semaphore");
    if (!mailbox && !semaphore) {
        return { };
    }
    const auto receiver = lower_hir_expression(
        source.operands.front(), 64U,
        frontend::SystemVerilogScalarKind::Chandle);
    if (!receiver) {
        return { true, false, std::nullopt };
    }
    const auto method = std::string_view { source.text }.substr(1U);
    const auto argument_count = source.operands.size() - 1U;
    const auto result = [&] {
        return allocate_register(32U, frontend::ValueDomain::Integer);
    };
    const auto resize_result = [&](const RegisterId value) {
        return expected_width != 0U && expected_width != 32U
            ? resize_register(value, expected_width, false)
            : value;
    };

    if (mailbox) {
        const auto* element = mailbox_element_type(receiver_type);
        const auto width = element
            ? hir_systemverilog_type_width(*element)
            : std::nullopt;
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-SVSYNC-002",
                "mailbox operations require exactly one finite positive "
                "packed element type",
                hir_source_span(source.source));
            return { true, false, std::nullopt };
        }
        const auto element_width = static_cast<std::uint32_t>(*width);
        if (method == "num" && argument_count == 0U) {
            const auto destination = result();
            process_.operations.emplace_back(
                MailboxNum { destination, *receiver });
            return { true, true, resize_result(destination) };
        }
        if ((method == "put" || method == "try_put")
            && argument_count == 1U) {
            auto value = lower_hir_expression(source.operands[1], *width);
            if (!value) {
                return { true, false, std::nullopt };
            }
            if (register_width(*value) != *width) {
                value = resize_register(
                    *value, *width,
                    hir_expression_signed(source.operands[1]));
            }
            if (method == "put") {
                process_.operations.emplace_back(MailboxPut {
                    *receiver, *value, element_width, std::nullopt });
                return { true, true, std::nullopt };
            }
            const auto destination = result();
            process_.operations.emplace_back(MailboxPut {
                *receiver, *value, element_width, destination });
            return { true, true, resize_result(destination) };
        }
        const bool get = method == "get" || method == "try_get";
        const bool peek = method == "peek" || method == "try_peek";
        if ((get || peek) && argument_count == 1U) {
            const auto domain = hir_expression_domain(
                source.operands[1], hir_process_scope_)
                                    .value_or(frontend::ValueDomain::Logic4);
            const auto value = allocate_register(*width, domain);
            std::optional<RegisterId> success;
            if (method.starts_with("try_")) {
                success = result();
            }
            process_.operations.emplace_back(MailboxGet {
                *receiver, value, element_width, success, peek });
            if (!lower_hir_packed_copy_out(source.operands[1], value)) {
                return { true, false, std::nullopt };
            }
            return success
                ? HirSynchronizationAttempt {
                      true, true, resize_result(*success) }
                : HirSynchronizationAttempt { true, true, std::nullopt };
        }
        report(
            "FSIM-ELAB-SVSYNC-003",
            "mailbox method requires the standard num, put/try_put, "
            "get/try_get, or peek/try_peek arity",
            hir_source_span(source.source));
        return { true, false, std::nullopt };
    }

    if ((method == "get" || method == "try_get" || method == "put")
        && argument_count <= 1U) {
        const auto keys = argument_count == 0U
            ? std::optional { allocate_register(
                  32U, frontend::ValueDomain::Integer) }
            : lower_hir_expression(source.operands[1], 32U);
        if (!keys) {
            return { true, false, std::nullopt };
        }
        if (argument_count == 0U) {
            process_.operations.emplace_back(LoadConstant {
                *keys, PackedLogic4::from_aval_bval(32U, 1U, 0U) });
        }
        if (method == "put") {
            process_.operations.emplace_back(SemaphorePut {
                *receiver, *keys });
            return { true, true, std::nullopt };
        }
        if (method == "get") {
            process_.operations.emplace_back(SemaphoreGet {
                *receiver, *keys, std::nullopt });
            return { true, true, std::nullopt };
        }
        const auto destination = result();
        process_.operations.emplace_back(SemaphoreGet {
            *receiver, *keys, destination });
        return { true, true, resize_result(destination) };
    }
    report(
        "FSIM-ELAB-SVSYNC-004",
        "semaphore method requires get/try_get/put with zero or one key-count "
        "argument",
        hir_source_span(source.source));
    return { true, false, std::nullopt };
}

} // namespace fsim::elaboration
