// SPDX-License-Identifier: Apache-2.0
#include "application_class_execution.hpp"

#include <ranges>

namespace fsim::app::application_detail {

SystemVerilogClassExecution::SystemVerilogClassExecution(
    const std::span<const frontend::SystemVerilogClassSpecialization>
        specializations,
    const semantic::sv::Hir& hir,
    runtime::SystemVerilogClassHeap& heap,
    runtime::SystemVerilogClassStaticStore& static_store,
    runtime::SystemVerilogUvmComponentService& components,
    runtime::SystemVerilogUvmPhaseService& phases,
    std::map<std::string, runtime::SystemVerilogUvmRootHandle, std::less<>>&
        roots_by_scope)
    : specializations_(specializations)
    , hir_(hir)
    , heap_(heap)
    , static_store_(static_store)
    , components_(components)
    , phases_(phases)
    , roots_by_scope_(roots_by_scope)
{
}

void SystemVerilogClassExecution::set_source_invokers(
    SourceProfileInvoker profile,
    SourceFunctionInvoker function,
    SourceStaticFunctionInvoker static_function)
{
    invoke_source_profile_ = std::move(profile);
    invoke_source_function_ = std::move(function);
    invoke_source_static_function_ = std::move(static_function);
}

[[nodiscard]] const frontend::SystemVerilogClassSpecialization&
SystemVerilogClassExecution::class_specialization(const std::string_view identity) const
{
    auto found = std::ranges::find(
        specializations_,
        identity,
        &frontend::SystemVerilogClassSpecialization::specialization_identity);
    if (found == specializations_.end()) {
        const auto matches = std::ranges::count(
            specializations_,
            identity,
            &frontend::SystemVerilogClassSpecialization::declaration_identity);
        if (matches == 1) {
            found = std::ranges::find(
                specializations_,
                identity,
                &frontend::SystemVerilogClassSpecialization::declaration_identity);
        }
    }
    if (found == specializations_.end()) {
        throw std::out_of_range {
            "SystemVerilog class specialization '" + std::string { identity }
            + "' is not available in this simulation"
        };
    }
    return *found;
}

runtime::SystemVerilogUvmRootHandle
SystemVerilogClassExecution::component_root(
    const std::string_view allocation_scope)
{
    const std::string identity {
        allocation_scope.empty() ? "$simulation" : allocation_scope
    };
    const auto found = roots_by_scope_.find(identity);
    if (found != roots_by_scope_.end())
        return found->second;
    const auto root = components_.create_root(identity);
    const auto [inserted, did_insert] = roots_by_scope_.emplace(identity, root);
    if (!did_insert) {
        components_.destroy_root(root);
        throw std::logic_error { "duplicate automatic UVM root identity" };
    }
    try {
        phases_.participate_standard_root(root);
    } catch (...) {
        roots_by_scope_.erase(inserted);
        components_.destroy_root(root);
        throw;
    }
    return root;
}

runtime::PackedLogic4 SystemVerilogClassExecution::invoke_source_randomize(
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const std::string> selected_names,
    const std::span<const runtime::SystemVerilogConstraintTemplate>
        inline_constraints)
{
    auto& object = heap_.object(handle);
    const auto prior_properties = object.properties;
    const auto callback = [&](const std::string_view name) {
        const auto* found = application_detail::systemverilog_randomize_callback(
            specializations_,
            heap_, handle, name);
        if (found == nullptr)
            return;
        if (found->kind != frontend::SystemVerilogClassMethodKind::Function
            || found->is_static || !found->arguments.empty()
            || found->return_type.spelling != "void") {
            throw std::invalid_argument {
                "randomize callback requires a nonstatic zero-argument void function"
            };
        }
        std::vector<runtime::PackedLogic4> actuals;
        (void)invoke_source_profile_(*found, handle, actuals);
    };
    try {
        callback("pre_randomize");
    } catch (...) {
        object.properties = prior_properties;
        return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
    }
    runtime::SystemVerilogClassRandomizeRequest request;
    application_detail::configure_systemverilog_randomize_selection(
        request, selected_names);
    request.call_identity = object.specialization_identity
        + "::randomize@source";
    request.class_constraints = [this, handle, specialization = object.specialization_identity](auto& solver, const auto& variables) {
        application_detail::configure_systemverilog_class_constraints(
            solver,
            variables,
            hir_,
            class_specialization(specialization),
            [this, handle](const auto identity) {
                return heap_.constraint_mode(handle, identity);
            });
    };
    if (!inline_constraints.empty()) {
        request.inline_constraints = [inline_constraints,
                                         identity = object.specialization_identity](
                                         auto& solver, const auto& variables) {
            runtime::configure_systemverilog_inline_constraints(
                solver,
                variables,
                inline_constraints,
                identity + "::randomize@inline");
        };
    }
    const auto result = runtime::randomize_systemverilog_class_object(
        heap_, handle, request);
    if (result.language_result() != 0) {
        try {
            callback("post_randomize");
        } catch (...) {
            object.properties = prior_properties;
            return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
        }
    }
    return runtime::PackedLogic4::from_aval_bval(
        32, result.language_result(), 0);
}

runtime::PackedLogic4
SystemVerilogClassExecution::invoke_source_randomization_mode(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view method,
    const std::span<const runtime::PackedLogic4> actuals)
{
    return application_detail::invoke_systemverilog_randomization_mode(
        heap_, handle, method, actuals);
}

runtime::PackedLogic4 SystemVerilogClassExecution::resize_packed(
    const runtime::PackedLogic4& value,
    const std::size_t width)
{
    runtime::PackedLogic4 result(width, runtime::Logic4::zero);
    for (std::size_t bit = 0; bit < std::min(width, value.width()); ++bit) {
        result.set(bit, value.get(bit));
    }
    return result;
}

runtime::PackedLogic4 SystemVerilogClassExecution::packed_property_value(
    const runtime::SystemVerilogClassPropertyValue& value)
{
    if (value.kind == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
        return runtime::PackedLogic4::from_aval_bval(64, value.handle, 0);
    }
    return value.packed;
}

[[nodiscard]] const frontend::SystemVerilogClassPropertyLayout&
SystemVerilogClassExecution::source_property(const std::string_view canonical_identity) const
{
    const auto [owner, name] = static_property_parts(canonical_identity);
    const auto& specialization = class_specialization(owner);
    const auto found = std::ranges::find_if(
        specialization.properties, [&](const auto& property) {
            return property.owner_identity == owner && property.name == name;
        });
    if (found == specialization.properties.end()) {
        throw std::out_of_range {
            "SystemVerilog class property profile '"
            + std::string { canonical_identity } + "' is not available"
        };
    }
    return *found;
}

void SystemVerilogClassExecution::assign_property_value(
    runtime::SystemVerilogClassPropertyValue& destination,
    const std::string_view canonical_identity,
    const runtime::PackedLogic4& value)
{
    if (destination.kind
        == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
        const auto word = value.low_word();
        if (word.bval != 0) {
            throw std::invalid_argument {
                "class-handle property assignment contains X or Z"
            };
        }
        const auto handle = word.aval;
        const auto& declared_type = source_property(
            canonical_identity)
                                        .type.systemverilog_class_declaration;
        if (handle != 0) {
            (void)heap_.checked_cast(handle, declared_type);
        }
        destination.handle = handle;
        return;
    }
    destination.packed = resize_packed(value, destination.packed.width());
}

runtime::PackedLogic4 SystemVerilogClassExecution::invoke_class_container(
    const runtime::SystemVerilogClassHandle receiver,
    const std::string_view operation,
    const std::span<const runtime::PackedLogic4> actuals)
{
    constexpr std::string_view read_prefix { "@container-read:" };
    constexpr std::string_view write_prefix { "@container-write:" };
    constexpr std::string_view resize_prefix { "@container-resize:" };
    constexpr std::string_view push_back_prefix {
        "@container-push-back:"
    };
    constexpr std::string_view pop_front_prefix {
        "@container-pop-front:"
    };
    constexpr std::string_view size_prefix { "@container-size:" };
    const auto prefix = operation.starts_with(read_prefix)
        ? read_prefix
        : operation.starts_with(write_prefix)
        ? write_prefix
        : operation.starts_with(resize_prefix)
        ? resize_prefix
        : operation.starts_with(push_back_prefix)
        ? push_back_prefix
        : operation.starts_with(pop_front_prefix)
        ? pop_front_prefix
        : operation.starts_with(size_prefix)
        ? size_prefix
        : std::string_view { };
    if (prefix.empty()) {
        throw std::invalid_argument {
            "unknown class handle container operation"
        };
    }
    auto& property = heap_.property(receiver, operation.substr(prefix.size()));
    if (!property.handle_container) {
        throw std::invalid_argument {
            "class property is not a handle container"
        };
    }
    auto& container = *property.handle_container;
    const auto known_word = [&](const std::size_t index) {
        if (index >= actuals.size()) {
            throw std::invalid_argument {
                "class handle container operation has missing actuals"
            };
        }
        const auto word = actuals[index].low_word();
        if (word.bval != 0) {
            throw std::invalid_argument {
                "class handle container operand contains X or Z"
            };
        }
        return word.aval;
    };
    if (prefix == resize_prefix) {
        if (actuals.size() != 1U
            || !std::in_range<std::size_t>(known_word(0))) {
            throw std::length_error {
                "class handle container size exceeds host storage"
            };
        }
        container.resize(static_cast<std::size_t>(known_word(0)));
        return runtime::PackedLogic4::from_aval_bval(64, 0, 0);
    }
    if (prefix == push_back_prefix) {
        if (actuals.size() != 1U) {
            throw std::invalid_argument {
                "class handle queue push_back has inconsistent actuals"
            };
        }
        const auto value = known_word(0);
        container.push_back(heap_, value);
        return runtime::PackedLogic4::from_aval_bval(64, value, 0);
    }
    if (prefix == pop_front_prefix) {
        if (!actuals.empty()) {
            throw std::invalid_argument {
                "class handle queue pop_front has inconsistent actuals"
            };
        }
        return runtime::PackedLogic4::from_aval_bval(
            64, container.pop_front(), 0);
    }
    if (prefix == size_prefix) {
        if (!actuals.empty()) {
            throw std::invalid_argument {
                "class handle container size has inconsistent actuals"
            };
        }
        return runtime::PackedLogic4::from_aval_bval(
            32, container.size(), 0);
    }
    if (actuals.size() != (prefix == read_prefix ? 1U : 2U)) {
        throw std::invalid_argument {
            "class handle container operation has inconsistent actuals"
        };
    }
    const auto index = known_word(0);
    const auto keyed = container.kind()
            == runtime::SystemVerilogClassContainerKind::AssociativeArray
        || container.kind()
            == runtime::SystemVerilogClassContainerKind::UnpackedAggregate;
    if (prefix == read_prefix) {
        const auto value = keyed
            ? container.at(std::to_string(index))
            : container.at(static_cast<std::size_t>(index));
        return runtime::PackedLogic4::from_aval_bval(64, value, 0);
    }
    const auto value = known_word(1);
    if (keyed) {
        container.set(heap_, std::to_string(index), value);
    } else {
        container.set(heap_, static_cast<std::size_t>(index), value);
    }
    return runtime::PackedLogic4::from_aval_bval(64, value, 0);
}

runtime::PackedLogic4
SystemVerilogClassExecution::invoke_checked_class_cast(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view operation) const
{
    constexpr std::string_view prefix { "@checked-cast:" };
    try {
        (void)heap_.checked_cast(handle, operation.substr(prefix.size()));
        return runtime::PackedLogic4::from_aval_bval(1, 1, 0);
    } catch (const std::invalid_argument&) {
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
}

std::pair<std::string_view, std::string_view>
SystemVerilogClassExecution::static_property_parts(const std::string_view identity)
{
    const auto separator = identity.rfind("::");
    if (separator == std::string_view::npos
        || separator == 0 || separator + 2U >= identity.size()) {
        throw std::invalid_argument {
            "class static property identity is malformed"
        };
    }
    return { identity.substr(0, separator), identity.substr(separator + 2U) };
}

[[nodiscard]] std::optional<runtime::PackedLogic4>
SystemVerilogClassExecution::evaluate_constructor_expression(
    const frontend::Expression& expression,
    const runtime::SystemVerilogClassHandle handle,
    ConstructorEnvironment& environment)
{
    using frontend::ExpressionKind;
    if (expression.kind == ExpressionKind::Identifier) {
        if (expression.text == "this" || expression.text == "super") {
            return runtime::PackedLogic4::from_aval_bval(64, handle, 0);
        }
        const auto found = environment.find(expression.text);
        return found == environment.end()
            ? std::nullopt
            : std::optional { found->second };
    }
    if (expression.kind == ExpressionKind::IntegerLiteral) {
        std::int64_t value { };
        const auto* begin = expression.text.data();
        const auto* end = begin + expression.text.size();
        const auto converted = std::from_chars(begin, end, value, 10);
        if (converted.ec != std::errc { } || converted.ptr != end) {
            return std::nullopt;
        }
        return runtime::PackedLogic4::from_aval_bval(
            64, static_cast<std::uint64_t>(value), 0);
    }
    constexpr std::string_view property_prefix { "@sv-property:" };
    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with(property_prefix)) {
        return packed_property_value(heap_.property(
            handle, expression.text.substr(property_prefix.size())));
    }
    constexpr std::string_view static_property_prefix { "@sv-static-property:" };
    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with(static_property_prefix)) {
        const auto [owner, name] = static_property_parts(
            std::string_view { expression.text }.substr(
                static_property_prefix.size()));
        return packed_property_value(static_store_.property(owner, name));
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "@sv-null") {
        return runtime::PackedLogic4::from_aval_bval(64, 0, 0);
    }
    constexpr std::string_view method_prefix { "@sv-method:" };
    constexpr std::string_view base_method_prefix { "@sv-base-method:" };
    if (expression.kind == ExpressionKind::Call
        && (expression.text.starts_with(method_prefix)
            || expression.text.starts_with(base_method_prefix))
        && !expression.operands.empty()) {
        const auto selected_prefix = expression.text.starts_with(base_method_prefix)
            ? base_method_prefix
            : method_prefix;
        const auto receiver = evaluate_constructor_expression(
            expression.operands.front(), handle, environment);
        if (!receiver || receiver->low_word().bval != 0)
            return std::nullopt;
        std::vector<runtime::PackedLogic4> actuals;
        for (const auto& operand : expression.operands | std::views::drop(1)) {
            const auto actual = evaluate_constructor_expression(
                operand, handle, environment);
            if (!actual)
                return std::nullopt;
            actuals.push_back(*actual);
        }
        std::vector<std::string> names(actuals.size());
        if (expression.call_argument_names.size()
            == expression.operands.size()) {
            std::ranges::copy(
                expression.call_argument_names | std::views::drop(1),
                names.begin());
        }
        std::vector<std::uint8_t> directions(actuals.size());
        if (expression.call_argument_directions.size()
            == expression.operands.size()) {
            std::ranges::transform(
                expression.call_argument_directions | std::views::drop(1),
                directions.begin(),
                [](const auto direction) {
                    return static_cast<std::uint8_t>(direction);
                });
        }
        std::vector<std::string> string_actuals(actuals.size());
        auto result = invoke_source_function_(
            receiver->low_word().aval,
            expression.text.substr(selected_prefix.size()),
            actuals,
            string_actuals,
            names,
            directions,
            selected_prefix == method_prefix);
        for (std::size_t index = 1; index < expression.operands.size(); ++index) {
            const auto direction = index - 1U < directions.size()
                ? static_cast<frontend::PortDirection>(directions[index - 1U])
                : frontend::PortDirection::Input;
            if (direction == frontend::PortDirection::Input
                || expression.operands[index].kind
                    != ExpressionKind::Identifier) {
                continue;
            }
            if (const auto found = environment.find(
                    expression.operands[index].text);
                found != environment.end()) {
                found->second = resize_packed(
                    actuals[index - 1U], found->second.width());
            }
        }
        return result;
    }
    constexpr std::string_view static_method_prefix { "@sv-static-method:" };
    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with(static_method_prefix)) {
        std::vector<runtime::PackedLogic4> actuals;
        for (const auto& operand : expression.operands) {
            const auto actual = evaluate_constructor_expression(
                operand, handle, environment);
            if (!actual)
                return std::nullopt;
            actuals.push_back(*actual);
        }
        auto names = expression.call_argument_names;
        if (names.empty())
            names.resize(actuals.size());
        std::vector<std::uint8_t> directions;
        std::ranges::transform(
            expression.call_argument_directions,
            std::back_inserter(directions),
            [](const auto direction) {
                return static_cast<std::uint8_t>(direction);
            });
        std::vector<std::string> string_actuals(actuals.size());
        return invoke_source_static_function_(
            expression.text.substr(static_method_prefix.size()),
            actuals,
            string_actuals,
            names,
            directions);
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1U) {
        const auto operand = evaluate_constructor_expression(
            expression.operands.front(), handle, environment);
        if (!operand)
            return std::nullopt;
        const auto word = operand->low_word();
        if (word.bval != 0) {
            return runtime::PackedLogic4(64, runtime::Logic4::x);
        }
        if (expression.text == "+")
            return operand;
        if (expression.text == "-") {
            return runtime::PackedLogic4::from_aval_bval(
                64, std::uint64_t { 0 } - word.aval, 0);
        }
        if (expression.text == "~") {
            return runtime::PackedLogic4::from_aval_bval(64, ~word.aval, 0);
        }
        if (expression.text == "!") {
            return runtime::PackedLogic4::from_aval_bval(
                1, word.aval == 0 ? 1 : 0, 0);
        }
        return std::nullopt;
    }
    if (expression.kind != ExpressionKind::Binary
        || expression.operands.size() != 2U) {
        return std::nullopt;
    }
    const auto left = evaluate_constructor_expression(
        expression.operands[0], handle, environment);
    const auto right = evaluate_constructor_expression(
        expression.operands[1], handle, environment);
    if (!left || !right)
        return std::nullopt;
    const auto left_word = left->low_word();
    const auto right_word = right->low_word();
    if (left_word.bval != 0 || right_word.bval != 0) {
        return runtime::PackedLogic4(64, runtime::Logic4::x);
    }
    const auto lhs = left_word.aval;
    const auto rhs = right_word.aval;
    std::uint64_t value { };
    if (expression.text == "+")
        value = lhs + rhs;
    else if (expression.text == "-")
        value = lhs - rhs;
    else if (expression.text == "*")
        value = lhs * rhs;
    else if (expression.text == "/") {
        if (rhs == 0)
            return std::nullopt;
        value = lhs / rhs;
    } else if (expression.text == "%") {
        if (rhs == 0)
            return std::nullopt;
        value = lhs % rhs;
    } else if (expression.text == "&")
        value = lhs & rhs;
    else if (expression.text == "|")
        value = lhs | rhs;
    else if (expression.text == "^")
        value = lhs ^ rhs;
    else if (expression.text == "<<")
        value = rhs < 64 ? lhs << rhs : 0;
    else if (expression.text == ">>")
        value = rhs < 64 ? lhs >> rhs : 0;
    else if (expression.text == "==" || expression.text == "===") {
        value = lhs == rhs;
    } else if (expression.text == "!=" || expression.text == "!==") {
        value = lhs != rhs;
    } else if (expression.text == "<")
        value = lhs < rhs;
    else if (expression.text == "<=")
        value = lhs <= rhs;
    else if (expression.text == ">")
        value = lhs > rhs;
    else if (expression.text == ">=")
        value = lhs >= rhs;
    else
        return std::nullopt;
    return runtime::PackedLogic4::from_aval_bval(64, value, 0);
}

SystemVerilogClassExecution::ConstructorEnvironment
SystemVerilogClassExecution::bind_constructor_actuals(
    const frontend::SystemVerilogClassMethodProfile& constructor,
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> actual_names)
{
    if (!actual_names.empty() && actual_names.size() != actuals.size()) {
        throw std::invalid_argument {
            "constructor actual names do not align with values"
        };
    }
    ConstructorEnvironment environment;
    std::vector<bool> assigned(constructor.arguments.size());
    std::size_t next_positional { };
    for (std::size_t index = 0; index < actuals.size(); ++index) {
        const auto name = actual_names.empty()
            ? std::string_view { }
            : std::string_view { actual_names[index] };
        std::size_t formal_index { };
        if (name.empty()) {
            while (next_positional < assigned.size()
                && assigned[next_positional]) {
                ++next_positional;
            }
            formal_index = next_positional;
        } else {
            const auto found = std::ranges::find(
                constructor.arguments,
                name,
                &frontend::FunctionArgument::name);
            if (found == constructor.arguments.end()) {
                throw std::invalid_argument {
                    "constructor has no formal named '" + std::string { name } + "'"
                };
            }
            formal_index = static_cast<std::size_t>(
                std::distance(constructor.arguments.begin(), found));
        }
        if (formal_index >= constructor.arguments.size()
            || assigned[formal_index]) {
            throw std::invalid_argument {
                "constructor actual cannot be associated with a unique formal"
            };
        }
        assigned[formal_index] = true;
        const auto width = constructor.arguments[formal_index].type.width();
        const auto actual_width = width
            ? static_cast<std::size_t>(*width)
            : actuals[index].width();
        environment.emplace(
            constructor.arguments[formal_index].name,
            constructor.arguments[formal_index].direction
                    == frontend::PortDirection::Output
                ? runtime::PackedLogic4(actual_width, runtime::Logic4::x)
                : resize_packed(actuals[index], actual_width));
    }
    for (std::size_t index = 0; index < constructor.arguments.size(); ++index) {
        if (assigned[index])
            continue;
        const auto& formal = constructor.arguments[index];
        if (!formal.default_value) {
            throw std::invalid_argument {
                "constructor formal '" + formal.name + "' has no actual"
            };
        }
        if (formal.type.domain == frontend::ValueDomain::String) {
            environment.emplace(
                formal.name, runtime::PackedLogic4(64, runtime::Logic4::zero));
            continue;
        }
        const auto value = evaluate_constructor_expression(
            *formal.default_value, handle, environment);
        if (!value) {
            throw std::invalid_argument {
                "constructor default for '" + formal.name
                + "' is not executable"
            };
        }
        const auto width = formal.type.width();
        environment.emplace(
            formal.name, resize_packed(*value, width ? *width : value->width()));
    }
    for (const auto& variable : constructor.variables) {
        const auto width = variable.type.width().value_or(64);
        if (variable.type.domain == frontend::ValueDomain::String) {
            environment[variable.name] = runtime::PackedLogic4(64, runtime::Logic4::zero);
            continue;
        }
        auto value = variable.initializer
            ? evaluate_constructor_expression(
                  *variable.initializer, handle, environment)
            : std::optional<runtime::PackedLogic4> {
                  runtime::PackedLogic4(width, runtime::Logic4::x)
              };
        if (!value) {
            throw std::invalid_argument {
                "constructor local initializer for '" + variable.name
                + "' is not executable"
            };
        }
        environment[variable.name] = resize_packed(*value, width);
    }
    return environment;
}

void SystemVerilogClassExecution::execute_constructor_statements(
    const std::span<const frontend::Statement> statements,
    const runtime::SystemVerilogClassHandle handle,
    ConstructorEnvironment& environment,
    const bool native_uvm_library)
{
    constexpr std::string_view property_prefix { "@sv-property:" };
    constexpr std::string_view base_prefix { "@sv-base-constructor:" };
    for (const auto& statement : statements) {
        if (statement.kind == frontend::StatementKind::TaskCall
            && statement.task_name.starts_with(base_prefix)) {
            continue;
        }
        if (statement.kind == frontend::StatementKind::Assignment) {
            const auto value = evaluate_constructor_expression(
                statement.value, handle, environment);
            if (!value) {
                if (native_uvm_library)
                    continue;
                throw std::invalid_argument {
                    "constructor assignment expression is not executable"
                };
            }
            if (statement.target.kind == frontend::ExpressionKind::Call
                && statement.target.text.starts_with(property_prefix)) {
                const auto identity = statement.target.text.substr(
                    property_prefix.size());
                assign_property_value(
                    heap_.property(handle, identity), identity, *value);
                continue;
            }
            if (statement.target.kind == frontend::ExpressionKind::Identifier) {
                const auto found = environment.find(statement.target.text);
                if (found != environment.end()) {
                    found->second = resize_packed(*value, found->second.width());
                    continue;
                }
            }
            if (native_uvm_library)
                continue;
            throw std::invalid_argument {
                "constructor assignment target is not executable"
            };
        }
        if (statement.kind == frontend::StatementKind::Block) {
            execute_constructor_statements(
                statement.statements, handle, environment, native_uvm_library);
            continue;
        }
        if (statement.kind == frontend::StatementKind::If) {
            const auto condition = evaluate_constructor_expression(
                statement.condition, handle, environment);
            if (!condition || condition->low_word().bval != 0) {
                if (native_uvm_library)
                    continue;
                throw std::invalid_argument {
                    "constructor condition is not a known packed value"
                };
            }
            execute_constructor_statements(
                condition->low_word().aval != 0
                    ? std::span<const frontend::Statement> { statement.statements }
                    : std::span<const frontend::Statement> { statement.else_statements },
                handle,
                environment,
                native_uvm_library);
            continue;
        }
        if (statement.kind != frontend::StatementKind::Null) {
            if (native_uvm_library)
                continue;
            throw std::invalid_argument {
                "constructor contains an operation that is not executable"
            };
        }
    }
}

} // namespace fsim::app::application_detail
