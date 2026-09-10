// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Lowerer::ExpressionAttempt Lowerer::lower_user_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    if (expression.kind != ExpressionKind::Call
        || !function_support_initialized_) {
        return ExpressionAttempt { };
    }
    if (expression.text.starts_with(".")
        && !expression.operands.empty()
        && expression.operands.front().kind
            == ExpressionKind::Identifier) {
        auto qualified = expression;
        qualified.text = qualified.operands.front().text
            + qualified.text;
        qualified.operands.erase(qualified.operands.begin());
        if (!qualified.call_argument_names.empty()) {
            qualified.call_argument_names.erase(
                qualified.call_argument_names.begin());
        }
        return lower_user_function_expression(
            qualified, expected_width, expected_type);
    }
    const auto selected = select_function_overload(
        expression, expected_type, FunctionResultKind::Packed);
    if (!selected.named) {
        return ExpressionAttempt { };
    }
    if (!selected.index) {
        return std::nullopt;
    }
    auto function_index = *selected.index;
    const auto& declared_function =
        *function_frames_[function_index].source;
    if (declared_function.return_type.domain
            == frontend::ValueDomain::String
        || declared_function.return_type.systemverilog_container) {
        return ExpressionAttempt { };
    }
    const bool has_defaults = std::ranges::any_of(
        declared_function.arguments,
        [](const frontend::FunctionArgument& argument) {
            return argument.default_value.has_value();
        });
    if (expression.call_argument_names.empty()
        && !has_defaults
        && expression.operands.size()
            != declared_function.arguments.size()) {
        report(
            "FSIM-ELAB-SVFUNC-003",
            "function '" + declared_function.name + "' expects "
                + std::to_string(declared_function.arguments.size())
                + " arguments but received "
                + std::to_string(expression.operands.size()),
            expression.span);
        return std::nullopt;
    }
    const auto actuals = bind_function_actuals(
        expression, declared_function);
    if (!actuals
        || !validate_function_reference_actuals(
            declared_function, *actuals)) {
        return std::nullopt;
    }

    if (declared_function.language
        == frontend::Language::Vhdl2008) {
        const auto simple_type_name = [](const std::string_view spelling) {
          const auto separator = spelling.find_last_of('.');
          return spelling.substr(
              separator == std::string_view::npos
                  ? 0U
                  : separator + 1U);
        };
        const auto unconstrained_builtin_array =
            [&](const frontend::Type& type) {
              const auto name = simple_type_name(type.spelling);
              return !type.packed_range && !type.vhdl_array
                  && (name == "bit_vector"
                      || name == "std_logic_vector"
                      || name == "std_ulogic_vector"
                      || name == "signed"
                      || name == "unsigned");
            };
        auto specialized = declared_function;
        ConstantEnvironment formal_environment;
        ConstantDomainEnvironment formal_domains;
        for (std::size_t index = 0;
             index < specialized.arguments.size(); ++index) {
            auto& formal = specialized.arguments[index];
            const auto& actual = *(*actuals)[index];
            if (formal.type.vhdl_unspecified) {
                if (const auto actual_type =
                        vhdl_expression_type(actual)) {
                    formal.type = *actual_type;
                }
            } else if (unconstrained_builtin_array(formal.type)) {
                if (const auto actual_type =
                        vhdl_expression_type(actual)) {
                    formal.type = *actual_type;
                } else if (const auto width = infer_width(actual);
                           width && *width != 0U) {
                    formal.type.packed_range = frontend::PackedRange{
                        static_cast<std::int64_t>(*width - 1U),
                        0,
                        true};
                }
            }
            if (formal.type.domain
                == frontend::ValueDomain::Integer) {
                if (const auto value = static_integer_value(actual)) {
                    formal_environment.insert_or_assign(
                        formal.name, *value);
                    formal_domains.insert_or_assign(
                        formal.name,
                        ConstantTypeInfo{
                            frontend::ValueDomain::Integer,
                            false,
                            {}});
                }
            }
        }
        if (unconstrained_builtin_array(specialized.return_type)) {
            if (expected_type != nullptr
                && !unconstrained_builtin_array(*expected_type)
                && expected_type->width().value_or(0U) != 0U) {
                specialized.return_type = *expected_type;
            } else if (expected_width != 0U) {
                specialized.return_type.packed_range =
                    frontend::PackedRange{
                        static_cast<std::int64_t>(expected_width - 1U),
                        0,
                        true};
            }
        }
        if (!specialized.vhdl_return_identifier.empty()) {
            const auto constrained_context = [&]() {
              if (expected_type == nullptr
                  || !is_vhdl_array_like(*expected_type)
                  || !vhdl_callable_type_matches(
                      specialized.return_type, *expected_type)
                  || expected_type->width().value_or(0U) == 0U) {
                return false;
              }
              if (!expected_type->vhdl_array) {
                return expected_type->packed_range.has_value();
              }
              return std::ranges::all_of(
                  expected_type->vhdl_array->dimensions,
                  [](const auto& dimension) {
                    return dimension.range.has_value()
                        && !dimension.unconstrained;
                  });
            }();
            if (!constrained_context) {
              report(
                  "FSIM-ELAB-VHRESULT-001",
                  "VHDL-2019 function result subtype '"
                      + specialized.vhdl_return_identifier
                      + "' requires a compatible fully constrained array "
                        "call context",
                  expression.span);
              return std::nullopt;
            }
            const auto implicit_subtype = std::ranges::find(
                specialized.type_aliases,
                specialized.vhdl_return_identifier,
                &frontend::TypeAliasDeclaration::name);
            if (implicit_subtype == specialized.type_aliases.end()) {
              report(
                  "FSIM-ELAB-VHRESULT-002",
                  "VHDL-2019 function result subtype metadata is missing",
                  specialized.vhdl_return_identifier_span);
              return std::nullopt;
            }
            const auto declaration_identity =
                implicit_subtype->type.vhdl_type_declaration;
            specialized.return_type = *expected_type;
            specialized.return_type.vhdl_type_declaration =
                declaration_identity;
            auto specialize_result_subtype =
                [&](frontend::Type& type) {
                  if (!declaration_identity.empty()
                      && type.vhdl_type_declaration
                          == declaration_identity) {
                    type = specialized.return_type;
                  }
                };
            elaboration_detail::visit_local_region_types(
                specialized, specialize_result_subtype);
            implicit_subtype->type = specialized.return_type;
        }
        for (auto& alias : specialized.type_aliases) {
            substitute_parameters(
                alias.type,
                formal_environment,
                formal_domains,
                diagnostics_,
                frontend::Language::Vhdl2008);
        }
        for (auto& variable : specialized.variables) {
            substitute_parameters(
                variable,
                formal_environment,
                formal_domains,
                diagnostics_,
                frontend::Language::Vhdl2008);
        }
        substitute_parameters(
            specialized.statements,
            formal_environment,
            formal_domains,
            diagnostics_,
            frontend::Language::Vhdl2008);
        const auto append_type_identity = [](
            std::string& identity,
            const frontend::Type& type) {
          identity += "|" + type.spelling + ":" + type.nominal_type
              + ":" + std::to_string(
                  static_cast<unsigned>(type.domain))
              + ":" + (type.is_signed ? "s" : "u") + ":";
          if (const auto width = type.width()) {
            identity += std::to_string(*width);
          } else {
            identity += "?";
          }
          if (type.packed_range) {
            identity += ":" + std::to_string(type.packed_range->left)
                + ":" + std::to_string(type.packed_range->right)
                + (type.packed_range->descending ? ":d" : ":a");
          }
          if (type.vhdl_array) {
            for (const auto& dimension :
                 type.vhdl_array->dimensions) {
              identity += ":dim=";
              if (dimension.range) {
                identity += std::to_string(dimension.range->left)
                    + ":" + std::to_string(dimension.range->right)
                    + (dimension.range->descending ? ":d" : ":a");
              } else {
                identity += "?";
              }
            }
          }
        };
        std::string specialization_identity =
            std::to_string(function_index);
        append_type_identity(
            specialization_identity, specialized.return_type);
        for (std::size_t index = 0;
             index < specialized.arguments.size(); ++index) {
          append_type_identity(
              specialization_identity,
              specialized.arguments[index].type);
          if (specialized.arguments[index].type.domain
              == frontend::ValueDomain::Integer) {
            specialization_identity += ":value=";
            if (const auto value = static_integer_value(
                    *(*actuals)[index])) {
              specialization_identity += std::to_string(*value);
            } else {
              specialization_identity += "?";
            }
          }
        }
        if (const auto found =
                vhdl_function_specialization_indices_.find(
                    specialization_identity);
            found
                != vhdl_function_specialization_indices_.end()) {
          function_index = found->second;
        } else {
          specialized.specialization_identity =
              specialization_identity;
          vhdl_function_specializations_.push_back(
              std::move(specialized));
          FunctionFrame specialized_frame;
          specialized_frame.source =
              &vhdl_function_specializations_.back();
          specialized_frame.invocation_identity =
              next_callable_invocation_identity_++;
          if (!specialized_frame.source->automatic) {
            specialized_frame.static_variables =
                allocate_static_callable_variables(
                    specialized_frame.source->variables,
                    specialized_frame.source->statements,
                    specialized_frame.source->name);
          }
          function_index = function_frames_.size();
          function_frames_.push_back(
              std::move(specialized_frame));
          function_dependencies_.emplace_back();
          function_procedure_dependencies_.emplace_back();
          vhdl_function_specialization_indices_.emplace(
              std::move(specialization_identity),
              function_index);
        }
    }
    auto& frame = function_frames_[function_index];
    const auto& function = *frame.source;

    if (!frame.allocated) {
        const auto return_width = function.return_type.width();
        if (!return_width || *return_width == 0) {
            report(
                "FSIM-ELAB-SVFUNC-004",
                "function '" + function.name
                    + "' return type must have a positive executable width",
                function.span);
            return std::nullopt;
        }
        frame.result = allocate_register(
            static_cast<std::size_t>(*return_width),
            function.return_type.domain);
        frame.arguments.reserve(function.arguments.size());
        frame.string_arguments.reserve(function.arguments.size());
        frame.container_arguments.reserve(function.arguments.size());
        frame.argument_is_string.reserve(function.arguments.size());
        frame.argument_is_container.reserve(function.arguments.size());
        for (const auto& argument : function.arguments) {
            if (argument.type.systemverilog_container) {
                const auto type = container_type(argument.type, argument.span);
                if (!type) {
                    return std::nullopt;
                }
                frame.arguments.push_back({ });
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back(
                    allocate_container_register(*type));
                frame.argument_is_string.push_back(false);
                frame.argument_is_container.push_back(true);
                continue;
            }
            if (argument.type.domain
                == frontend::ValueDomain::String) {
                frame.arguments.push_back({ });
                frame.string_arguments.push_back(
                    allocate_string_register());
                frame.container_arguments.push_back({ });
                frame.argument_is_string.push_back(true);
                frame.argument_is_container.push_back(false);
                continue;
            }
            const auto width = argument.type.width();
            if (!width || *width == 0) {
                report(
                    "FSIM-ELAB-SVFUNC-004",
                    "function argument '" + argument.name
                        + "' must have a positive executable width",
                    argument.span);
                return std::nullopt;
            }
            frame.arguments.push_back(allocate_register(
                static_cast<std::size_t>(*width),
                argument.type.domain));
            frame.string_arguments.push_back({ });
            frame.container_arguments.push_back({ });
            frame.argument_is_string.push_back(false);
            frame.argument_is_container.push_back(false);
        }
        if (function.automatic) {
            frame.invocation_packed.push_back(frame.result);
            for (std::size_t index = 0;
                index < function.arguments.size(); ++index) {
                if (frame.argument_is_container[index]) {
                    frame.invocation_containers.push_back(
                        frame.container_arguments[index]);
                } else if (frame.argument_is_string[index]) {
                    frame.invocation_strings.push_back(
                        frame.string_arguments[index]);
                } else {
                    frame.invocation_packed.push_back(frame.arguments[index]);
                }
            }
        }
        frame.allocated = true;
    }

    std::vector<Expression> copy_out_targets(function.arguments.size());
    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        if (function.arguments[index].direction
            == frontend::PortDirection::Input) {
            continue;
        }
        auto target = capture_callable_copy_out_target(
            *(*actuals)[index],
            "@function_target_" + std::to_string(function_index)
                + "_" + std::to_string(index)
                + "_" + std::to_string(process_.operations.size()));
        if (!target) {
            return std::nullopt;
        }
        copy_out_targets[index] = std::move(*target);
    }

    std::vector<RegisterId> packed_actuals(function.arguments.size());
    std::vector<StringRegisterId> string_actuals(function.arguments.size());
    std::vector<ContainerRegisterId> container_actuals(
        function.arguments.size());
    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction != frontend::PortDirection::Output
            && !validate_sv_nominal_assignment(
                &formal.type, *(*actuals)[index])) {
            return std::nullopt;
        }
        if (formal.direction == frontend::PortDirection::Output) {
            if (frame.argument_is_container[index]
                || frame.argument_is_string[index]) {
                report(
                    "FSIM-ELAB-SVFUNC-011",
                    "function output/inout/ref formals require a bounded "
                    "packed integral type",
                    formal.span);
                return std::nullopt;
            }
            packed_actuals[index] = allocate_register(
                static_cast<std::size_t>(*formal.type.width()),
                formal.type.domain);
            continue;
        }
        if (frame.argument_is_container[index]) {
            const auto formal_type = container_type(formal.type, formal.span);
            if (!formal_type) {
                return std::nullopt;
            }
            const auto actual = formal_type->fixed
                ? lower_static_container_assignment_value(
                      *(*actuals)[index],
                      *formal_type)
                : lower_container_expression(
                      *(*actuals)[index]);
            if (!actual) {
                return std::nullopt;
            }
            if (process_.container_register_types.at(*actual)
                != *formal_type) {
                report(
                    "FSIM-ELAB-SVFUNC-009",
                    "function container arguments require an exactly "
                    "compatible kind and profile",
                    (*actuals)[index]->span);
                return std::nullopt;
            }
            container_actuals[index] = allocate_container_register(*formal_type);
            process_.operations.emplace_back(CopyContainerRegister {
                container_actuals[index], *actual });
            continue;
        }
        if (frame.argument_is_string[index]) {
            const auto actual = lower_string_expression(
                *(*actuals)[index]);
            if (!actual) {
                return std::nullopt;
            }
            string_actuals[index] = allocate_string_register();
            process_.operations.emplace_back(CopyStringRegister {
                string_actuals[index], *actual });
            continue;
        }
        const auto width = static_cast<std::size_t>(
            *formal.type.width());
        auto actual = lower_expression(
            *(*actuals)[index], width, &formal.type);
        if (!actual) {
            return std::nullopt;
        }
        if (register_width(*actual) != width) {
            *actual = resize_register(
                *actual,
                width,
                is_signed_expression(*(*actuals)[index]));
        }
        packed_actuals[index] = allocate_register(width, formal.type.domain);
        process_.operations.emplace_back(
            CopyRegister { packed_actuals[index], *actual });
    }

    if (function.automatic) {
        const auto push_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(CallableFramePush {
            frame.invocation_identity,
            frame.invocation_packed,
            frame.invocation_strings,
            frame.invocation_containers,
            true });
        if (!frame.invocation_layout_finalized) {
            frame.invocation_push_sites.push_back(push_site);
        }
    }
    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction == frontend::PortDirection::Output) {
            const auto width = static_cast<std::size_t>(
                *formal.type.width());
            process_.operations.emplace_back(LoadConstant {
                frame.arguments[index],
                default_packed_value(formal.type, width) });
        } else if (frame.argument_is_container[index]) {
            process_.operations.emplace_back(CopyContainerRegister {
                frame.container_arguments[index], container_actuals[index] });
        } else if (frame.argument_is_string[index]) {
            process_.operations.emplace_back(CopyStringRegister {
                frame.string_arguments[index], string_actuals[index] });
        } else {
            process_.operations.emplace_back(CopyRegister {
                frame.arguments[index], packed_actuals[index] });
        }
    }
    const auto return_width = static_cast<std::size_t>(*function.return_type.width());
    process_.operations.emplace_back(LoadConstant {
        frame.result,
        default_packed_value(function.return_type, return_width) });
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0),
        static_cast<InstructionIndex>(call_site + 1U),
        function_call_stack_ });
    if (frame.target) {
        fsim::runtime::simir::operation_get<Call>(process_.operations[call_site]).target = *frame.target;
    } else {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_functions_.push_back(function_index);
    }
    if (active_function_) {
        function_dependencies_[*active_function_].insert(
            function_index);
    }
    const auto destination = allocate_register(
        return_width, function.return_type.domain);
    process_.operations.emplace_back(
        CopyRegister { destination, frame.result });
    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction == frontend::PortDirection::Input) {
            continue;
        }
        if (frame.argument_is_container[index]) {
            process_.operations.emplace_back(CopyContainerRegister {
                container_actuals[index], frame.container_arguments[index] });
        } else if (frame.argument_is_string[index]) {
            process_.operations.emplace_back(CopyStringRegister {
                string_actuals[index], frame.string_arguments[index] });
        } else {
            process_.operations.emplace_back(CopyRegister {
                packed_actuals[index], frame.arguments[index] });
        }
    }
    if (function.automatic) {
        std::vector<RegisterId> preserve_packed { destination };
        std::vector<StringRegisterId> preserve_strings;
        std::vector<ContainerRegisterId> preserve_containers;
        for (std::size_t index = 0;
            index < function.arguments.size(); ++index) {
            if (function.arguments[index].direction
                == frontend::PortDirection::Input) {
                continue;
            }
            if (frame.argument_is_container[index]) {
                preserve_containers.push_back(container_actuals[index]);
            } else if (frame.argument_is_string[index]) {
                preserve_strings.push_back(string_actuals[index]);
            } else {
                preserve_packed.push_back(packed_actuals[index]);
            }
        }
        process_.operations.emplace_back(CallableFramePop {
            frame.invocation_identity,
            std::move(preserve_packed),
            std::move(preserve_strings),
            std::move(preserve_containers) });
    }
    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction == frontend::PortDirection::Input) {
            continue;
        }
        lower_callable_copy_out(
            copy_out_targets[index],
            formal.type,
            packed_actuals[index],
            string_actuals[index],
            container_actuals[index],
            frame.argument_is_string[index],
            frame.argument_is_container[index],
            "@function_copyout_" + std::to_string(function_index)
                + "_" + std::to_string(index));
    }
    if (expected_width != 0 && expected_width != return_width) {
        return resize_register(
            destination,
            expected_width,
            function.return_type.is_signed);
    }
    return destination;
}

std::optional<ContainerRegisterId>
Lowerer::lower_user_container_function_expression(
    const Expression& expression)
{
    if (expression.kind != ExpressionKind::Call
        || !function_support_initialized_) {
        return std::nullopt;
    }
    if (expression.text.starts_with(".")
        && !expression.operands.empty()
        && expression.operands.front().kind
            == ExpressionKind::Identifier) {
        auto qualified = expression;
        qualified.text = qualified.operands.front().text
            + qualified.text;
        qualified.operands.erase(qualified.operands.begin());
        if (!qualified.call_argument_names.empty()) {
            qualified.call_argument_names.erase(
                qualified.call_argument_names.begin());
        }
        return lower_user_container_function_expression(qualified);
    }
    const auto selected = select_function_overload(
        expression, nullptr, FunctionResultKind::Container);
    if (!selected.named) {
        return std::nullopt;
    }
    if (!selected.index) {
        return std::nullopt;
    }
    const auto function_index = *selected.index;
    auto& frame = function_frames_[function_index];
    const auto& function = *frame.source;
    if (!function.return_type.systemverilog_container) {
        return std::nullopt;
    }
    const bool has_defaults = std::ranges::any_of(
        function.arguments,
        [](const frontend::FunctionArgument& argument) {
            return argument.default_value.has_value();
        });
    if (expression.call_argument_names.empty()
        && !has_defaults
        && expression.operands.size() != function.arguments.size()) {
        report(
            "FSIM-ELAB-SVFUNC-003",
            "function '" + function.name + "' expects "
                + std::to_string(function.arguments.size())
                + " arguments but received "
                + std::to_string(expression.operands.size()),
            expression.span);
        return std::nullopt;
    }
    const auto actuals = bind_function_actuals(expression, function);
    if (!actuals
        || !validate_function_reference_actuals(function, *actuals)) {
        return std::nullopt;
    }

    const auto result_type = container_type(function.return_type, function.span);
    if (!result_type) {
        return std::nullopt;
    }
    if (!frame.allocated) {
        frame.result_is_container = true;
        frame.container_result = allocate_container_register(*result_type);
        frame.container_result_default = allocate_container_register(*result_type);
        frame.arguments.reserve(function.arguments.size());
        frame.string_arguments.reserve(function.arguments.size());
        frame.container_arguments.reserve(function.arguments.size());
        frame.argument_is_string.reserve(function.arguments.size());
        frame.argument_is_container.reserve(function.arguments.size());
        for (const auto& argument : function.arguments) {
            if (argument.type.systemverilog_container) {
                const auto type = container_type(argument.type, argument.span);
                if (!type) {
                    return std::nullopt;
                }
                frame.arguments.push_back({ });
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back(
                    allocate_container_register(*type));
                frame.argument_is_string.push_back(false);
                frame.argument_is_container.push_back(true);
                continue;
            }
            if (argument.type.domain
                == frontend::ValueDomain::String) {
                frame.arguments.push_back({ });
                frame.string_arguments.push_back(
                    allocate_string_register());
                frame.container_arguments.push_back({ });
                frame.argument_is_string.push_back(true);
                frame.argument_is_container.push_back(false);
                continue;
            }
            const auto width = argument.type.width();
            if (!width || *width == 0) {
                report(
                    "FSIM-ELAB-SVFUNC-004",
                    "function argument '" + argument.name
                        + "' must have a positive executable width",
                    argument.span);
                return std::nullopt;
            }
            frame.arguments.push_back(allocate_register(
                static_cast<std::size_t>(*width),
                argument.type.domain));
            frame.string_arguments.push_back({ });
            frame.container_arguments.push_back({ });
            frame.argument_is_string.push_back(false);
            frame.argument_is_container.push_back(false);
        }
        if (function.automatic) {
            frame.invocation_containers.push_back(frame.container_result);
            for (std::size_t index = 0;
                index < function.arguments.size(); ++index) {
                if (frame.argument_is_container[index]) {
                    frame.invocation_containers.push_back(
                        frame.container_arguments[index]);
                } else if (frame.argument_is_string[index]) {
                    frame.invocation_strings.push_back(
                        frame.string_arguments[index]);
                } else {
                    frame.invocation_packed.push_back(frame.arguments[index]);
                }
            }
        }
        frame.allocated = true;
    }

    std::vector<Expression> copy_out_targets(function.arguments.size());
    std::vector<RegisterId> packed_actuals(function.arguments.size());
    std::vector<StringRegisterId> string_actuals(function.arguments.size());
    std::vector<ContainerRegisterId> container_actuals(
        function.arguments.size());
    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction != frontend::PortDirection::Input) {
            auto target = capture_callable_copy_out_target(
                *(*actuals)[index],
                "@function_target_" + std::to_string(function_index)
                    + "_" + std::to_string(index)
                    + "_" + std::to_string(process_.operations.size()));
            if (!target) {
                return std::nullopt;
            }
            copy_out_targets[index] = std::move(*target);
        }
        if (formal.direction == frontend::PortDirection::Output) {
            if (frame.argument_is_container[index]
                || frame.argument_is_string[index]) {
                report(
                    "FSIM-ELAB-SVFUNC-011",
                    "function output/inout/ref formals require a bounded "
                    "packed integral type",
                    formal.span);
                return std::nullopt;
            }
            packed_actuals[index] = allocate_register(
                static_cast<std::size_t>(*formal.type.width()),
                formal.type.domain);
            continue;
        }
        if (frame.argument_is_container[index]) {
            const auto formal_type = container_type(formal.type, formal.span);
            if (!formal_type) {
                return std::nullopt;
            }
            const auto actual = formal_type->fixed
                ? lower_static_container_assignment_value(
                      *(*actuals)[index],
                      *formal_type)
                : lower_container_expression(
                      *(*actuals)[index]);
            if (!actual) {
                return std::nullopt;
            }
            if (process_.container_register_types.at(*actual)
                != *formal_type) {
                report(
                    "FSIM-ELAB-SVFUNC-009",
                    "function container arguments require an exactly "
                    "compatible kind and profile",
                    (*actuals)[index]->span);
                return std::nullopt;
            }
            container_actuals[index] = allocate_container_register(*formal_type);
            process_.operations.emplace_back(CopyContainerRegister {
                container_actuals[index], *actual });
            continue;
        }
        if (frame.argument_is_string[index]) {
            const auto actual = lower_string_expression(*(*actuals)[index]);
            if (!actual) {
                return std::nullopt;
            }
            string_actuals[index] = allocate_string_register();
            process_.operations.emplace_back(CopyStringRegister {
                string_actuals[index], *actual });
            continue;
        }
        const auto width = static_cast<std::size_t>(
            *formal.type.width());
        auto actual = lower_expression(
            *(*actuals)[index], width, &formal.type);
        if (!actual) {
            return std::nullopt;
        }
        if (register_width(*actual) != width) {
            *actual = resize_register(
                *actual,
                width,
                is_signed_expression(*(*actuals)[index]));
        }
        packed_actuals[index] = allocate_register(width, formal.type.domain);
        process_.operations.emplace_back(
            CopyRegister { packed_actuals[index], *actual });
    }

    if (function.automatic) {
        const auto push_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(CallableFramePush {
            frame.invocation_identity,
            frame.invocation_packed,
            frame.invocation_strings,
            frame.invocation_containers,
            true });
        if (!frame.invocation_layout_finalized) {
            frame.invocation_push_sites.push_back(push_site);
        }
    }

    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction == frontend::PortDirection::Output) {
            const auto width = static_cast<std::size_t>(
                *formal.type.width());
            process_.operations.emplace_back(LoadConstant {
                frame.arguments[index],
                default_packed_value(formal.type, width) });
        } else if (frame.argument_is_container[index]) {
            process_.operations.emplace_back(CopyContainerRegister {
                frame.container_arguments[index], container_actuals[index] });
        } else if (frame.argument_is_string[index]) {
            process_.operations.emplace_back(CopyStringRegister {
                frame.string_arguments[index], string_actuals[index] });
        } else {
            process_.operations.emplace_back(CopyRegister {
                frame.arguments[index], packed_actuals[index] });
        }
    }

    process_.operations.emplace_back(
        CopyContainerRegister {
            frame.container_result,
            frame.container_result_default });
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0),
        static_cast<InstructionIndex>(call_site + 1U),
        function_call_stack_ });
    if (frame.target) {
        fsim::runtime::simir::operation_get<Call>(process_.operations[call_site]).target = *frame.target;
    } else {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_functions_.push_back(function_index);
    }
    if (active_function_) {
        function_dependencies_[*active_function_].insert(
            function_index);
    }
    const auto destination = allocate_container_register(*result_type);
    process_.operations.emplace_back(
        CopyContainerRegister {
            destination, frame.container_result });
    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        if (function.arguments[index].direction
            == frontend::PortDirection::Input) {
            continue;
        }
        if (frame.argument_is_container[index]) {
            process_.operations.emplace_back(CopyContainerRegister {
                container_actuals[index], frame.container_arguments[index] });
        } else if (frame.argument_is_string[index]) {
            process_.operations.emplace_back(CopyStringRegister {
                string_actuals[index], frame.string_arguments[index] });
        } else {
            process_.operations.emplace_back(CopyRegister {
                packed_actuals[index], frame.arguments[index] });
        }
    }
    if (function.automatic) {
        std::vector<RegisterId> preserve_packed;
        std::vector<StringRegisterId> preserve_strings;
        std::vector<ContainerRegisterId> preserve_containers { destination };
        for (std::size_t index = 0;
            index < function.arguments.size(); ++index) {
            if (function.arguments[index].direction
                == frontend::PortDirection::Input) {
                continue;
            }
            if (frame.argument_is_container[index]) {
                preserve_containers.push_back(container_actuals[index]);
            } else if (frame.argument_is_string[index]) {
                preserve_strings.push_back(string_actuals[index]);
            } else {
                preserve_packed.push_back(packed_actuals[index]);
            }
        }
        process_.operations.emplace_back(CallableFramePop {
            frame.invocation_identity,
            std::move(preserve_packed),
            std::move(preserve_strings),
            std::move(preserve_containers) });
    }
    for (std::size_t index = 0;
        index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction == frontend::PortDirection::Input) {
            continue;
        }
        lower_callable_copy_out(
            copy_out_targets[index],
            formal.type,
            packed_actuals[index],
            string_actuals[index],
            container_actuals[index],
            frame.argument_is_string[index],
            frame.argument_is_container[index],
            "@function_copyout_" + std::to_string(function_index)
                + "_" + std::to_string(index));
    }
    return destination;
}

void Lowerer::lower_function_return(const Statement& statement)
{
    if (!active_function_) {
        report(
            "FSIM-ELAB-SVFUNC-005",
            "return statement is outside an executable function",
            statement.span);
        return;
    }
    auto& frame = function_frames_[*active_function_];
    const auto& function = *frame.source;
    if (frame.result_is_container) {
        if (!statement.value.valid()) {
            report(
                "FSIM-ELAB-SVFUNC-005",
                "static-array function return requires a value",
                statement.span);
        } else {
            const auto result_type = container_type(
                function.return_type, function.span);
            if (result_type) {
                const auto value = result_type->fixed
                    ? lower_static_container_assignment_value(
                          statement.value, *result_type)
                    : lower_container_expression(
                          statement.value);
                if (value) {
                    if (!result_type->fixed
                        && process_.container_register_types.at(*value)
                            != *result_type) {
                        report(
                            "FSIM-ELAB-SVFUNC-008",
                            "function return container kind and profile "
                            "must exactly match the declared result",
                            statement.value.span);
                    } else {
                        process_.operations.emplace_back(
                            CopyContainerRegister {
                                frame.container_result, *value });
                    }
                }
            }
        }
        function_return_jumps_.push_back(
            static_cast<InstructionIndex>(
                process_.operations.size()));
        process_.operations.emplace_back(Jump { });
        return;
    }
    if (frame.result_is_string) {
        if (!statement.value.valid()) {
            report(
                "FSIM-ELAB-SVFUNC-005",
                "string function return requires a value",
                statement.span);
        } else if (const auto value = lower_string_expression(statement.value)) {
            process_.operations.emplace_back(
                CopyStringRegister {
                    frame.string_result, *value });
        }
        function_return_jumps_.push_back(
            static_cast<InstructionIndex>(
                process_.operations.size()));
        process_.operations.emplace_back(Jump { });
        return;
    }
    const auto width = static_cast<std::size_t>(*function.return_type.width());
    if (!statement.value.valid()) {
        report(
            "FSIM-ELAB-SVFUNC-005",
            "function return requires a value",
            statement.span);
    } else if (!validate_sv_nominal_assignment(
                   &function.return_type, statement.value)) {
        return;
    } else {
        auto value = lower_expression(
            statement.value, width, &function.return_type);
        if (value) {
            if (register_width(*value) != width) {
                *value = resize_register(
                    *value,
                    width,
                    is_signed_expression(statement.value));
            }
            process_.operations.emplace_back(
                CopyRegister { frame.result, *value });
        }
    }
    function_return_jumps_.push_back(
        static_cast<InstructionIndex>(
            process_.operations.size()));
    process_.operations.emplace_back(Jump { });
}

void Lowerer::lower_function_body(const std::size_t function_index)
{
    auto& frame = function_frames_[function_index];
    if (frame.lowered || !frame.allocated) {
        return;
    }
    frame.target = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto call_site : frame.call_sites) {
        fsim::runtime::simir::operation_get<Call>(process_.operations[call_site]).target = *frame.target;
    }
    frame.call_sites.clear();
    frame.lowered = true;

    auto saved_locals = std::move(locals_);
    auto saved_string_locals = std::move(string_locals_);
    auto saved_container_locals = std::move(container_locals_);
    auto saved_signed = std::move(local_signed_);
    auto saved_ranges = std::move(local_ranges_);
    auto saved_integer_ranges = std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    auto saved_loop_controls = std::move(loop_controls_);
    auto saved_block_controls = std::move(block_controls_);
    auto saved_named_block_controls = std::move(named_block_controls_);
    auto saved_named_fork_controls = std::move(named_fork_controls_);
    auto saved_return_jumps = std::move(function_return_jumps_);
    const auto saved_active = active_function_;
    locals_.clear();
    string_locals_.clear();
    container_locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    local_scope_ = { frame.source->name };
    loop_controls_.clear();
    block_controls_.clear();
    named_block_controls_.clear();
    named_fork_controls_.clear();
    function_return_jumps_.clear();
    active_function_ = function_index;

    const auto bind =
        [&](const std::string& name,
            const frontend::Type& type,
            const RegisterId register_id) {
            locals_.insert_or_assign(name, register_id);
            local_signed_.insert_or_assign(name, type.is_signed);
            local_ranges_.insert_or_assign(name, type.packed_range);
            local_integer_ranges_.insert_or_assign(
                name, type.integer_range);
            local_members_.insert_or_assign(
                name, type.packed_members);
            local_types_.insert_or_assign(name, &type);
        };
    if (frame.result_is_container) {
        container_locals_.insert_or_assign(
            frame.source->name, frame.container_result);
        local_types_.insert_or_assign(
            frame.source->name, &frame.source->return_type);
        const auto separator = frame.source->name.rfind("::");
        const auto alias = separator == std::string::npos
            ? frame.source->name
            : frame.source->name.substr(separator + 2);
        process_.debug_container_locals.push_back(
            DebugContainerLocal {
                scoped_local_name(alias),
                frame.container_result,
                process_.container_register_types.at(
                    frame.container_result),
                SourceLocation {
                    frame.source->span.source_name.str(),
                    static_cast<std::uint32_t>(
                        frame.source->span.begin.line),
                    static_cast<std::uint32_t>(
                        frame.source->span.begin.column) } });
    } else if (frame.result_is_string) {
        string_locals_.insert_or_assign(
            frame.source->name, frame.string_result);
        local_types_.insert_or_assign(
            frame.source->name, &frame.source->return_type);
    } else {
        bind(
            frame.source->name,
            frame.source->return_type,
            frame.result);
    }
    if (const auto separator = frame.source->name.rfind("::");
        separator != std::string::npos) {
        if (frame.result_is_container) {
            const auto alias = frame.source->name.substr(separator + 2);
            container_locals_.insert_or_assign(
                alias, frame.container_result);
            local_types_.insert_or_assign(
                alias, &frame.source->return_type);
        } else if (frame.result_is_string) {
            string_locals_.insert_or_assign(
                frame.source->name.substr(separator + 2),
                frame.string_result);
        } else {
            bind(
                frame.source->name.substr(separator + 2),
                frame.source->return_type,
                frame.result);
        }
    }
    for (std::size_t index = 0;
        index < frame.source->arguments.size(); ++index) {
        if (frame.argument_is_container[index]) {
            container_locals_.insert_or_assign(
                frame.source->arguments[index].name,
                frame.container_arguments[index]);
            local_types_.insert_or_assign(
                frame.source->arguments[index].name,
                &frame.source->arguments[index].type);
        } else if (frame.argument_is_string[index]) {
            string_locals_.insert_or_assign(
                frame.source->arguments[index].name,
                frame.string_arguments[index]);
            local_types_.insert_or_assign(
                frame.source->arguments[index].name,
                &frame.source->arguments[index].type);
        } else {
            bind(
                frame.source->arguments[index].name,
                frame.source->arguments[index].type,
                frame.arguments[index]);
        }
    }
    if (frame.source->automatic) {
        const auto packed_begin = next_register_;
        const auto string_begin = next_string_register_;
        const auto container_begin = next_container_register_;
        initialize_variables(frame.source->variables);
        lower_statements(frame.source->statements);
        for (auto id = packed_begin; id < next_register_; ++id) {
            frame.invocation_packed.push_back(id);
        }
        for (auto id = string_begin; id < next_string_register_; ++id) {
            frame.invocation_strings.push_back(id);
        }
        for (auto id = container_begin;
            id < next_container_register_; ++id) {
            frame.invocation_containers.push_back(id);
        }
    } else {
        bind_static_callable_variables(
            frame.source->variables, frame.static_variables);
        lower_statements(frame.source->statements);
    }
    const auto epilogue = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto jump : function_return_jumps_) {
        process_.operations[jump] = Jump { epilogue };
    }
    if (frame.source->automatic) {
        emit_vhdl_access_scope_cleanup(frame.source->variables);
    }
    process_.operations.emplace_back(
        Return { function_call_stack_ });
    for (const auto push_site : frame.invocation_push_sites) {
        process_.operations[push_site] = CallableFramePush {
            frame.invocation_identity,
            frame.invocation_packed,
            frame.invocation_strings,
            frame.invocation_containers,
            true
        };
    }
    frame.invocation_push_sites.clear();
    frame.invocation_layout_finalized = true;

    active_function_ = saved_active;
    function_return_jumps_ = std::move(saved_return_jumps);
    loop_controls_ = std::move(saved_loop_controls);
    block_controls_ = std::move(saved_block_controls);
    named_block_controls_ = std::move(saved_named_block_controls);
    named_fork_controls_ = std::move(saved_named_fork_controls);
    local_scope_ = std::move(saved_scope);
    local_types_ = std::move(saved_types);
    local_members_ = std::move(saved_members);
    local_integer_ranges_ = std::move(saved_integer_ranges);
    local_ranges_ = std::move(saved_ranges);
    local_signed_ = std::move(saved_signed);
    locals_ = std::move(saved_locals);
    string_locals_ = std::move(saved_string_locals);
    container_locals_ = std::move(saved_container_locals);
}

void Lowerer::lower_pending_functions()
{
    while (!pending_functions_.empty()) {
        const auto function = pending_functions_.front();
        pending_functions_.pop_front();
        function_frames_[function].queued = false;
        lower_function_body(function);
    }
}

} // namespace fsim::elaboration
