// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <functional>
#include <limits>

namespace fsim::elaboration {
namespace {

std::optional<std::size_t> compiled_literal_width(
    const std::string_view text,
    const std::size_t fallback)
{
    const auto quote = text.find('\'');
    if (quote == std::string_view::npos) {
        return fallback;
    }
    if (quote == 0U) {
        return std::size_t { 1U };
    }
    std::size_t width { };
    const auto parsed = std::from_chars(
        text.data(), text.data() + quote, width);
    if (parsed.ec != std::errc { }
        || parsed.ptr != text.data() + quote || width == 0U
        || width > runtime::simir::maximum_module_path_expression_storage_bytes) {
        return std::nullopt;
    }
    return width;
}

bool compiled_literal_signed(const std::string_view text)
{
    const auto quote = text.find('\'');
    return quote == std::string_view::npos
        || (quote + 1U < text.size()
            && (text[quote + 1U] == 's' || text[quote + 1U] == 'S'));
}

std::optional<runtime::PackedLogic4> compiled_logic_literal(
    const std::string_view spelling)
{
    std::string text;
    text.reserve(spelling.size());
    for (const auto byte : spelling) {
        if (byte != '_' && !std::isspace(
                static_cast<unsigned char>(byte))) {
            text.push_back(byte);
        }
    }
    const auto quote = text.find('\'');
    if (quote == std::string::npos) {
        return std::nullopt;
    }
    const auto width = compiled_literal_width(text, 1U);
    if (!width) {
        return std::nullopt;
    }
    auto cursor = quote + 1U;
    if (cursor < text.size()
        && (text[cursor] == 's' || text[cursor] == 'S')) {
        ++cursor;
    }
    if (cursor >= text.size()) {
        return std::nullopt;
    }
    if (quote == 0U && cursor + 1U == text.size()) {
        auto digit = static_cast<char>(std::tolower(
            static_cast<unsigned char>(text[cursor])));
        if (digit == '?') {
            digit = 'z';
        }
        if (digit != '0' && digit != '1' && digit != 'x'
            && digit != 'z') {
            return std::nullopt;
        }
        return runtime::PackedLogic4 {
            *width,
            digit == '0' ? runtime::Logic4::zero
                : digit == '1' ? runtime::Logic4::one
                : digit == 'z' ? runtime::Logic4::z
                               : runtime::Logic4::x
        };
    }
    const auto base = static_cast<char>(std::tolower(
        static_cast<unsigned char>(text[cursor++])));
    const auto digit_width = base == 'b' ? 1U
        : base == 'o'                   ? 3U
        : base == 'h'                   ? 4U
                                        : 0U;
    if (digit_width == 0U || cursor >= text.size()) {
        return std::nullopt;
    }
    std::string bits;
    bits.reserve((text.size() - cursor) * digit_width);
    for (; cursor < text.size(); ++cursor) {
        const auto digit = static_cast<char>(std::tolower(
            static_cast<unsigned char>(text[cursor])));
        if (digit == 'x' || digit == 'z' || digit == '?') {
            bits.append(digit_width, digit == '?' ? 'z' : digit);
            continue;
        }
        unsigned value { };
        if (digit >= '0' && digit <= '9') {
            value = static_cast<unsigned>(digit - '0');
        } else if (digit >= 'a' && digit <= 'f') {
            value = static_cast<unsigned>(digit - 'a' + 10);
        } else {
            return std::nullopt;
        }
        if (value >= (1U << digit_width)) {
            return std::nullopt;
        }
        for (auto bit = digit_width; bit > 0U; --bit) {
            bits.push_back(
                (value & (1U << (bit - 1U))) != 0U ? '1' : '0');
        }
    }
    if (bits.size() < *width) {
        bits.insert(bits.begin(), *width - bits.size(),
            bits.empty() ? '0' : bits.front() == 'x' ? 'x'
                : bits.front() == 'z'                ? 'z'
                                                     : '0');
    } else if (bits.size() > *width) {
        bits.erase(0U, bits.size() - *width);
    }
    return runtime::PackedLogic4::from_msb_string(bits);
}

runtime::PackedLogic4 compiled_integral_constant(
    const std::int64_t value,
    const std::size_t width)
{
    const auto words = (width + 63U) / 64U;
    std::vector<std::uint64_t> aval(
        words, value < 0 ? std::numeric_limits<std::uint64_t>::max() : 0U);
    std::vector<std::uint64_t> bval(words, 0U);
    if (!aval.empty()) {
        aval.front() = static_cast<std::uint64_t>(value);
        const auto final_bits = width % 64U;
        if (final_bits != 0U) {
            aval.back() &= (std::uint64_t { 1U } << final_bits) - 1U;
        }
    }
    return runtime::PackedLogic4::from_word_planes(
        width, aval, bval);
}

frontend::SourceSpan compiled_expression_source_span(
    const semantic::CompiledDesign& compiled,
    const semantic::SourceSpanId source)
{
    frontend::SourceSpan result;
    const auto& spans = compiled.semantics.source_spans();
    if (!source.valid() || source.value() >= spans.size()) {
        return result;
    }
    const auto& span = spans[source.value()];
    result.source_name = span.logical_name;
    result.begin = { static_cast<std::size_t>(span.begin.offset),
        span.begin.line, span.begin.column };
    result.end = { static_cast<std::size_t>(span.end.offset),
        span.end.line, span.end.column };
    const auto& files = compiled.semantics.source_files();
    if (span.file.valid() && span.file.value() < files.size()) {
        result.physical_source_name = files[span.file.value()].physical_name;
    }
    const auto& expansions = compiled.semantics.expansions();
    auto expansion = span.expansion;
    while (expansion && expansion->valid()
        && expansion->value() < expansions.size()) {
        const auto& record = expansions[expansion->value()];
        result.expansion_stack.push_back(record.description);
        expansion = record.parent;
    }
    std::ranges::reverse(result.expansion_stack);
    return result;
}

}  // namespace

std::optional<runtime::simir::ModulePathExpression>
HierarchyBuilder::compile_verilog_specify_expression(
    const semantic::ExpressionId expression,
    const SignalMap& signals,
    const semantic::SpecializedHirUnit& specialization,
    const std::string_view role)
{
    using Kind = semantic::sv::ExpressionKind;
    using runtime::simir::BinaryOperator;
    using runtime::simir::LogicalBinaryOperator;
    using runtime::simir::ModulePathExpression;
    using runtime::simir::ModulePathExpressionNode;
    using runtime::simir::ModulePathExpressionOperator;
    using runtime::simir::ReductionOperator;
    using runtime::simir::ShiftOperator;

    ModulePathExpression program;
    bool failed { };
    const auto append = [&](ModulePathExpressionNode node)
        -> std::optional<std::uint32_t> {
        if (program.nodes.size()
                >= runtime::simir::maximum_module_path_expression_storage_bytes
                    / sizeof(ModulePathExpressionNode)
            || program.nodes.size()
                > std::numeric_limits<std::uint32_t>::max()) {
            failed = true;
            return std::nullopt;
        }
        const auto id = static_cast<std::uint32_t>(program.nodes.size());
        program.nodes.push_back(std::move(node));
        return id;
    };

    std::function<std::optional<std::size_t>(semantic::ExpressionId)> width;
    std::function<bool(semantic::ExpressionId)> is_signed;
    width = [&](const semantic::ExpressionId id)
        -> std::optional<std::size_t> {
        const auto view = specialization.find_expression(id);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& source = *view->systemverilog;
        if (source.kind == Kind::boolean_literal) {
            return 1U;
        }
        if (source.kind == Kind::integer_literal) {
            return compiled_literal_width(source.text, 32U);
        }
        if (source.kind == Kind::logic_literal) {
            return compiled_literal_width(source.text, 1U);
        }
        if (source.kind == Kind::name) {
            const auto signal = signals.find(source.text);
            if (signal != signals.end()) {
                return design_.signal_info_.at(signal->second).width;
            }
            if (source.referenced_name
                && source.referenced_name->selected) {
                const auto declaration = specialization.find_declaration(
                    *source.referenced_name->selected);
                if (declaration && declaration->systemverilog != nullptr) {
                    const auto& selected = *declaration->systemverilog;
                    if (selected.type
                        && selected.type->executable_width) {
                        return static_cast<std::size_t>(
                            *selected.type->executable_width);
                    }
                    if (selected.initializer) {
                        return width(*selected.initializer);
                    }
                }
            }
            return std::nullopt;
        }
        if (source.kind == Kind::index) {
            return 1U;
        }
        if (source.kind == Kind::slice && !source.operands.empty()) {
            auto base = source.operands.front();
            while (true) {
                const auto base_view = specialization.find_expression(base);
                if (!base_view || base_view->systemverilog == nullptr
                    || (base_view->systemverilog->kind != Kind::index
                        && base_view->systemverilog->kind != Kind::slice)
                    || base_view->systemverilog->operands.empty()) {
                    break;
                }
                base = base_view->systemverilog->operands.front();
            }
            const auto base_view = specialization.find_expression(base);
            if (base_view && base_view->systemverilog != nullptr) {
                const auto signal = signals.find(
                    base_view->systemverilog->text);
                if (signal != signals.end()) {
                    const auto selection = resolve_verilog_specify_selection(
                        id, signal->second,
                        design_.signal_info_.at(signal->second),
                        specialization);
                    if (selection) {
                        return selection->width;
                    }
                }
            }
            return std::nullopt;
        }
        if (source.kind == Kind::concatenation) {
            std::size_t result { };
            for (const auto operand : source.operands) {
                const auto operand_width = width(operand);
                if (!operand_width
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - result) {
                    return std::nullopt;
                }
                result += *operand_width;
            }
            return result == 0U ? std::nullopt
                                : std::optional<std::size_t> { result };
        }
        if (source.kind == Kind::unary
            && source.operands.size() == 1U) {
            if (source.text == "!" || source.text == "&"
                || source.text == "|" || source.text == "^"
                || source.text == "~&" || source.text == "~|"
                || source.text == "~^" || source.text == "^~") {
                return 1U;
            }
            return width(source.operands.front());
        }
        if ((source.kind == Kind::binary
                || (source.kind == Kind::invalid
                    && source.text == "?:"))
            && source.operands.size() >= 2U) {
            if (source.text == "==" || source.text == "!="
                || source.text == "===" || source.text == "!=="
                || source.text == "<" || source.text == "<="
                || source.text == ">" || source.text == ">="
                || source.text == "&&" || source.text == "||") {
                return 1U;
            }
            if (source.text == "?:" && source.operands.size() == 3U) {
                const auto when_true = width(source.operands[1]);
                const auto when_false = width(source.operands[2]);
                return when_true && when_false
                    ? std::optional<std::size_t> {
                          std::max(*when_true, *when_false) }
                    : std::nullopt;
            }
            const auto left = width(source.operands[0]);
            if (source.text == "<<" || source.text == "<<<"
                || source.text == ">>" || source.text == ">>>") {
                return left;
            }
            const auto right = width(source.operands[1]);
            return left && right
                ? std::optional<std::size_t> { std::max(*left, *right) }
                : std::nullopt;
        }
        return std::nullopt;
    };
    is_signed = [&](const semantic::ExpressionId id) {
        const auto view = specialization.find_expression(id);
        if (!view || view->systemverilog == nullptr) {
            return false;
        }
        const auto& source = *view->systemverilog;
        if (source.kind == Kind::integer_literal
            || source.kind == Kind::logic_literal) {
            return compiled_literal_signed(source.text);
        }
        if (source.kind == Kind::name) {
            if (const auto signal = signals.find(source.text);
                signal != signals.end()) {
                return design_.signal_info_.at(signal->second).is_signed;
            }
            if (source.referenced_name
                && source.referenced_name->selected) {
                const auto declaration = specialization.find_declaration(
                    *source.referenced_name->selected);
                if (declaration && declaration->systemverilog != nullptr
                    && declaration->systemverilog->type) {
                    return declaration->systemverilog->type->signed_value;
                }
            }
            return false;
        }
        if (source.kind == Kind::unary
            && source.operands.size() == 1U && source.text != "!") {
            return is_signed(source.operands.front());
        }
        if (source.text == "?:" && source.operands.size() == 3U) {
            return is_signed(source.operands[1])
                && is_signed(source.operands[2]);
        }
        return source.kind == Kind::binary
            && source.operands.size() == 2U
            && is_signed(source.operands[0])
            && is_signed(source.operands[1]);
    };

    std::function<std::optional<std::uint32_t>(semantic::ExpressionId)>
        compile;
    compile = [&](const semantic::ExpressionId id)
        -> std::optional<std::uint32_t> {
        const auto view = specialization.find_expression(id);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& candidate = *view->systemverilog;
        if (candidate.kind == Kind::logic_literal) {
            if (auto literal = compiled_logic_literal(candidate.text)) {
                ModulePathExpressionNode node;
                node.operation = ModulePathExpressionOperator::constant;
                node.constant = std::move(*literal);
                node.width = static_cast<std::uint32_t>(
                    node.constant.width());
                node.is_signed = compiled_literal_signed(candidate.text);
                return append(std::move(node));
            }
        }
        if (const auto constant
            = specialization.evaluate_integral_expression(id)) {
            const auto constant_width = width(id).value_or(32U);
            if (constant_width == 0U
                || constant_width > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            ModulePathExpressionNode node;
            node.operation = ModulePathExpressionOperator::constant;
            node.constant = compiled_integral_constant(
                *constant, constant_width);
            node.width = static_cast<std::uint32_t>(constant_width);
            node.is_signed = is_signed(id);
            return append(std::move(node));
        }

        auto base = id;
        while (true) {
            const auto base_view = specialization.find_expression(base);
            if (!base_view || base_view->systemverilog == nullptr
                || (base_view->systemverilog->kind != Kind::index
                    && base_view->systemverilog->kind != Kind::slice)
                || base_view->systemverilog->operands.empty()) {
                break;
            }
            base = base_view->systemverilog->operands.front();
        }
        const auto base_view = specialization.find_expression(base);
        if (base_view && base_view->systemverilog != nullptr
            && base_view->systemverilog->kind == Kind::name) {
            const auto signal = signals.find(
                base_view->systemverilog->text);
            if (signal != signals.end()) {
                const auto terminal = resolve_verilog_specify_selection(
                    id, signal->second,
                    design_.signal_info_.at(signal->second),
                    specialization);
                if (terminal) {
                    ModulePathExpressionNode node;
                    node.operation = ModulePathExpressionOperator::terminal;
                    node.terminal = runtime::simir::ModulePathTerminal {
                        terminal->signal, terminal->offset, terminal->width
                    };
                    node.width = terminal->width;
                    node.is_signed
                        = design_.signal_info_.at(signal->second).is_signed;
                    return append(std::move(node));
                }
            }
        }

        const auto compile_operands = [&]()
            -> std::optional<std::vector<std::uint32_t>> {
            std::vector<std::uint32_t> result;
            result.reserve(candidate.operands.size());
            for (const auto operand : candidate.operands) {
                const auto compiled = compile(operand);
                if (!compiled) {
                    return std::nullopt;
                }
                result.push_back(*compiled);
            }
            return result;
        };
        if (candidate.kind == Kind::unary
            && candidate.operands.size() == 1U) {
            const auto source = compile(candidate.operands.front());
            if (!source) {
                return std::nullopt;
            }
            if (candidate.text == "+") {
                return source;
            }
            ModulePathExpressionNode node;
            node.operands = { *source };
            node.width = program.nodes[*source].width;
            node.is_signed = program.nodes[*source].is_signed;
            if (candidate.text == "~") {
                node.operation = ModulePathExpressionOperator::bit_not;
            } else if (candidate.text == "!") {
                node.operation = ModulePathExpressionOperator::logical_not;
                node.width = 1U;
                node.is_signed = false;
            } else if (candidate.text == "&" || candidate.text == "|"
                || candidate.text == "^" || candidate.text == "~&"
                || candidate.text == "~|" || candidate.text == "~^"
                || candidate.text == "^~") {
                node.operation = ModulePathExpressionOperator::reduction;
                node.width = 1U;
                node.is_signed = false;
                node.reduction = candidate.text.find('&')
                        != std::string::npos
                    ? ReductionOperator::bit_and
                    : candidate.text.find('|') != std::string::npos
                    ? ReductionOperator::bit_or
                    : ReductionOperator::bit_xor;
                const auto reduced = append(std::move(node));
                if (!reduced || candidate.text.front() != '~') {
                    return reduced;
                }
                ModulePathExpressionNode invert;
                invert.operation = ModulePathExpressionOperator::bit_not;
                invert.operands = { *reduced };
                invert.width = 1U;
                return append(std::move(invert));
            } else if (candidate.text == "-") {
                ModulePathExpressionNode zero;
                zero.operation = ModulePathExpressionOperator::constant;
                zero.constant = runtime::PackedLogic4 {
                    node.width, runtime::Logic4::zero
                };
                zero.width = node.width;
                zero.is_signed = node.is_signed;
                const auto zero_id = append(std::move(zero));
                if (!zero_id) {
                    return std::nullopt;
                }
                node.operation = ModulePathExpressionOperator::binary;
                node.binary = node.is_signed
                    ? BinaryOperator::subtract_signed
                    : BinaryOperator::subtract_unsigned;
                node.operands = { *zero_id, *source };
            } else {
                return std::nullopt;
            }
            return append(std::move(node));
        }
        if (candidate.kind == Kind::concatenation) {
            const auto operands = compile_operands();
            if (!operands || operands->empty()) {
                return std::nullopt;
            }
            std::uint64_t result_width { };
            for (const auto operand : *operands) {
                result_width += program.nodes[operand].width;
            }
            if (result_width > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            ModulePathExpressionNode node;
            node.operation = ModulePathExpressionOperator::concatenate;
            node.operands = *operands;
            node.width = static_cast<std::uint32_t>(result_width);
            return append(std::move(node));
        }
        const bool conditional = candidate.text == "?:"
            && candidate.operands.size() == 3U;
        if (!conditional
            && (candidate.kind != Kind::binary
                || candidate.operands.size() != 2U)) {
            return std::nullopt;
        }
        const auto operands = compile_operands();
        if (!operands) {
            return std::nullopt;
        }
        ModulePathExpressionNode node;
        node.operands = *operands;
        if (conditional) {
            node.operation = ModulePathExpressionOperator::conditional;
            node.width = std::max(program.nodes[(*operands)[1]].width,
                program.nodes[(*operands)[2]].width);
            node.is_signed = program.nodes[(*operands)[1]].is_signed
                && program.nodes[(*operands)[2]].is_signed;
            return append(std::move(node));
        }
        if (candidate.text == "&&" || candidate.text == "||") {
            node.operation = ModulePathExpressionOperator::logical_binary;
            node.logical = candidate.text == "&&"
                ? LogicalBinaryOperator::logical_and
                : LogicalBinaryOperator::logical_or;
            node.width = 1U;
            return append(std::move(node));
        }
        if (candidate.text == "<<" || candidate.text == "<<<"
            || candidate.text == ">>" || candidate.text == ">>>") {
            node.operation = ModulePathExpressionOperator::shift;
            node.shift = candidate.text == "<<" || candidate.text == "<<<"
                ? ShiftOperator::logical_left
                : candidate.text == ">>>"
                ? ShiftOperator::arithmetic_right
                : ShiftOperator::logical_right;
            node.width = program.nodes[(*operands)[0]].width;
            node.is_signed = program.nodes[(*operands)[0]].is_signed;
            return append(std::move(node));
        }
        node.operation = ModulePathExpressionOperator::binary;
        const bool signed_operation
            = program.nodes[(*operands)[0]].is_signed
            && program.nodes[(*operands)[1]].is_signed;
        const auto select = [&](const BinaryOperator unsigned_operation,
                                const BinaryOperator signed_operation_kind) {
            node.binary = signed_operation
                ? signed_operation_kind
                : unsigned_operation;
        };
        bool invert { };
        bool relational { };
        if (candidate.text == "&") {
            node.binary = BinaryOperator::bit_and;
        } else if (candidate.text == "|") {
            node.binary = BinaryOperator::bit_or;
        } else if (candidate.text == "^" || candidate.text == "~^"
            || candidate.text == "^~") {
            node.binary = BinaryOperator::bit_xor;
            invert = candidate.text != "^";
        } else if (candidate.text == "+") {
            select(BinaryOperator::add_unsigned,
                BinaryOperator::add_signed);
        } else if (candidate.text == "-") {
            select(BinaryOperator::subtract_unsigned,
                BinaryOperator::subtract_signed);
        } else if (candidate.text == "*") {
            select(BinaryOperator::multiply_unsigned,
                BinaryOperator::multiply_signed);
        } else if (candidate.text == "**") {
            select(BinaryOperator::power_unsigned,
                BinaryOperator::power_signed);
        } else if (candidate.text == "/") {
            select(BinaryOperator::divide_unsigned,
                BinaryOperator::divide_signed);
        } else if (candidate.text == "%") {
            select(BinaryOperator::modulo_unsigned,
                BinaryOperator::modulo_signed);
        } else if (candidate.text == "==") {
            node.binary = BinaryOperator::equal;
        } else if (candidate.text == "!=") {
            node.binary = BinaryOperator::not_equal;
        } else if (candidate.text == "===" || candidate.text == "!==") {
            node.binary = BinaryOperator::case_equal;
            invert = candidate.text == "!==";
        } else if (candidate.text == "<") {
            select(BinaryOperator::less_unsigned,
                BinaryOperator::less_signed);
            relational = true;
        } else if (candidate.text == "<=") {
            select(BinaryOperator::less_equal_unsigned,
                BinaryOperator::less_equal_signed);
            relational = true;
        } else if (candidate.text == ">") {
            select(BinaryOperator::greater_unsigned,
                BinaryOperator::greater_signed);
            relational = true;
        } else if (candidate.text == ">=") {
            select(BinaryOperator::greater_equal_unsigned,
                BinaryOperator::greater_equal_signed);
            relational = true;
        } else {
            return std::nullopt;
        }
        const bool comparison = relational || candidate.text == "=="
            || candidate.text == "!=" || candidate.text == "==="
            || candidate.text == "!==";
        node.width = comparison ? 1U : std::max(
            program.nodes[(*operands)[0]].width,
            program.nodes[(*operands)[1]].width);
        node.is_signed = !comparison && signed_operation;
        const auto result = append(std::move(node));
        if (!result || !invert) {
            return result;
        }
        ModulePathExpressionNode inverse;
        inverse.operation = comparison
            ? ModulePathExpressionOperator::logical_not
            : ModulePathExpressionOperator::bit_not;
        inverse.operands = { *result };
        inverse.width = program.nodes[*result].width;
        return append(std::move(inverse));
    };

    const auto root = compile(expression);
    if (!root || failed) {
        const auto source = specialization.find_expression(expression);
        const auto span = source && source->systemverilog != nullptr
            ? source->systemverilog->source
            : semantic::SourceSpanId { };
        const auto location = compiled_ != nullptr
            ? compiled_expression_source_span(*compiled_, span)
            : frontend::SourceSpan { };
        report("FSIM-ELAB-SVSPEC-008",
            "specify " + std::string { role }
                + " is not a bounded executable integral expression",
            location);
        return std::nullopt;
    }
    program.root = *root;
    return program;
}

}  // namespace fsim::elaboration
