// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <fsim/semantic/systemverilog_class_specialization.hpp>
#include <fsim/semantic/systemverilog_type_descriptor.hpp>
#include <fsim/support/payload_checksum.hpp>

#include <algorithm>
#include <cstdlib>

namespace fsim::elaboration {
namespace {

using StandardClass = support::SystemVerilogStandardClass;

[[nodiscard]] bool synchronization_type(
    const semantic::sv::TypeReference* type,
    const support::SystemVerilogStandardClass standard_class)
{
    return type != nullptr && type->standard_class == standard_class;
}

[[nodiscard]] const semantic::sv::TypeReference* mailbox_element_type(
    const semantic::sv::TypeReference* type)
{
    if (!synchronization_type(type, StandardClass::mailbox)
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
    // An unpacked array of strings is an array (7.4).
    if (type.container_form || !type.unpacked_dimensions.empty()) {
        return false;
    }
    if (type.value_form == semantic::sv::TypeForm::string) {
        return true;
    }
    return type.builtin == support::SystemVerilogBuiltinType::string;
}

} // namespace


// The bit-stream width of a message of an unpacked type carried as its bit
// stream: a structure whose every member is a packed or scalar leaf (not a
// union, 7.3), which a static or an automatic variable converts to and
// from in one layout (first member most significant). Any other unpacked
// type, arrays among them, is carried as a container value.
std::optional<std::uint32_t> Lowerer::hir_mailbox_stream_width(
    const std::optional<runtime::simir::ContainerType>& container) const
{
    const auto leaves = container && container->aggregate_value
        ? hir_aggregate_packed_leaves(*container)
        : std::nullopt;
    if (!leaves) {
        return std::nullopt;
    }
    std::uint64_t width { };
    for (const auto& leaf : *leaves) {
        width += leaf.width;
    }
    return width != 0U && width <= std::numeric_limits<std::uint32_t>::max()
        ? std::optional { static_cast<std::uint32_t>(width) }
        : std::nullopt;
}

// Store a message carried as its bit stream into `target`. A target of an
// unpacked structure type held as a container value (an automatic
// variable or formal) takes the stream's leaves, first member most
// significant, as a static variable of that type holds them.
bool Lowerer::lower_hir_mailbox_copy_out(
    const semantic::ExpressionId target, const RegisterId value)
{
    if (const auto binding = hir_container_object_binding(target);
        binding && binding->local && binding->type != nullptr
        && binding->type->aggregate_value) {
        const auto unpacked = unpack_hir_aggregate_value(value, *binding->type);
        if (!unpacked) {
            return false;
        }
        process_.operations.emplace_back(
            CopyContainerRegister { *binding->local, *unpacked });
        return true;
    }
    return lower_hir_packed_copy_out(target, value);
}

std::optional<Lowerer::HirMailboxElement> Lowerer::hir_mailbox_element(
    const semantic::sv::TypeReference* mailbox_type,
    const std::string_view message, const frontend::SourceSpan& span)
{
    // `mailbox` with no parameter is typeless (15.4.9).
    if (synchronization_type(mailbox_type, StandardClass::mailbox)
        && mailbox_type->interface_parameter_actuals.empty()) {
        return HirMailboxElement { false, 0U, true };
    }
    const auto* element = mailbox_element_type(mailbox_type);
    std::optional<semantic::sv::TypeReference> named_element;
    const auto resolver = hir_resolver();
    // A typedef's bounds use the names visible where it is declared (a
    // class's local parameters).
    auto element_scope = hir_process_scope_;
    // A typedef name actual (`mailbox #(my_t)`) parses as an expression;
    // it denotes the type the typedef names (IEEE 1800-2017 15.4, 6.18).
    if (element == nullptr && synchronization_type(mailbox_type, StandardClass::mailbox)
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
            // The typedef's own type, as a variable declared with the
            // typedef name has it (6.18): an unpacked structure or union
            // is the type the typedef declares, not its body's.
            if (declaration->systemverilog->declared_type) {
                named_element = *element;
                named_element->target.target
                    = *declaration->systemverilog->declared_type;
                named_element->target.spelling = declaration->systemverilog->name;
                element = &*named_element;
            }
        }
    } else if (element != nullptr) {
        // A typedef type actual (`mailbox #(C::t)`).
        const auto definition = element->target.target.valid()
            ? specialized_hir_unit_->find_type(element->target.target)
            : std::nullopt;
        const auto alias = definition && definition->systemverilog != nullptr
            ? std::optional { definition->systemverilog->declaration }
            : element->names_type()
                && !semantic::sv::names_builtin_type(*element)
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
    // An unpacked array, structure or union element (IEEE 1800-2023 15.4:
    // a mailbox holds data of any type): a fixed-size one of known width
    // is carried as its bit stream (11.4.14), any other as one container
    // value per message.
    if (element) {
        using Kind = semantic::SystemVerilogTypeDescriptor::Kind;
        // The type a class type parameter stands for in this
        // specialization (8.25).
        const auto described = semantic::describe_systemverilog_type(
            *specialized_hir_unit_,
            effective_element ? *effective_element : *element, element_scope);
        if (described
            && (!described->unpacked.empty()
                || ((described->kind == Kind::structure
                        || described->kind == Kind::union_type)
                    && !described->packed_aggregate))) {
            // The element type as written, as a variable declared with it
            // has it.
            auto container = hir_systemverilog_container_type(*element);
            if (!container && effective_element) {
                container = hir_systemverilog_container_type(*effective_element);
            }
            if (const auto bits = hir_mailbox_stream_width(container)) {
                return HirMailboxElement { false, *bits };
            }
            if (!container) {
                report("FSIM-ELAB-SVSYNC-002", std::string { message }, span);
                return std::nullopt;
            }
            HirMailboxElement result { false, 0U, false };
            result.container = *container;
            return result;
        }
    }
    // A typedef element (`mailbox #(my_t)`) has its aliased type's width.
    auto width = element
        ? hir_systemverilog_type_width(*element, element_scope)
        : std::nullopt;
    if (!width && effective_element) {
        width = hir_systemverilog_type_width(
            *effective_element, element_scope);
    }
    if (!effective_element || !width || *width == 0U
        || *width > std::numeric_limits<std::uint32_t>::max()) {
        report("FSIM-ELAB-SVSYNC-002", std::string { message }, span);
        return std::nullopt;
    }
    return HirMailboxElement { false, static_cast<std::uint32_t>(*width) };
}

std::optional<Lowerer::HirMailboxMessage>
Lowerer::hir_typeless_mailbox_message(const semantic::ExpressionId message,
    const frontend::SourceSpan& span)
{
    using Kind = semantic::SystemVerilogTypeDescriptor::Kind;
    // The message's static type: a cast's target type (`T'(x)`, `T'{...}`,
    // 6.24.1, 10.9), the declared type of a variable, member, element or
    // class property, else the type of the expression's form.
    // An enumeration literal has its enumeration's type (6.19.3), and a
    // cast to a named type has that type, not the type it is based on.
    std::optional<semantic::sv::TypeReference> declared;
    if (const auto expression = specialized_hir_unit_->find_expression(message);
        expression && expression->systemverilog != nullptr) {
        const auto& source = *expression->systemverilog;
        if (source.kind == semantic::sv::ExpressionKind::name
            && source.referenced_name && source.referenced_name->selected) {
            const auto literal = specialized_hir_unit_->find_declaration(
                *source.referenced_name->selected);
            if (literal && literal->systemverilog != nullptr
                && literal->systemverilog->form
                    == semantic::sv::DeclarationForm::enumeration_literal
                && literal->systemverilog->type) {
                declared = literal->systemverilog->type;
            }
        } else if (source.kind == semantic::sv::ExpressionKind::call
            && source.marker == support::ExpressionMarker::sv_cast
            && source.cast_target
                == support::SystemVerilogCastTarget::named) {
            const auto selected = hir_systemverilog_named_type_declaration(
                source.marker_argument, source.scope);
            const auto target = selected
                ? specialized_hir_unit_->find_declaration(*selected)
                : std::nullopt;
            if (target && target->systemverilog != nullptr
                && target->systemverilog->declared_type) {
                semantic::sv::TypeReference type;
                type.target.target = *target->systemverilog->declared_type;
                type.target.spelling = target->systemverilog->name;
                type.named = true;
                declared = type;
            }
        }
    }
    if (!declared) {
        declared = hir_systemverilog_cast_type(message);
    }
    if (!declared) {
        declared = hir_systemverilog_value_type(message);
    }
    const auto described = declared
        ? semantic::describe_systemverilog_type(
              *specialized_hir_unit_, *declared, hir_process_scope_)
        : semantic::describe_systemverilog_expression_type(
              *specialized_hir_unit_, message);
    // The equivalence class of a message's element type (6.22.2): a
    // string; a packed integral type by its total width, signedness and 2-
    // or 4-state values; an enumeration, a class and an unpacked structure
    // or union only by itself; realtime is real. `element_type` is the
    // element's type reference where known (a class specialization).
    const auto element_identity = [&](const semantic::SystemVerilogTypeDescriptor& element,
                                      const std::optional<semantic::sv::TypeReference>& element_type)
        -> std::optional<std::string> {
        if (element.kind == Kind::string) {
            return std::string { "string" };
        }
        if ((element.kind == Kind::structure || element.kind == Kind::union_type)
            && !element.packed_aggregate) {
            if (!element.nominal) {
                return std::nullopt;
            }
            return "aggregate:" + std::to_string(element.nominal->value());
        }
        std::string identity;
        switch (element.kind) {

        case Kind::integer_atom:
        case Kind::integer_vector:
            break;
        case Kind::structure:
        case Kind::union_type:
            if (!element.packed_aggregate) {
                return std::nullopt;
            }
            break;
        case Kind::real:
        case Kind::realtime:
            identity = "real";
            break;
        case Kind::shortreal:
            identity = "shortreal";
            break;
        case Kind::chandle:
            identity = "chandle";
            break;
        case Kind::enumeration:
        case Kind::class_handle: {
            // An enumeration is its enumeration type, however a typedef names
            // it; a class type is its class and, for a parameterized class, the
            // specialization its parameter values select (6.22.1, 8.25).
            const auto wanted = element.kind == Kind::enumeration
                ? semantic::sv::TypeForm::enumeration
                : semantic::sv::TypeForm::class_handle;
            std::optional<semantic::TypeId> nominal;
            std::vector<semantic::TypeId> visited;
            for (auto current = element.nominal.value_or(semantic::TypeId { });
                current.valid()
                && std::ranges::find(visited, current) == visited.end();) {
                visited.push_back(current);
                const auto type = specialized_hir_unit_->find_type(current);
                if (!type || type->systemverilog == nullptr) {
                    break;
                }
                if (type->systemverilog->form == wanted) {
                    nominal = current;
                    break;
                }
                if (type->systemverilog->form != semantic::sv::TypeForm::alias) {
                    break;
                }
                current = type->systemverilog->base.target.target;
            }
            std::optional<std::uint32_t> specialization;
            if (element.kind == Kind::class_handle && element_type) {
                const auto expression
                    = specialized_hir_unit_->find_expression(message);
                const auto source = expression && expression->systemverilog
                    ? expression->systemverilog->source
                    : semantic::SourceSpanId { };
                specialization
                    = hir_instance_class_specialization(*element_type, source);
                if (!specialization) {
                    const auto effective
                        = hir_resolver().effective_systemverilog_type(
                            *element_type, hir_process_scope_);
                    if (effective && !effective->class_identity.empty()) {
                        specialization = class_specializations().specialize(
                            *effective, hir_running_class_specialization(),
                            source);
                    }
                }
            }
            if (specialization) {
                identity = "class:" + std::to_string(*specialization);
            } else if (nominal) {
                identity = (element.kind == Kind::enumeration ? "enum:"
                                                                 : "class-type:")
                    + std::to_string(nominal->value());
            } else {
                return std::nullopt;
            }
            break;
        }
        case Kind::string:
        case Kind::event:
        case Kind::interface:
        case Kind::other:
            return std::nullopt;
        }
        // A class or chandle value is the 64-bit handle.
        const auto width = element.kind == Kind::class_handle
                || element.kind == Kind::chandle
            ? std::optional<std::uint64_t> { 64U }
            : element.total_bits();
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        if (identity.empty()) {
            identity = "packed:" + std::to_string(*width)
                + (element.is_signed ? ":signed" : ":unsigned")
                + (element.four_state ? ":4" : ":2");
        }
        return identity;
    };
    const auto refuse = [&]() -> std::optional<HirMailboxMessage> {
        report("FSIM-ELAB-SVSYNC-002",
            "a typeless mailbox message must have a type of known "
            "equivalence (IEEE 1800-2023 6.22.2, 15.4.9)",
            span);
        return std::nullopt;
    };
    if (!described) {
        return refuse();
    }
    // An unpacked array, structure or union is one container message
    // (15.4): a fixed-size array is equivalent to another of equivalent
    // elements and equal size, a dynamic array, queue or associative array
    // to one of the same kind with equivalent index and element types
    // (6.22.2 d, e).
    const bool unpacked_message = !described->unpacked.empty()
        || ((described->kind == Kind::structure
                || described->kind == Kind::union_type)
            && !described->packed_aggregate);
    if (unpacked_message) {
        const auto message_type = declared
            ? declared
            : semantic::systemverilog_type_reference(*described);
        const auto container = message_type
            ? hir_systemverilog_container_type(*message_type)
            : std::nullopt;
        // The element's type reference: the message type without its
        // unpacked dimensions.
        std::optional<semantic::sv::TypeReference> element_type;
        if (message_type) {
            auto current = hir_resolver()
                               .effective_systemverilog_type(
                                   *message_type, hir_process_scope_)
                               .value_or(*message_type);
            for (std::size_t depth { }; depth < 16U; ++depth) {
                if (current.container_form
                    && current.container_element_types.size() == 1U) {
                    current = current.container_element_types.front();
                } else if (!current.unpacked_dimensions.empty()) {
                    current.unpacked_dimensions.clear();
                    break;
                } else {
                    break;
                }
                current = hir_resolver()
                              .effective_systemverilog_type(
                                  current, hir_process_scope_)
                              .value_or(current);
            }
            element_type = current;
        }
        std::string identity { "unpacked" };
        using Form = semantic::sv::TypeForm;
        for (const auto& dimension : described->unpacked) {
            if (!dimension.form) {
                if (!dimension.left || !dimension.right) {
                    return refuse();
                }
                identity += ":fixed:"
                    + std::to_string(std::llabs(*dimension.left - *dimension.right) + 1);
            } else if (*dimension.form == Form::dynamic_array) {
                identity += ":dynamic";
            } else if (*dimension.form == Form::queue) {
                identity += ":queue";
            } else if (*dimension.form != Form::associative_array) {
                return refuse();
            } else if (dimension.wildcard_index) {
                identity += ":associative[*]";
            } else if (!dimension.index.empty()) {
                const auto index
                    = element_identity(dimension.index.front(), std::nullopt);
                if (!index) {
                    return refuse();
                }
                identity += ":associative[" + *index + "]";
            } else {
                return refuse();
            }
        }
        auto element = *described;
        element.unpacked.clear();
        const auto leaf = element_identity(element, element_type);
        if (!leaf) {
            return refuse();
        }
        identity += ":" + *leaf;
        if (const auto bits = hir_mailbox_stream_width(container)) {
            return HirMailboxMessage { false, *bits,
                support::Xxh64::digest(identity) | 1U };
        }
        if (!container) {
            return refuse();
        }
        HirMailboxMessage result { false, 0U,
            support::Xxh64::digest(identity) | 1U };
        result.container = *container;
        return result;
    }
    if (described->kind == Kind::string) {
        return HirMailboxMessage { true, 0U,
            support::Xxh64::digest("string") | 1U };
    }
    const auto identity = element_identity(*described, declared);
    if (!identity) {
        return refuse();
    }
    const auto width = described->kind == Kind::class_handle
            || described->kind == Kind::chandle
        ? std::optional<std::uint64_t> { 64U }
        : described->total_bits();
    return HirMailboxMessage { false, static_cast<std::uint32_t>(*width),
        support::Xxh64::digest(*identity) | 1U };
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
    const bool mailbox = synchronization_type(type, StandardClass::mailbox);
    if (mailbox || synchronization_type(type, StandardClass::semaphore)) {
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
        = synchronization_type(property->type, StandardClass::mailbox);
    if (!property_mailbox
        && !synchronization_type(property->type, StandardClass::semaphore)) {
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
        bool string_element = mailbox_element->string;
        auto width = mailbox_element->width;
        auto container = mailbox_element->container;
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
        // A typeless mailbox's message has the argument's own type.
        std::uint64_t message_type { };
        if (mailbox_element->dynamic) {
            const auto message = hir_typeless_mailbox_message(
                argument, hir_source_span(statement.source));
            if (!message) {
                return true;
            }
            string_element = message->string;
            width = message->width;
            message_type = message->type;
            container = message->container;
        }
        if (container) {
            if (profile->method == "put") {
                const auto value
                    = lower_hir_static_container_actual(argument, *container);
                if (!value) {
                    return false;
                }
                MailboxPut put {
                    receiver, 0U, 0U, std::nullopt,
                    MailboxElementKind::container, { }, message_type,
                };
                put.container_source = *value;
                process_.operations.emplace_back(std::move(put));
                return true;
            }
            const auto value = allocate_container_register(*container);
            MailboxGet get {
                receiver, 0U, 0U, std::nullopt, profile->method == "peek",
                MailboxElementKind::container, { }, message_type,
            };
            get.container_destination = value;
            process_.operations.emplace_back(std::move(get));
            return lower_hir_container_copy_out(argument, value);
        }
        if (profile->method == "put") {
            if (string_element) {
                const auto value = lower_hir_string_expression(argument);
                if (!value) {
                    return false;
                }
                process_.operations.emplace_back(MailboxPut {
                    receiver, 0U, 0U, std::nullopt,
                    MailboxElementKind::string, *value, message_type,
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
                    std::nullopt, MailboxElementKind::packed, { },
                    message_type,
                });
            }
            return true;
        }
        if (string_element) {
            const auto value = allocate_string_register();
            process_.operations.emplace_back(MailboxGet {
                receiver, 0U, 0U, std::nullopt,
                profile->method == "peek", MailboxElementKind::string,
                value, message_type,
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
            MailboxElementKind::packed, { }, message_type,
        });
        return lower_hir_mailbox_copy_out(argument, value);
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

    if (source.marker == support::ExpressionMarker::sv_sync_new) {
        const bool mailbox
            = synchronization_type(expected_type, StandardClass::mailbox);
        const bool semaphore
            = synchronization_type(expected_type, StandardClass::semaphore);
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
                mailbox_element->dynamic      ? MailboxElementKind::dynamic
                    : mailbox_element->container ? MailboxElementKind::container
                    : string_element          ? MailboxElementKind::string
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
    // A class property receiver (`m.try_get(t)` in a method, `c.m.num()`)
    // has the property's type in the accessed specialization (IEEE
    // 1800-2023 8.4, 15.4), as the statement form's receiver does.
    std::optional<HirClassPropertyProfile> property_receiver;
    if (receiver_type == nullptr) {
        property_receiver = hir_class_property_profile(
            source.operands.front());
        if (property_receiver) {
            receiver_type = &property_receiver->type;
        }
    }
    const bool mailbox = synchronization_type(receiver_type, StandardClass::mailbox);
    const bool semaphore = synchronization_type(receiver_type, StandardClass::semaphore);
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
        // a SimIR scalar branch condition: a positive result took a message
        // (a negative one is a typeless mailbox's message of another type,
        // IEEE 1800-2023 15.4.6, 15.4.8).
        const auto zero = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            zero, PackedLogic4::from_aval_bval(32U, 0U, 0U) });
        const auto succeeded = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::greater_signed, succeeded, result, zero });
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
        bool string_element = mailbox_element->string;
        auto element_width = mailbox_element->width;
        auto container = mailbox_element->container;
        // A typeless mailbox's message has the argument's own type.
        std::uint64_t message_type { };
        if (mailbox_element->dynamic && argument_count == 1U) {
            const auto message = hir_typeless_mailbox_message(
                source.operands[1], hir_source_span(source.source));
            if (!message) {
                return { true, false, std::nullopt };
            }
            string_element = message->string;
            element_width = message->width;
            message_type = message->type;
            container = message->container;
        }
        if (method == "num" && argument_count == 0U) {
            const auto destination = result();
            process_.operations.emplace_back(
                MailboxNum { destination, *receiver });
            return { true, true, resize_result(destination) };
        }
        if ((method == "put" || method == "try_put")
            && argument_count == 1U && container) {
            const auto value = lower_hir_static_container_actual(
                source.operands[1], *container);
            if (!value) {
                return { true, false, std::nullopt };
            }
            std::optional<RegisterId> destination;
            if (method == "try_put") {
                destination = result();
            }
            MailboxPut put {
                *receiver, 0U, 0U, destination,
                MailboxElementKind::container, { }, message_type,
            };
            put.container_source = *value;
            process_.operations.emplace_back(std::move(put));
            return { true, true,
                destination ? std::optional { resize_result(*destination) }
                            : std::nullopt };
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
                    MailboxElementKind::string, *value, message_type,
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
                    *receiver, *value, element_width, std::nullopt,
                    MailboxElementKind::packed, { }, message_type });
                return { true, true, std::nullopt };
            }
            const auto destination = result();
            process_.operations.emplace_back(MailboxPut {
                *receiver, *value, element_width, destination,
                MailboxElementKind::packed, { }, message_type });
            return { true, true, resize_result(destination) };
        }
        const bool get = method == "get" || method == "try_get";
        const bool peek = method == "peek" || method == "try_peek";
        if ((get || peek) && argument_count == 1U && container) {
            const auto value = allocate_container_register(*container);
            std::optional<RegisterId> success;
            if (method.starts_with("try_")) {
                success = result();
            }
            MailboxGet operation {
                *receiver, 0U, 0U, success, peek,
                MailboxElementKind::container, { }, message_type,
            };
            operation.container_destination = value;
            process_.operations.emplace_back(std::move(operation));
            if (success) {
                const auto condition = mailbox_result_truth(*success);
                const auto branch = process_.operations.size();
                process_.operations.emplace_back(Branch {
                    condition, 0U, 0U, UnknownBranchPolicy::error });
                const auto copy_out = static_cast<InstructionIndex>(
                    process_.operations.size());
                if (!lower_hir_container_copy_out(source.operands[1], value)) {
                    return { true, false, std::nullopt };
                }
                const auto end = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations[branch] = Branch {
                    condition, copy_out, end, UnknownBranchPolicy::error };
                return { true, true, resize_result(*success) };
            }
            if (!lower_hir_container_copy_out(source.operands[1], value)) {
                return { true, false, std::nullopt };
            }
            return { true, true, std::nullopt };
        }
        if ((get || peek) && argument_count == 1U) {
            if (string_element) {
                const auto value = allocate_string_register();
                std::optional<RegisterId> success;
                if (method.starts_with("try_")) {
                    success = result();
                }
                process_.operations.emplace_back(MailboxGet {
                    *receiver, 0U, 0U, success, peek,
                    MailboxElementKind::string, value, message_type,
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
                *receiver, value, element_width, success, peek,
                MailboxElementKind::packed, { }, message_type });
            if (success) {
                const auto condition
                    = mailbox_result_truth(*success);
                const auto branch = process_.operations.size();
                process_.operations.emplace_back(Branch {
                    condition, 0U, 0U, UnknownBranchPolicy::error });
                const auto copy_out = static_cast<InstructionIndex>(
                    process_.operations.size());
                if (!lower_hir_mailbox_copy_out(
                        source.operands[1], value)) {
                    return { true, false, std::nullopt };
                }
                const auto end = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations[branch] = Branch {
                    condition, copy_out, end, UnknownBranchPolicy::error };
            } else if (!lower_hir_mailbox_copy_out(
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
