// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include "fsim/support/sha256.hpp"

#include <set>

namespace fsim::elaboration::elaboration_detail {
namespace {

using Value = SystemVerilogConstantValue;
using CallCache = std::unordered_map<std::string, Value>;

thread_local CallCache* active_fold_call_cache { };
thread_local std::vector<std::unique_ptr<CallCache>>
    active_elaboration_call_caches;

void append_key_component(
    std::string& key,
    const std::string_view component)
{
    key += std::to_string(component.size());
    key += ':';
    key += component;
}

void append_expression_behavior(
    std::string& identity,
    const frontend::Expression& expression,
    std::set<std::string>& identifiers)
{
    append_key_component(identity,
        std::to_string(static_cast<unsigned>(expression.kind)));
    append_key_component(identity, expression.text);
    append_key_component(identity, expression.nominal_type);
    if (expression.kind == frontend::ExpressionKind::Identifier) {
        identifiers.insert(expression.text);
    }
    for (const auto& operand : expression.operands) {
        append_expression_behavior(identity, operand, identifiers);
    }
    for (const auto& choices :
        expression.aggregate_choice_expressions) {
        for (const auto& choice : choices) {
            append_expression_behavior(identity, choice, identifiers);
        }
    }
}

void append_statement_behavior(
    std::string& identity,
    const frontend::Statement& statement,
    std::set<std::string>& identifiers)
{
    append_key_component(identity,
        std::to_string(static_cast<unsigned>(statement.kind)));
    append_key_component(identity, statement.label);
    append_key_component(identity, statement.task_name);
    append_key_component(identity, statement.procedure_name);
    append_key_component(identity, statement.loop_variable);
    append_key_component(identity,
        std::to_string(statement.loop_descending));
    append_key_component(identity,
        std::to_string(statement.loop_limit_exclusive));
    append_expression_behavior(identity, statement.target, identifiers);
    append_expression_behavior(identity, statement.value, identifiers);
    append_expression_behavior(identity, statement.condition, identifiers);
    append_expression_behavior(
        identity, statement.loop_initial, identifiers);
    append_expression_behavior(
        identity, statement.loop_limit, identifiers);
    for (const auto& argument : statement.task_arguments) {
        append_expression_behavior(identity, argument, identifiers);
    }
    for (const auto& association : statement.procedure_arguments) {
        append_expression_behavior(
            identity, association.value, identifiers);
    }
    for (const auto& declaration : statement.declarations) {
        append_key_component(identity,
            vhdl_type_identity(declaration.type));
        if (declaration.initializer) {
            append_expression_behavior(
                identity, *declaration.initializer, identifiers);
        }
    }
    for (const auto& alternative : statement.case_alternatives) {
        for (const auto& choice : alternative.choices) {
            append_expression_behavior(identity, choice, identifiers);
        }
        for (const auto& nested : alternative.statements) {
            append_statement_behavior(identity, nested, identifiers);
        }
    }
    for (const auto& nested : statement.statements) {
        append_statement_behavior(identity, nested, identifiers);
    }
    for (const auto& nested : statement.else_statements) {
        append_statement_behavior(identity, nested, identifiers);
    }
}

struct CallableBehaviorIdentity {
    std::string digest;
    std::set<std::string> identifiers;
};

CallableBehaviorIdentity callable_behavior_identity(
    const frontend::FunctionDeclaration& function)
{
    std::string identity;
    std::set<std::string> identifiers;
    append_key_component(identity, function.name);
    append_key_component(identity, function.specialization_identity);
    append_key_component(identity,
        vhdl_type_identity(function.return_type));
    for (const auto& argument : function.arguments) {
        append_key_component(identity, argument.name);
        append_key_component(identity,
            vhdl_type_identity(argument.type));
        if (argument.default_value) {
            append_expression_behavior(
                identity, *argument.default_value, identifiers);
        }
    }
    for (const auto& constant : function.constants) {
        append_key_component(identity, constant.name);
        append_key_component(identity,
            vhdl_type_identity(constant.type));
        append_expression_behavior(
            identity, constant.default_value, identifiers);
    }
    for (const auto& variable : function.variables) {
        append_key_component(identity, variable.name);
        append_key_component(identity,
            vhdl_type_identity(variable.type));
        if (variable.initializer) {
            append_expression_behavior(
                identity, *variable.initializer, identifiers);
        }
    }
    for (const auto& statement : function.statements) {
        append_statement_behavior(identity, statement, identifiers);
    }
    for (const auto& nested : function.functions) {
        const auto nested_identity = callable_behavior_identity(nested);
        append_key_component(identity, nested_identity.digest);
        identifiers.insert(
            nested_identity.identifiers.begin(),
            nested_identity.identifiers.end());
    }
    const auto digest = support::Sha256::hex(
        support::Sha256::digest(identity));
    return { digest, std::move(identifiers) };
}

class FoldCallCacheScope final {
public:
    explicit FoldCallCacheScope(CallCache& cache)
        : previous_ { active_fold_call_cache }
    {
        active_fold_call_cache = &cache;
    }

    FoldCallCacheScope(const FoldCallCacheScope&) = delete;
    FoldCallCacheScope& operator=(const FoldCallCacheScope&) = delete;

    ~FoldCallCacheScope()
    {
        active_fold_call_cache = previous_;
    }

private:
    CallCache* previous_ { };
};

enum class Flow {
    normal,
    returned,
    broken,
    continued,
    failed,
};

[[nodiscard]] bool callable_name_matches(
    const std::string_view declared,
    const std::string_view referenced,
    const frontend::Language language) {
    if (declared == referenced) {
        return true;
    }
    const auto leaf_name = [](const std::string_view name) {
        const auto separator = name.find_last_of(".:");
        return separator == std::string_view::npos
            ? name : name.substr(separator + 1U);
    };
    return language != frontend::Language::Vhdl2008
        && leaf_name(declared) == leaf_name(referenced)
        && (declared.find_first_of(".[") != std::string_view::npos
            || referenced.find_first_of(".[") != std::string_view::npos);
}

class ConstantFunctionEvaluator final {
public:
    ConstantFunctionEvaluator(
        const std::vector<frontend::FunctionDeclaration>& functions,
        const SystemVerilogConstantEnvironment& globals,
        const ConstantEnvironment& fallback)
        : functions_(functions),
          globals_(globals),
          fallback_(fallback),
          call_cache_(!active_elaboration_call_caches.empty()
                  ? *active_elaboration_call_caches.back()
                  : active_fold_call_cache != nullptr
                      ? *active_fold_call_cache : owned_call_cache_)
    {}

    std::optional<Value> evaluate(
        const Expression& expression,
        std::string& error) {
        return evaluate_expression(expression, globals_, error);
    }

private:
    struct FixedArrayLayout {
        const frontend::Type* element_type { };
        std::int64_t left { };
        std::int64_t right { };
        std::uint32_t element_width { };
        std::uint32_t total_width { };
    };

    [[nodiscard]] std::optional<FixedArrayLayout>
    fixed_array_layout(
        const frontend::Type& type,
        const SystemVerilogConstantEnvironment& environment) const {
        std::optional<std::int64_t> left;
        std::optional<std::int64_t> right;
        const frontend::Type* element_type = nullptr;
        if (type.vhdl_array
            && type.vhdl_array->dimensions.size() == 1U
            && type.vhdl_array->dimensions.front().range
            && !type.vhdl_array->dimensions.front().null
            && type.vhdl_array->element_types.size() == 1U
            && !type.vhdl_array->element_types.front().vhdl_array) {
            left = type.vhdl_array->dimensions.front().range->left;
            right = type.vhdl_array->dimensions.front().range->right;
            element_type = &type.vhdl_array->element_types.front();
        } else if (type.systemverilog_container
                   && type.systemverilog_container->kind
                       == frontend::SystemVerilogContainerKind::StaticArray
                   && type.systemverilog_container->element_types.size()
                       == 1U
                   && !type.systemverilog_container->element_types.front()
                           .systemverilog_container) {
            const auto& container = *type.systemverilog_container;
            element_type = &container.element_types.front();
            if (!container.static_range_expressions.empty()) {
            std::string ignored;
            const auto left_value =
                evaluate_systemverilog_constant_expression(
                    container.static_range_expressions.front().left,
                    environment,
                    fallback_,
                    ignored);
            const auto right_value = left_value
                ? evaluate_systemverilog_constant_expression(
                      container.static_range_expressions.front().right,
                      environment,
                      fallback_,
                      ignored)
                : std::nullopt;
            if (left_value) {
                left = left_value->integer_value();
            }
            if (right_value) {
                right = right_value->integer_value();
            }
            } else if (container.static_range) {
                left = container.static_range->left;
                right = container.static_range->right;
            }
        } else {
            return std::nullopt;
        }
        const auto element_width = element_type->width();
        if (!left || !right || !element_width || *element_width == 0U
            || *element_width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        const auto count = index_distance(*left, *right) + 1U;
        if (count == 0U
            || count > std::numeric_limits<std::uint32_t>::max()
                    / *element_width) {
            return std::nullopt;
        }
        return FixedArrayLayout {
            element_type,
            *left,
            *right,
            static_cast<std::uint32_t>(*element_width),
            static_cast<std::uint32_t>(count * *element_width)
        };
    }

    [[nodiscard]] static std::optional<std::uint32_t>
    fixed_array_offset(
        const FixedArrayLayout& layout,
        const std::int64_t index) {
        const auto low = std::min(layout.left, layout.right);
        const auto high = std::max(layout.left, layout.right);
        if (index < low || index > high) {
            return std::nullopt;
        }
        const auto position = layout.right >= index
            ? static_cast<std::uint64_t>(layout.right - index)
            : static_cast<std::uint64_t>(index - layout.right);
        return static_cast<std::uint32_t>(
            position * layout.element_width);
    }

    std::optional<Value> evaluate_expression(
        const Expression& expression,
        const SystemVerilogConstantEnvironment& environment,
        std::string& error,
        const frontend::Type* expected_type = nullptr) {
        if (expression.kind == ExpressionKind::Identifier
            && !environment.contains(expression.text)) {
            std::vector<const frontend::FunctionDeclaration*> matches;
            for (const auto& function : functions_) {
                if (callable_name_matches(
                        function.name, expression.text, function.language)
                    && function.arguments.empty()) {
                    matches.push_back(&function);
                }
            }
            for (const auto* active : call_stack_) {
                for (const auto& nested : active->functions) {
                    if (callable_name_matches(
                            nested.name, expression.text, nested.language)
                        && nested.arguments.empty()) {
                        matches.push_back(&nested);
                    }
                }
            }
            if (matches.size() == 1U) {
                return evaluate_call(
                    *matches.front(),
                    Expression{
                        ExpressionKind::Call,
                        expression.text,
                        {},
                        expression.span},
                    environment,
                    expected_type,
                    error);
            }
            if (matches.size() > 1U) {
                error = "zero-argument constant function reference '"
                    + expression.text + "' is ambiguous";
                return std::nullopt;
            }
        }
        if (expression.kind == ExpressionKind::Call
            && expression.operands.size() == 1U
            && !type_scopes_.empty()) {
            const auto type = type_scopes_.back()->find(expression.text);
            if (type != type_scopes_.back()->end()
                && (type->second->packed_range
                    || fixed_array_layout(*type->second, environment))) {
                return evaluate_expression(
                    Expression{
                        ExpressionKind::Index,
                        "index",
                        {Expression{
                             ExpressionKind::Identifier,
                             expression.text,
                             {},
                             expression.span},
                         expression.operands.front()},
                        expression.span},
                    environment,
                    error,
                    expected_type);
            }
        }
        if (expected_type != nullptr
            && (expression.kind == ExpressionKind::Aggregate
                || expression.kind == ExpressionKind::StringLiteral
                || expression.kind == ExpressionKind::LogicLiteral)) {
            if (auto packed = static_vhdl_value(
                    expression, *expected_type, error)) {
                return Value{
                    std::move(*packed),
                    expected_type->is_signed,
                    false,
                    expected_type->domain,
                    expected_type->nominal_type,
                    expression.span};
            }
            if (expression.kind == ExpressionKind::Aggregate) {
                return std::nullopt;
            }
        }
        if (expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2U
            && expression.operands.front().kind
                == ExpressionKind::Identifier
            && !type_scopes_.empty()) {
            const auto& name = expression.operands.front().text;
            const auto type = type_scopes_.back()->find(name);
            const auto base = environment.find(name);
            const auto layout = type != type_scopes_.back()->end()
                ? fixed_array_layout(*type->second, environment)
                : std::nullopt;
            if (layout && base != environment.end()) {
                const auto index = evaluate_expression(
                    expression.operands[1], environment, error);
                const auto integer = index
                    ? index->integer_value() : std::nullopt;
                const auto offset = integer
                    ? fixed_array_offset(*layout, *integer)
                    : std::nullopt;
                if (!offset) {
                    error = "constant function fixed-array index is outside "
                            "the declared range";
                    return std::nullopt;
                }
                auto packed = runtime::PackedLogic4(
                    layout->element_width, runtime::Logic4::zero);
                for (std::uint32_t bit = 0;
                     bit < layout->element_width; ++bit) {
                    const auto state = base->second.packed.get_logic9(
                        static_cast<std::size_t>(*offset + bit));
                    if (packed.is_logic9()) {
                        packed.set_logic9(bit, state);
                    } else {
                        packed.set(bit, runtime::to_logic4(state));
                    }
                }
                return Value {
                    std::move(packed),
                    layout->element_type->is_signed,
                    false,
                    layout->element_type->domain,
                    layout->element_type->nominal_type,
                    expression.span
                };
            }
        }
        const bool packed_query =
            expression.kind == ExpressionKind::Call
            && (expression.text == "$bits"
                || expression.text == "$left"
                || expression.text == "$right"
                || expression.text == "$low"
                || expression.text == "$high"
                || expression.text == "$size"
                || expression.text == "$increment"
                || expression.text == "$dimensions"
                || expression.text == "$unpacked_dimensions");
        if (packed_query) {
            // Query the owning value directly so a declared ascending or
            // nonzero packed range is not erased by literal substitution.
            return evaluate_systemverilog_constant_expression(
                expression, environment, fallback_, error);
        }
        if (expression.kind == ExpressionKind::Call) {
            const auto separator = expression.text.find_last_of(".:");
            const auto intrinsic = expression.text.substr(
                separator == std::string::npos ? 0U : separator + 1U);
            const bool sized_numeric = intrinsic == "to_unsigned"
                || intrinsic == "to_signed" || intrinsic == "resize";
            if ((intrinsic == "maximum" || intrinsic == "minimum")
                && expression.operands.size() == 2U) {
                const auto left = evaluate_expression(
                    expression.operands[0], environment, error);
                const auto right = evaluate_expression(
                    expression.operands[1], environment, error);
                const auto left_integer = left
                    ? left->integer_value() : std::nullopt;
                const auto right_integer = right
                    ? right->integer_value() : std::nullopt;
                if (!left_integer || !right_integer) {
                    error = intrinsic
                        + " requires two known integer arguments";
                    return std::nullopt;
                }
                const auto result = intrinsic == "maximum"
                    ? std::max(*left_integer, *right_integer)
                    : std::min(*left_integer, *right_integer);
                return Value{
                    integer_value(result),
                    true,
                    false,
                    frontend::ValueDomain::Integer,
                    {},
                    expression.span};
            }
            if (sized_numeric && expression.operands.size() == 2U) {
                const auto source = evaluate_expression(
                    expression.operands[0], environment, error);
                const auto size_value = evaluate_expression(
                    expression.operands[1], environment, error);
                const auto size = size_value
                    ? size_value->integer_value() : std::nullopt;
                if (!source || !size || *size <= 0
                    || static_cast<std::uint64_t>(*size)
                        > std::numeric_limits<std::uint32_t>::max()) {
                    error = intrinsic
                        + " requires a value and a positive static size";
                    return std::nullopt;
                }
                const auto width = static_cast<std::uint32_t>(*size);
                auto packed = runtime::PackedLogic4(
                    width, runtime::Logic4::zero);
                if (source->packed.is_logic9()) {
                    packed = packed.promoted_to_logic9();
                }
                const bool signed_result = intrinsic == "to_signed"
                    || (intrinsic == "resize" && source->is_signed);
                if (signed_result && source->width != 0U
                    && source->packed.get_logic9(source->width - 1U)
                        == runtime::Logic9::one) {
                    if (packed.is_logic9()) {
                        packed.fill(runtime::Logic9::one);
                    } else {
                        packed.fill(runtime::Logic4::one);
                    }
                }
                for (std::uint32_t bit = 0;
                     bit < std::min(width, source->width); ++bit) {
                    const auto state = source->packed.get_logic9(bit);
                    if (packed.is_logic9()) {
                        packed.set_logic9(bit, state);
                    } else {
                        packed.set(bit, runtime::to_logic4(state));
                    }
                }
                return Value{
                    std::move(packed),
                    signed_result,
                    false,
                    expected_type != nullptr
                        ? expected_type->domain
                        : frontend::ValueDomain::Logic9,
                    expected_type != nullptr
                        ? expected_type->nominal_type : std::string{},
                    expression.span};
            }
            const bool vector_conversion = intrinsic == "std_logic_vector"
                || intrinsic == "std_ulogic_vector"
                || intrinsic == "unsigned" || intrinsic == "signed";
            if (vector_conversion && expression.operands.size() == 1U) {
                auto value = evaluate_expression(
                    expression.operands.front(), environment, error);
                if (!value) {
                    return std::nullopt;
                }
                value->is_signed = intrinsic == "signed";
                value->domain = expected_type != nullptr
                    ? expected_type->domain
                    : frontend::ValueDomain::Logic9;
                value->nominal_type = expected_type != nullptr
                    ? expected_type->nominal_type : std::string{};
                value->source = expression.span;
                return value;
            }
            if (intrinsic == "to_integer"
                && expression.operands.size() == 1U) {
                const auto source = evaluate_expression(
                    expression.operands.front(), environment, error);
                const auto integer = source
                    ? source->integer_value() : std::nullopt;
                if (!integer) {
                    error = "to_integer requires a known bounded vector";
                    return std::nullopt;
                }
                return Value{
                    integer_value(*integer),
                    true,
                    false,
                    frontend::ValueDomain::Integer,
                    {},
                    expression.span};
            }
            std::optional<Value> selected;
            std::size_t matches = 0;
            bool named_function = false;
            std::string candidate_error;
            std::vector<const frontend::FunctionDeclaration*> candidates;
            candidates.reserve(functions_.size() + 8U);
            const auto append_candidate =
                [&](const frontend::FunctionDeclaration& function) {
                  const auto duplicate = std::ranges::any_of(
                      candidates,
                      [&](const auto* existing) {
                        return existing->name == function.name
                            && existing->arguments.size()
                                == function.arguments.size()
                            && existing->span.begin.offset
                                == function.span.begin.offset
                            && existing->span.end.offset
                                == function.span.end.offset
                            && frontend::physical_source(existing->span)
                                == frontend::physical_source(function.span)
                            && existing->specialization_identity
                                == function.specialization_identity;
                      });
                  if (!duplicate) {
                    candidates.push_back(&function);
                  }
                };
            for (const auto& function : functions_) {
                append_candidate(function);
            }
            for (const auto* active : call_stack_) {
                for (const auto& nested : active->functions) {
                    append_candidate(nested);
                }
            }
            for (const auto* candidate : candidates) {
                const auto& function = *candidate;
                if (!callable_name_matches(
                        function.name, expression.text, function.language)) {
                    continue;
                }
                named_function = true;
                const bool contextual_vhdl_array_result =
                    expected_type != nullptr
                    && function.language
                        == frontend::Language::Vhdl2008
                    && unconstrained_vhdl_builtin_array(
                        function.return_type)
                    && expected_type->width().value_or(0U) != 0U;
                const bool contextual_vhdl_result_mismatch =
                    expected_type != nullptr
                    && function.language
                        == frontend::Language::Vhdl2008
                    && !contextual_vhdl_array_result
                    && (function.return_type.domain
                            != expected_type->domain
                        || ((!function.return_type.nominal_type.empty()
                             || !expected_type->nominal_type.empty())
                            && function.return_type.nominal_type
                                != expected_type->nominal_type));
                if (contextual_vhdl_result_mismatch) {
                    continue;
                }
                bool obvious_match =
                    expression.operands.size()
                        <= function.arguments.size()
                    && (expression.call_argument_names.empty()
                        || expression.call_argument_names.size()
                            == expression.operands.size());
                for (std::size_t index = 0;
                     obvious_match
                     && index < expression.operands.size();
                     ++index) {
                    std::size_t formal_index = index;
                    if (!expression.call_argument_names.empty()
                        && !expression.call_argument_names[index].empty()) {
                        const auto formal = std::ranges::find(
                            function.arguments,
                            expression.call_argument_names[index],
                            &frontend::FunctionArgument::name);
                        if (formal == function.arguments.end()) {
                            obvious_match = false;
                            break;
                        }
                        formal_index = static_cast<std::size_t>(
                            std::distance(
                                function.arguments.begin(), formal));
                    }
                    const auto kind = expression.operands[index].kind;
                    const auto domain =
                        function.arguments[formal_index].type.domain;
                    obvious_match = kind == ExpressionKind::IntegerLiteral
                        ? function.language
                                == frontend::Language::Vhdl2008
                            ? domain
                                == frontend::ValueDomain::Integer
                            : domain
                                    == frontend::ValueDomain::Bit2
                                || domain
                                    == frontend::ValueDomain::Logic4
                                || domain
                                    == frontend::ValueDomain::Integer
                        : kind == ExpressionKind::BooleanLiteral
                        ? domain
                            == frontend::ValueDomain::Boolean
                        : kind == ExpressionKind::StringLiteral
                        ? domain
                            == frontend::ValueDomain::String
                        : true;
                }
                if (!obvious_match) {
                    continue;
                }
                std::string current_error;
                auto value = evaluate_call(
                    function,
                    expression,
                    environment,
                    expected_type,
                    current_error);
                if (!value) {
                    if (candidate_error.empty()) {
                        candidate_error = std::move(current_error);
                    }
                    continue;
                }
                ++matches;
                if (!selected) {
                    selected = std::move(*value);
                }
            }
            if (matches == 1) {
                return selected;
            }
            if (matches > 1) {
                error = "constant function call '" + expression.text
                    + "' is ambiguous among visible overloads";
                return std::nullopt;
            }
            if (named_function) {
                error = candidate_error.empty()
                    ? "constant function call '" + expression.text
                        + "' matches no visible overload"
                    : std::move(candidate_error);
                return std::nullopt;
            }
            if (expression.text == "inside") {
                if (expression.operands.size() < 2) {
                    return evaluate_systemverilog_constant_expression(
                        expression, environment, fallback_, error);
                }
                const auto left = evaluate_expression(
                    expression.operands.front(), environment, error);
                if (!left) {
                    return std::nullopt;
                }
                std::optional<Value> unknown;
                std::optional<Value> no_match;
                for (std::size_t index = 1;
                     index < expression.operands.size(); ++index) {
                    auto item = expression.operands[index];
                    if (item.kind == ExpressionKind::Call
                        && item.text == "@inside-range") {
                        for (auto& bound : item.operands) {
                            const auto value = evaluate_expression(
                                bound, environment, error);
                            if (!value) {
                                return std::nullopt;
                            }
                            bound = value->expression(bound.span);
                        }
                    } else {
                        const auto value = evaluate_expression(
                            item, environment, error);
                        if (!value) {
                            return std::nullopt;
                        }
                        item = value->expression(item.span);
                    }
                    Expression single{
                        ExpressionKind::Call,
                        "inside",
                        {left->expression(expression.operands.front().span),
                         std::move(item)},
                        expression.span};
                    const auto matched =
                        evaluate_systemverilog_constant_expression(
                            single, environment, fallback_, error);
                    if (!matched) {
                        return std::nullopt;
                    }
                    const auto truth = matched->truth_value();
                    if (truth && *truth) {
                        return matched;
                    }
                    if (truth) {
                        no_match = matched;
                    } else {
                        unknown = matched;
                    }
                }
                return unknown ? unknown : no_match;
            }
        }
        if (expression.kind == ExpressionKind::Binary
            && expression.operands.size() == 2U
            && (expression.text == "&&" || expression.text == "||")) {
            const auto left = evaluate_expression(
                expression.operands[0], environment, error);
            if (!left) {
                return std::nullopt;
            }
            const auto left_truth = left->truth_value();
            if (expression.text == "&&" && left_truth && !*left_truth) {
                return Value {
                    runtime::PackedLogic4(1, runtime::Logic4::zero),
                    false,
                    false,
                    frontend::ValueDomain::Bit2,
                    { },
                    expression.span
                };
            }
            if (expression.text == "||" && left_truth && *left_truth) {
                return Value {
                    runtime::PackedLogic4(1, runtime::Logic4::one),
                    false,
                    false,
                    frontend::ValueDomain::Bit2,
                    { },
                    expression.span
                };
            }
            const auto right = evaluate_expression(
                expression.operands[1], environment, error);
            if (!right) {
                return std::nullopt;
            }
            Expression folded {
                ExpressionKind::Binary,
                expression.text,
                { left->expression(expression.operands[0].span),
                    right->expression(expression.operands[1].span) },
                expression.span
            };
            return evaluate_systemverilog_constant_expression(
                folded, environment, fallback_, error);
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "?:"
            && expression.operands.size() == 3U) {
            const auto condition = evaluate_expression(
                expression.operands[0], environment, error);
            if (!condition) {
                return std::nullopt;
            }
            const auto condition_truth = condition->truth_value();
            if (condition_truth) {
                const auto selected_index = *condition_truth
                    ? std::size_t { 1 }
                    : std::size_t { 2 };
                const auto selected = evaluate_expression(
                    expression.operands[selected_index],
                    environment,
                    error,
                    expected_type);
                if (!selected) {
                    return std::nullopt;
                }
                auto folded = expression;
                folded.operands[0] = condition->expression(
                    expression.operands[0].span);
                folded.operands[selected_index] = selected->expression(
                    expression.operands[selected_index].span);
                return evaluate_systemverilog_constant_expression(
                    folded, environment, fallback_, error);
            }
        }
        if (expression.kind == ExpressionKind::Binary
            && expression.operands.size() == 2U
            && !call_stack_.empty()
            && call_stack_.back()->language
                == frontend::Language::Vhdl2008
            && (expression.text == "mod"
                || expression.text == "rem")) {
            const auto left = evaluate_expression(
                expression.operands[0], environment, error);
            const auto right = left
                ? evaluate_expression(
                      expression.operands[1], environment, error)
                : std::nullopt;
            const auto dividend = left
                ? left->integer_value() : std::nullopt;
            const auto divisor = right
                ? right->integer_value() : std::nullopt;
            if (dividend && divisor) {
                if (*divisor == 0) {
                    error = "division by zero in VHDL constant expression";
                    return std::nullopt;
                }
                const auto remainder = *divisor == -1
                    ? std::int64_t { 0 }
                    : *dividend % *divisor;
                const auto result = expression.text == "mod"
                        && remainder != 0
                        && ((*dividend < 0) != (*divisor < 0))
                    ? remainder + *divisor
                    : remainder;
                return Value {
                    integer_value(result),
                    true,
                    false,
                    frontend::ValueDomain::Integer,
                    { },
                    expression.span
                };
            }
        }
        auto folded = expression;
        if (folded.kind == ExpressionKind::Binary) {
            if (!call_stack_.empty()
                && call_stack_.back()->language
                    == frontend::Language::Vhdl2008
                && folded.text == "&") {
                folded.kind = ExpressionKind::Concatenation;
                folded.text = "concat";
            } else if (folded.text == "mod" || folded.text == "rem") {
                folded.text = "%";
            } else if (folded.text == "xor") {
                folded.text = "^";
            } else if (folded.text == "and") {
                folded.text = "&";
            } else if (folded.text == "or") {
                folded.text = "|";
            } else if (folded.text == "sll") {
                folded.text = "<<";
            } else if (folded.text == "srl") {
                folded.text = ">>";
            } else if (folded.text == "=") {
                folded.text = "==";
            } else if (folded.text == "/=") {
                folded.text = "!=";
            }
        } else if (folded.kind == ExpressionKind::Unary
                   && folded.text == "not") {
            folded.text = "~";
        }
        for (auto& operand : folded.operands) {
            const auto value =
                evaluate_expression(operand, environment, error);
            if (!value) {
                return std::nullopt;
            }
            operand = value->expression(operand.span);
        }
        for (auto& association :
             folded.aggregate_choice_expressions) {
            for (auto& choice : association) {
                if (choice.kind == ExpressionKind::DefaultChoice) {
                    continue;
                }
                const auto value =
                    evaluate_expression(
                        choice, environment, error);
                if (!value) {
                    return std::nullopt;
                }
                choice = value->expression(choice.span);
            }
        }
        auto value = evaluate_systemverilog_constant_expression(
            folded, environment, fallback_, error);
        if (!value) {
            error += " while evaluating '" + expression.text + "'";
        }
        return value;
    }

    struct ConstantPatternMatch {
        bool matched { };
        std::unordered_map<std::string, Value> bindings;
    };

    std::optional<ConstantPatternMatch> evaluate_case_pattern(
        const Expression& pattern,
        const Value& selector,
        const frontend::Type* type,
        const SystemVerilogConstantEnvironment& environment,
        std::string& error)
    {
        if (pattern.kind == ExpressionKind::Call
            && pattern.text == "@match-wildcard") {
            return ConstantPatternMatch { true, { } };
        }
        constexpr std::string_view bind_prefix { "@match-bind:" };
        if (pattern.kind == ExpressionKind::Call
            && pattern.text.starts_with(bind_prefix)) {
            const auto name = pattern.text.substr(bind_prefix.size());
            if (name.empty()) {
                error = "constant function case pattern binding has no name";
                return std::nullopt;
            }
            return ConstantPatternMatch {
                true, { { name, selector } }
            };
        }
        const auto extract = [&](const std::uint64_t offset,
                                 const std::uint64_t width,
                                 const bool is_signed,
                                 const frontend::ValueDomain domain)
            -> std::optional<Value> {
            if (width == 0U
                || width > std::numeric_limits<std::uint32_t>::max()
                || offset > selector.width
                || width > selector.width - offset) {
                return std::nullopt;
            }
            auto packed = runtime::PackedLogic4(
                static_cast<std::size_t>(width), runtime::Logic4::zero);
            if (selector.packed.is_logic9()) {
                packed = packed.promoted_to_logic9();
            }
            for (std::uint32_t bit = 0;
                bit < static_cast<std::uint32_t>(width);
                ++bit) {
                if (packed.is_logic9()) {
                    packed.set_logic9(
                        bit,
                        selector.packed.get_logic9(
                            static_cast<std::size_t>(offset) + bit));
                } else {
                    packed.set(
                        bit,
                        selector.packed.get(
                            static_cast<std::size_t>(offset) + bit));
                }
            }
            return Value {
                std::move(packed),
                is_signed,
                false,
                domain,
                { },
                pattern.span
            };
        };
        constexpr std::string_view tagged_prefix { "@match-tagged:" };
        if (pattern.kind == ExpressionKind::Call
            && pattern.text.starts_with(tagged_prefix)) {
            if (type == nullptr
                || type->packed_aggregate
                    != frontend::PackedAggregateKind::TaggedUnion
                || type->packed_members.empty()
                || pattern.operands.size() > 1U) {
                error = "constant function tagged pattern requires a "
                        "tagged-union selector";
                return std::nullopt;
            }
            const auto member_name = pattern.text.substr(
                tagged_prefix.size());
            const auto member = std::ranges::find_if(
                type->packed_members,
                [&](const frontend::PackedMember& candidate) {
                    return candidate.name == member_name;
                });
            if (member == type->packed_members.end()) {
                error = "constant function tagged pattern names unknown "
                        "member '"
                    + member_name + "'";
                return std::nullopt;
            }
            const auto member_index = static_cast<std::size_t>(
                std::distance(type->packed_members.begin(), member));
            const auto tag_width = std::max<std::size_t>(
                1U, static_cast<std::size_t>(
                        std::bit_width(type->packed_members.size() - 1U)));
            if (selector.width < tag_width) {
                error = "constant function tagged selector has no tag bits";
                return std::nullopt;
            }
            const auto tag = extract(
                selector.width - tag_width,
                tag_width,
                false,
                frontend::ValueDomain::Bit2);
            const auto tag_value = tag ? tag->integer_value() : std::nullopt;
            if (!tag_value
                || *tag_value != static_cast<std::int64_t>(member_index)) {
                return ConstantPatternMatch { false, { } };
            }
            if (pattern.operands.empty()) {
                return ConstantPatternMatch { true, { } };
            }
            const auto member_width = member->width();
            const auto member_value = member_width
                ? extract(
                      member->lsb_offset,
                      *member_width,
                      member->is_signed,
                      member->domain)
                : std::nullopt;
            if (!member_value) {
                error = "constant function tagged member has no packed layout";
                return std::nullopt;
            }
            return evaluate_case_pattern(
                pattern.operands.front(),
                *member_value,
                member->nested_types.empty()
                    ? nullptr
                    : &member->nested_types.front(),
                environment,
                error);
        }
        if (pattern.kind == ExpressionKind::Aggregate
            && pattern.text == "@match-structure") {
            if (type == nullptr
                || (type->packed_aggregate
                        != frontend::PackedAggregateKind::Struct
                    && type->packed_aggregate
                        != frontend::PackedAggregateKind::Union)
                || pattern.aggregate_choices.size()
                    != pattern.operands.size()
                || pattern.aggregate_choice_expressions.size()
                    != pattern.operands.size()) {
                error = "constant function structured pattern requires "
                        "compatible packed aggregate metadata";
                return std::nullopt;
            }
            ConstantPatternMatch result { true, { } };
            std::vector<bool> selected(type->packed_members.size());
            const auto named = pattern.aggregate_choices.front() == "@key";
            if (std::ranges::any_of(
                    pattern.aggregate_choices,
                    [named](const std::string& choice) {
                        return (choice == "@key") != named;
                    })
                || (!named
                    && pattern.operands.size()
                        != type->packed_members.size())) {
                error = "constant function structured patterns cannot mix "
                        "positional and named members, and positional patterns "
                        "must cover every member";
                return std::nullopt;
            }
            std::size_t positional = 0;
            for (std::size_t index = 0;
                index < pattern.operands.size(); ++index) {
                std::size_t member_index = positional++;
                if (pattern.aggregate_choices[index] == "@key") {
                    const auto& choices = pattern.aggregate_choice_expressions[index];
                    if (choices.size() != 1U
                        || choices.front().kind
                            != ExpressionKind::Identifier) {
                        error = "constant function named structured pattern "
                                "requires a direct member";
                        return std::nullopt;
                    }
                    const auto member = std::ranges::find_if(
                        type->packed_members,
                        [&](const frontend::PackedMember& candidate) {
                            return candidate.name == choices.front().text;
                        });
                    if (member == type->packed_members.end()) {
                        error = "constant function structured pattern names "
                                "unknown member '"
                            + choices.front().text + "'";
                        return std::nullopt;
                    }
                    member_index = static_cast<std::size_t>(
                        std::distance(type->packed_members.begin(), member));
                } else if (!pattern.aggregate_choices[index].empty()) {
                    error = "constant function structured pattern has an "
                            "invalid member key";
                    return std::nullopt;
                }
                if (member_index >= type->packed_members.size()
                    || selected[member_index]) {
                    error = "constant function structured pattern has too "
                            "many or duplicate members";
                    return std::nullopt;
                }
                selected[member_index] = true;
                const auto& member = type->packed_members[member_index];
                const auto member_width = member.width();
                const auto member_value = member_width
                    ? extract(
                          member.lsb_offset,
                          *member_width,
                          member.is_signed,
                          member.domain)
                    : std::nullopt;
                if (!member_value) {
                    error = "constant function structured member has no "
                            "packed layout";
                    return std::nullopt;
                }
                auto nested = evaluate_case_pattern(
                    pattern.operands[index],
                    *member_value,
                    member.nested_types.empty()
                        ? nullptr
                        : &member.nested_types.front(),
                    environment,
                    error);
                if (!nested) {
                    return std::nullopt;
                }
                if (!nested->matched) {
                    return ConstantPatternMatch { false, { } };
                }
                for (auto& [name, bound] : nested->bindings) {
                    if (!result.bindings.emplace(name, std::move(bound)).second) {
                        error = "constant function case pattern binds '"
                            + name + "' more than once";
                        return std::nullopt;
                    }
                }
            }
            return result;
        }
        const auto value = evaluate_expression(
            pattern, environment, error);
        if (!value) {
            return std::nullopt;
        }
        Expression equality {
            ExpressionKind::Binary,
            "===",
            { selector.expression(pattern.span),
                value->expression(pattern.span) },
            pattern.span
        };
        const auto matched = evaluate_systemverilog_constant_expression(
            equality, environment, fallback_, error);
        if (!matched) {
            return std::nullopt;
        }
        return ConstantPatternMatch {
            matched->truth_value().value_or(false), { }
        };
    }

    std::optional<Value> converted(
        const Expression& expression,
        const frontend::Type& type,
        const SystemVerilogConstantEnvironment& environment,
        std::string& error) {
        const auto value =
            evaluate_expression(
                expression, environment, error, &type);
        if (!value) {
            return std::nullopt;
        }
        return convert_systemverilog_parameter_value(
            *value, type, error);
    }

    static std::string result_alias(
        const std::string_view name) {
        const auto separator = name.rfind("::");
        return separator == std::string_view::npos
            ? std::string{name}
            : std::string{name.substr(separator + 2)};
    }

    static bool unconstrained_vhdl_builtin_array(
        const frontend::Type& type) {
        const auto separator = type.spelling.find_last_of('.');
        const auto name = std::string_view{type.spelling}.substr(
            separator == std::string::npos ? 0U : separator + 1U);
        return !type.packed_range && !type.vhdl_array
            && (name == "bit_vector" || name == "std_logic_vector"
                || name == "std_ulogic_vector" || name == "signed"
                || name == "unsigned");
    }

    std::optional<Value> evaluate_call(
        const frontend::FunctionDeclaration& declared_function,
        const Expression& call,
        const SystemVerilogConstantEnvironment& caller,
        const frontend::Type* expected_type,
        std::string& error) {
        std::optional<frontend::FunctionDeclaration> specialized_function;
        if (declared_function.language == frontend::Language::Vhdl2008) {
            specialized_function.emplace(declared_function);
        }
        const auto& function = specialized_function
            ? *specialized_function : declared_function;
        if (!function.automatic
            && function.language != frontend::Language::Verilog2005) {
            error = "static or implicit-lifetime function '"
                + function.name + "' is not a constant function";
            return std::nullopt;
        }
        std::vector<const Expression*> actuals(
            function.arguments.size());
        const bool have_names = !call.call_argument_names.empty();
        if (have_names
            && call.call_argument_names.size()
                != call.operands.size()) {
            error = "constant function association metadata is inconsistent";
            return std::nullopt;
        }
        bool named_seen = false;
        std::size_t positional = 0;
        for (std::size_t index = 0;
             index < call.operands.size(); ++index) {
            const auto& name = have_names
                ? call.call_argument_names[index]
                : std::string{};
            std::size_t formal_index = 0;
            if (name.empty()) {
                if (named_seen || positional >= actuals.size()) {
                    error = "invalid positional constant function argument";
                    return std::nullopt;
                }
                formal_index = positional++;
            } else {
                named_seen = true;
                const auto found = std::ranges::find(
                    function.arguments,
                    name,
                    &frontend::FunctionArgument::name);
                if (found == function.arguments.end()) {
                    error = "unknown named constant function argument '"
                        + name + "'";
                    return std::nullopt;
                }
                formal_index = static_cast<std::size_t>(
                    std::distance(function.arguments.begin(), found));
            }
            if (actuals[formal_index] != nullptr) {
                error = "duplicate constant function argument association";
                return std::nullopt;
            }
            if (call.operands[index].valid()) {
                actuals[formal_index] = &call.operands[index];
            }
        }
        for (std::size_t index = 0;
             index < function.arguments.size(); ++index) {
            const auto& formal = function.arguments[index];
            if (formal.direction != frontend::PortDirection::Input
                || formal.reference) {
                error = "constant functions require input value arguments";
                return std::nullopt;
            }
            if (actuals[index] == nullptr && formal.default_value) {
                actuals[index] = &*formal.default_value;
            }
            if (actuals[index] == nullptr) {
                error = "constant function argument '" + formal.name
                    + "' has no actual or default value";
                return std::nullopt;
            }
        }
        if (function.language == frontend::Language::Vhdl2008) {
            auto& mutable_function = *specialized_function;
            const auto simple_type_name = [](const std::string_view spelling) {
              const auto separator = spelling.find_last_of('.');
              return spelling.substr(
                  separator == std::string_view::npos
                      ? 0U : separator + 1U);
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
            ConstantEnvironment formal_environment;
            ConstantDomainEnvironment formal_domains;
            for (std::size_t index = 0;
                 index < mutable_function.arguments.size(); ++index) {
                auto& formal = mutable_function.arguments[index];
                const auto actual = evaluate_expression(
                    *actuals[index], caller, error);
                if (!actual) {
                    error = "constant function argument '" + formal.name
                        + "': " + error;
                    return std::nullopt;
                }
                if (unconstrained_builtin_array(formal.type)) {
                    formal.type.packed_range = frontend::PackedRange{
                        static_cast<std::int64_t>(actual->width - 1U),
                        0,
                        true};
                    formal.type.domain = actual->domain;
                    formal.type.is_signed = actual->is_signed;
                }
                if (formal.type.domain
                    == frontend::ValueDomain::Integer) {
                    if (const auto integer = actual->integer_value()) {
                        formal_environment.insert_or_assign(
                            formal.name, *integer);
                        formal_domains.insert_or_assign(
                            formal.name,
                            ConstantTypeInfo{
                                frontend::ValueDomain::Integer,
                                false,
                                {}});
                    }
                }
            }
            if (unconstrained_builtin_array(mutable_function.return_type)
                && expected_type != nullptr
                && expected_type->width().value_or(0U) != 0U) {
                mutable_function.return_type = *expected_type;
            }
            std::vector<Diagnostic> ignored_diagnostics;
            for (auto& constant : mutable_function.constants) {
                substitute_parameters(
                    constant.type,
                    formal_environment,
                    formal_domains,
                    ignored_diagnostics,
                    frontend::Language::Vhdl2008);
                substitute_parameters(
                    constant.default_value,
                    formal_environment,
                    formal_domains,
                    frontend::Language::Vhdl2008);
                std::string ignored;
                if (const auto value = evaluate_constant_expression(
                        constant.default_value,
                        formal_environment,
                        ignored)) {
                    formal_environment.insert_or_assign(
                        constant.name, *value);
                    formal_domains.insert_or_assign(
                        constant.name,
                        ConstantTypeInfo{
                            constant.type.domain,
                            false,
                            constant.type.nominal_type});
                }
            }
            for (auto& alias : mutable_function.type_aliases) {
                substitute_parameters(
                    alias.type,
                    formal_environment,
                    formal_domains,
                    ignored_diagnostics,
                    frontend::Language::Vhdl2008);
            }
            for (auto& variable : mutable_function.variables) {
                substitute_parameters(
                    variable,
                    formal_environment,
                    formal_domains,
                    ignored_diagnostics,
                    frontend::Language::Vhdl2008);
            }
            substitute_parameters(
                mutable_function.return_type,
                formal_environment,
                formal_domains,
                ignored_diagnostics,
                frontend::Language::Vhdl2008);
            substitute_parameters(
                mutable_function.statements,
                formal_environment,
                formal_domains,
                ignored_diagnostics,
                frontend::Language::Vhdl2008);
        }
        std::vector<Value> converted_arguments;
        converted_arguments.reserve(function.arguments.size());
        for (std::size_t index = 0;
             index < function.arguments.size(); ++index) {
            auto value = converted(
                *actuals[index],
                function.arguments[index].type,
                caller,
                error);
            if (!value) {
                error =
                    "constant function argument '"
                    + function.arguments[index].name + "': "
                    + error;
                return std::nullopt;
            }
            converted_arguments.push_back(std::move(*value));
        }
        if (std::ranges::any_of(
                call_stack_, [&](const auto* active) {
                    return active->name == function.name
                        && active->arguments.size()
                            == function.arguments.size();
                })) {
            error =
                "recursive constant function call involving '"
                + function.name + "'";
            return std::nullopt;
        }

        std::optional<std::string> cache_key{std::in_place};
        const auto append_cache_component =
            [&](const std::string_view component) {
              append_key_component(*cache_key, component);
            };
        auto behavior = callable_behaviors_.find(&declared_function);
        if (behavior == callable_behaviors_.end()) {
            behavior = callable_behaviors_.emplace(
                &declared_function,
                callable_behavior_identity(declared_function)).first;
        }
        append_cache_component(behavior->second.digest);
        for (const auto& identifier : behavior->second.identifiers) {
            if (const auto found = caller.find(identifier);
                found != caller.end()) {
                append_cache_component(identifier);
                append_cache_component(found->second.canonical());
                continue;
            }
            if (const auto found = globals_.find(identifier);
                found != globals_.end()) {
                append_cache_component(identifier);
                append_cache_component(found->second.canonical());
                continue;
            }
            if (const auto found = fallback_.find(identifier);
                found != fallback_.end()) {
                append_cache_component(identifier);
                append_cache_component(std::to_string(found->second));
            }
        }
        append_cache_component(
            std::to_string(static_cast<unsigned>(
                declared_function.language)));
        append_cache_component(declared_function.name);
        append_cache_component(
            frontend::physical_source(declared_function.span));
        append_cache_component(std::to_string(
            declared_function.span.begin.offset));
        append_cache_component(std::to_string(
            declared_function.span.end.offset));
        append_cache_component(std::to_string(
            declared_function.arguments.size()));
        if (function.language == frontend::Language::Vhdl2008) {
            append_cache_component("vhdl-call-v1");
        }
        append_cache_component(
            vhdl_type_identity(function.return_type));
        for (const auto& argument : function.arguments) {
            append_cache_component(
                vhdl_type_identity(argument.type));
        }
        for (const auto& value : converted_arguments) {
            append_cache_component(value.canonical());
        }
        const auto cached = call_cache_.find(*cache_key);
        if (cached != call_cache_.end()) {
            auto result = cached->second;
            result.source = call.span;
            return result;
        }

        auto environment = globals_;
        for (std::size_t index = 0;
             index < function.arguments.size(); ++index) {
            environment.insert_or_assign(
                function.arguments[index].name,
                std::move(converted_arguments[index]));
        }
        call_stack_.push_back(&function);
        struct Pop {
            std::vector<const frontend::FunctionDeclaration*>& stack;
            ~Pop() { stack.pop_back(); }
        } pop{call_stack_};

        std::unordered_map<std::string, const frontend::Type*> types;
        types.emplace(function.name, &function.return_type);
        types.emplace(
            result_alias(function.name), &function.return_type);
        for (const auto& argument : function.arguments) {
            types.emplace(argument.name, &argument.type);
        }
        const auto collect_declarations =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> void {
              for (const auto& statement : statements) {
                  for (const auto& declaration :
                       statement.declarations) {
                      types.insert_or_assign(
                          declaration.name, &declaration.type);
                  }
                  self(self, statement.statements);
                  self(self, statement.else_statements);
                  for (const auto& alternative :
                       statement.case_alternatives) {
                      self(self, alternative.statements);
                  }
              }
            };
        for (const auto& variable : function.variables) {
            types.insert_or_assign(
                variable.name, &variable.type);
        }
        for (const auto& constant : function.constants) {
            types.insert_or_assign(
                constant.name, &constant.type);
        }
        collect_declarations(
            collect_declarations, function.statements);
        type_scopes_.push_back(&types);
        struct PopTypes {
            std::vector<const std::unordered_map<
                std::string, const frontend::Type*>*>& stack;
            ~PopTypes() { stack.pop_back(); }
        } pop_types { type_scopes_ };

        const auto initialize =
            [&](const frontend::VariableDeclaration& variable)
                -> bool {
              const auto layout = fixed_array_layout(
                  variable.type, environment);
              const auto declared_width = variable.type.width();
              const auto width = layout
                  ? static_cast<std::uint64_t>(layout->total_width)
                  : static_cast<std::uint64_t>(
                        declared_width.value_or(32));
              if (width == 0
                  || width > std::numeric_limits<std::uint32_t>::max()) {
                  error = "constant function variable '" + variable.name
                      + "' has an invalid zero or oversized packed width";
                  return false;
              }
              Value value{
                  0,
                  0,
                  0,
                  static_cast<std::uint32_t>(width),
                  layout ? false : variable.type.is_signed,
                  false,
                  variable.span};
              if (variable.initializer) {
                  bool initialized = false;
                  if (layout
                      && variable.initializer->kind
                          == ExpressionKind::Aggregate
                      && variable.initializer->operands.size() == 1U
                      && variable.initializer->aggregate_choices.size()
                          == 1U
                      && variable.initializer->aggregate_choices.front()
                          == "others") {
                      const auto element = converted(
                          variable.initializer->operands.front(),
                          *layout->element_type,
                          environment,
                          error);
                      if (!element
                          || element->width != layout->element_width) {
                          return false;
                      }
                      for (std::uint32_t offset = 0;
                           offset < layout->total_width;
                           offset += layout->element_width) {
                          for (std::uint32_t bit = 0;
                               bit < layout->element_width; ++bit) {
                              const auto state =
                                  element->packed.get_logic9(bit);
                              if (value.packed.is_logic9()) {
                                  value.packed.set_logic9(
                                      offset + bit, state);
                              } else {
                                  value.packed.set(
                                      offset + bit,
                                      runtime::to_logic4(state));
                              }
                          }
                      }
                      value.refresh_low_word_mirrors();
                      initialized = true;
                  }
                  if (!initialized) {
                      const auto initial = converted(
                          *variable.initializer,
                          variable.type,
                          environment,
                          error);
                      if (!initial) {
                          return false;
                      }
                      value = *initial;
                  }
              }
              environment.insert_or_assign(
                  variable.name, std::move(value));
              return true;
            };
        for (const auto& constant : function.constants) {
            frontend::VariableDeclaration local_constant;
            local_constant.name = constant.name;
            local_constant.type = constant.type;
            local_constant.initializer = constant.default_value;
            local_constant.span = constant.span;
            if (!initialize(local_constant)) {
                error = "constant function local constant '"
                    + constant.name + "': " + error;
                return std::nullopt;
            }
        }
        for (const auto& variable : function.variables) {
            if (!initialize(variable)) {
                return std::nullopt;
            }
        }
        const auto initialize_nested =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> bool {
              for (const auto& statement : statements) {
                  for (const auto& declaration :
                       statement.declarations) {
                      if (!initialize(declaration)) {
                          return false;
                      }
                  }
                  if (!self(self, statement.statements)
                      || !self(self, statement.else_statements)) {
                      return false;
                  }
                  for (const auto& alternative :
                       statement.case_alternatives) {
                      if (!self(self, alternative.statements)) {
                          return false;
                      }
                  }
              }
              return true;
            };
        if (!initialize_nested(
                initialize_nested, function.statements)) {
            return std::nullopt;
        }

        std::optional<Value> result;
        const auto flow = execute_statements(
            function.statements,
            environment,
            types,
            result,
            error);
        if (flow == Flow::failed) {
            error = "constant function '" + function.name
                + "': " + error;
            return std::nullopt;
        }
        if (!result) {
            const auto named = environment.find(function.name);
            const auto alias =
                environment.find(result_alias(function.name));
            if (named != environment.end()) {
                result = named->second;
            } else if (alias != environment.end()) {
                result = alias->second;
            }
        }
        if (!result) {
            error =
                "constant function '" + function.name
                + "' did not assign a result";
            return std::nullopt;
        }
        if (function.language == frontend::Language::Vhdl2008
            && unconstrained_vhdl_builtin_array(
                function.return_type)) {
            call_cache_.try_emplace(
                std::move(*cache_key), *result);
            return result;
        }
        auto converted_result = convert_systemverilog_parameter_value(
            *result, function.return_type, error);
        if (converted_result && cache_key) {
            call_cache_.try_emplace(
                std::move(*cache_key), *converted_result);
        }
        return converted_result;
    }

    Flow execute_statements(
        const std::vector<Statement>& statements,
        SystemVerilogConstantEnvironment& environment,
        const std::unordered_map<
            std::string, const frontend::Type*>& types,
        std::optional<Value>& result,
        std::string& error) {
        for (const auto& statement : statements) {
            const auto flow = execute_statement(
                statement, environment, types, result, error);
            if (flow != Flow::normal) {
                if (flow == Flow::failed) {
                    error = "line "
                        + std::to_string(statement.span.begin.line)
                        + ": " + error;
                }
                return flow;
            }
        }
        return Flow::normal;
    }

    Flow execute_statement(
        const Statement& statement,
        SystemVerilogConstantEnvironment& environment,
        const std::unordered_map<
            std::string, const frontend::Type*>& types,
        std::optional<Value>& result,
        std::string& error) {
        if (statement.kind == StatementKind::Assignment) {
            const Expression* root = &statement.target;
            while ((root->kind == ExpressionKind::Index
                    || root->kind == ExpressionKind::Slice)
                   && !root->operands.empty()) {
                root = &root->operands.front();
            }
            if (root->kind != ExpressionKind::Identifier) {
                error =
                    "constant function assignment target is not a local "
                    "packed variable or selection";
                return Flow::failed;
            }
            const auto type = types.find(root->text);
            if (type == types.end()) {
                error =
                    "constant function assignment target '"
                    + root->text + "' is not local";
                return Flow::failed;
            }
            std::optional<Value> value;
            if (statement.target.kind == ExpressionKind::Identifier) {
                value = converted(
                    statement.value, *type->second, environment, error);
            } else if ((statement.target.kind == ExpressionKind::Index
                        && statement.target.operands.size() == 2U)
                       || (statement.target.kind == ExpressionKind::Slice
                           && statement.target.operands.size() == 3U)) {
                const auto base = environment.find(root->text);
                const auto layout = type != types.end()
                    ? fixed_array_layout(*type->second, environment)
                    : std::nullopt;
                std::optional<std::uint32_t> array_offset;
                if (layout
                    && statement.target.kind == ExpressionKind::Index) {
                    const auto index = evaluate_expression(
                        statement.target.operands[1], environment, error);
                    const auto integer = index
                        ? index->integer_value() : std::nullopt;
                    array_offset = integer
                        ? fixed_array_offset(*layout, *integer)
                        : std::nullopt;
                    if (!array_offset) {
                        error = "constant function fixed-array assignment "
                                "index is outside the declared range";
                        return Flow::failed;
                    }
                }
                const auto selected = layout
                    ? std::optional<Value> { Value {
                          0,
                          0,
                          0,
                          layout->element_width,
                          layout->element_type->is_signed,
                          false,
                          statement.target.span } }
                    : base != environment.end()
                    ? evaluate_expression(
                          statement.target, environment, error)
                    : std::nullopt;
                if (base == environment.end()) {
                    error =
                        "constant function selected assignment target '"
                        + root->text + "' has no current value";
                }
                if (!selected) {
                    return Flow::failed;
                }
                frontend::Type selected_type{
                    base->second.domain,
                    base->second.domain == frontend::ValueDomain::Bit2
                        ? "bit" : "logic",
                    frontend::PackedRange{
                        static_cast<std::int64_t>(selected->width - 1U),
                        0,
                        true},
                    false};
                const auto replacement = converted(
                    statement.value,
                    layout ? *layout->element_type : selected_type,
                    environment,
                    error);
                if (!replacement) {
                    return Flow::failed;
                }
                std::vector<std::uint32_t> offsets;
                offsets.reserve(selected->width);
                if (layout) {
                    for (std::uint32_t bit = 0;
                         bit < layout->element_width; ++bit) {
                        offsets.push_back(*array_offset + bit);
                    }
                } else if (statement.target.kind == ExpressionKind::Index) {
                    const auto index = evaluate_expression(
                        statement.target.operands[1], environment, error);
                    const auto position =
                        index ? index->integer_value() : std::nullopt;
                    if (!position || *position < 0
                        || static_cast<std::uint64_t>(*position)
                            >= base->second.width) {
                        error =
                            "constant function bit-select assignment is "
                            "outside the packed target";
                        return Flow::failed;
                    }
                    offsets.push_back(
                        static_cast<std::uint32_t>(*position));
                } else {
                    const auto first = evaluate_expression(
                        statement.target.operands[1], environment, error);
                    const auto second = first
                        ? evaluate_expression(
                              statement.target.operands[2],
                              environment, error)
                        : std::nullopt;
                    const auto first_index =
                        first ? first->integer_value() : std::nullopt;
                    const auto second_index =
                        second ? second->integer_value() : std::nullopt;
                    if (!first_index || !second_index) {
                        error =
                            "constant function part-select assignment "
                            "bounds must be known integers";
                        return Flow::failed;
                    }
                    for (std::uint32_t bit = 0;
                         bit < selected->width;
                         ++bit) {
                        bool add = true;
                        auto anchor = *second_index;
                        auto offset = bit;
                        if (statement.target.text == "+:") {
                            anchor = *first_index;
                        } else if (statement.target.text == "-:") {
                            anchor = *first_index;
                            offset = selected->width - bit - 1U;
                            add = false;
                        } else if (*first_index < *second_index) {
                            add = false;
                        }
                        if (anchor < 0) {
                            error =
                                "constant function part-select assignment "
                                "is outside the packed target";
                            return Flow::failed;
                        }
                        const auto unsigned_anchor =
                            static_cast<std::uint64_t>(anchor);
                        if ((!add && unsigned_anchor < offset)
                            || (add && unsigned_anchor
                                > std::numeric_limits<std::uint64_t>::max()
                                    - offset)) {
                            error =
                                "constant function part-select assignment "
                                "is outside the packed target";
                            return Flow::failed;
                        }
                        const auto source = add
                            ? unsigned_anchor + offset
                            : unsigned_anchor - offset;
                        if (source >= base->second.width) {
                            error =
                                "constant function part-select assignment "
                                "is outside the packed target";
                            return Flow::failed;
                        }
                        offsets.push_back(
                            static_cast<std::uint32_t>(source));
                    }
                }
                auto& updated = base->second;
                for (std::uint32_t bit = 0;
                     bit < offsets.size();
                     ++bit) {
                    const auto state = replacement->packed.get_logic9(bit);
                    if (updated.packed.is_logic9()) {
                        updated.packed.set_logic9(offsets[bit], state);
                    } else {
                        updated.packed.set(
                            offsets[bit], runtime::to_logic4(state));
                    }
                }
                updated.refresh_low_word_mirrors();
                updated.source = statement.target.span;
                if (root->text == call_stack_.back()->name
                    || root->text
                        == result_alias(call_stack_.back()->name)) {
                    result = updated;
                }
                return Flow::normal;
            } else {
                error =
                    "constant functions support one packed bit/part-select "
                    "assignment level";
                return Flow::failed;
            }
            if (!value) {
                return Flow::failed;
            }
            environment.insert_or_assign(
                root->text, *value);
            if (root->text == call_stack_.back()->name
                || root->text == result_alias(call_stack_.back()->name)) {
                result = *value;
            }
            return Flow::normal;
        }
        if (statement.kind == StatementKind::Return) {
            const auto& return_type = call_stack_.back()->return_type;
            const auto value = call_stack_.back()->language
                        == frontend::Language::Vhdl2008
                    && unconstrained_vhdl_builtin_array(return_type)
                ? evaluate_expression(
                      statement.value, environment, error)
                : converted(
                      statement.value,
                      return_type,
                      environment,
                      error);
            if (!value) {
                return Flow::failed;
            }
            result = *value;
            return Flow::returned;
        }
        if (statement.kind == StatementKind::Block) {
            return execute_statements(
                statement.statements,
                environment,
                types,
                result,
                error);
        }
        if (statement.kind == StatementKind::If) {
            const auto condition = evaluate_expression(
                statement.condition, environment, error);
            if (!condition) {
                return Flow::failed;
            }
            const auto truth = condition->truth_value();
            if (!truth) {
                error =
                    "constant function condition contains X or Z";
                return Flow::failed;
            }
            return execute_statements(
                *truth
                    ? statement.statements
                    : statement.else_statements,
                environment,
                types,
                result,
                error);
        }
        if (statement.kind == StatementKind::Case) {
            const auto selector = evaluate_expression(
                statement.condition, environment, error);
            if (!selector) {
                return Flow::failed;
            }
            const frontend::Type* selector_type = nullptr;
            if (statement.condition.kind == ExpressionKind::Identifier) {
                const auto found = types.find(statement.condition.text);
                if (found != types.end()) {
                    selector_type = found->second;
                }
            }
            const frontend::CaseAlternative* selected = nullptr;
            const frontend::CaseAlternative* fallback = nullptr;
            struct ShadowedBinding {
                std::string name;
                std::optional<Value> previous;
            };
            std::vector<ShadowedBinding> selected_bindings;
            for (const auto& alternative :
                 statement.case_alternatives) {
                if (alternative.is_default) {
                    fallback = &alternative;
                    continue;
                }
                if (statement.case_match_kind
                    == frontend::CaseMatchKind::Matches) {
                    if (alternative.choices.size() != 1) {
                        error =
                            "constant function case matches item requires "
                            "exactly one pattern";
                        return Flow::failed;
                    }
                    const auto& choice = alternative.choices.front();
                    const Expression* pattern = &choice;
                    const Expression* guard = nullptr;
                    if (choice.kind == ExpressionKind::Call
                        && choice.text == "@match-guard") {
                        if (choice.operands.size() != 2U) {
                            error = "constant function guarded case matches "
                                    "requires one pattern and one guard";
                            return Flow::failed;
                        }
                        pattern = &choice.operands[0];
                        guard = &choice.operands[1];
                    }
                    const auto matched = evaluate_case_pattern(
                        *pattern,
                        *selector,
                        selector_type,
                        environment,
                        error);
                    if (!matched) {
                        return Flow::failed;
                    }
                    if (!matched->matched) {
                        continue;
                    }
                    std::vector<ShadowedBinding> bindings;
                    for (const auto& [name, value] : matched->bindings) {
                        const auto previous = environment.find(name);
                        bindings.push_back(ShadowedBinding {
                            name,
                            previous != environment.end()
                                ? std::optional<Value> { previous->second }
                                : std::nullopt });
                        environment.insert_or_assign(name, value);
                    }
                    bool pattern_matched = true;
                    if (guard != nullptr) {
                        const auto guarded = evaluate_expression(
                            *guard, environment, error);
                        if (!guarded) {
                            return Flow::failed;
                        }
                        pattern_matched = guarded->truth_value().value_or(false);
                    }
                    if (pattern_matched) {
                        selected = &alternative;
                        selected_bindings = std::move(bindings);
                        break;
                    }
                    for (const auto& binding : bindings) {
                        if (binding.previous) {
                            environment.insert_or_assign(
                                binding.name, *binding.previous);
                        } else {
                            environment.erase(binding.name);
                        }
                    }
                    continue;
                }
                if (statement.case_match_kind
                    == frontend::CaseMatchKind::Inside) {
                    Expression membership{
                        ExpressionKind::Call,
                        "inside",
                        {selector->expression(statement.condition.span)},
                        alternative.span};
                    membership.operands.insert(
                        membership.operands.end(),
                        alternative.choices.begin(),
                        alternative.choices.end());
                    const auto matched = evaluate_expression(
                        membership, environment, error);
                    if (!matched) {
                        return Flow::failed;
                    }
                    if (matched->truth_value().value_or(false)) {
                        selected = &alternative;
                        break;
                    }
                    continue;
                }
                for (const auto& choice : alternative.choices) {
                    const auto value = evaluate_expression(
                        choice, environment, error);
                    if (!value) {
                        return Flow::failed;
                    }
                    if (value->width == selector->width
                        && value->packed == selector->packed) {
                        selected = &alternative;
                        break;
                    }
                }
                if (selected != nullptr) {
                    break;
                }
            }
            selected = selected != nullptr ? selected : fallback;
            if (selected == nullptr) {
                return Flow::normal;
            }
            const auto flow = execute_statements(
                selected->statements,
                environment,
                types,
                result,
                error);
            for (const auto& binding : selected_bindings) {
                if (binding.previous) {
                    environment.insert_or_assign(
                        binding.name, *binding.previous);
                } else {
                    environment.erase(binding.name);
                }
            }
            return flow;
        }
        if (statement.kind == StatementKind::Loop) {
            constexpr std::size_t maximum_iterations = 1'000'000;
            std::size_t iteration = 0;
            if (statement.loop_runtime) {
                if (statement.target.valid()
                    && statement.loop_initial.valid()) {
                    Statement initializer;
                    initializer.kind = StatementKind::Assignment;
                    initializer.target = statement.target;
                    initializer.value = statement.loop_initial;
                    const auto flow = execute_statement(
                        initializer,
                        environment,
                        types,
                        result,
                        error);
                    if (flow != Flow::normal) {
                        return flow;
                    }
                }
                for (;;) {
                    if (!statement.loop_post_test) {
                        const auto condition = evaluate_expression(
                            statement.condition,
                            environment,
                            error);
                        if (!condition) {
                            return Flow::failed;
                        }
                        const auto truth = condition->truth_value();
                        if (!truth) {
                            error =
                                "constant function loop condition "
                                "contains X or Z";
                            return Flow::failed;
                        }
                        if (!*truth) {
                            return Flow::normal;
                        }
                    }
                    if (iteration++ == maximum_iterations) {
                        error =
                            "constant function loop exceeds 1,000,000 "
                            "iterations";
                        return Flow::failed;
                    }
                    const auto flow = execute_statements(
                        statement.statements,
                        environment,
                        types,
                        result,
                        error);
                    if (flow == Flow::returned
                        || flow == Flow::failed) {
                        return flow;
                    }
                    if (flow == Flow::broken) {
                        return Flow::normal;
                    }
                    if (statement.loop_update_target.valid()) {
                        Statement update;
                        update.kind = StatementKind::Assignment;
                        update.target = statement.loop_update_target;
                        update.value = statement.value;
                        const auto update_flow = execute_statement(
                            update,
                            environment,
                            types,
                            result,
                            error);
                        if (update_flow != Flow::normal) {
                            return update_flow;
                        }
                    }
                    if (statement.loop_post_test) {
                        const auto condition = evaluate_expression(
                            statement.condition,
                            environment,
                            error);
                        if (!condition) {
                            return Flow::failed;
                        }
                        const auto truth = condition->truth_value();
                        if (!truth || !*truth) {
                            return truth
                                ? Flow::normal
                                : Flow::failed;
                        }
                    }
                }
            }
            const auto initial = evaluate_expression(
                statement.loop_initial, environment, error);
            const auto limit = initial
                ? evaluate_expression(
                      statement.loop_limit, environment, error)
                : std::nullopt;
            if (!initial || !limit
                || !initial->integer_value()
                || !limit->integer_value()) {
                if (error.empty()) {
                    error =
                        "constant function loop bounds must be known "
                        "integers";
                }
                return Flow::failed;
            }
            auto value = *initial->integer_value();
            const auto final = *limit->integer_value();
            const auto in_range = [&]() {
                return statement.loop_descending
                    ? (statement.loop_limit_exclusive
                           ? value > final : value >= final)
                    : (statement.loop_limit_exclusive
                           ? value < final : value <= final);
            };
            while (in_range()) {
                if (iteration++ == maximum_iterations) {
                    error =
                        "constant function loop exceeds 1,000,000 "
                        "iterations";
                    return Flow::failed;
                }
                if (!statement.loop_variable.empty()) {
                    environment.insert_or_assign(
                        statement.loop_variable,
                        Value{
                            static_cast<std::uint64_t>(value),
                            0,
                            0,
                            32,
                            true,
                            false,
                            statement.span});
                }
                const auto flow = execute_statements(
                    statement.statements,
                    environment,
                    types,
                    result,
                    error);
                if (flow == Flow::returned
                    || flow == Flow::failed) {
                    return flow;
                }
                if (flow == Flow::broken) {
                    return Flow::normal;
                }
                value += statement.loop_descending ? -1 : 1;
            }
            return Flow::normal;
        }
        if (statement.kind == StatementKind::Break) {
            return Flow::broken;
        }
        if (statement.kind == StatementKind::Continue) {
            return Flow::continued;
        }
        if (statement.kind == StatementKind::Null) {
            return Flow::normal;
        }
        error =
            "statement is not permitted in a constant function";
        return Flow::failed;
    }

    const std::vector<frontend::FunctionDeclaration>& functions_;
    const SystemVerilogConstantEnvironment& globals_;
    const ConstantEnvironment& fallback_;
    CallCache owned_call_cache_;
    CallCache& call_cache_;
    std::unordered_map<
        const frontend::FunctionDeclaration*,
        CallableBehaviorIdentity> callable_behaviors_;
    std::vector<const frontend::FunctionDeclaration*> call_stack_;
    std::vector<const std::unordered_map<
        std::string, const frontend::Type*>*> type_scopes_;
};

void fold_expression(
    Expression& expression,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback) {
    for (auto& operand : expression.operands) {
        fold_expression(
            operand, functions, environment, fallback);
    }
    for (auto& association :
         expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            fold_expression(
                choice, functions, environment, fallback);
        }
    }
    if (expression.kind != ExpressionKind::Call
        || std::ranges::none_of(
            functions,
            [&](const auto& function) {
                return (function.language == frontend::Language::Vhdl2008
                            ? function.name == expression.text
                            : callable_name_matches(
                                  function.name,
                                  expression.text,
                                  function.language))
                    && !function.return_type.systemverilog_container;
            })) {
        return;
    }
    std::string error;
    ConstantFunctionEvaluator evaluator{
        functions, environment, fallback};
    if (const auto value = evaluator.evaluate(expression, error)) {
        expression = value->expression(expression.span);
    }
}

void fold_type(
    frontend::Type& type,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback) {
    const auto fold_range = [&](auto& range) {
        if (range) {
            fold_expression(
                range->left, functions, environment, fallback);
            fold_expression(
                range->right, functions, environment, fallback);
        }
    };
    fold_range(type.packed_range_expression);
    fold_range(type.integer_range_expression);
    fold_range(type.integer_base_range_expression);
    fold_range(type.discrete_range_expression);
    fold_range(type.enumeration_range_expression);
    fold_range(type.enumeration_base_range_expression);
    for (auto& member : type.packed_members) {
        if (!member.nested_types.empty()) {
            fold_type(
                member.nested_types.front(),
                functions,
                environment,
                fallback);
        }
        fold_range(member.packed_range_expression);
    }
}

void fold_statements(
    std::vector<Statement>& statements,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback) {
    for (auto& statement : statements) {
        fold_expression(
            statement.target, functions, environment, fallback);
        fold_expression(
            statement.value, functions, environment, fallback);
        fold_expression(
            statement.condition, functions, environment, fallback);
        for (auto& argument : statement.task_arguments) {
            fold_expression(
                argument, functions, environment, fallback);
        }
        for (auto& association :
             statement.procedure_arguments) {
            fold_expression(
                association.value,
                functions,
                environment,
                fallback);
        }
        fold_expression(
            statement.loop_initial, functions, environment, fallback);
        fold_expression(
            statement.loop_limit, functions, environment, fallback);
        for (auto& declaration : statement.declarations) {
            fold_type(
                declaration.type,
                functions,
                environment,
                fallback);
            if (declaration.initializer) {
                fold_expression(
                    *declaration.initializer,
                    functions,
                    environment,
                    fallback);
            }
        }
        for (auto& alternative :
             statement.case_alternatives) {
            for (auto& choice : alternative.choices) {
                fold_expression(
                    choice, functions, environment, fallback);
            }
            fold_statements(
                alternative.statements,
                functions,
                environment,
                fallback);
        }
        fold_statements(
            statement.statements,
            functions,
            environment,
            fallback);
        fold_statements(
            statement.else_statements,
            functions,
            environment,
            fallback);
    }
}

void fold_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback);

void fold_generate_body(
    frontend::GenerateBody& body,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback) {
    std::optional<std::vector<frontend::FunctionDeclaration>>
        owned_visible_functions;
    const auto* visible_functions = &functions;
    std::set<std::string> local_function_names;
    for (const auto& function : body.functions) {
        if (function.language != frontend::Language::Vhdl2008) {
            local_function_names.insert(function.name);
        }
    }
    if (!local_function_names.empty()) {
        owned_visible_functions = functions;
        std::erase_if(
            *owned_visible_functions,
            [&](const auto& function) {
                return std::ranges::any_of(
                    local_function_names,
                    [&](const auto& local_name) {
                        return callable_name_matches(
                            function.name, local_name, function.language);
                    });
            });
        for (const auto& function : body.functions) {
            if (function.language != frontend::Language::Vhdl2008) {
                owned_visible_functions->push_back(function);
            }
        }
        visible_functions = &*owned_visible_functions;
    }
    for (auto& alias : body.type_aliases) {
        fold_type(alias.type, *visible_functions, environment, fallback);
    }
    for (auto& constant : body.constants) {
        fold_type(
            constant.type, *visible_functions, environment, fallback);
        fold_expression(
            constant.default_value,
            *visible_functions,
            environment,
            fallback);
    }
    for (auto& signal : body.signals) {
        fold_type(
            signal.type, *visible_functions, environment, fallback);
    }
    for (auto& function : body.functions) {
        fold_type(
            function.return_type,
            *visible_functions,
            environment,
            fallback);
        for (auto& argument : function.arguments) {
            fold_type(
                argument.type, *visible_functions, environment, fallback);
            if (argument.default_value) {
                fold_expression(
                    *argument.default_value,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        for (auto& variable : function.variables) {
            fold_type(
                variable.type, *visible_functions, environment, fallback);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        fold_statements(
            function.statements,
            *visible_functions,
            environment,
            fallback);
    }
    for (auto& task : body.tasks) {
        for (auto& argument : task.arguments) {
            fold_type(
                argument.type, *visible_functions, environment, fallback);
            if (argument.default_value) {
                fold_expression(
                    *argument.default_value,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        for (auto& variable : task.variables) {
            fold_type(
                variable.type, *visible_functions, environment, fallback);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        fold_statements(
            task.statements,
            *visible_functions,
            environment,
            fallback);
    }
    fold_statements(
        body.concurrent_statements,
        *visible_functions,
        environment,
        fallback);
    for (auto& process : body.processes) {
        for (auto& variable : process.variables) {
            fold_type(
                variable.type, *visible_functions, environment, fallback);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        fold_statements(
            process.statements,
            *visible_functions,
            environment,
            fallback);
    }
    for (auto& instance : body.instances) {
        for (auto& override : instance.parameter_overrides) {
            fold_expression(
                override.value,
                *visible_functions,
                environment,
                fallback);
        }
        for (auto& connection : instance.connections) {
            fold_expression(
                connection.value,
                *visible_functions,
                environment,
                fallback);
        }
    }
    for (auto& declaration : body.verilog_defparams) {
        for (auto& segment : declaration.path) {
            for (auto& index : segment.indices) {
                fold_expression(
                    index, *visible_functions, environment, fallback);
            }
        }
        fold_expression(
            declaration.value,
            *visible_functions,
            environment,
            fallback);
    }
    fold_generate_regions(
        body.generate_regions, *visible_functions, environment, fallback);
}

void fold_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback) {
    for (auto& region : regions) {
        fold_expression(
            region.initial, functions, environment, fallback);
        fold_expression(
            region.condition, functions, environment, fallback);
        fold_expression(
            region.iteration, functions, environment, fallback);
        if (region.kind == frontend::GenerateKind::Conditional) {
            std::string error;
            ConstantFunctionEvaluator evaluator {
                functions, environment, fallback };
            if (const auto condition = evaluator.evaluate(
                    region.condition, error)) {
                if (const auto truth = condition->truth_value()) {
                    fold_generate_body(
                        *truth ? region.then_body : region.else_body,
                        functions,
                        environment,
                        fallback);
                    continue;
                }
            }
        }
        fold_generate_body(
            region.then_body, functions, environment, fallback);
        fold_generate_body(
            region.else_body, functions, environment, fallback);
        for (auto& alternative : region.alternatives) {
            for (auto& choice : alternative.choices) {
                fold_expression(
                    choice.left, functions, environment, fallback);
                if (choice.right) {
                    fold_expression(
                        *choice.right,
                        functions,
                        environment,
                        fallback);
                }
            }
            fold_generate_body(
                alternative.body,
                functions,
                environment,
                fallback);
        }
    }
}

} // namespace

ConstantFunctionMemoizationScope::ConstantFunctionMemoizationScope()
{
    active_elaboration_call_caches.push_back(
        std::make_unique<CallCache>());
}

ConstantFunctionMemoizationScope::~ConstantFunctionMemoizationScope()
{
    active_elaboration_call_caches.pop_back();
}

std::optional<SystemVerilogConstantValue>
evaluate_systemverilog_constant_function_expression(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    const std::vector<frontend::FunctionDeclaration>& functions,
    std::string& error) {
    ConstantFunctionEvaluator evaluator{
        functions, environment, fallback_environment};
    return evaluator.evaluate(expression, error);
}

void fold_systemverilog_constant_functions(
    frontend::GenerateBody& body,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment) {
    CallCache call_cache;
    const FoldCallCacheScope cache_scope { call_cache };
    fold_generate_body(
        body, functions, environment, fallback_environment);
}

void fold_systemverilog_constant_functions(
    DesignUnit& unit,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::vector<Diagnostic>&) {
    CallCache call_cache;
    const FoldCallCacheScope cache_scope { call_cache };
    const auto& functions = unit.functions;
    for (auto& parameter : unit.parameters) {
        fold_type(
            parameter.type,
            functions,
            environment,
            fallback_environment);
        fold_expression(
            parameter.default_value,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& alias : unit.type_aliases) {
        fold_type(
            alias.type,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& port : unit.ports) {
        fold_type(
            port.type,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& signal : unit.signals) {
        fold_type(
            signal.type,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& function : unit.functions) {
        fold_type(
            function.return_type,
            functions,
            environment,
            fallback_environment);
        for (auto& argument : function.arguments) {
            fold_type(
                argument.type,
                functions,
                environment,
                fallback_environment);
        }
        for (auto& variable : function.variables) {
            fold_type(
                variable.type,
                functions,
                environment,
                fallback_environment);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_statements(
            function.statements,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& task : unit.tasks) {
        for (auto& argument : task.arguments) {
            fold_type(
                argument.type,
                functions,
                environment,
                fallback_environment);
        }
        for (auto& variable : task.variables) {
            fold_type(
                variable.type,
                functions,
                environment,
                fallback_environment);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_statements(
            task.statements,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& procedure : unit.procedures) {
        for (auto& argument : procedure.arguments) {
            fold_type(
                argument.type,
                functions,
                environment,
                fallback_environment);
            if (argument.default_value) {
                fold_expression(
                    *argument.default_value,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        for (auto& variable : procedure.variables) {
            fold_type(
                variable.type,
                functions,
                environment,
                fallback_environment);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_statements(
            procedure.statements,
            functions,
            environment,
            fallback_environment);
    }
    fold_statements(
        unit.concurrent_statements,
        functions,
        environment,
        fallback_environment);
    for (auto& process : unit.processes) {
        for (auto& variable : process.variables) {
            fold_type(
                variable.type,
                functions,
                environment,
                fallback_environment);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_statements(
            process.statements,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& instance : unit.instances) {
        for (auto& override : instance.parameter_overrides) {
            fold_expression(
                override.value,
                functions,
                environment,
                fallback_environment);
        }
        for (auto& connection : instance.connections) {
            fold_expression(
                connection.value,
                functions,
                environment,
                fallback_environment);
        }
    }
    for (auto& declaration : unit.verilog_defparams) {
        for (auto& segment : declaration.path) {
            for (auto& index : segment.indices) {
                fold_expression(
                    index,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_expression(
            declaration.value,
            functions,
            environment,
            fallback_environment);
    }
    fold_generate_regions(
        unit.generate_regions,
        functions,
        environment,
        fallback_environment);
}

} // namespace fsim::elaboration::elaboration_detail
