// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fsim::elaboration {
namespace {

    std::optional<std::uint64_t> packed_index_offset(
        const std::int64_t index,
        const SignalInfo& signal)
    {
        if (!signal.packed_range) {
            return index == 0 && signal.width == 1U
                ? std::optional<std::uint64_t> { 0U }
                : std::nullopt;
        }
        const auto& range = *signal.packed_range;
        const auto low = std::min(range.left, range.right);
        const auto high = std::max(range.left, range.right);
        if (index < low || index > high) {
            return std::nullopt;
        }
        return range.descending
            ? static_cast<std::uint64_t>(index)
                - static_cast<std::uint64_t>(range.right)
            : static_cast<std::uint64_t>(range.right)
                - static_cast<std::uint64_t>(index);
    }

    frontend::SourceSpan compiled_source_span(
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
            result.physical_source_name
                = files[span.file.value()].physical_name;
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

    std::optional<semantic::ExpressionId> compiled_terminal_base(
        const semantic::ExpressionId expression,
        const semantic::SpecializedHirUnit& specialization)
    {
        const auto view = specialization.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        using Kind = semantic::sv::ExpressionKind;
        if ((view->systemverilog->kind == Kind::index
                || view->systemverilog->kind == Kind::slice)
            && !view->systemverilog->operands.empty()) {
            return compiled_terminal_base(
                view->systemverilog->operands.front(), specialization);
        }
        return expression;
    }

    std::string canonical_specparam_integral_identity(
        const std::int64_t value)
    {
        constexpr std::size_t width = 32U;
        std::string bits(width, value < 0 ? '1' : '0');
        const auto raw = static_cast<std::uint64_t>(value);
        for (std::size_t offset = 0U; offset < width; ++offset) {
            bits[width - offset - 1U]
                = (raw & (std::uint64_t { 1U } << offset)) != 0U
                ? '1'
                : '0';
        }
        return "svconst-v3:b=0:w=32:s=1:u=0:d="
            + std::to_string(static_cast<unsigned>(
                frontend::ValueDomain::Logic4))
            + ":n=0::v=" + bits;
    }

    std::optional<std::string> compiled_specparam_identity(
        const semantic::ExpressionId expression,
        const semantic::SpecializedHirUnit& specialization,
        const std::optional<std::int64_t> integral)
    {
        const auto literal_identity = [](const std::string_view spelling)
            -> std::optional<std::string> {
            if (spelling.starts_with("svconst-v3:")) {
                return std::string { spelling };
            }
            std::string normalized;
            normalized.reserve(spelling.size());
            std::ranges::copy_if(
                spelling, std::back_inserter(normalized),
                [](const char value) { return value != '_'; });
            const auto quote = normalized.find('\'');
            if (quote == std::string::npos || quote == 0U) {
                return std::nullopt;
            }
            std::size_t width { };
            const auto parsed = std::from_chars(
                normalized.data(), normalized.data() + quote, width);
            if (parsed.ec != std::errc { }
                || parsed.ptr != normalized.data() + quote
                || width == 0U || width > 1'048'576U) {
                return std::nullopt;
            }
            auto cursor = quote + 1U;
            bool signed_value { };
            if (cursor < normalized.size()
                && (normalized[cursor] == 's'
                    || normalized[cursor] == 'S')) {
                signed_value = true;
                ++cursor;
            }
            if (cursor >= normalized.size()) {
                return std::nullopt;
            }
            const auto base = static_cast<char>(std::tolower(
                static_cast<unsigned char>(normalized[cursor++])));
            const auto digit_width = base == 'b' ? 1U
                : base == 'o'                 ? 3U
                : base == 'h'                 ? 4U
                                              : 0U;
            if (digit_width == 0U || cursor == normalized.size()) {
                return std::nullopt;
            }
            std::string bits;
            for (; cursor < normalized.size(); ++cursor) {
                const auto digit = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(normalized[cursor])));
                if (digit == 'x' || digit == 'z' || digit == '?') {
                    bits.append(digit_width, digit == '?' ? 'x' : digit);
                    continue;
                }
                const auto value = digit >= '0' && digit <= '9'
                    ? static_cast<unsigned>(digit - '0')
                    : digit >= 'a' && digit <= 'f'
                    ? static_cast<unsigned>(digit - 'a' + 10)
                    : 16U;
                const auto limit = 1U << digit_width;
                if (value >= limit) {
                    return std::nullopt;
                }
                for (auto bit = digit_width; bit != 0U; --bit) {
                    bits.push_back(
                        (value & (1U << (bit - 1U))) != 0U ? '1' : '0');
                }
            }
            if (bits.size() < width) {
                bits.insert(bits.begin(), width - bits.size(), '0');
            } else if (bits.size() > width) {
                bits.erase(0U, bits.size() - width);
            }
            return "svconst-v3:b=0:w=" + std::to_string(width)
                + ":s=" + (signed_value ? "1" : "0")
                + ":u=0:d="
                + std::to_string(static_cast<unsigned>(
                    frontend::ValueDomain::Logic4))
                + ":n=0::v=" + bits;
        };
        std::function<std::optional<std::string>(
            semantic::ExpressionId, std::size_t)> resolve;
        resolve = [&](const semantic::ExpressionId candidate,
                      const std::size_t depth)
            -> std::optional<std::string> {
            if (depth > specialization.design()
                    .systemverilog_hir.expressions().size()) {
                return std::nullopt;
            }
            const auto view = specialization.find_expression(candidate);
            if (!view || view->systemverilog == nullptr) {
                return std::nullopt;
            }
            const auto& source = *view->systemverilog;
            if (source.kind == semantic::sv::ExpressionKind::integer_literal
                || source.kind
                    == semantic::sv::ExpressionKind::logic_literal
                || source.kind
                    == semantic::sv::ExpressionKind::boolean_literal) {
                return literal_identity(source.text);
            }
            if (source.kind != semantic::sv::ExpressionKind::name) {
                return std::nullopt;
            }
            const auto resolver
                = semantic::CompiledDesignResolver { specialization };
            const auto selected = resolver.resolve_expression_name(
                                              candidate, { }, true)
                                      .unique();
            const auto& actuals
                = specialization.specialization().actual_identities;
            const auto actual = std::ranges::find_if(
                actuals, [&](const auto& item) {
                    return selected && item.declaration == *selected;
                });
            if (actual != actuals.end()
                && actual->identity.starts_with("svconst-v3:")) {
                return actual->identity;
            }
            const auto declaration = selected
                ? specialization.find_declaration(*selected)
                : std::nullopt;
            return declaration
                    && declaration->systemverilog != nullptr
                    && declaration->systemverilog->initializer
                ? resolve(
                      *declaration->systemverilog->initializer, depth + 1U)
                : std::nullopt;
        };
        if (const auto identity = resolve(expression, 0U)) {
            return identity;
        }
        return integral
            ? std::optional<std::string> {
                  canonical_specparam_integral_identity(*integral) }
            : std::nullopt;
    }

    frontend::VerilogSpecifyEdge frontend_specify_edge(
        const semantic::sv::SpecifyEdge edge)
    {
        using Input = semantic::sv::SpecifyEdge;
        using Output = frontend::VerilogSpecifyEdge;
        switch (edge) {
        case Input::none:
            return Output::None;
        case Input::positive:
            return Output::Posedge;
        case Input::negative:
            return Output::Negedge;
        case Input::any:
            return Output::Edge;
        }
        return Output::None;
    }

    frontend::VerilogModulePathKind frontend_module_path_kind(
        const semantic::sv::ModulePathKind kind)
    {
        return kind == semantic::sv::ModulePathKind::full
            ? frontend::VerilogModulePathKind::Full
            : frontend::VerilogModulePathKind::Parallel;
    }

    frontend::VerilogPathPolarity frontend_path_polarity(
        const semantic::sv::PathPolarity polarity)
    {
        using Input = semantic::sv::PathPolarity;
        using Output = frontend::VerilogPathPolarity;
        switch (polarity) {
        case Input::none:
            return Output::None;
        case Input::positive:
            return Output::Positive;
        case Input::negative:
            return Output::Negative;
        }
        return Output::None;
    }

    frontend::VerilogPulseStyle frontend_pulse_style(
        const semantic::sv::PulseStyle style)
    {
        return style == semantic::sv::PulseStyle::ondetect
            ? frontend::VerilogPulseStyle::Ondetect
            : frontend::VerilogPulseStyle::Onevent;
    }

    runtime::simir::ModuleTimingCheckKind runtime_timing_check_kind(
        const semantic::sv::TimingCheckKind kind)
    {
        using Input = semantic::sv::TimingCheckKind;
        using Output = runtime::simir::ModuleTimingCheckKind;
        switch (kind) {
        case Input::setup:
            return Output::setup;
        case Input::hold:
            return Output::hold;
        case Input::setup_hold:
            return Output::setuphold;
        case Input::recovery:
            return Output::recovery;
        case Input::removal:
            return Output::removal;
        case Input::recovery_removal:
            return Output::recrem;
        case Input::skew:
            return Output::skew;
        case Input::time_skew:
            return Output::timeskew;
        case Input::full_skew:
            return Output::fullskew;
        case Input::period:
            return Output::period;
        case Input::width:
            return Output::width;
        case Input::no_change:
            return Output::nochange;
        }
        return Output::setup;
    }

    runtime::simir::ModulePathEdge runtime_specify_edge(
        const semantic::sv::SpecifyEdge edge)
    {
        using Input = semantic::sv::SpecifyEdge;
        using Output = runtime::simir::ModulePathEdge;
        switch (edge) {
        case Input::none:
            return Output::none;
        case Input::positive:
            return Output::posedge;
        case Input::negative:
            return Output::negedge;
        case Input::any:
            return Output::edge;
        }
        return Output::none;
    }

} // namespace

std::optional<VerilogSpecifyTerminalInfo>
HierarchyBuilder::resolve_verilog_specify_selection(
    const semantic::ExpressionId expression,
    const runtime::simir::SignalId signal,
    const SignalInfo& info,
    const semantic::SpecializedHirUnit& specialization) const
{
    if (info.width == 0U
        || info.width > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto view = specialization.find_expression(expression);
    if (!view || view->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *view->systemverilog;
    using Kind = semantic::sv::ExpressionKind;
    if (source.kind == Kind::name) {
        return VerilogSpecifyTerminalInfo {
            signal, 0U, static_cast<std::uint32_t>(info.width)
        };
    }
    if (source.kind == Kind::index && source.operands.size() == 2U) {
        const auto index = specialization.evaluate_integral_expression(
            source.operands[1]);
        const auto offset = index
            ? packed_index_offset(*index, info)
            : std::nullopt;
        if (!offset
            || *offset > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return VerilogSpecifyTerminalInfo {
            signal, static_cast<std::uint32_t>(*offset), 1U
        };
    }
    if (source.kind != Kind::slice || source.operands.size() != 3U) {
        return std::nullopt;
    }
    const auto first = specialization.evaluate_integral_expression(
        source.operands[1]);
    const auto second = specialization.evaluate_integral_expression(
        source.operands[2]);
    if (!first || !second) {
        return std::nullopt;
    }
    auto left = *first;
    auto right = *second;
    if (source.text == "+:" || source.text == "-:") {
        if (*second <= 0) {
            return std::nullopt;
        }
        const auto width = static_cast<std::uint64_t>(*second);
        if (width - 1U
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        const auto delta = static_cast<std::int64_t>(width - 1U);
        if ((source.text == "+:"
                && left > std::numeric_limits<std::int64_t>::max() - delta)
            || (source.text == "-:"
                && left < std::numeric_limits<std::int64_t>::min() + delta)) {
            return std::nullopt;
        }
        right = source.text == "+:" ? left + delta : left - delta;
    } else if (source.text != ":") {
        return std::nullopt;
    }
    const auto left_offset = packed_index_offset(left, info);
    const auto right_offset = packed_index_offset(right, info);
    if (!left_offset || !right_offset) {
        return std::nullopt;
    }
    const auto low_offset = std::min(*left_offset, *right_offset);
    const auto high_offset = std::max(*left_offset, *right_offset);
    const auto width = high_offset - low_offset + 1U;
    if (low_offset > std::numeric_limits<std::uint32_t>::max()
        || width > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return VerilogSpecifyTerminalInfo { signal,
        static_cast<std::uint32_t>(low_offset),
        static_cast<std::uint32_t>(width) };
}

void HierarchyBuilder::activate_compiled_systemverilog_defparams(
    const std::span<const semantic::sv::Defparam> defparams,
    const std::string_view hierarchy_prefix,
    const semantic::SpecializedHirUnit& specialization)
{
    if (compiled_ == nullptr) {
        return;
    }
    const auto owner = compiled_->find_unit(specialization.unit());
    const auto owner_name = owner && owner->systemverilog != nullptr
        ? std::string_view { owner->systemverilog->name }
        : std::string_view { };
    for (const auto& defparam : defparams) {
        std::vector<std::string> segments;
        segments.reserve(defparam.path.size());
        bool valid = true;
        for (const auto& segment : defparam.path) {
            auto selected = segment.name;
            for (const auto index_expression : segment.indices) {
                const auto index
                    = specialization.evaluate_integral_expression(
                        index_expression);
                if (!index) {
                    report("FSIM-ELAB-DEFPARAM-001",
                        "cannot resolve defparam hierarchy index in '"
                            + segment.name + "'",
                        compiled_source_span(
                            *compiled_, segment.source));
                    valid = false;
                    break;
                }
                selected += "[" + std::to_string(*index) + "]";
            }
            if (!valid) {
                break;
            }
            segments.push_back(std::move(selected));
        }
        if (!valid) {
            continue;
        }
        if (segments.size() > 2U && !owner_name.empty()
            && segments.front() == owner_name) {
            segments.erase(segments.begin());
        }
        if (segments.size() < 2U) {
            report("FSIM-ELAB-DEFPARAM-001",
                "cannot resolve defparam hierarchy path",
                compiled_source_span(*compiled_, defparam.source));
            continue;
        }
        auto target_path = std::string { hierarchy_prefix };
        for (std::size_t index = 0U;
            index + 1U < segments.size(); ++index) {
            if (!target_path.empty()) {
                target_path += '.';
            }
            target_path += segments[index];
        }
        active_compiled_systemverilog_defparams_.push_back({
            std::move(target_path),
            std::move(segments.back()),
            defparam.value,
            defparam.source,
            &specialization,
            false,
        });
    }
}

void HierarchyBuilder::validate_verilog_specify(
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialization,
    const std::string& path,
    const SignalMap& signals,
    SpecializationInfo& specialization_info)
{
    if (unit.timing.empty()) {
        return;
    }
    if (compiled_ == nullptr) {
        report("FSIM-ELAB-HIR-001",
            "compiled specify timing requires a compiled design owner",
            { });
        return;
    }
    const auto source = [&](const semantic::SourceSpanId span) {
        return compiled_source_span(*compiled_, span);
    };
    std::unordered_map<std::string, const semantic::sv::Declaration*>
        declarations;
    for (const auto declaration_id : unit.declarations) {
        const auto declaration = specialization.find_declaration(
            declaration_id);
        if (declaration && declaration->systemverilog != nullptr) {
            declarations.try_emplace(
                declaration->systemverilog->name,
                declaration->systemverilog);
        }
    }

    auto identities
        = specialization.specialization().hierarchy_identities;
    auto working = specialization.with_hierarchy_identities(identities);
    std::unordered_map<std::string, std::int64_t> specparam_values;
    const auto bind_specparam = [&](const std::string& name,
                                    std::string identity,
                                    const std::optional<std::int64_t> value) {
        const auto existing = std::ranges::find(
            identities, name,
            &semantic::SpecializedHirNamedIdentity::name);
        if (existing == identities.end()) {
            identities.push_back({ name, std::move(identity) });
        } else {
            existing->identity = std::move(identity);
        }
        if (value) {
            specparam_values.insert_or_assign(name, *value);
        }
        working = specialization.with_hierarchy_identities(identities);
    };
    struct StaticSpecparamValue {
        std::string identity;
        std::optional<std::int64_t> integral;
    };
    const auto validate_specparam_expression = [&](
                                                   const semantic::ExpressionId
                                                       expression,
                                                   const semantic::SourceSpanId
                                                       expression_source,
                                                   const std::string_view
                                                       description)
        -> std::optional<StaticSpecparamValue> {
        const auto integral
            = working.evaluate_integral_expression(expression);
        const auto identity = compiled_specparam_identity(
            expression, working, integral);
        if (!identity) {
            report("FSIM-ELAB-SVSPEC-001",
                std::string { description }
                    + " is not a locally static integral expression",
                source(expression_source));
            return std::nullopt;
        }
        return StaticSpecparamValue { *identity, integral };
    };
    for (const auto& block : unit.timing) {
        for (const auto& specparam : block.specparams) {
            const auto value = validate_specparam_expression(
                specparam.value, specparam.source,
                "specparam '" + specparam.name + "'");
            if (value) {
                bind_specparam(specparam.name, value->identity,
                    value->integral);
                specialization_info.parameter_identity_values.emplace_back(
                    "@specparam:" + specparam.name,
                    value->identity);
            }
            const auto validate_alternative = [&](const auto alternative) {
                if (alternative) {
                    static_cast<void>(validate_specparam_expression(
                        *alternative, specparam.source,
                        "specparam alternative"));
                }
            };
            validate_alternative(specparam.minimum);
            validate_alternative(specparam.typical);
            validate_alternative(specparam.maximum);
            validate_alternative(specparam.path_pulse_error_limit);
        }
    }

    const auto declaration = [&](const std::string_view name)
        -> const semantic::sv::Declaration* {
        const auto found = declarations.find(std::string { name });
        return found == declarations.end() ? nullptr : found->second;
    };
    const auto expression_source = [&](
                                       const semantic::ExpressionId
                                           expression,
                                       const semantic::SourceSpanId fallback) {
        const auto view = working.find_expression(expression);
        return view && view->systemverilog != nullptr
            ? view->systemverilog->source
            : fallback;
    };
    const auto resolve = [&](const semantic::ExpressionId expression,
                             const std::string_view role,
                             const bool require_input,
                             const bool require_output,
                             const bool require_port = false)
        -> std::optional<VerilogSpecifyTerminalInfo> {
        const auto base_id = compiled_terminal_base(expression, working);
        const auto base = base_id
            ? working.find_expression(*base_id)
            : std::optional<semantic::CompiledExpressionView> { };
        if (!base || base->systemverilog == nullptr
            || base->systemverilog->kind
                != semantic::sv::ExpressionKind::name) {
            report("FSIM-ELAB-SVSPEC-002",
                "specify " + std::string { role } + " at '" + path
                    + "' is not a static module terminal",
                source(base && base->systemverilog != nullptr
                        ? base->systemverilog->source
                        : semantic::SourceSpanId { }));
            return std::nullopt;
        }
        const auto& name = base->systemverilog->text;
        const auto signal = signals.find(name);
        const auto* declared = declaration(name);
        if (signal == signals.end() || declared == nullptr) {
            report("FSIM-ELAB-SVSPEC-002",
                "specify " + std::string { role } + " '" + name
                    + "' was not found in module instance '" + path + "'",
                source(base->systemverilog->source));
            return std::nullopt;
        }
        const bool input = declared->direction
                == semantic::sv::Direction::input
            || declared->direction == semantic::sv::Direction::inout;
        const bool output = declared->direction
                == semantic::sv::Direction::output
            || declared->direction == semantic::sv::Direction::inout;
        const bool port = input || output;
        if ((require_input && !input) || (require_output && !output)
            || (require_port && !port)) {
            report("FSIM-ELAB-SVSPEC-003",
                "specify " + std::string { role } + " '" + name
                    + "' has an incompatible module-port direction at '"
                    + path + "'",
                source(base->systemverilog->source));
            return std::nullopt;
        }
        const auto selection = resolve_verilog_specify_selection(
            expression, signal->second,
            design_.signal_info_.at(signal->second), working);
        if (!selection) {
            report("FSIM-ELAB-SVSPEC-004",
                "specify " + std::string { role } + " '" + name
                    + "' does not have a static nonzero packed width",
                source(base->systemverilog->source));
        }
        return selection;
    };
    const auto static_timing_value = [&](
                                         const semantic::ExpressionId
                                             expression) {
        const auto view = working.find_expression(expression);
        if (view && view->systemverilog != nullptr
            && view->systemverilog->kind
                == semantic::sv::ExpressionKind::name) {
            const auto named = specparam_values.find(
                view->systemverilog->text);
            if (named != specparam_values.end()) {
                return std::optional<std::int64_t> { named->second };
            }
            if (view->systemverilog->referenced_name) {
                const auto spelled = specparam_values.find(
                    view->systemverilog->referenced_name->spelling);
                if (spelled != specparam_values.end()) {
                    return std::optional<std::int64_t> { spelled->second };
                }
            }
        }
        return working.evaluate_integral_expression(expression);
    };
    const auto simulation_time = [&](
                                      const semantic::sv::DelayValue& delay,
                                      const semantic::SourceSpanId location,
                                      const std::string_view code,
                                      const std::string_view role,
                                      const std::optional<semantic::ExpressionId>
                                          expression_override = std::nullopt)
        -> std::optional<runtime::SimulationTick> {
        auto ticks = delay.magnitude;
        const auto expression = expression_override
            ? expression_override
            : delay.expression;
        if (expression) {
            const auto value = static_timing_value(*expression);
            const bool overflow = !value || *value < 0
                || delay.divisor == 0U
                || (delay.magnitude != 0U
                    && static_cast<std::uint64_t>(*value)
                        > std::numeric_limits<std::uint64_t>::max()
                            / delay.magnitude);
            if (overflow) {
                report(std::string { code },
                    std::string { role } + " at '" + path
                        + "' is not a static nonnegative simulation time",
                    source(location));
                return std::nullopt;
            }
            ticks = static_cast<std::uint64_t>(*value)
                * delay.magnitude;
        }
        if (delay.divisor != 1U || !delay.unit.empty()) {
            report(std::string { code },
                std::string { role } + " at '" + path
                    + "' was not normalized to project ticks",
                source(location));
            return std::nullopt;
        }
        return ticks;
    };
    const auto pulse_limit = [&](
                                 const semantic::sv::Delay& delay,
                                 const semantic::SourceSpanId location,
                                 const std::optional<semantic::ExpressionId>
                                     expression_override = std::nullopt) {
        return simulation_time(delay.primary, location,
            "FSIM-ELAB-SVSPEC-011", "PATHPULSE limit",
            expression_override);
    };
    const auto timing_limit = [&](
                                  const semantic::sv::Delay& delay,
                                  const semantic::SourceSpanId location)
        -> std::optional<std::int64_t> {
        const auto& value = delay.primary;
        if (value.divisor != 1U || !value.unit.empty()) {
            report("FSIM-ELAB-SVSPEC-016",
                "timing-check limit at '" + path
                    + "' was not normalized to project ticks",
                source(location));
            return std::nullopt;
        }
        if (!value.expression) {
            if (value.magnitude
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                report("FSIM-ELAB-SVSPEC-016",
                    "timing-check limit at '" + path
                        + "' overflows signed simulation time",
                    source(location));
                return std::nullopt;
            }
            return static_cast<std::int64_t>(value.magnitude);
        }
        const auto evaluated = static_timing_value(*value.expression);
        if (!evaluated
            || value.magnitude
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            report("FSIM-ELAB-SVSPEC-016",
                "timing-check limit at '" + path
                    + "' is not a static representable simulation time",
                source(location));
            return std::nullopt;
        }
        const auto magnitude = static_cast<std::int64_t>(value.magnitude);
        const bool overflow = magnitude != 0
            && ((*evaluated > 0
                    && *evaluated
                        > std::numeric_limits<std::int64_t>::max()
                            / magnitude)
                || (*evaluated < 0
                    && *evaluated
                        < std::numeric_limits<std::int64_t>::min()
                            / magnitude));
        if (overflow) {
            report("FSIM-ELAB-SVSPEC-016",
                "timing-check limit at '" + path
                    + "' is not a static representable simulation time",
                source(location));
            return std::nullopt;
        }
        return *evaluated * magnitude;
    };

    std::size_t path_ordinal { };
    std::size_t timing_check_ordinal { };
    for (const auto& block : unit.timing) {
        const auto block_path_begin
            = design_.verilog_specify_paths_.size();
        for (const auto& module_path : block.module_paths) {
            VerilogSpecifyPathInfo normalized;
            normalized.id = static_cast<VerilogSpecifyPathId>(
                design_.verilog_specify_paths_.size());
            if (static_cast<std::size_t>(normalized.id)
                != design_.verilog_specify_paths_.size()) {
                throw std::length_error { "too many Verilog specify paths" };
            }
            normalized.identity = "sdf:iopath:" + path + ":"
                + std::to_string(path_ordinal++);
            normalized.instance = path;
            normalized.kind = frontend_module_path_kind(module_path.kind);
            normalized.source_edge
                = frontend_specify_edge(module_path.source_edge);
            normalized.polarity
                = frontend_path_polarity(module_path.polarity);
            normalized.conditional = module_path.conditional;
            normalized.ifnone = module_path.ifnone;
            normalized.source = source(module_path.source);
            bool valid_programs = true;
            if (module_path.conditional && module_path.condition) {
                const auto program = compile_verilog_specify_expression(
                    *module_path.condition, signals, working,
                    "path condition");
                if (program) {
                    normalized.condition_program = *program;
                } else {
                    valid_programs = false;
                }
            } else if (module_path.conditional) {
                report("FSIM-ELAB-SVSPEC-008",
                    "specify path condition is missing",
                    source(module_path.source));
                valid_programs = false;
            }
            if (module_path.destination_data_source) {
                const auto program = compile_verilog_specify_expression(
                    *module_path.destination_data_source,
                    signals, working, "destination data source");
                if (program) {
                    normalized.data_source_program = *program;
                } else {
                    valid_programs = false;
                }
            }
            for (const auto terminal : module_path.sources) {
                if (const auto selection = resolve(
                        terminal, "path source", true, false)) {
                    normalized.sources.push_back(*selection);
                }
            }
            for (const auto terminal : module_path.destinations) {
                if (const auto selection = resolve(
                        terminal, "path destination", false, true)) {
                    normalized.destinations.push_back(*selection);
                }
            }
            if (!normalized.data_source_program.empty()) {
                const auto width = normalized.data_source_program.nodes.at(
                    normalized.data_source_program.root).width;
                if (std::ranges::any_of(normalized.destinations,
                        [&](const VerilogSpecifyTerminalInfo& destination) {
                            return width != 1U
                                && width != destination.width;
                        })) {
                    report("FSIM-ELAB-SVSPEC-009",
                        "specify destination data source at '" + path
                            + "' must be scalar or match every destination width",
                        source(expression_source(
                            *module_path.destination_data_source,
                            module_path.source)));
                    valid_programs = false;
                }
            }
            if (module_path.source_edge
                    != semantic::sv::SpecifyEdge::none
                && std::ranges::any_of(normalized.sources,
                    [](const VerilogSpecifyTerminalInfo& terminal) {
                        return terminal.width != 1U;
                    })) {
                report("FSIM-ELAB-SVSPEC-010",
                    "edge-sensitive specify path at '" + path
                        + "' requires scalar source terminals",
                    source(module_path.source));
                valid_programs = false;
            }
            bool valid_parallel = true;
            if (module_path.kind == semantic::sv::ModulePathKind::parallel
                && (normalized.sources.size()
                        != module_path.sources.size()
                    || normalized.destinations.size()
                        != module_path.destinations.size()
                    || !std::ranges::equal(normalized.sources,
                        normalized.destinations,
                        [](const auto& left, const auto& right) {
                            return left.width == right.width;
                        }))) {
                report("FSIM-ELAB-SVSPEC-005",
                    "parallel specify path at '" + path
                        + "' requires pairwise equal source and destination terminal widths",
                    source(module_path.source));
                valid_parallel = false;
            }
            const bool valid_delay_count = module_path.delays.size() == 1U
                || module_path.delays.size() == 2U
                || module_path.delays.size() == 3U
                || module_path.delays.size() == 6U
                || module_path.delays.size() == 12U;
            if (!valid_delay_count) {
                report("FSIM-ELAB-SVSPEC-006",
                    "specify path at '" + path
                        + "' requires 1, 2, 3, 6, or 12 transition delays",
                    source(module_path.source));
            }
            for (const auto& delay : module_path.delays) {
                const auto ticks = simulation_time(delay.primary,
                    delay.primary.source, "FSIM-ELAB-SVSPEC-007",
                    "specify path delay");
                if (ticks) {
                    normalized.delays.push_back(*ticks);
                }
            }
            if (normalized.sources.size() == module_path.sources.size()
                && normalized.destinations.size()
                    == module_path.destinations.size()
                && valid_delay_count && valid_programs && valid_parallel
                && normalized.delays.size() == module_path.delays.size()) {
                normalized.selection_group = normalized.id;
                if (module_path.conditional || module_path.ifnone) {
                    auto candidates = std::span {
                        design_.verilog_specify_paths_ }
                                          .subspan(block_path_begin);
                    const auto prior = std::ranges::find_if(candidates,
                        [&](const VerilogSpecifyPathInfo& candidate) {
                            return (candidate.conditional || candidate.ifnone)
                                && candidate.kind == normalized.kind
                                && candidate.source_edge
                                    == normalized.source_edge
                                && candidate.sources == normalized.sources
                                && candidate.destinations
                                    == normalized.destinations;
                        });
                    if (prior != candidates.end()) {
                        normalized.selection_group = prior->selection_group;
                    }
                }
                design_.verilog_specify_paths_.push_back(
                    std::move(normalized));
            }
        }

        for (const auto& pulse : block.pulse_declarations) {
            std::vector<VerilogSpecifyTerminalInfo> terminals;
            for (const auto terminal : pulse.terminals) {
                if (const auto selected = resolve(
                        terminal, "pulse terminal", false, true)) {
                    terminals.push_back(*selected);
                }
            }
            const auto overlaps = [](
                                      const VerilogSpecifyTerminalInfo& left,
                                      const VerilogSpecifyTerminalInfo& right) {
                if (left.signal != right.signal) {
                    return false;
                }
                const auto left_end
                    = static_cast<std::uint64_t>(left.offset) + left.width;
                const auto right_end
                    = static_cast<std::uint64_t>(right.offset) + right.width;
                return left.offset < right_end && right.offset < left_end;
            };
            for (auto& path_info : std::span {
                     design_.verilog_specify_paths_ }
                     .subspan(block_path_begin)) {
                if (std::ranges::none_of(path_info.destinations,
                        [&](const VerilogSpecifyTerminalInfo& destination) {
                            return std::ranges::any_of(terminals,
                                [&](const VerilogSpecifyTerminalInfo& terminal) {
                                    return overlaps(destination, terminal);
                                });
                        })) {
                    continue;
                }
                if (pulse.controls_style) {
                    path_info.pulse_style
                        = frontend_pulse_style(pulse.style);
                } else {
                    path_info.show_cancelled = pulse.show_cancelled;
                }
            }
        }

        for (std::size_t specificity = 0U; specificity <= 2U;
            ++specificity) {
            for (const auto& specparam : block.specparams) {
                if (!specparam.path_pulse
                    || static_cast<std::size_t>(
                           !specparam.path_pulse_input.empty())
                            + static_cast<std::size_t>(
                                !specparam.path_pulse_output.empty())
                        != specificity
                    || !specparam.path_pulse_reject_delay) {
                    continue;
                }
                const auto reject = pulse_limit(
                    *specparam.path_pulse_reject_delay,
                    specparam.source, specparam.value);
                const auto error = specparam.path_pulse_error_delay
                    ? pulse_limit(*specparam.path_pulse_error_delay,
                          specparam.source,
                          specparam.path_pulse_error_limit)
                    : reject;
                if (!reject || !error) {
                    continue;
                }
                if (*reject > *error) {
                    report("FSIM-ELAB-SVSPEC-012",
                        "PATHPULSE rejection limit at '" + path
                            + "' cannot exceed its error limit",
                        source(specparam.source));
                    continue;
                }
                const auto input = specparam.path_pulse_input.empty()
                    ? signals.end()
                    : signals.find(specparam.path_pulse_input);
                const auto output = specparam.path_pulse_output.empty()
                    ? signals.end()
                    : signals.find(specparam.path_pulse_output);
                if ((!specparam.path_pulse_input.empty()
                        && input == signals.end())
                    || (!specparam.path_pulse_output.empty()
                        && output == signals.end())) {
                    report("FSIM-ELAB-SVSPEC-013",
                        "PATHPULSE terminal selector at '" + path
                            + "' does not name module signals",
                        source(specparam.source));
                    continue;
                }
                for (auto& path_info : std::span {
                         design_.verilog_specify_paths_ }
                         .subspan(block_path_begin)) {
                    if (input != signals.end()
                        && std::ranges::none_of(path_info.sources,
                            [&](const VerilogSpecifyTerminalInfo& terminal) {
                                return terminal.signal == input->second;
                            })) {
                        continue;
                    }
                    if (output != signals.end()
                        && std::ranges::none_of(path_info.destinations,
                            [&](const VerilogSpecifyTerminalInfo& terminal) {
                                return terminal.signal == output->second;
                            })) {
                        continue;
                    }
                    path_info.pulse_reject_limit = *reject;
                    path_info.pulse_error_limit = *error;
                }
            }
        }

        for (const auto& check : block.timing_checks) {
            runtime::simir::ModuleTimingCheck normalized;
            normalized.id = static_cast<std::uint32_t>(
                design_.verilog_timing_checks_.size());
            if (static_cast<std::size_t>(normalized.id)
                != design_.verilog_timing_checks_.size()) {
                throw std::length_error {
                    "too many Verilog timing checks"
                };
            }
            normalized.identity = "sdf:timingcheck:" + path + ":"
                + std::to_string(timing_check_ordinal++);
            const auto normalize_event = [&](
                                             const semantic::sv::TimingCheckEvent&
                                                 event,
                                             const std::string_view role)
                -> std::optional<runtime::simir::ModuleTimingEvent> {
                const auto terminal = resolve(
                    event.expression, role, false, false, true);
                if (!terminal || terminal->width != 1U) {
                    if (terminal) {
                        report("FSIM-ELAB-SVSPEC-014",
                            "specify " + std::string { role } + " at '"
                                + path + "' must be scalar",
                            source(event.source));
                    }
                    return std::nullopt;
                }
                runtime::simir::ModuleTimingEvent result;
                result.terminal = { terminal->signal, terminal->offset,
                    terminal->width };
                result.edge = runtime_specify_edge(event.edge);
                result.edge_descriptors = event.edge_descriptors;
                if (event.edge == semantic::sv::SpecifyEdge::any) {
                    static constexpr std::array valid_descriptors {
                        std::string_view { "01" },
                        std::string_view { "10" },
                        std::string_view { "0x" },
                        std::string_view { "x1" },
                        std::string_view { "1x" },
                        std::string_view { "x0" },
                        std::string_view { "0z" },
                        std::string_view { "z1" },
                        std::string_view { "1z" },
                        std::string_view { "z0" },
                        std::string_view { "xz" },
                        std::string_view { "zx" },
                    };
                    const bool descriptors_valid
                        = !result.edge_descriptors.empty()
                        && std::ranges::all_of(result.edge_descriptors,
                            [&](std::string descriptor) {
                                std::ranges::transform(descriptor,
                                    descriptor.begin(),
                                    [](const unsigned char character) {
                                        return static_cast<char>(
                                            std::tolower(character));
                                    });
                                return std::ranges::find(
                                           valid_descriptors, descriptor)
                                    != valid_descriptors.end();
                            });
                    if (!descriptors_valid) {
                        report("FSIM-ELAB-SVSPEC-019",
                            "timing-check edge descriptor at '" + path
                                + "' is not an IEEE 1364 scalar transition",
                            source(event.source));
                        return std::nullopt;
                    }
                }
                if (event.condition) {
                    const auto condition
                        = compile_verilog_specify_expression(
                            *event.condition, signals, working,
                            "timing-event condition");
                    if (!condition) {
                        return std::nullopt;
                    }
                    result.condition = *condition;
                }
                return result;
            };
            const auto reference = normalize_event(
                check.reference_event, "timing-check reference event");
            const auto data = check.data_event
                ? normalize_event(
                      *check.data_event, "timing-check data event")
                : std::optional<runtime::simir::ModuleTimingEvent> { };
            if (!reference || (check.data_event && !data)) {
                continue;
            }
            normalized.reference = *reference;
            normalized.data = data;
            normalized.kind = runtime_timing_check_kind(check.kind);
            bool valid = check.normalized_limits.size()
                == check.limits.size();
            const bool controlled_reference = check.reference_event.edge
                != semantic::sv::SpecifyEdge::none;
            if ((normalized.kind
                        == runtime::simir::ModuleTimingCheckKind::period
                    || normalized.kind
                        == runtime::simir::ModuleTimingCheckKind::width)
                && !controlled_reference) {
                report("FSIM-ELAB-SVSPEC-019",
                    "controlled timing check at '" + path
                        + "' requires an edge-qualified reference event",
                    source(check.reference_event.source));
                valid = false;
            }
            for (const auto& limit : check.normalized_limits) {
                const auto value = timing_limit(
                    limit, limit.primary.source);
                if (value) {
                    normalized.limits.push_back(*value);
                } else {
                    valid = false;
                }
            }
            const bool signed_limits = normalized.kind
                    == runtime::simir::ModuleTimingCheckKind::setuphold
                || normalized.kind
                    == runtime::simir::ModuleTimingCheckKind::recrem
                || normalized.kind
                    == runtime::simir::ModuleTimingCheckKind::nochange;
            if (!signed_limits
                && std::ranges::any_of(normalized.limits,
                    [](const std::int64_t limit) { return limit < 0; })) {
                report("FSIM-ELAB-SVSPEC-016",
                    "timing-check limit at '" + path
                        + "' must be nonnegative for this check",
                    source(check.source));
                valid = false;
            }
            if ((normalized.kind
                        == runtime::simir::ModuleTimingCheckKind::setuphold
                    || normalized.kind
                        == runtime::simir::ModuleTimingCheckKind::recrem)
                && normalized.limits.size() == 2U) {
                const auto first = normalized.limits[0];
                const auto second = normalized.limits[1];
                const bool overflow = (second > 0
                                          && first
                                              > std::numeric_limits<std::int64_t>::max()
                                                  - second)
                    || (second < 0
                        && first
                            < std::numeric_limits<std::int64_t>::min()
                                - second);
                if (overflow || first + second <= 0) {
                    report("FSIM-ELAB-SVSPEC-016",
                        "combined timing-check limits at '" + path
                            + "' must have a positive representable sum",
                        source(check.source));
                    valid = false;
                }
            }
            if (normalized.kind
                    == runtime::simir::ModuleTimingCheckKind::nochange
                && normalized.limits.size() == 2U
                && normalized.limits[0] > normalized.limits[1]) {
                report("FSIM-ELAB-SVSPEC-016",
                    "$nochange offsets at '" + path
                        + "' are not in ascending order",
                    source(check.source));
                valid = false;
            }
            if (check.normalized_threshold) {
                const auto threshold = timing_limit(
                    *check.normalized_threshold,
                    check.normalized_threshold->primary.source);
                if (!threshold || *threshold < 0) {
                    valid = false;
                } else {
                    normalized.threshold
                        = static_cast<runtime::SimulationTick>(*threshold);
                }
            }
            const auto optional_signal = [&](
                                             const std::optional<
                                                 semantic::ExpressionId>
                                                 expression)
                -> std::optional<runtime::simir::SignalId> {
                if (!expression) {
                    return std::nullopt;
                }
                const auto view = working.find_expression(*expression);
                if (!view || view->systemverilog == nullptr
                    || view->systemverilog->kind
                        != semantic::sv::ExpressionKind::name) {
                    return std::nullopt;
                }
                const auto found = signals.find(
                    view->systemverilog->text);
                return found == signals.end()
                        || design_.signal_info_.at(found->second).width != 1U
                    ? std::nullopt
                    : std::optional<runtime::simir::SignalId> {
                          found->second };
            };
            if (check.notifier) {
                normalized.notifier = optional_signal(check.notifier);
                const auto view = working.find_expression(*check.notifier);
                const auto* notifier_declaration
                    = view && view->systemverilog != nullptr
                        ? declaration(view->systemverilog->text)
                        : nullptr;
                if (!normalized.notifier
                    || notifier_declaration == nullptr
                    || (notifier_declaration->type
                        && !notifier_declaration->type
                                ->systemverilog_net_type.empty())) {
                    report("FSIM-ELAB-SVSPEC-015",
                        "timing-check notifier at '" + path
                            + "' is not a scalar variable",
                        source(view && view->systemverilog != nullptr
                                ? view->systemverilog->source
                                : check.source));
                    valid = false;
                }
            }
            const auto optional_condition = [&](
                                                const std::optional<
                                                    semantic::ExpressionId>
                                                    expression,
                                                const std::string_view role)
                -> std::optional<runtime::simir::ModulePathExpression> {
                if (!expression) {
                    return runtime::simir::ModulePathExpression { };
                }
                return compile_verilog_specify_expression(
                    *expression, signals, working, role);
            };
            if (check.timestamp_condition) {
                const auto condition = optional_condition(
                    check.timestamp_condition, "timestamp condition");
                if (condition) {
                    normalized.timestamp_condition = *condition;
                } else {
                    valid = false;
                }
            }
            if (check.timecheck_condition) {
                const auto condition = optional_condition(
                    check.timecheck_condition, "timecheck condition");
                if (condition) {
                    normalized.timecheck_condition = *condition;
                } else {
                    valid = false;
                }
            }
            const auto delayed_terminal = [&](
                                              const semantic::ExpressionId expression,
                                              const std::string_view role)
                -> std::optional<runtime::simir::ModulePathTerminal> {
                const auto terminal = resolve(
                    expression, role, false, false);
                const auto view = working.find_expression(expression);
                if (!terminal || terminal->width != 1U) {
                    if (terminal) {
                        report("FSIM-ELAB-SVSPEC-017",
                            "specify " + std::string { role } + " at '"
                                + path + "' must be scalar",
                            source(view && view->systemverilog != nullptr
                                    ? view->systemverilog->source
                                    : check.source));
                    }
                    return std::nullopt;
                }
                return runtime::simir::ModulePathTerminal {
                    terminal->signal, terminal->offset, terminal->width
                };
            };
            if (check.delayed_reference) {
                normalized.delayed_reference = delayed_terminal(
                    *check.delayed_reference,
                    "delayed-reference signal");
                valid = valid
                    && normalized.delayed_reference.has_value();
            }
            if (check.delayed_data) {
                normalized.delayed_data = delayed_terminal(
                    *check.delayed_data, "delayed-data signal");
                valid = valid && normalized.delayed_data.has_value();
            }
            const auto static_flag = [&](
                                         const std::optional<semantic::ExpressionId>
                                             expression,
                                         const std::string_view role,
                                         bool& destination) {
                if (!expression) {
                    return;
                }
                const auto value = working.evaluate_integral_expression(
                    *expression);
                if (!value) {
                    const auto view = working.find_expression(*expression);
                    report("FSIM-ELAB-SVSPEC-018",
                        "timing-check " + std::string { role } + " at '"
                            + path + "' is not a static two-state value",
                        source(view && view->systemverilog != nullptr
                                ? view->systemverilog->source
                                : check.source));
                    valid = false;
                    return;
                }
                destination = *value != 0;
            };
            static_flag(check.event_based_flag,
                "event-based flag", normalized.event_based);
            static_flag(check.remain_active_flag,
                "remain-active flag", normalized.remain_active);
            const auto check_source = source(check.source);
            normalized.source = runtime::simir::SourceLocation {
                check_source.source_name.str(),
                static_cast<std::uint32_t>(check_source.begin.line),
                static_cast<std::uint32_t>(check_source.begin.column)
            };
            if (valid) {
                design_.verilog_timing_checks_.push_back(
                    std::move(normalized));
            }
        }
    }
}

void HierarchyBuilder::attach_verilog_specify_drivers(
    const std::size_t first_path,
    const std::span<const runtime::simir::ProcessId> processes)
{
    const auto regions_overlap = [](
                                     const Process::DriverRegion& region,
                                     const VerilogSpecifyTerminalInfo& terminal) {
        if (region.signal != terminal.signal) {
            return false;
        }
        if (region.whole) {
            return true;
        }
        const auto region_end
            = static_cast<std::uint64_t>(region.offset) + region.width;
        const auto terminal_end
            = static_cast<std::uint64_t>(terminal.offset) + terminal.width;
        return region.offset < terminal_end
            && terminal.offset < region_end;
    };
    for (auto path_index = first_path;
        path_index < design_.verilog_specify_paths_.size();
        ++path_index) {
        auto& specify_path = design_.verilog_specify_paths_[path_index];
        for (const auto process_id : processes) {
            const auto& process = design_.processes_.at(process_id);
            if (std::ranges::any_of(process.driver_regions,
                    [&](const auto& region) {
                        return std::ranges::any_of(
                            specify_path.destinations,
                            [&](const auto& terminal) {
                                return regions_overlap(region, terminal);
                            });
                    })) {
                specify_path.drivers.push_back(process_id);
            }
        }
    }
}

} // namespace fsim::elaboration
