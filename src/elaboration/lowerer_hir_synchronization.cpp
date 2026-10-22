// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <fsim/semantic/systemverilog_type_descriptor.hpp>

#include <algorithm>
#include <cstdlib>

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

[[nodiscard]] bool mailbox_string_element(
    const semantic::sv::TypeReference& type)
{
    if (type.value_form == semantic::sv::TypeForm::string) {
        return true;
    }
    auto name = std::string_view { type.target.spelling };
    if (const auto separator = name.rfind("::");
        separator != std::string_view::npos) {
        name.remove_prefix(separator + 2U);
    }
    return name == "string";
}

} // namespace

std::optional<Lowerer::HirMailboxElement> Lowerer::hir_mailbox_element(
    const semantic::sv::TypeReference* mailbox_type,
    const std::string_view message, const frontend::SourceSpan& span)
{
    const auto* element = mailbox_element_type(mailbox_type);
    const auto resolver = hir_resolver();
    // A typedef's bounds use the names visible where it is declared (a
    // class's local parameters).
    auto element_scope = hir_process_scope_;
    // A typedef name actual (`mailbox #(my_t)`) parses as an expression;
    // it denotes the type the typedef names (IEEE 1800-2017 15.4, 6.18).
    if (element == nullptr && synchronization_type(mailbox_type, "mailbox")
        && mailbox_type->interface_parameter_actuals.size() == 1U) {
        const auto& actual = mailbox_type->interface_parameter_actuals.front();
        const auto expression = actual.kind == semantic::sv::ActualKind::expression
                && actual.expression
            ? specialized_hir_unit_->find_expression(*actual.expression)
            : std::nullopt;
        const auto alias = expression && expression->systemverilog != nullptr
                && expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::name
            ? resolver.resolve_systemverilog_named_type(
                  expression->systemverilog->text,
                  expression->systemverilog->scope).unique()
            : std::nullopt;
        const auto declaration = alias
            ? specialized_hir_unit_->find_declaration(*alias)
            : std::nullopt;
        if (declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->type) {
            element = &*declaration->systemverilog->type;
            element_scope = declaration->systemverilog->scope;
        }
    } else if (element != nullptr) {
        // A typedef type actual (`mailbox #(C::t)`).
        const auto definition = element->target.target.valid()
            ? specialized_hir_unit_->find_type(element->target.target)
            : std::nullopt;
        const auto alias = definition && definition->systemverilog != nullptr
            ? std::optional { definition->systemverilog->declaration }
            : !element->target.spelling.empty()
            ? resolver.resolve_systemverilog_named_type(
                  element->target.spelling, hir_process_scope_).unique()
            : std::nullopt;
        const auto declaration = alias
            ? specialized_hir_unit_->find_declaration(*alias)
            : std::nullopt;
        if (declaration && declaration->systemverilog != nullptr) {
            element_scope = declaration->systemverilog->scope;
        }
    }
    const auto effective_element = element
        ? std::optional { fully_effective_systemverilog_type(
              resolver, *element, element_scope) }
        : std::nullopt;
    if (effective_element && mailbox_string_element(*effective_element)) {
        return HirMailboxElement { true, 0U };
    }
    // A typedef element (`mailbox #(my_t)`) has its aliased type's width.
    auto width = element
        ? hir_systemverilog_type_width(*element, element_scope)
        : std::nullopt;
    if (!width && effective_element) {
        width = hir_systemverilog_type_width(
            *effective_element, element_scope);
    }
    // A fixed-size unpacked element is carried as its bit stream (IEEE
    // 1800-2017 11.4.14).
    if (!width && element) {
        if (const auto described = semantic::describe_systemverilog_type(
                *specialized_hir_unit_, *element, element_scope);
            described && described->bits && *described->bits != 0U) {
            std::uint64_t bits = *described->bits;
            for (const auto& dimension : described->unpacked) {
                if (!dimension.container.empty() || !dimension.left
                    || !dimension.right) {
                    bits = 0U;
                    break;
                }
                bits *= static_cast<std::uint64_t>(std::llabs(
                            *dimension.left - *dimension.right))
                    + 1U;
            }
            if (bits != 0U) {
                width = static_cast<std::size_t>(bits);
            }
        }
    }
    // Unpacked bounds written with class local parameters (`int t[2**W]`)
    // fold as constant expressions.
    if (!width && element && !element->unpacked_dimensions.empty()
        && !element->container_form) {
        auto packed = *element;
        packed.unpacked_dimensions.clear();
        auto bits = hir_systemverilog_type_width(packed, element_scope)
                        .value_or(0U);
        for (const auto& dimension : element->unpacked_dimensions) {
            const auto bound = [&](const std::optional<std::int64_t> value,
                                   const std::optional<semantic::ExpressionId> expression) {
                return value ? value
                    : expression ? hir_constant_integer(*expression)
                    : std::nullopt;
            };
            const auto left = bound(dimension.left, dimension.left_expression);
            const auto right = bound(dimension.right, dimension.right_expression);
            if (!left || !right) {
                bits = 0U;
                break;
            }
            bits *= static_cast<std::size_t>(std::llabs(*left - *right)) + 1U;
        }
        if (bits != 0U) {
            width = bits;
        }
    }
    // Bounds written with class parameters fold in the active class frame.
    if (!width && element) {
        if (const auto container = hir_systemverilog_container_type(*element);
            container && container->fixed
            && container->element_kind
                == runtime::simir::ContainerElementKind::Packed) {
            std::uint64_t bits = container->element_width;
            for (const auto& [left, right] : container->dimensions) {
                bits *= static_cast<std::uint64_t>(std::llabs(
                            static_cast<std::int64_t>(left) - right))
                    + 1U;
            }
            if (bits != 0U) {
                width = static_cast<std::size_t>(bits);
            }
        }
    }
    if (!effective_element || !width || *width == 0U
        || *width > std::numeric_limits<std::uint32_t>::max()) {
        report("FSIM-ELAB-SVSYNC-002", std::string { message }, span);
        return std::nullopt;
    }
    return HirMailboxElement { false, static_cast<std::uint32_t>(*width) };
}

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
    const auto receiver
        = hir_resolver()
              .resolve_systemverilog(receiver_name, process_scope, { }, true)
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
    if (mailbox || synchronization_type(type, "semaphore")) {
        // A property named in a method (`mb.put(x)`) is the object's.
        auto property = hir_named_class_property(
            receiver_name, process_scope, 0U);
        const auto is_property = std::ranges::any_of(
            specialized_hir_unit_->design().systemverilog_hir.classes(),
            [&](const semantic::sv::ClassDeclaration& candidate) {
                return std::ranges::any_of(candidate.properties,
                    [&](const semantic::sv::ClassProperty& item) {
                        return item.declaration == *receiver;
                    });
            });
        if (!is_property) {
            property.reset();
        }
        return HirSynchronizationTaskProfile {
            *receiver, method, mailbox, std::move(property)
        };
    }
    // `c.mb.put(x)`: a property of a class object (IEEE 1800-2017 8.4).
    auto property = hir_named_class_property(receiver_name, process_scope, 0U);
    if (!property || property->type == nullptr) {
        return std::nullopt;
    }
    const bool property_mailbox
        = synchronization_type(property->type, "mailbox");
    if (!property_mailbox
        && !synchronization_type(property->type, "semaphore")) {
        return std::nullopt;
    }
    return HirSynchronizationTaskProfile {
        receiver.value_or(semantic::DeclarationId { }), method,
        property_mailbox, std::move(property)
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
    const auto binding = profile->class_property ? std::nullopt
        : hir_runtime_binding(profile->receiver, hir_process_scope_, true);
    const auto* receiver_type = profile->class_property
        ? profile->class_property->type
        : declaration
            && declaration->systemverilog != nullptr
            && declaration->systemverilog->type
        ? &*declaration->systemverilog->type
        : nullptr;
    const auto property_handle = profile->class_property
        ? lower_hir_named_class_property(*profile->class_property)
        : std::nullopt;
    if (profile->class_property && !property_handle) {
        return false;
    }
    if (!profile->class_property
        && (!binding || binding->width != 64U
            || (!binding->local && !binding->signal))) {
        report(
            "FSIM-ELAB-SVSYNC-001",
            "mailbox/semaphore method requires a compatible writable "
            "64-bit receiver",
            hir_source_span(statement.source));
        return true;
    }
    const auto receiver = [&]() {
        if (property_handle) {
            return *property_handle;
        }
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
        record_implicit_signal_dependency(*binding->signal);
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
        const auto mailbox_element = hir_mailbox_element(
            receiver_type,
            "mailbox operations require exactly one finite positive "
            "packed element type",
            hir_source_span(statement.source));
        if (!mailbox_element) {
            return true;
        }
        const bool string_element = mailbox_element->string;
        const auto width = mailbox_element->width;
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
            if (string_element) {
                const auto value = lower_hir_string_expression(argument);
                if (!value) {
                    return false;
                }
                process_.operations.emplace_back(MailboxPut {
                    receiver, 0U, 0U, std::nullopt,
                    MailboxElementKind::string, *value,
                });
            } else {
                auto value = lower_hir_expression(argument, width);
                if (!value) {
                    return false;
                }
                if (register_width(*value) != width) {
                    value = resize_register(
                        *value, width, hir_expression_signed(argument));
                }
                process_.operations.emplace_back(MailboxPut {
                    receiver, *value, width,
                    std::nullopt,
                });
            }
            return true;
        }
        if (string_element) {
            const auto value = allocate_string_register();
            process_.operations.emplace_back(MailboxGet {
                receiver, 0U, 0U, std::nullopt,
                profile->method == "peek", MailboxElementKind::string,
                value,
            });
            return lower_hir_string_copy_out(argument, value);
        }
        const auto domain = hir_expression_domain(
            argument, hir_process_scope_)
                                .value_or(frontend::ValueDomain::Logic4);
        const auto value = allocate_register(width, domain);
        process_.operations.emplace_back(MailboxGet {
            receiver, value, width,
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
            const auto mailbox_element = hir_mailbox_element(
                expected_type,
                "typed mailbox construction requires exactly one finite "
                "positive packed element type",
                hir_source_span(source.source));
            if (!mailbox_element) {
                return { true, false, std::nullopt };
            }
            const bool string_element = mailbox_element->string;
            const auto width = mailbox_element->width;
            process_.operations.emplace_back(MailboxCreate {
                destination, *count,
                width,
                string_element ? MailboxElementKind::string
                               : MailboxElementKind::packed });
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
    const auto mailbox_result_truth = [&](const RegisterId result) {
        // try_get/try_peek expose an int result, while their copyout guard is
        // a SimIR scalar branch condition.
        const auto is_zero = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalNot { is_zero, result });
        const auto succeeded = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(
            LogicalNot { succeeded, is_zero });
        return succeeded;
    };

    if (mailbox) {
        const auto mailbox_element = hir_mailbox_element(
            receiver_type,
            "mailbox operations require exactly one finite positive "
            "packed element type",
            hir_source_span(source.source));
        if (!mailbox_element) {
            return { true, false, std::nullopt };
        }
        const bool string_element = mailbox_element->string;
        const auto element_width = mailbox_element->width;
        if (method == "num" && argument_count == 0U) {
            const auto destination = result();
            process_.operations.emplace_back(
                MailboxNum { destination, *receiver });
            return { true, true, resize_result(destination) };
        }
        if ((method == "put" || method == "try_put")
            && argument_count == 1U) {
            if (string_element) {
                const auto value = lower_hir_string_expression(
                    source.operands[1]);
                if (!value) {
                    return { true, false, std::nullopt };
                }
                std::optional<RegisterId> destination;
                if (method == "try_put") {
                    destination = result();
                }
                process_.operations.emplace_back(MailboxPut {
                    *receiver, 0U, 0U, destination,
                    MailboxElementKind::string, *value,
                });
                return { true, true,
                    destination ? std::optional { resize_result(*destination) }
                                : std::nullopt };
            }
            auto value
                = lower_hir_expression(source.operands[1], element_width);
            if (!value) {
                return { true, false, std::nullopt };
            }
            if (register_width(*value) != element_width) {
                value = resize_register(
                    *value, element_width,
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
            if (string_element) {
                const auto value = allocate_string_register();
                std::optional<RegisterId> success;
                if (method.starts_with("try_")) {
                    success = result();
                }
                process_.operations.emplace_back(MailboxGet {
                    *receiver, 0U, 0U, success, peek,
                    MailboxElementKind::string, value,
                });
                if (success) {
                    const auto condition
                        = mailbox_result_truth(*success);
                    const auto branch = process_.operations.size();
                    process_.operations.emplace_back(Branch {
                        condition, 0U, 0U, UnknownBranchPolicy::error });
                    const auto copy_out = static_cast<InstructionIndex>(
                        process_.operations.size());
                    if (!lower_hir_string_copy_out(
                            source.operands[1], value)) {
                        return { true, false, std::nullopt };
                    }
                    const auto end = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations[branch] = Branch {
                        condition, copy_out, end,
                        UnknownBranchPolicy::error };
                } else if (!lower_hir_string_copy_out(
                               source.operands[1], value)) {
                    return { true, false, std::nullopt };
                }
                return success
                    ? HirSynchronizationAttempt {
                          true, true, resize_result(*success) }
                    : HirSynchronizationAttempt { true, true, std::nullopt };
            }
            const auto domain = hir_expression_domain(
                source.operands[1], hir_process_scope_)
                                    .value_or(frontend::ValueDomain::Logic4);
            const auto value = allocate_register(element_width, domain);
            std::optional<RegisterId> success;
            if (method.starts_with("try_")) {
                success = result();
            }
            process_.operations.emplace_back(MailboxGet {
                *receiver, value, element_width, success, peek });
            if (success) {
                const auto condition
                    = mailbox_result_truth(*success);
                const auto branch = process_.operations.size();
                process_.operations.emplace_back(Branch {
                    condition, 0U, 0U, UnknownBranchPolicy::error });
                const auto copy_out = static_cast<InstructionIndex>(
                    process_.operations.size());
                if (!lower_hir_packed_copy_out(
                        source.operands[1], value)) {
                    return { true, false, std::nullopt };
                }
                const auto end = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations[branch] = Branch {
                    condition, copy_out, end, UnknownBranchPolicy::error };
            } else if (!lower_hir_packed_copy_out(
                           source.operands[1], value)) {
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
