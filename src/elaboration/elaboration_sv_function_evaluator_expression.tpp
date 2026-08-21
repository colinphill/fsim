// SPDX-License-Identifier: Apache-2.0

// Internal ConstantFunctionEvaluator fragment included by
// elaboration_sv_functions.cpp.

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
