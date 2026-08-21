// SPDX-License-Identifier: Apache-2.0

// Internal constant-expression evaluator included by
// elaboration_sv_constants.cpp.

[[nodiscard]] std::optional<Value> evaluate_impl(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error) {
    if (expression.kind == ExpressionKind::BooleanLiteral) {
        if (expression.text == "true" || expression.text == "false") {
            return make_known(
                expression.text == "true" ? 1U : 0U,
                1,
                false,
                false,
                expression.span);
        }
        error = "Boolean literal is malformed";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::IntegerLiteral) {
        if (expression.text.find('#') != std::string::npos) {
            const auto value = evaluate_constant_expression(
                expression, {}, error);
            if (!value) {
                return std::nullopt;
            }
            return Value{
                integer_value(*value),
                true,
                false,
                frontend::ValueDomain::Integer,
                expression.nominal_type,
                expression.span};
        }
        auto digits = cleaned_digits(expression.text);
        auto parsed = parse_decimal_packed(
            digits, std::nullopt, true, error);
        if (!parsed) {
            return std::nullopt;
        }
        return Value{
            std::move(parsed->packed),
            true,
            true,
            frontend::ValueDomain::Logic4,
            expression.nominal_type,
            expression.span};
    }
    if (expression.kind == ExpressionKind::LogicLiteral) {
        if (expression.text.size() == 3
            && expression.text.front() == '\''
            && expression.text.back() == '\'') {
            const auto digit = ascii_lower(expression.text[1]);
            Logic4 value = Logic4::x;
            if (digit == '0' || digit == 'l') {
                value = Logic4::zero;
            } else if (digit == '1' || digit == 'h') {
                value = Logic4::one;
            } else if (digit == 'z') {
                value = Logic4::z;
            } else if (digit != 'x' && digit != 'u'
                       && digit != 'w' && digit != '-') {
                error = "VHDL logic character literal is malformed";
                return std::nullopt;
            }
            return Value{
                PackedLogic4{1, value},
                false,
                false,
                frontend::ValueDomain::Logic4,
                expression.nominal_type,
                expression.span};
        }
        if (expression.text.find('#') != std::string::npos) {
            const auto value = evaluate_constant_expression(
                expression, {}, error);
            if (!value) {
                return std::nullopt;
            }
            return Value{
                integer_value(*value),
                true,
                false,
                frontend::ValueDomain::Integer,
                expression.nominal_type,
                expression.span};
        }
        auto result = parse_based_literal(expression, error);
        if (result) {
            result->nominal_type = expression.nominal_type;
        }
        return result;
    }
    if (expression.kind == ExpressionKind::Identifier) {
        if (expression.text == "$") {
            auto result = Value {
                PackedLogic4 { 1, Logic4::zero },
                false,
                false,
                frontend::ValueDomain::Bit2,
                {},
                expression.span
            };
            result.unbounded = true;
            return result;
        }
        if (const auto found = environment.find(expression.text);
            found != environment.end()) {
            auto result = found->second;
            result.source = expression.span;
            return result;
        }
        if (const auto fallback =
                fallback_environment.find(expression.text);
            fallback != fallback_environment.end()) {
            auto result = make_known(
                static_cast<std::uint64_t>(fallback->second),
                64,
                true,
                false,
                expression.span);
            result.nominal_type = expression.nominal_type;
            return result;
        }
        error =
            "unknown or forward parameter reference '"
            + expression.text + "'";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Index
        && expression.operands.size() == 2U) {
        const auto base = evaluate_impl(
            expression.operands[0], environment, fallback_environment, error);
        const auto index = base
            ? evaluate_impl(
                  expression.operands[1], environment,
                  fallback_environment, error)
            : std::nullopt;
        if (!base || !index) {
            return std::nullopt;
        }
        auto packed = PackedLogic4{1, Logic4::x};
        if (base->packed.is_logic9()) {
            packed = packed.promoted_to_logic9();
        }
        const auto position = index->integer_value();
        if (position && *position >= 0
            && static_cast<std::uint64_t>(*position) < base->width) {
            if (base->packed.is_logic9()) {
                packed.set_logic9(
                    0, base->packed.get_logic9(
                           static_cast<std::uint32_t>(*position)));
            } else {
                packed.set(
                    0, base->packed.get(
                           static_cast<std::uint32_t>(*position)));
            }
        }
        return Value{
            std::move(packed), false, false, base->domain, {},
            expression.span};
    }
    if (expression.kind == ExpressionKind::Slice
        && expression.operands.size() == 3U) {
        const auto base = evaluate_impl(
            expression.operands[0], environment, fallback_environment, error);
        const auto first = base
            ? evaluate_impl(
                  expression.operands[1], environment,
                  fallback_environment, error)
            : std::nullopt;
        const auto second = first
            ? evaluate_impl(
                  expression.operands[2], environment,
                  fallback_environment, error)
            : std::nullopt;
        if (!base || !first || !second) {
            return std::nullopt;
        }
        const auto first_index = first->integer_value();
        const auto second_index = second->integer_value();
        if (!first_index || !second_index) {
            error = "constant part-select bounds must be known integers";
            return std::nullopt;
        }
        std::uint64_t width = 0;
        if (expression.text == "+:" || expression.text == "-:") {
            if (*second_index <= 0) {
                error = "constant indexed part-select width must be positive";
                return std::nullopt;
            }
            width = static_cast<std::uint64_t>(*second_index);
        } else {
            const auto distance = *first_index >= *second_index
                ? static_cast<std::uint64_t>(*first_index)
                    - static_cast<std::uint64_t>(*second_index)
                : static_cast<std::uint64_t>(*second_index)
                    - static_cast<std::uint64_t>(*first_index);
            if (distance == std::numeric_limits<std::uint64_t>::max()) {
                error = "constant part-select width exceeds the resource limit";
                return std::nullopt;
            }
            width = distance + 1U;
        }
        if (width == 0U || width > maximum_constant_width) {
            error = "constant part-select width exceeds the resource limit";
            return std::nullopt;
        }
        const auto result_width = static_cast<std::uint32_t>(width);
        auto packed = PackedLogic4{result_width, Logic4::x};
        if (base->packed.is_logic9()) {
            packed = packed.promoted_to_logic9();
        }
        const auto source_at = [&](const std::uint32_t bit)
            -> std::optional<std::uint32_t> {
            bool add = true;
            auto anchor = *second_index;
            auto offset = bit;
            if (expression.text == "+:") {
                anchor = *first_index;
            } else if (expression.text == "-:") {
                anchor = *first_index;
                offset = result_width - bit - 1U;
                add = false;
            } else if (*first_index < *second_index) {
                add = false;
            }
            if (anchor < 0) {
                return std::nullopt;
            }
            const auto unsigned_anchor = static_cast<std::uint64_t>(anchor);
            if ((!add && unsigned_anchor < offset)
                || (add && unsigned_anchor
                        > std::numeric_limits<std::uint64_t>::max() - offset)) {
                return std::nullopt;
            }
            const auto source = add
                ? unsigned_anchor + offset : unsigned_anchor - offset;
            return source < base->width
                ? std::optional<std::uint32_t>{
                      static_cast<std::uint32_t>(source)}
                : std::nullopt;
        };
        for (std::uint32_t bit = 0; bit < result_width; ++bit) {
            const auto source = source_at(bit);
            if (!source) {
                continue;
            }
            if (base->packed.is_logic9()) {
                packed.set_logic9(bit, base->packed.get_logic9(*source));
            } else {
                packed.set(bit, base->packed.get(*source));
            }
        }
        return Value{
            std::move(packed), false, false, base->domain, {},
            expression.span};
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1U) {
        auto operand = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!operand) {
            return std::nullopt;
        }
        if (operand->unbounded) {
            error = "symbolic unbounded '$' is only valid as a parameter "
                    "value or an argument to $isunbounded";
            return std::nullopt;
        }
        operand->source = expression.span;
        if (expression.text == "+") {
            return operand;
        }
        if (expression.text == "!") {
            const auto value = truth(*operand);
            return logical_result(
                value == Truth::True ? Truth::False
                : value == Truth::False ? Truth::True
                                        : Truth::Unknown,
                expression.span);
        }
        if (expression.text == "-") {
            if (!operand->known()) {
                return make_unknown(
                    operand->width,
                    operand->is_signed,
                    expression.span);
            }
            operand->packed =
                packed_negate(operand->packed, operand->width);
            operand->refresh_low_word_mirrors();
            return operand;
        }
        if (expression.text == "~") {
            for (std::uint32_t bit = 0; bit < operand->width; ++bit) {
                const auto state = runtime::to_logic4(
                    operand->packed.get_logic9(bit));
                operand->packed.set(
                    bit,
                    state == Logic4::zero ? Logic4::one
                    : state == Logic4::one ? Logic4::zero
                                           : Logic4::x);
            }
            operand->refresh_low_word_mirrors();
            return operand;
        }
        if (expression.text == "&" || expression.text == "|"
            || expression.text == "^" || expression.text == "~&"
            || expression.text == "~|" || expression.text == "~^"
            || expression.text == "^~") {
            Truth reduced = Truth::False;
            if (expression.text == "&" || expression.text == "~&") {
                reduced = Truth::True;
                for (std::uint32_t bit = 0; bit < operand->width; ++bit) {
                    const auto state = runtime::to_logic4(
                        operand->packed.get_logic9(bit));
                    if (state == Logic4::x || state == Logic4::z) {
                        if (reduced == Truth::True) {
                            reduced = Truth::Unknown;
                        }
                    } else if (state == Logic4::zero) {
                        reduced = Truth::False;
                        break;
                    }
                }
            } else if (expression.text == "|" || expression.text == "~|") {
                reduced = truth(*operand);
            } else if (!operand->known()) {
                reduced = Truth::Unknown;
            } else {
                bool odd = false;
                for (std::uint32_t bit = 0; bit < operand->width; ++bit) {
                    odd = odd != packed_one(operand->packed, bit);
                }
                reduced = odd ? Truth::True : Truth::False;
            }
            if (expression.text == "~&" || expression.text == "~|"
                || expression.text == "~^" || expression.text == "^~") {
                reduced =
                    reduced == Truth::True ? Truth::False
                    : reduced == Truth::False ? Truth::True
                                              : Truth::Unknown;
            }
            return logical_result(reduced, expression.span);
        }
        error =
            "unsupported SystemVerilog unary constant operator '"
            + expression.text + "'";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2U) {
        return evaluate_binary(
            expression, environment, fallback_environment, error);
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$signed"
            || expression.text == "$unsigned")) {
        if (expression.operands.size() != 1U) {
            error = expression.text + " requires exactly one argument";
            return std::nullopt;
        }
        auto result = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (result) {
            result->is_signed = expression.text == "$signed";
            result->source = expression.span;
        }
        return result;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with("@sv-cast:")) {
        if (expression.operands.size() != 1U) {
            error = "SystemVerilog type casts require exactly one argument";
            return std::nullopt;
        }
        const auto type_name = std::string_view{expression.text}.substr(
            std::string_view{"@sv-cast:"}.size());
        std::uint32_t width = 0;
        bool is_signed = false;
        bool two_state = false;
        auto domain = frontend::ValueDomain::Unknown;
        if (type_name == "bit") {
            width = 1;
            two_state = true;
            domain = frontend::ValueDomain::Bit2;
        } else if (type_name == "logic" || type_name == "reg") {
            width = 1;
            domain = frontend::ValueDomain::Logic4;
        } else if (type_name == "byte") {
            width = 8;
            is_signed = true;
            two_state = true;
            domain = frontend::ValueDomain::Integer;
        } else if (type_name == "shortint") {
            width = 16;
            is_signed = true;
            two_state = true;
            domain = frontend::ValueDomain::Integer;
        } else if (type_name == "int") {
            width = 32;
            is_signed = true;
            two_state = true;
            domain = frontend::ValueDomain::Integer;
        } else if (type_name == "longint") {
            width = 64;
            is_signed = true;
            two_state = true;
            domain = frontend::ValueDomain::Integer;
        } else if (type_name == "integer") {
            width = 32;
            is_signed = true;
            domain = frontend::ValueDomain::Logic4;
        } else if (expression.call_result_width != 0U
                   && expression.call_result_width
                       <= maximum_constant_width
                   && expression.call_result_domain
                       != frontend::ValueDomain::Unknown) {
            width = static_cast<std::uint32_t>(
                expression.call_result_width);
            is_signed = expression.call_result_signed;
            domain = expression.call_result_domain;
            two_state = is_two_state_domain(domain);
        } else {
            error =
                "constant casts to named or nonintegral type '"
                + std::string{type_name}
                + "' require resolved type layout";
            return std::nullopt;
        }
        auto result = evaluate_impl(
            expression.operands.front(), environment,
            fallback_environment, error);
        if (!result) {
            return std::nullopt;
        }
        result = resized(std::move(*result), width);
        if (two_state) {
            convert_to_two_state(*result);
        }
        if (domain == frontend::ValueDomain::Logic4
            && result->packed.is_logic9()) {
            result->packed = runtime::collapse_to_logic4(result->packed);
            result->refresh_low_word_mirrors();
        } else if (domain == frontend::ValueDomain::Logic9
                   && !result->packed.is_logic9()) {
            result->packed = result->packed.promoted_to_logic9();
            result->refresh_low_word_mirrors();
        }
        result->is_signed = is_signed;
        result->unsized = false;
        result->domain = domain;
        result->nominal_type = expression.nominal_type.empty()
            ? std::string{type_name}
            : expression.nominal_type;
        result->source = expression.span;
        return result;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "@stream-left"
            || expression.text == "@stream-right")) {
        if (expression.operands.size() < 2U) {
            error =
                "streaming concatenation requires a slice size and at "
                "least one operand";
            return std::nullopt;
        }
        const auto slice = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!slice) {
            return std::nullopt;
        }
        const auto slice_size = nonnegative_count(*slice, error);
        if (!slice_size || *slice_size == 0U
            || *slice_size > maximum_constant_width) {
            if (slice_size) {
                error =
                    "streaming concatenation slice size exceeds the "
                    "constant-width resource limit";
            }
            return std::nullopt;
        }

        std::vector<Value> operands;
        std::uint64_t width = 0;
        for (std::size_t index = 1;
             index < expression.operands.size(); ++index) {
            auto operand = evaluate_impl(
                expression.operands[index],
                environment,
                fallback_environment,
                error);
            if (!operand) {
                return std::nullopt;
            }
            if (operand->width > maximum_constant_width - width) {
                error =
                    "streaming concatenation result exceeds the constant-"
                    "width resource limit";
                return std::nullopt;
            }
            width += operand->width;
            operands.push_back(std::move(*operand));
        }
        if (width == 0U) {
            error = "streaming concatenation requires a nonempty operand";
            return std::nullopt;
        }
        if (operands.size() > maximum_constant_work_units / width) {
            error =
                "streaming concatenation exceeds the constant-evaluation "
                "work limit";
            return std::nullopt;
        }

        Value ordinary{
            0,
            0,
            0,
            static_cast<std::uint32_t>(width),
            false,
            false,
            expression.span};
        for (const auto& operand : operands) {
            append_packed(ordinary, operand);
        }
        normalize(ordinary);
        if (expression.text == "@stream-right"
            || *slice_size >= width) {
            return ordinary;
        }

        auto packed = PackedLogic4{width, Logic4::zero};
        if (ordinary.packed.is_logic9()) {
            packed = packed.promoted_to_logic9();
        }
        for (std::uint64_t offset = 0; offset < width;) {
            const auto chunk = std::min(*slice_size, width - offset);
            const auto destination = width - offset - chunk;
            for (std::uint64_t bit = 0; bit < chunk; ++bit) {
                if (ordinary.packed.is_logic9()) {
                    packed.set_logic9(
                        destination + bit,
                        ordinary.packed.get_logic9(offset + bit));
                } else {
                    packed.set(
                        destination + bit,
                        ordinary.packed.get(offset + bit));
                }
            }
            offset += chunk;
        }
        return Value{
            std::move(packed), false, false, ordinary.domain, {},
            expression.span};
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "inside") {
        if (expression.operands.size() < 2U) {
            error = "inside requires a left operand and a nonempty list";
            return std::nullopt;
        }
        const auto left = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!left) {
            return std::nullopt;
        }
        Truth accumulated = Truth::False;
        for (std::size_t index = 1;
             index < expression.operands.size(); ++index) {
            const auto& item = expression.operands[index];
            Truth matched = Truth::False;
            if (item.kind == ExpressionKind::Call
                && item.text == "@inside-range") {
                if (item.operands.size() != 2U) {
                    error = "inside range requires a low and high bound";
                    return std::nullopt;
                }
                const auto low = evaluate_impl(
                    item.operands[0], environment,
                    fallback_environment, error);
                const auto high = evaluate_impl(
                    item.operands[1], environment,
                    fallback_environment, error);
                if (!low || !high) {
                    return std::nullopt;
                }
                matched = logical_and(
                    relational(*low, *high, true),
                    logical_and(
                        relational(*left, *low, false),
                        relational(*left, *high, true)));
            } else {
                const auto value = evaluate_impl(
                    item, environment, fallback_environment, error);
                if (!value) {
                    return std::nullopt;
                }
                matched = wildcard_equal(*left, *value);
            }
            if (matched == Truth::True) {
                return logical_result(Truth::True, expression.span);
            }
            if (matched == Truth::Unknown) {
                accumulated = Truth::Unknown;
            }
        }
        return logical_result(accumulated, expression.span);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$isunknown") {
        if (expression.operands.size() != 1U) {
            error = "$isunknown requires exactly one argument";
            return std::nullopt;
        }
        const auto operand = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!operand) {
            return std::nullopt;
        }
        return make_known(
            operand->known() ? 0U : 1U,
            1,
            false,
            false,
            expression.span);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$isunbounded") {
        if (expression.operands.size() != 1U) {
            error = "$isunbounded requires exactly one argument";
            return std::nullopt;
        }
        const auto operand = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!operand) {
            return std::nullopt;
        }
        return make_known(
            operand->unbounded ? 1U : 0U,
            1,
            false,
            false,
            expression.span);
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
        const bool accepts_dimension =
            expression.text == "$left"
            || expression.text == "$right"
            || expression.text == "$low"
            || expression.text == "$high"
            || expression.text == "$size"
            || expression.text == "$increment";
        if (expression.operands.empty()
            || expression.operands.size() > (accepts_dimension ? 2U : 1U)) {
            error = expression.text
                + " requires one packed argument"
                + (accepts_dimension
                       ? " and at most one dimension argument" : "");
            return std::nullopt;
        }
        const auto operand = evaluate_impl(
            expression.operands.front(), environment,
            fallback_environment, error);
        if (!operand) {
            return std::nullopt;
        }
        if (expression.operands.size() == 2U) {
            const auto dimension = evaluate_impl(
                expression.operands[1], environment,
                fallback_environment, error);
            if (!dimension) {
                return std::nullopt;
            }
            const auto selected = nonnegative_count(*dimension, error);
            if (!selected || *selected != 1U) {
                if (selected) {
                    error = expression.text
                        + " supports only packed dimension 1";
                }
                return std::nullopt;
            }
        }
        const auto range = operand->packed_range.value_or(
            frontend::PackedRange{
                static_cast<std::int64_t>(operand->width - 1U),
                0,
                true});
        std::int64_t result = 0;
        if (expression.text == "$bits" || expression.text == "$size") {
            result = static_cast<std::int64_t>(operand->width);
        } else if (expression.text == "$left") {
            result = range.left;
        } else if (expression.text == "$right") {
            result = range.right;
        } else if (expression.text == "$low") {
            result = std::min(range.left, range.right);
        } else if (expression.text == "$high") {
            result = std::max(range.left, range.right);
        } else if (expression.text == "$increment") {
            result = range.left >= range.right ? 1 : -1;
        } else if (expression.text == "$dimensions") {
            result = 1;
        } else if (expression.text == "$unpacked_dimensions") {
            result = 0;
        }
        return make_known(
            static_cast<std::uint32_t>(result), 32, true, false,
            expression.span);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$clog2") {
        if (expression.operands.size() != 1U) {
            error = "$clog2 requires exactly one argument";
            return std::nullopt;
        }
        const auto operand = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!operand || !operand->known()) {
            if (operand) {
                error = "$clog2 argument contains X or Z";
            }
            return std::nullopt;
        }
        if (operand->is_signed
            && packed_one(operand->packed, operand->width - 1U)) {
            error = "$clog2 requires a nonnegative integral argument";
            return std::nullopt;
        }
        std::uint32_t highest_one = 0;
        bool found_one = false;
        bool lower_one = false;
        for (auto bit = operand->width; bit-- > 0U;) {
            if (!packed_one(operand->packed, bit)) {
                continue;
            }
            if (!found_one) {
                highest_one = bit;
                found_one = true;
            } else {
                lower_one = true;
                break;
            }
        }
        const auto result =
            !found_one || highest_one == 0U
                ? 0U
                : highest_one + (lower_one ? 1U : 0U);
        return make_known(
            result, 32, true, false, expression.span);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "?:"
        && expression.operands.size() == 3U) {
        const auto condition = evaluate_impl(
            expression.operands[0],
            environment,
            fallback_environment,
            error);
        if (!condition) {
            return std::nullopt;
        }
        const auto condition_truth = truth(*condition);
        if (condition_truth != Truth::Unknown) {
            const auto selected_index = condition_truth == Truth::True
                ? std::size_t { 1 }
                : std::size_t { 2 };
            const auto unselected_index = selected_index == 1U
                ? std::size_t { 2 }
                : std::size_t { 1 };
            auto selected = evaluate_impl(
                expression.operands[selected_index],
                environment,
                fallback_environment,
                error);
            if (!selected) {
                return std::nullopt;
            }
            const auto unselected = constant_profile(
                expression.operands[unselected_index],
                environment,
                fallback_environment);
            if (unselected) {
                const auto width = std::max(
                    selected->width, unselected->width);
                const bool common_signed = selected->is_signed && unselected->is_signed;
                *selected = common_operand(
                    *selected, width, common_signed);
            }
            selected->source = expression.span;
            return selected;
        }
        auto when_true = evaluate_impl(
            expression.operands[1],
            environment,
            fallback_environment,
            error);
        if (!when_true) {
            return std::nullopt;
        }
        auto when_false = evaluate_impl(
            expression.operands[2],
            environment,
            fallback_environment,
            error);
        if (!when_false) {
            return std::nullopt;
        }
        const auto width = std::max(when_true->width, when_false->width);
        const bool common_signed =
            when_true->is_signed && when_false->is_signed;
        auto lhs = common_operand(*when_true, width, common_signed);
        auto rhs = common_operand(*when_false, width, common_signed);
        if (truth(*condition) == Truth::True) {
            lhs.source = expression.span;
            return lhs;
        }
        if (truth(*condition) == Truth::False) {
            rhs.source = expression.span;
            return rhs;
        }
        auto packed = PackedLogic4{width, Logic4::x};
        const bool logic9 =
            lhs.packed.is_logic9() || rhs.packed.is_logic9();
        if (logic9) {
            packed = packed.promoted_to_logic9();
        }
        for (std::uint32_t bit = 0; bit < width; ++bit) {
            const auto lhs_state = lhs.packed.get_logic9(bit);
            const auto rhs_state = rhs.packed.get_logic9(bit);
            if (logic9) {
                packed.set_logic9(
                    bit,
                    lhs_state == rhs_state
                        ? lhs_state : runtime::Logic9::x);
            } else if (lhs_state == rhs_state) {
                packed.set(bit, runtime::to_logic4(lhs_state));
            }
        }
        const auto domain = lhs.domain == rhs.domain
            ? lhs.domain : frontend::ValueDomain::Logic4;
        return Value{
            std::move(packed), common_signed, false, domain, {},
            expression.span};
    }
    const bool scalar_default_pattern =
        expression.kind == ExpressionKind::Aggregate
        && expression.text == "sv-pattern"
        && expression.operands.size() == 1U
        && expression.aggregate_choices.size() == 1U
        && expression.aggregate_choices.front() == "default";
    if (scalar_default_pattern) {
        const auto value = evaluate_impl(
            expression.operands.front(), environment,
            fallback_environment, error);
        if (!value) {
            return std::nullopt;
        }
        auto packed = PackedLogic4{1, Logic4::x};
        if (value->packed.is_logic9()) {
            packed = packed.promoted_to_logic9();
            packed.set_logic9(0, value->packed.get_logic9(0));
        } else {
            packed.set(0, value->packed.get(0));
        }
        return Value{
            std::move(packed), true, true, value->domain, {},
            expression.span};
    }
    const bool positional_pattern =
        expression.kind == ExpressionKind::Aggregate
        && expression.text == "sv-pattern"
        && std::ranges::all_of(
            expression.aggregate_choices,
            [](const auto& choice) { return choice.empty(); });
    if (expression.kind == ExpressionKind::Aggregate
        && expression.text == "sv-pattern" && !positional_pattern
        && !scalar_default_pattern) {
        error =
            "keyed/default assignment patterns require aggregate type "
            "layout and are deferred to aggregate constant evaluation";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Concatenation
        || expression.kind == ExpressionKind::Replication
        || positional_pattern) {
        std::size_t first = 0;
        std::uint64_t repetitions = 1;
        if (expression.kind == ExpressionKind::Replication) {
            if (expression.operands.size() < 2U) {
                error = "replication requires a count and at least one operand";
                return std::nullopt;
            }
            const auto count = evaluate_impl(
                expression.operands.front(),
                environment,
                fallback_environment,
                error);
            if (!count) {
                return std::nullopt;
            }
            const auto converted = nonnegative_count(*count, error);
            if (!converted) {
                return std::nullopt;
            }
            if (*converted == 0U) {
                return Value {
                    PackedLogic4 { 1, Logic4::zero },
                    false,
                    false,
                    frontend::ValueDomain::Logic4,
                    {},
                    expression.span
                };
            }
            repetitions = *converted;
            first = 1;
        }
        if (expression.operands.size() == first) {
            error = "concatenation requires at least one operand";
            return std::nullopt;
        }
        std::vector<Value> operands;
        std::uint64_t element_width = 0;
        for (std::size_t index = first;
             index < expression.operands.size();
             ++index) {
            const auto& operand_expression = expression.operands[index];
            if (expression.kind == ExpressionKind::Concatenation
                && operand_expression.kind
                    == ExpressionKind::Replication
                && !operand_expression.operands.empty()) {
                const auto count = evaluate_impl(
                    operand_expression.operands.front(),
                    environment,
                    fallback_environment,
                    error);
                if (!count) {
                    return std::nullopt;
                }
                const auto converted = nonnegative_count(*count, error);
                if (!converted) {
                    return std::nullopt;
                }
                if (*converted == 0U) {
                    continue;
                }
            }
            auto operand = evaluate_impl(
                operand_expression,
                environment,
                fallback_environment,
                error);
            if (!operand) {
                return std::nullopt;
            }
            if (operand->width > maximum_constant_width - element_width) {
                error =
                    "concatenation element width exceeds the "
                    "16,777,216-bit resource limit";
                return std::nullopt;
            }
            element_width += operand->width;
            operands.push_back(std::move(*operand));
        }
        if (element_width == 0
            || repetitions > maximum_constant_width / element_width) {
            error =
                "concatenation result exceeds the 16,777,216-bit "
                "resource limit";
            return std::nullopt;
        }
        const auto result_width = element_width * repetitions;
        if (operands.size() == 1U && element_width == 1U
            && result_width <= maximum_constant_work_units / 64U) {
            return Value{
                PackedLogic4(
                    static_cast<std::size_t>(result_width),
                    runtime::to_logic4(
                        operands.front().packed.get_logic9(0))),
                false,
                false,
                operands.front().domain,
                {},
                expression.span};
        }
        if (repetitions
                > maximum_constant_work_units / operands.size()
            || result_width
                > maximum_constant_work_units
                    / (repetitions * operands.size())) {
            error =
                "concatenation or replication exceeds the constant-"
                "evaluation work limit";
            return std::nullopt;
        }
        Value result{
            0,
            0,
            0,
            static_cast<std::uint32_t>(result_width),
            false,
            false,
            expression.span};
        for (std::uint64_t repetition = 0;
             repetition < repetitions;
             ++repetition) {
            for (const auto& operand : operands) {
                append_packed(result, operand);
            }
        }
        normalize(result);
        return result;
    }
    error =
        "expression form is not a supported SystemVerilog integral "
        "constant expression";
    return std::nullopt;
}
