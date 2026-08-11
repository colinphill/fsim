// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_uvm_registry.hpp"

#include <ranges>
#include <set>

namespace fsim::app {
using namespace application_detail;

struct Simulation::Impl {
    enum class Lifecycle {
        ready,
        finished,
        poisoned,
    };

#include "application_simulation_setup.tpp"

    std::vector<runtime::SystemVerilogUvmCommandReportApplication>
    apply_uvm_report_settings(
        const std::string_view phase,
        const SimulationTick time)
    {
        return uvm_command_line.apply_report_settings(
            uvm_components, uvm_reports, phase, time);
    }

    void schedule_uvm_report_settings()
    {
        auto& scheduler = interpreter->scheduler();
        for (const auto& handle : uvm_report_setting_tasks) {
            (void)scheduler.cancel(handle);
        }
        uvm_report_setting_tasks.clear();
        const auto now = scheduler.now();
        (void)apply_uvm_report_settings("time", now);
        std::set<SimulationTick> offsets;
        for (const auto& setting : uvm_command_line.settings().verbosity_settings) {
            if (setting.phase == "time" && setting.time_offset
                && *setting.time_offset > now) {
                offsets.insert(*setting.time_offset);
            }
        }
        runtime::StableOrder order { };
        for (const auto offset : offsets) {
            uvm_report_setting_tasks.push_back(
                scheduler.schedule_after_cancelable(
                    offset - now, runtime::SchedulerPhase::active, order++,
                    [this, offset](runtime::Scheduler&) {
                        (void)apply_uvm_report_settings("time", offset);
                    }));
        }
    }

    void validate_external_value(
        const SignalId signal,
        const PackedLogic4& value,
        const std::string_view operation) const
    {
        const auto& info = built.design.signals().at(signal);
        if (info.width != value.width()) {
            throw std::invalid_argument(
                std::string { operation } + " width does not match signal '"
                + info.name + "'");
        }
        if (info.source_domain != frontend::ValueDomain::Bit2
            && info.source_domain != frontend::ValueDomain::Boolean) {
            return;
        }
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            const auto state = value.get(bit);
            if (state != runtime::Logic4::zero
                && state != runtime::Logic4::one) {
                throw std::invalid_argument(
                    std::string { operation }
                    + " would place an X/Z value into two-state signal '"
                    + info.name + "'");
            }
        }
    }

    [[nodiscard]] const frontend::SystemVerilogClassSpecialization&
    class_specialization(const std::string_view identity) const
    {
        auto found = std::ranges::find(
            built.systemverilog_class_specializations,
            identity,
            &frontend::SystemVerilogClassSpecialization::specialization_identity);
        if (found == built.systemverilog_class_specializations.end()) {
            const auto matches = std::ranges::count(
                built.systemverilog_class_specializations,
                identity,
                &frontend::SystemVerilogClassSpecialization::declaration_identity);
            if (matches == 1) {
                found = std::ranges::find(
                    built.systemverilog_class_specializations,
                    identity,
                    &frontend::SystemVerilogClassSpecialization::declaration_identity);
            }
        }
        if (found == built.systemverilog_class_specializations.end()) {
            throw std::out_of_range {
                "SystemVerilog class specialization '" + std::string { identity }
                + "' is not available in this simulation"
            };
        }
        return *found;
    }

    [[nodiscard]] runtime::SystemVerilogUvmRootHandle component_root(
        const std::string_view allocation_scope)
    {
        const std::string identity {
            allocation_scope.empty() ? "$simulation" : allocation_scope
        };
        const auto found = uvm_roots_by_scope.find(identity);
        if (found != uvm_roots_by_scope.end())
            return found->second;
        const auto root = uvm_components.create_root(identity);
        const auto [inserted, did_insert] = uvm_roots_by_scope.emplace(identity, root);
        if (!did_insert) {
            uvm_components.destroy_root(root);
            throw std::logic_error { "duplicate automatic UVM root identity" };
        }
        try {
            uvm_phases.participate_standard_root(root);
        } catch (...) {
            uvm_roots_by_scope.erase(inserted);
            uvm_components.destroy_root(root);
            throw;
        }
        return root;
    }

    [[nodiscard]] runtime::PackedLogic4 invoke_source_randomize(
        const runtime::SystemVerilogClassHandle handle,
        const std::span<const std::string> selected_names)
    {
        auto& object = class_heap.object(handle);
        const auto prior_properties = object.properties;
        const auto callback = [&](const std::string_view name) {
            const auto* found = application_detail::systemverilog_randomize_callback(
                built.systemverilog_class_specializations,
                class_heap, handle, name);
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
            (void)invoke_source_profile(*found, handle, actuals, { }, { });
        };
        try {
            callback("pre_randomize");
        } catch (...) {
            object.properties = prior_properties;
            return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
        }
        runtime::SystemVerilogClassRandomizeRequest request;
        for (const auto& name : selected_names) {
            if (!name.empty())
                request.variable_list.push_back(name);
        }
        request.call_identity = object.specialization_identity
            + "::randomize@source";
        request.class_constraints = [this, handle, specialization = object.specialization_identity](auto& solver, const auto& variables) {
            application_detail::configure_systemverilog_class_constraints(
                solver,
                variables,
                built.systemverilog_hir,
                class_specialization(specialization),
                [this, handle](const auto identity) {
                    return class_heap.constraint_mode(handle, identity);
                });
        };
        const auto result = runtime::randomize_systemverilog_class_object(
            class_heap, handle, request);
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

    [[nodiscard]] runtime::PackedLogic4 invoke_source_randomization_mode(
        const runtime::SystemVerilogClassHandle handle,
        const std::string_view method,
        const std::span<const runtime::PackedLogic4> actuals)
    {
        return application_detail::invoke_systemverilog_randomization_mode(
            class_heap, handle, method, actuals);
    }

    using ConstructorEnvironment = std::map<std::string, runtime::PackedLogic4, std::less<>>;

    [[nodiscard]] static runtime::PackedLogic4 resize_packed(
        const runtime::PackedLogic4& value,
        const std::size_t width)
    {
        runtime::PackedLogic4 result(width, runtime::Logic4::zero);
        for (std::size_t bit = 0; bit < std::min(width, value.width()); ++bit) {
            result.set(bit, value.get(bit));
        }
        return result;
    }

    [[nodiscard]] static runtime::PackedLogic4 packed_property_value(
        const runtime::SystemVerilogClassPropertyValue& value)
    {
        if (value.kind == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
            return runtime::PackedLogic4::from_aval_bval(64, value.handle, 0);
        }
        return value.packed;
    }

    [[nodiscard]] const frontend::SystemVerilogClassPropertyLayout&
    source_property(const std::string_view canonical_identity) const
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

    void assign_property_value(
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
                (void)class_heap.checked_cast(handle, declared_type);
            }
            destination.handle = handle;
            return;
        }
        destination.packed = resize_packed(value, destination.packed.width());
    }

    [[nodiscard]] runtime::PackedLogic4 invoke_class_container(
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
        auto& property = class_heap.property(receiver, operation.substr(prefix.size()));
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
            container.push_back(class_heap, value);
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
            container.set(class_heap, std::to_string(index), value);
        } else {
            container.set(class_heap, static_cast<std::size_t>(index), value);
        }
        return runtime::PackedLogic4::from_aval_bval(64, value, 0);
    }

    [[nodiscard]] runtime::PackedLogic4 invoke_checked_class_cast(
        const runtime::SystemVerilogClassHandle handle,
        const std::string_view operation) const
    {
        constexpr std::string_view prefix { "@checked-cast:" };
        try {
            (void)class_heap.checked_cast(handle, operation.substr(prefix.size()));
            return runtime::PackedLogic4::from_aval_bval(1, 1, 0);
        } catch (const std::invalid_argument&) {
            return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
        }
    }

    [[nodiscard]] static std::pair<std::string_view, std::string_view>
    static_property_parts(const std::string_view identity)
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
    evaluate_constructor_expression(
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
            return packed_property_value(class_heap.property(
                handle, expression.text.substr(property_prefix.size())));
        }
        constexpr std::string_view static_property_prefix { "@sv-static-property:" };
        if (expression.kind == ExpressionKind::Call
            && expression.text.starts_with(static_property_prefix)) {
            const auto [owner, name] = static_property_parts(
                std::string_view { expression.text }.substr(
                    static_property_prefix.size()));
            return packed_property_value(class_static_store.property(owner, name));
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
            auto result = invoke_source_function(
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
            return invoke_source_static_function(
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

    [[nodiscard]] ConstructorEnvironment bind_constructor_actuals(
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

    void execute_constructor_statements(
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
                        class_heap.property(handle, identity), identity, *value);
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

#include "application_simulation_source_methods.tpp"

    [[nodiscard]] runtime::SystemVerilogClassHandle allocate_class(
        const std::string_view specialization_identity,
        const std::string_view declared_type,
        const std::string_view allocation_scope = "$api")
    {
        const auto& specialization = class_specialization(
            specialization_identity);
        runtime::SystemVerilogClassDescriptor descriptor;
        descriptor.dynamic_type = specialization.declaration_identity;
        descriptor.declared_type = declared_type.empty()
            ? descriptor.dynamic_type
            : std::string { declared_type };
        descriptor.specialization_identity = specialization.specialization_identity;
        const auto separator = allocation_scope.find('.');
        descriptor.random_root_identity = std::string {
            allocation_scope.substr(0, separator)
        };
        const auto* current = &specialization;
        while (current != nullptr) {
            descriptor.assignable_declared_types.push_back(
                current->declaration_identity);
            if (current->base_specialization_identity.empty())
                break;
            current = &class_specialization(
                current->base_specialization_identity);
        }
        for (const auto& property : specialization.properties) {
            if (!property.is_static) {
                descriptor.properties.push_back(
                    class_property_descriptor(property, true));
            }
        }
        const auto declaration = std::ranges::find(
            built.systemverilog_hir.classes(),
            specialization.declaration_identity,
            &semantic::sv::ClassDeclaration::canonical_identity);
        if (declaration != built.systemverilog_hir.classes().end()) {
            for (const auto& constraint : declaration->composed_constraints) {
                if (constraint.override_legal) {
                    descriptor.constraint_modes.emplace_back(
                        constraint.selected_identity,
                        constraint.mode_enabled);
                }
            }
        } else {
            descriptor.constraint_modes = specialization.constraint_modes;
        }
        return class_heap.allocate(descriptor);
    }

    [[nodiscard]] runtime::SystemVerilogClassHandle construct_class(
        const std::string_view specialization_identity,
        const std::string_view declared_type,
        const std::span<const runtime::PackedLogic4> actuals,
        const std::span<const std::string> string_actuals,
        const std::span<const std::string> actual_names,
        const std::string_view allocation_scope,
        const runtime::SystemVerilogUvmRootHandle requested_root = 0)
    {
        const auto& specialization = class_specialization(
            specialization_identity);
        const auto handle = allocate_class(
            specialization.specialization_identity,
            declared_type,
            allocation_scope);
        const auto before = packed_class_snapshot();
        const auto component_specialization = is_systemverilog_uvm_type(
            built.systemverilog_class_specializations,
            specialization,
            "uvm_component");
        bool object_initialized { };
        bool automatic_root_created { };
        runtime::SystemVerilogUvmRootHandle automatic_root { };
        std::string automatic_root_identity;
        try {
            invoke_source_constructor(
                specialization, handle, actuals, actual_names);
            if (is_systemverilog_uvm_type(
                    built.systemverilog_class_specializations,
                    specialization,
                    "uvm_object")) {
                uvm_objects.initialize(handle);
                object_initialized = true;
                std::size_t name_index { };
                if (!actual_names.empty()) {
                    const auto found = std::ranges::find(actual_names, "name");
                    if (found != actual_names.end()) {
                        name_index = static_cast<std::size_t>(
                            std::distance(actual_names.begin(), found));
                    }
                }
                if (name_index < string_actuals.size()
                    && !string_actuals[name_index].empty()) {
                    uvm_objects.set_name(handle, string_actuals[name_index]);
                }
            }
            if (component_specialization) {
                std::size_t name_index { };
                std::size_t parent_index { 1U };
                if (!actual_names.empty()) {
                    const auto named_index = [&](const std::string_view name,
                                                 const std::size_t fallback) {
                        const auto found = std::ranges::find(actual_names, name);
                        return found == actual_names.end()
                            ? fallback
                            : static_cast<std::size_t>(
                                  std::distance(actual_names.begin(), found));
                    };
                    name_index = named_index("name", 0U);
                    parent_index = named_index("parent", 1U);
                }
                std::string name = name_index < string_actuals.size()
                    ? string_actuals[name_index]
                    : std::string { };
                if (name.empty()) {
                    name = "COMP_" + std::to_string(uvm_objects.instance_id(handle));
                }
                const auto parent = parent_index < actuals.size()
                    ? actuals[parent_index].low_word().aval
                    : runtime::SystemVerilogClassHandle { };
                auto root = requested_root;
                if (parent == 0 && root == 0) {
                    automatic_root_identity = allocation_scope.empty()
                        ? "$simulation"
                        : std::string { allocation_scope };
                    automatic_root_created = !uvm_roots_by_scope.contains(automatic_root_identity);
                    root = component_root(allocation_scope);
                    automatic_root = root;
                }
                uvm_components.initialize(handle, std::move(name), parent, root);
            }
        } catch (...) {
            if (component_specialization) {
                if (uvm_components.contains(handle)) {
                    try {
                        uvm_components.release(handle);
                    } catch (...) {
                    }
                } else {
                    if (object_initialized)
                        uvm_objects.erase(handle);
                    (void)class_heap.release(handle);
                }
                if (automatic_root_created
                    && uvm_components.contains_root(automatic_root)) {
                    try {
                        uvm_phases.unparticipate_standard_root(automatic_root);
                    } catch (...) {
                    }
                    try {
                        uvm_components.destroy_root(automatic_root);
                    } catch (...) {
                    }
                    uvm_roots_by_scope.erase(automatic_root_identity);
                }
            }
            if (component_specialization && class_heap.contains(handle)) {
                (void)class_heap.release(handle);
            }
            throw;
        }
        notify_class_changes(before);
        return handle;
    }

    using PackedSnapshot = std::map<
        std::pair<runtime::SystemVerilogClassHandle, std::string>,
        runtime::PackedLogic4>;
    using StaticPackedSnapshot = std::map<
        std::pair<std::string, std::string>, runtime::PackedLogic4>;

    [[nodiscard]] PackedSnapshot packed_class_snapshot() const
    {
        PackedSnapshot result;
        for (const auto handle : class_heap.live_handles()) {
            const auto& object = class_heap.object(handle);
            for (std::size_t index = 0; index < object.properties.size(); ++index) {
                const auto& property = object.properties[index];
                if (property.packed.width() != 0) {
                    result.emplace(
                        std::pair { handle, object.property_names[index] },
                        property.packed);
                }
            }
        }
        return result;
    }

    void notify_class_changes(const PackedSnapshot& before)
    {
        if (!class_property_change_hook)
            return;
        const auto after = packed_class_snapshot();
        for (const auto& [identity, value] : after) {
            const auto prior = before.find(identity);
            if (prior == before.end() || prior->second != value) {
                class_property_change_hook(
                    identity.first, identity.second, value,
                    interpreter->scheduler().now(),
                    interpreter->scheduler().delta());
            }
        }
    }

    [[nodiscard]] StaticPackedSnapshot packed_static_snapshot() const
    {
        StaticPackedSnapshot result;
        for (const auto& state : class_static_store.snapshots()) {
            for (std::size_t index = 0; index < state.properties.size(); ++index) {
                if (state.properties[index].packed.width() != 0) {
                    result.emplace(
                        std::pair { state.specialization_identity,
                            state.property_names[index] },
                        state.properties[index].packed);
                }
            }
        }
        return result;
    }

    void notify_static_changes(const StaticPackedSnapshot& before)
    {
        if (!class_static_property_change_hook)
            return;
        for (const auto& [identity, value] : packed_static_snapshot()) {
            const auto prior = before.find(identity);
            if (prior == before.end() || prior->second != value) {
                class_static_property_change_hook(
                    identity.first, identity.second, value,
                    interpreter->scheduler().now(), interpreter->scheduler().delta());
            }
        }
    }

    [[nodiscard]] runtime::SystemVerilogClassInvocationResult
    invoke_class_method(
        const std::string_view canonical_method,
        const runtime::SystemVerilogClassHandle this_handle,
        std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
        const std::optional<std::uint32_t> virtual_slot)
    {
        const auto before = packed_class_snapshot();
        const auto static_before = packed_static_snapshot();
        auto result = virtual_slot
            ? class_methods.invoke_virtual(*virtual_slot, this_handle, actuals)
            : class_methods.invoke(canonical_method, this_handle, actuals);
        notify_class_changes(before);
        notify_static_changes(static_before);
        return result;
    }

#include "application_simulation_uvm_phase.tpp"

    [[nodiscard]] std::optional<runtime::simir::SignalId>
    backdoor_signal(const std::string_view path) const noexcept
    {
        const auto found = std::ranges::find_if(
            built.design_ir.objects(), [&](const auto& object) {
                return design_object_is_signal_bearing(object) && object.path == path && object.runtime_index <= std::numeric_limits<runtime::simir::SignalId>::max();
            });
        return found == built.design_ir.objects().end()
            ? std::nullopt
            : std::optional<runtime::simir::SignalId> { static_cast<
                  runtime::simir::SignalId>(found->runtime_index) };
    }

    ~Impl()
    {
        if (vpi_started && !vpi_ended) {
            try {
                end_vpi();
            } catch (...) {
            }
        }
        if (systemc_start_attempted && !systemc_ended) {
            try {
                end_systemc();
            } catch (...) {
            }
        }
        if (vpi_registry && vpi_value_state_observer) {
            (void)vpi_registry->remove_value_state_observer(
                *vpi_value_state_observer);
        }
    }

    [[nodiscard]] runtime::SystemVerilogVpiStoredValue vpi_value(
        const SignalId signal,
        PackedLogic4 value) const
    {
        return systemverilog_vpi_signal_value(
            std::move(value), vpi_scalar_kinds.at(signal),
            vpi_categories.at(signal));
    }

    [[nodiscard]] runtime::SystemVerilogVpiStoredValue vpi_driver_value(
        const SignalId signal,
        const SystemVerilogVpiDriverBinding& binding) const
    {
        auto result = vpi_value(signal,
            interpreter->driver_value(binding.process, signal));
        result.strength = binding.strength;
        return result;
    }

    static void require_vpi_value(
        const runtime::SystemVerilogVpiValueError error,
        const std::string_view operation)
    {
        if (error == runtime::SystemVerilogVpiValueError::None) {
            return;
        }
        throw std::logic_error { "live VPI " + std::string { operation }
            + " failed with error "
            + std::to_string(static_cast<unsigned>(error)) };
    }

    [[nodiscard]] runtime::SystemVerilogVpiValueError apply_vpi_value_state(
        const runtime::SystemVerilogVpiValueStateUpdate& update)
    {
        std::scoped_lock bridge_lock { vpi_bridge_mutex };
        try {
            const auto word = vpi_word_handles.find(update.object);
            if (word != vpi_word_handles.end()) {
                if (update.forced_value) {
                    return runtime::SystemVerilogVpiValueError::ReadOnly;
                }
                auto value
                    = interpreter->container_object_value(word->second.first);
                auto& element = value.elements.at(word->second.second);
                const auto replacement = systemverilog_vpi_packed_value(
                    update.value,
                    vpi_container_scalar_kinds.at(word->second.first),
                    vpi_container_categories.at(word->second.first));
                if (element != replacement) {
                    element = replacement;
                    interpreter->deposit_container_object(
                        word->second.first, std::move(value));
                }
                return runtime::SystemVerilogVpiValueError::None;
            }
            const auto signal = vpi_handle_signals.find(update.object);
            if (signal == vpi_handle_signals.end()) {
                return runtime::SystemVerilogVpiValueError::None;
            }
            const auto stored = systemverilog_vpi_packed_value(
                update.value, vpi_scalar_kinds.at(signal->second),
                vpi_categories.at(signal->second));
            const bool was_forced
                = vpi_forced_signals.contains(signal->second);
            for (const auto alias : vpi_signal_handles.at(signal->second)) {
                if (alias == update.object) {
                    continue;
                }
                require_vpi_value(
                    vpi_registry->update_bound_value(alias, update.value),
                    "alias stored-value publication");
                if (update.forced_value) {
                    require_vpi_value(
                        vpi_registry->update_forced_value(
                            alias, *update.forced_value),
                        "alias forced-value publication");
                } else if (was_forced) {
                    const auto released
                        = vpi_registry->release_bound_force(alias);
                    if (released != runtime::SystemVerilogVpiValueError::None
                        && released
                            != runtime::SystemVerilogVpiValueError::NotForced) {
                        require_vpi_value(
                            released, "alias force release publication");
                    }
                }
            }
            if (interpreter->stored_signal_value(signal->second) != stored) {
                interpreter->deposit_signal(signal->second, stored);
            }
            if (update.forced_value) {
                const auto forced = systemverilog_vpi_packed_value(
                    *update.forced_value,
                    vpi_scalar_kinds.at(signal->second),
                    vpi_categories.at(signal->second));
                if (!interpreter->signal_is_forced(signal->second)
                    || interpreter->signal_value(signal->second) != forced) {
                    interpreter->force_signal(signal->second, forced);
                }
                vpi_forced_signals.insert(signal->second);
            } else {
                if (interpreter->signal_is_forced(signal->second)) {
                    interpreter->release_signal(signal->second);
                }
                vpi_forced_signals.erase(signal->second);
            }
            return runtime::SystemVerilogVpiValueError::None;
        } catch (...) {
            return runtime::SystemVerilogVpiValueError::ResourceLimit;
        }
    }

    void publish_vpi_stored_signal(const SignalId signal)
    {
        std::scoped_lock bridge_lock { vpi_bridge_mutex };
        if (vpi_event_handles.contains(signal)) {
            publish_vpi_event(signal);
        }
        const auto objects = vpi_signal_handles.find(signal);
        if (objects == vpi_signal_handles.end()) {
            return;
        }
        for (const auto object : objects->second) {
            require_vpi_value(
                vpi_registry->update_bound_value(
                    object,
                    vpi_value(signal,
                        interpreter->stored_signal_value(signal))),
                "stored-value publication");
        }
        const auto drivers = vpi_driver_bindings.find(signal);
        if (drivers != vpi_driver_bindings.end()) {
            for (const auto& driver : drivers->second) {
                require_vpi_value(
                    vpi_registry->update_bound_value(
                        driver.handle,
                        vpi_driver_value(signal, driver)),
                    "driver publication");
            }
        }
    }

    void publish_vpi_driver(
        const runtime::simir::ProcessId process,
        const SignalId signal)
    {
        std::scoped_lock bridge_lock { vpi_bridge_mutex };
        const auto drivers = vpi_driver_bindings.find(signal);
        if (drivers == vpi_driver_bindings.end()) {
            return;
        }
        for (const auto& driver : drivers->second) {
            if (driver.process != process) {
                continue;
            }
            require_vpi_value(
                vpi_registry->update_bound_value(
                    driver.handle,
                    vpi_driver_value(signal, driver)),
                "driver publication");
        }
    }

    void publish_vpi_container(
        const runtime::simir::ContainerObjectId object)
    {
        std::scoped_lock bridge_lock { vpi_bridge_mutex };
        const auto words = vpi_container_words.find(object);
        if (words == vpi_container_words.end()) {
            return;
        }
        const auto& value = interpreter->container_object_value(object);
        for (const auto& [handle, ordinal] : words->second) {
            require_vpi_value(
                vpi_registry->update_bound_value(handle,
                    systemverilog_vpi_signal_value(
                        value.elements.at(ordinal),
                        vpi_container_scalar_kinds.at(object),
                        vpi_container_categories.at(object))),
                "memory-word publication");
        }
    }

    void publish_vpi_event(const SignalId event)
    {
        std::scoped_lock bridge_lock { vpi_bridge_mutex };
        const auto objects = vpi_event_handles.find(event);
        if (objects == vpi_event_handles.end()) {
            return;
        }
        for (const auto object : objects->second) {
            const auto dispatched = vpi_callbacks->dispatch_named_event(object);
            if (dispatched
                != runtime::SystemVerilogVpiCallbackError::None) {
                throw std::logic_error {
                    "live VPI named-event callback dispatch failed with error "
                    + std::to_string(static_cast<unsigned>(dispatched))
                };
            }
        }
    }

    void publish_vpi_signal(
        const SignalId signal,
        const PackedLogic4& value)
    {
        std::scoped_lock bridge_lock { vpi_bridge_mutex };
        const auto objects = vpi_signal_handles.find(signal);
        if (objects == vpi_signal_handles.end()) {
            return;
        }
        if (interpreter->signal_is_forced(signal)) {
            for (const auto object : objects->second) {
                require_vpi_value(
                    vpi_registry->update_forced_value(
                        object, vpi_value(signal, value)),
                    "forced-value publication");
            }
            vpi_forced_signals.insert(signal);
        } else {
            for (const auto object : objects->second) {
                require_vpi_value(
                    vpi_registry->update_bound_value(
                        object,
                        vpi_value(signal,
                            interpreter->stored_signal_value(signal))),
                    "effective-value publication");
            }
            if (vpi_forced_signals.erase(signal) != 0U) {
                for (const auto object : objects->second) {
                    const auto released
                        = vpi_registry->release_bound_force(object);
                    if (released != runtime::SystemVerilogVpiValueError::None
                        && released
                            != runtime::SystemVerilogVpiValueError::NotForced) {
                        require_vpi_value(
                            released, "force release publication");
                    }
                }
            }
        }
    }

    void start_vpi()
    {
        if (vpi_started) {
            return;
        }
        const auto sealed = vpi_systems->seal_registrations();
        if (sealed != runtime::SystemVerilogVpiSystemError::None) {
            throw std::logic_error { "failed to seal VPI system registrations" };
        }
        const auto callback = vpi_callbacks->dispatch_lifecycle_now(
            runtime::SystemVerilogVpiCallbackKind::StartOfSimulation);
        if (callback != runtime::SystemVerilogVpiCallbackError::None) {
            throw std::logic_error { "failed to dispatch VPI start callbacks" };
        }
        vpi_started = true;
    }

    void end_vpi()
    {
        if (!vpi_started || vpi_ended) {
            return;
        }
        const auto callback = vpi_callbacks->dispatch_lifecycle_now(
            runtime::SystemVerilogVpiCallbackKind::EndOfSimulation);
        if (callback != runtime::SystemVerilogVpiCallbackError::None) {
            throw std::logic_error { "failed to dispatch VPI end callbacks" };
        }
        vpi_control->mark_finished();
        vpi_ended = true;
    }

    void start_systemc()
    {
        if (systemc_start_attempted) {
            return;
        }
        systemc_start_attempted = true;
        for_each_systemc_registry([&](auto& registry, const auto& roots) {
            registry.start_simulation(roots);
        });
    }

    void end_systemc()
    {
        if (!systemc_start_attempted || systemc_ended) {
            return;
        }
        for_each_systemc_registry([&](auto& registry, const auto& roots) {
            registry.end_simulation(roots);
        });
        systemc_ended = true;
    }

    template <typename Callback>
    void for_each_systemc_registry(Callback&& callback)
    {
        if (built.systemc_hierarchies.empty()) {
            if (built.systemc_hierarchy) {
                callback(*built.systemc_hierarchy, built.systemc_roots);
            }
            return;
        }
        for (const auto& registry : built.systemc_hierarchies) {
            std::vector<std::uint64_t> roots;
            std::ranges::copy_if(
                built.systemc_roots,
                std::back_inserter(roots),
                [&](const auto handle) {
                    return registry->owns_handle(handle);
                });
            callback(*registry, roots);
        }
    }

    BuiltProject built;
    runtime::SystemVerilogClassHeap class_heap;
    runtime::SystemVerilogChandleRegistry chandle_registry;
    runtime::SystemVerilogClassStaticStore class_static_store;
    runtime::SystemVerilogClassMethodRuntime class_methods;
    runtime::SystemVerilogUvmObjectService uvm_objects;
    runtime::SystemVerilogUvmComponentService uvm_components;
    runtime::SystemVerilogUvmActivityService uvm_activity;
    runtime::SystemVerilogUvmPhaseService uvm_phases;
    runtime::SystemVerilogUvmObjectionService uvm_objections;
    runtime::SystemVerilogUvmTlm1Service uvm_tlm1;
    runtime::SystemVerilogUvmTlm2Service uvm_tlm2;
    runtime::SystemVerilogUvmSequenceService uvm_sequences;
    runtime::SystemVerilogUvmCallbackService uvm_callbacks;
    runtime::SystemVerilogUvmTransactionRecorderService uvm_transactions;
    runtime::SystemVerilogUvmRegisterModelService uvm_register_model;
    runtime::SystemVerilogUvmForeignService uvm_foreign;
    runtime::SystemVerilogUvmRegistryService uvm_registry;
    runtime::SystemVerilogUvmFactoryService uvm_factory;
    runtime::SystemVerilogUvmResourcePoolService uvm_resources;
    runtime::SystemVerilogUvmSynchronizationService uvm_synchronization;
    runtime::SystemVerilogUvmConfigDbService uvm_config_db;
    runtime::SystemVerilogUvmCommandLineService uvm_command_line;
    runtime::SystemVerilogUvmTestRunnerService uvm_test_runner;
    runtime::SystemVerilogUvmReportService uvm_reports;
    std::vector<runtime::ScheduledTaskHandle> uvm_report_setting_tasks;
    std::map<std::string, runtime::SystemVerilogUvmRootHandle, std::less<>>
        uvm_roots_by_scope;
#if defined(FSIM_HAS_LLVM)
    // Shared by every compiled executor. It is fully populated before executor
    // installation and outlives the interpreter that owns those executors.
    std::vector<std::uint32_t> signal_widths;
    std::vector<runtime::simir::ValueKind>
        signal_value_kinds;
    // The interpreter owns executors referring to this JIT. Member destruction
    // is reversed, so declaring the JIT first destroys the interpreter first.
    std::unique_ptr<compiler::LlvmJit> jit;
#endif
    std::unique_ptr<runtime::simir::Interpreter> interpreter;
    std::unique_ptr<runtime::SystemVerilogVpiObjectRegistry> vpi_registry;
    std::unique_ptr<runtime::SystemVerilogVpiTimeService> vpi_time;
    std::unique_ptr<runtime::SystemVerilogVpiCallbackManager> vpi_callbacks;
    std::unique_ptr<runtime::SystemVerilogVpiValueControl> vpi_values;
    std::unique_ptr<runtime::SystemVerilogVpiControlService> vpi_control;
    std::unique_ptr<runtime::SystemVerilogVpiSystemRegistry> vpi_systems;
    std::map<SignalId, std::vector<fsim_vpi_handle_v1>> vpi_signal_handles;
    std::map<fsim_vpi_handle_v1, SignalId> vpi_handle_signals;
    std::map<SignalId, std::vector<SystemVerilogVpiDriverBinding>>
        vpi_driver_bindings;
    std::map<SignalId, std::vector<fsim_vpi_handle_v1>> vpi_event_handles;
    std::map<SignalId, runtime::SystemVerilogScalarKind> vpi_scalar_kinds;
    std::map<SignalId, runtime::SystemVerilogVpiValueCategory> vpi_categories;
    std::map<runtime::simir::ContainerObjectId,
        std::vector<std::pair<fsim_vpi_handle_v1, std::size_t>>>
        vpi_container_words;
    std::map<fsim_vpi_handle_v1,
        std::pair<runtime::simir::ContainerObjectId, std::size_t>>
        vpi_word_handles;
    std::map<runtime::simir::ContainerObjectId,
        runtime::SystemVerilogScalarKind>
        vpi_container_scalar_kinds;
    std::map<runtime::simir::ContainerObjectId,
        runtime::SystemVerilogVpiValueCategory>
        vpi_container_categories;
    std::optional<std::uint64_t> vpi_value_state_observer;
    std::set<SignalId> vpi_forced_signals;
    std::recursive_mutex vpi_bridge_mutex;
    bool vpi_started { };
    bool vpi_ended { };
    std::unique_ptr<runtime::VhdlVhpiObjectRegistry> vhdl_vhpi_registry;
    std::unique_ptr<VhdlPslExecution> vhdl_psl;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    SignalChangeHook signal_change_hook;
    std::map<std::uint64_t, SignalChangeHook> signal_observers;
    std::uint64_t next_signal_observer { 1 };
    ScalarSignalChangeHook scalar_signal_change_hook;
    std::map<std::uint64_t, ScalarSignalChangeHook> scalar_signal_observers;
    std::uint64_t next_scalar_signal_observer { 1 };
    SafePointHook safe_point_hook;
    std::map<std::uint64_t, SafePointHook> safe_point_observers;
    std::uint64_t next_safe_point_observer { 1 };
    OutputHook output_hook;
    ReportHook report_hook;
    std::vector<ConcurrentAssertionCoverage>
        concurrent_assertion_coverage;
    std::vector<ConcurrentAssertionEvent>
        concurrent_assertion_events;
    ConcurrentAssertionHook concurrent_assertion_hook;
    VhdlPslAttemptHook vhdl_psl_attempt_hook;
    std::map<std::uint32_t, std::size_t>
        concurrent_assertion_indices;
    std::map<std::uint32_t, bool>
        concurrent_assertion_actions_suppressed;
    bool concurrent_assertions_enabled { true };
    bool concurrent_assertion_pass_actions_enabled { true };
    bool concurrent_assertion_failure_actions_enabled { true };
    ClassPropertyChangeHook class_property_change_hook;
    ClassStaticPropertyChangeHook class_static_property_change_hook;
    std::map<std::string, ConstructorEnvironment> source_static_locals_;
    std::size_t source_method_depth_ { };
    Lifecycle lifecycle { Lifecycle::ready };
    bool systemc_start_attempted { };
    bool systemc_ended { };
};

Simulation::Simulation(
    BuiltProject project,
    const std::uint64_t max_deltas,
    const SimulationEngine engine)
    : impl_(
          std::make_unique<Impl>(
              std::move(project), max_deltas, engine))
{
}
Simulation::~Simulation() = default;
Simulation::Simulation(Simulation&&) noexcept = default;
Simulation& Simulation::operator=(Simulation&&) noexcept = default;

#include "application_simulation_accessors.tpp"

const runtime::SystemVerilogClassPropertyValue&
Simulation::read_class_property(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view property) const
{
    return impl_->class_heap.property(handle, property);
}

void Simulation::deposit_class_property(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view property,
    runtime::PackedLogic4 value)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    auto& destination = impl_->class_heap.property(handle, property);
    if (destination.packed.width() == 0
        || destination.packed.width() != value.width()) {
        throw std::invalid_argument {
            "class property deposit requires an equal-width packed property"
        };
    }
    if (destination.kind == runtime::SystemVerilogClassPropertyKind::Bit2) {
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            if (value.get(bit) != runtime::Logic4::zero
                && value.get(bit) != runtime::Logic4::one) {
                throw std::invalid_argument {
                    "class property deposit would place X/Z into two-state storage"
                };
            }
        }
    }
    destination.packed = std::move(value);
    if (impl_->class_property_change_hook) {
        impl_->class_property_change_hook(
            handle, property, destination.packed,
            impl_->interpreter->scheduler().now(),
            impl_->interpreter->scheduler().delta());
    }
}

runtime::SystemVerilogClassInvocationResult
Simulation::invoke_class_method(
    const std::string_view canonical_method,
    const runtime::SystemVerilogClassHandle this_handle,
    std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
    const std::optional<std::uint32_t> virtual_slot)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class methods are no longer mutable" };
    }
    return impl_->invoke_class_method(
        canonical_method, this_handle, actuals, virtual_slot);
}

runtime::SystemVerilogUvmPhaseExecutionResult
Simulation::execute_uvm_function_phase(
    const runtime::SystemVerilogUvmPhaseHandle phase)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation UVM phases are no longer mutable" };
    }
    return impl_->execute_uvm_function_phase(phase);
}

runtime::SystemVerilogUvmPhaseExecutionResult
Simulation::execute_uvm_task_phase(
    const runtime::SystemVerilogUvmPhaseHandle phase,
    const UvmTaskPhaseContinuation& continuation)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation UVM phases are no longer mutable" };
    }
    return impl_->execute_uvm_task_phase(phase, continuation);
}

void Simulation::schedule_class_method(
    const runtime::SimulationTick time,
    const runtime::StableOrder stable_order,
    std::string canonical_method,
    const runtime::SystemVerilogClassHandle this_handle,
    std::vector<runtime::SystemVerilogClassMethodValue> actuals,
    const std::optional<std::uint32_t> virtual_slot,
    ClassMethodCompletion completion)
{
    if (impl_->lifecycle != Impl::Lifecycle::ready) {
        throw std::logic_error {
            "class methods may only be scheduled on a ready simulation"
        };
    }
    impl_->interpreter->scheduler().schedule_at(
        time,
        runtime::SchedulerPhase::active,
        stable_order,
        [implementation = impl_.get(),
            canonical_method = std::move(canonical_method),
            this_handle,
            actuals = std::move(actuals),
            virtual_slot,
            completion = std::move(completion)](runtime::Scheduler&) mutable {
            const auto result = implementation->invoke_class_method(
                canonical_method, this_handle, actuals, virtual_slot);
            if (completion)
                completion(result, actuals);
        });
}

void Simulation::deposit_string_object(
    const runtime::simir::StringObjectId object,
    const std::string_view value)
{
    impl_->interpreter->deposit_string_object(object, value);
}

void Simulation::deposit_container_object(
    const runtime::simir::ContainerObjectId object,
    runtime::simir::ContainerValue value)
{
    impl_->interpreter->deposit_container_object(
        object, std::move(value));
}

void Simulation::release_signal(const SignalId signal)
{
    impl_->interpreter->release_signal(signal);
}

bool Simulation::signal_is_forced(const SignalId signal) const
{
    return impl_->interpreter->signal_is_forced(signal);
}

void Simulation::start()
{
    if (impl_->lifecycle != Impl::Lifecycle::ready) {
        throw std::logic_error { "simulation is not ready to start" };
    }
    try {
        impl_->start_vpi();
        impl_->start_systemc();
        impl_->interpreter->start();
    } catch (...) {
        impl_->lifecycle = Impl::Lifecycle::poisoned;
        throw;
    }
}

runtime::RunResult Simulation::run(
    const std::optional<SimulationTick> until)
{
    if (impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error(
            "simulation is unavailable after a fatal runtime error");
    }
    if (impl_->lifecycle == Impl::Lifecycle::finished) {
        throw std::logic_error("simulation has finished");
    }
    if (until && *until < now()) {
        throw std::invalid_argument("run time limit is before the current time");
    }
    try {
        impl_->start_vpi();
        impl_->start_systemc();
        auto result = impl_->interpreter->run(until);
        if (impl_->vpi_control->state()
            == runtime::SystemVerilogVpiControlState::Finished) {
            const auto final_result = impl_->interpreter->finish();
            result.time = final_result.time;
            result.delta = final_result.delta;
            result.callbacks_executed += final_result.callbacks_executed;
            result.status = runtime::RunStatus::stopped;
        }
        if (result.status == runtime::RunStatus::completed
            || impl_->interpreter->stopped_by_design()
            || impl_->vpi_control->state()
                == runtime::SystemVerilogVpiControlState::Finished) {
            impl_->vhdl_psl->finish(result.time, result.delta);
            impl_->end_vpi();
            impl_->end_systemc();
            impl_->lifecycle = Impl::Lifecycle::finished;
        }
        return result;
    } catch (...) {
        impl_->lifecycle = Impl::Lifecycle::poisoned;
        throw;
    }
}

void Simulation::request_stop() noexcept
{
    impl_->interpreter->scheduler().request_stop();
}

void Simulation::clear_stop() noexcept
{
    if (impl_->lifecycle == Impl::Lifecycle::ready) {
        impl_->interpreter->scheduler().clear_stop();
    }
}

SimulationTick Simulation::now() const noexcept
{
    return impl_->interpreter->scheduler().now();
}

std::uint64_t Simulation::delta() const noexcept
{
    return impl_->interpreter->scheduler().delta();
}

bool Simulation::has_pending() const noexcept
{
    return impl_->interpreter->scheduler().has_pending();
}

bool Simulation::finished() const noexcept
{
    return impl_->lifecycle == Impl::Lifecycle::finished;
}

bool Simulation::poisoned() const noexcept
{
    return impl_->lifecycle == Impl::Lifecycle::poisoned;
}

std::size_t Simulation::compiled_process_count() const noexcept
{
    return impl_->compiled_processes;
}

std::size_t Simulation::compiled_module_count() const noexcept
{
    return impl_->compiled_modules;
}

NativeCacheStatistics Simulation::native_cache_statistics() const noexcept
{
#if defined(FSIM_HAS_LLVM)
    if (impl_->jit) {
        const auto statistics = impl_->jit->cache_statistics();
        return {
            statistics.hits,
            statistics.misses,
            statistics.stores,
            statistics.rejected_entries,
            statistics.load_failures,
            statistics.store_failures,
            statistics.pruned_entries,
            statistics.pruned_bytes,
            statistics.prune_failures,
        };
    }
#endif
    return { };
}

std::vector<ConcurrentAssertionCoverage>
Simulation::concurrent_assertion_coverage() const
{
    auto result = impl_->concurrent_assertion_coverage;
    std::ranges::sort(
        result, { }, &ConcurrentAssertionCoverage::process);
    return result;
}

const std::vector<ConcurrentAssertionEvent>&
Simulation::concurrent_assertion_events() const noexcept
{
    return impl_->concurrent_assertion_events;
}

const std::vector<runtime::VhdlPslAttemptSnapshot>&
Simulation::vhdl_psl_attempts() const noexcept
{
    return impl_->vhdl_psl->attempts();
}

std::vector<ConcurrentAssertionCoverage> Simulation::vhdl_psl_coverage() const
{
    return impl_->vhdl_psl->coverage();
}

runtime::VhdlVhpiObjectRegistry& Simulation::vhdl_vhpi_objects() noexcept
{
    return *impl_->vhdl_vhpi_registry;
}

const runtime::VhdlVhpiObjectRegistry& Simulation::vhdl_vhpi_objects() const
    noexcept
{
    return *impl_->vhdl_vhpi_registry;
}

runtime::SystemVerilogVpiObjectRegistry&
Simulation::systemverilog_vpi_objects() noexcept
{
    return *impl_->vpi_registry;
}

const runtime::SystemVerilogVpiObjectRegistry&
Simulation::systemverilog_vpi_objects() const noexcept
{
    return *impl_->vpi_registry;
}

runtime::SystemVerilogVpiTimeService&
Simulation::systemverilog_vpi_time() noexcept
{
    return *impl_->vpi_time;
}

const runtime::SystemVerilogVpiTimeService&
Simulation::systemverilog_vpi_time() const noexcept
{
    return *impl_->vpi_time;
}

runtime::SystemVerilogVpiCallbackManager&
Simulation::systemverilog_vpi_callbacks() noexcept
{
    return *impl_->vpi_callbacks;
}

const runtime::SystemVerilogVpiCallbackManager&
Simulation::systemverilog_vpi_callbacks() const noexcept
{
    return *impl_->vpi_callbacks;
}

runtime::SystemVerilogVpiValueControl&
Simulation::systemverilog_vpi_values() noexcept
{
    return *impl_->vpi_values;
}

const runtime::SystemVerilogVpiValueControl&
Simulation::systemverilog_vpi_values() const noexcept
{
    return *impl_->vpi_values;
}

runtime::SystemVerilogVpiControlService&
Simulation::systemverilog_vpi_control() noexcept
{
    return *impl_->vpi_control;
}

const runtime::SystemVerilogVpiControlService&
Simulation::systemverilog_vpi_control() const noexcept
{
    return *impl_->vpi_control;
}

runtime::SystemVerilogVpiSystemRegistry&
Simulation::systemverilog_vpi_systems() noexcept
{
    return *impl_->vpi_systems;
}

const runtime::SystemVerilogVpiSystemRegistry&
Simulation::systemverilog_vpi_systems() const noexcept
{
    return *impl_->vpi_systems;
}

#include "application_simulation_scalar.tpp"

void Simulation::set_safe_point_hook(SafePointHook hook)
{
    impl_->safe_point_hook = std::move(hook);
}

std::uint64_t Simulation::add_safe_point_hook(SafePointHook hook)
{
    if (!hook) {
        throw std::invalid_argument("safe-point observer cannot be empty");
    }
    if (impl_->next_safe_point_observer == 0) {
        throw std::overflow_error("safe-point observer token space exhausted");
    }
    const auto token = impl_->next_safe_point_observer++;
    impl_->safe_point_observers.emplace(token, std::move(hook));
    return token;
}

void Simulation::remove_safe_point_hook(const std::uint64_t token) noexcept
{
    impl_->safe_point_observers.erase(token);
}

void Simulation::set_execution_point_hook(ExecutionPointHook hook)
{
    impl_->interpreter->set_execution_point_hook(std::move(hook));
}

void Simulation::set_output_hook(OutputHook hook)
{
    impl_->output_hook = std::move(hook);
}

void Simulation::set_report_hook(ReportHook hook)
{
    impl_->report_hook = std::move(hook);
}

void Simulation::set_concurrent_assertion_hook(
    ConcurrentAssertionHook hook)
{
    impl_->concurrent_assertion_hook = std::move(hook);
}

void Simulation::set_vhdl_psl_attempt_hook(VhdlPslAttemptHook hook)
{
    impl_->vhdl_psl_attempt_hook = std::move(hook);
}

std::uint64_t Simulation::add_uvm_activity_hook(UvmActivityHook hook)
{
    return impl_->uvm_activity.add_observer(std::move(hook));
}

void Simulation::remove_uvm_activity_hook(
    const std::uint64_t token) noexcept
{
    impl_->uvm_activity.remove_observer(token);
}

void Simulation::set_class_property_change_hook(
    ClassPropertyChangeHook hook)
{
    impl_->class_property_change_hook = std::move(hook);
}

void Simulation::set_class_static_property_change_hook(
    ClassStaticPropertyChangeHook hook)
{
    impl_->class_static_property_change_hook = std::move(hook);
}

} // namespace fsim::app
