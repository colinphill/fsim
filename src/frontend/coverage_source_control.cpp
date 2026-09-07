// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_source_control.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <utility>

namespace fsim::frontend {
namespace {

constexpr std::size_t kMetricCount = 13U;

struct ParsedDirective {
    CoverageSourceControlAction action { CoverageSourceControlAction::Off };
    CoverageSourceMetric metric { CoverageSourceMetric::All };
    std::string reason;
};

struct ActiveExclusion {
    std::size_t begin_offset { };
    std::string reason;
    std::uint64_t line { };
};

std::string_view trim_left(std::string_view value) noexcept
{
    while (!value.empty()
        && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1U);
    }
    return value;
}

std::string_view trim(std::string_view value) noexcept
{
    value = trim_left(value);
    while (!value.empty()
        && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1U);
    }
    return value;
}

std::optional<CoverageSourceMetric> parse_metric(
    const std::string_view value) noexcept
{
    if (value == "all")
        return CoverageSourceMetric::All;
    if (value == "statement")
        return CoverageSourceMetric::Statement;
    if (value == "branch")
        return CoverageSourceMetric::Branch;
    if (value == "line")
        return CoverageSourceMetric::Line;
    if (value == "condition")
        return CoverageSourceMetric::Condition;
    if (value == "expression")
        return CoverageSourceMetric::Expression;
    if (value == "toggle")
        return CoverageSourceMetric::Toggle;
    if (value == "fsm_state")
        return CoverageSourceMetric::FsmState;
    if (value == "fsm_transition")
        return CoverageSourceMetric::FsmTransition;
    if (value == "coverpoint")
        return CoverageSourceMetric::SystemVerilogCoverpoint;
    if (value == "cross")
        return CoverageSourceMetric::SystemVerilogCross;
    if (value == "psl_directive")
        return CoverageSourceMetric::PslDirective;
    if (value == "psl_property")
        return CoverageSourceMetric::PslProperty;
    return std::nullopt;
}

std::string_view take_word(std::string_view& input) noexcept
{
    input = trim_left(input);
    const auto end = input.find_first_of(" \t\r\n");
    const auto word = input.substr(0U, end);
    input = end == std::string_view::npos
        ? std::string_view { }
        : input.substr(end);
    return word;
}

CoverageSourceControlError parse_directive(
    std::string_view input,
    ParsedDirective& result,
    const CoverageSourceControlLimits limits,
    std::size_t& total_reason_bytes)
{
    if (take_word(input) != "fsim" || take_word(input) != "coverage")
        return CoverageSourceControlError::MalformedDirective;
    const auto action = take_word(input);
    if (action == "off") {
        result.action = CoverageSourceControlAction::Off;
    } else if (action == "on") {
        result.action = CoverageSourceControlAction::On;
    } else {
        return CoverageSourceControlError::MalformedDirective;
    }

    std::optional<CoverageSourceMetric> metric;
    std::optional<std::string> reason;
    while (!(input = trim_left(input)).empty()) {
        const auto equal = input.find('=');
        if (equal == std::string_view::npos)
            return CoverageSourceControlError::MalformedDirective;
        const auto key = trim(input.substr(0U, equal));
        if (key.empty() || key.find_first_of(" \t\r\n") != std::string_view::npos)
            return CoverageSourceControlError::MalformedDirective;
        input.remove_prefix(equal + 1U);
        if (key == "reason") {
            if (reason || input.empty() || input.front() != '"')
                return CoverageSourceControlError::MalformedDirective;
            input.remove_prefix(1U);
            std::string decoded;
            bool closed { };
            while (!input.empty()) {
                const auto character = input.front();
                input.remove_prefix(1U);
                if (character == '"') {
                    closed = true;
                    break;
                }
                if (character == '\\') {
                    if (input.empty()
                        || (input.front() != '\\' && input.front() != '"')) {
                        return CoverageSourceControlError::MalformedDirective;
                    }
                    decoded.push_back(input.front());
                    input.remove_prefix(1U);
                } else {
                    if (static_cast<unsigned char>(character) < 0x20U)
                        return CoverageSourceControlError::MalformedDirective;
                    decoded.push_back(character);
                }
                if (decoded.size() > limits.maximum_reason_bytes)
                    return CoverageSourceControlError::ResourceLimit;
            }
            if (!closed)
                return CoverageSourceControlError::MalformedDirective;
            reason = std::move(decoded);
        } else {
            const auto value = take_word(input);
            if (key != "metric" || metric || value.empty())
                return CoverageSourceControlError::MalformedDirective;
            metric = parse_metric(value);
            if (!metric)
                return CoverageSourceControlError::UnknownMetric;
        }
    }
    if (!metric)
        return CoverageSourceControlError::MalformedDirective;
    result.metric = *metric;
    if (result.action == CoverageSourceControlAction::Off) {
        if (!reason || reason->empty())
            return CoverageSourceControlError::MissingReason;
        if (total_reason_bytes > limits.maximum_total_reason_bytes
            || reason->size()
                > limits.maximum_total_reason_bytes - total_reason_bytes) {
            return CoverageSourceControlError::ResourceLimit;
        }
        total_reason_bytes += reason->size();
        result.reason = std::move(*reason);
    } else if (reason) {
        return CoverageSourceControlError::UnexpectedReason;
    }
    return CoverageSourceControlError::None;
}

std::optional<std::size_t> line_comment_offset(
    const std::string_view line,
    const Language language,
    bool& in_verilog_block_comment) noexcept
{
    bool in_string { };
    for (std::size_t index = 0U; index < line.size(); ++index) {
        if (language != Language::Vhdl2008 && in_verilog_block_comment) {
            const auto close = line.find("*/", index);
            if (close == std::string_view::npos)
                return std::nullopt;
            in_verilog_block_comment = false;
            index = close + 1U;
            continue;
        }
        const auto character = line[index];
        if (in_string) {
            if (language == Language::Vhdl2008 && character == '"'
                && index + 1U < line.size() && line[index + 1U] == '"') {
                ++index;
            } else if (language != Language::Vhdl2008 && character == '\\') {
                ++index;
            } else if (character == '"') {
                in_string = false;
            }
            continue;
        }
        if (character == '"') {
            in_string = true;
            continue;
        }
        if (language == Language::Vhdl2008) {
            if (character == '-' && index + 1U < line.size()
                && line[index + 1U] == '-') {
                return index;
            }
            continue;
        }
        if (character == '/' && index + 1U < line.size()) {
            if (line[index + 1U] == '/')
                return index;
            if (line[index + 1U] == '*') {
                in_verilog_block_comment = true;
                ++index;
            }
        }
    }
    return std::nullopt;
}

} // namespace

CoverageSourceControlResult parse_coverage_source_controls(
    const std::string_view source,
    const Language language,
    const CoverageSourceControlLimits limits) noexcept
{
    CoverageSourceControlResult result;
    const auto reject = [&](const CoverageSourceControlError error,
                            const std::size_t offset,
                            const std::uint64_t line) {
        result.directives.clear();
        result.exclusions.clear();
        result.error = error;
        result.error_offset = offset;
        result.error_line = line;
        return result;
    };
    try {
        if (language != Language::Verilog2005
            && language != Language::SystemVerilog2017
            && language != Language::Vhdl2008) {
            return reject(CoverageSourceControlError::InvalidLanguage, 0U, 0U);
        }
        if (source.size() > limits.maximum_source_bytes)
            return reject(CoverageSourceControlError::ResourceLimit, 0U, 1U);

        std::array<std::optional<ActiveExclusion>, kMetricCount> active;
        std::size_t total_reason_bytes { };
        std::size_t line_begin { };
        std::uint64_t line_number = 1U;
        bool in_verilog_block_comment { };
        while (line_begin <= source.size()) {
            const auto newline = source.find('\n', line_begin);
            const auto line_end = newline == std::string_view::npos
                ? source.size()
                : newline;
            auto line = source.substr(line_begin, line_end - line_begin);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1U);
            if (line.size() > limits.maximum_line_bytes) {
                return reject(CoverageSourceControlError::ResourceLimit,
                    line_begin, line_number);
            }
            const auto comment = line_comment_offset(
                line, language, in_verilog_block_comment);
            if (comment) {
                auto payload = trim_left(line.substr(*comment + 2U));
                constexpr std::string_view prefix = "fsim coverage";
                const auto is_control = payload.starts_with(prefix)
                    && (payload.size() == prefix.size()
                        || std::isspace(static_cast<unsigned char>(
                               payload[prefix.size()]))
                            != 0);
                if (is_control) {
                    if (result.directives.size()
                        >= limits.maximum_directives) {
                        return reject(CoverageSourceControlError::ResourceLimit,
                            line_begin + *comment, line_number);
                    }
                    ParsedDirective parsed;
                    const auto error = parse_directive(
                        payload, parsed, limits, total_reason_bytes);
                    if (error != CoverageSourceControlError::None) {
                        return reject(error, line_begin + *comment, line_number);
                    }
                    const auto metric_index
                        = static_cast<std::size_t>(parsed.metric);
                    if (metric_index >= active.size()) {
                        return reject(CoverageSourceControlError::UnknownMetric,
                            line_begin + *comment, line_number);
                    }
                    if (parsed.action == CoverageSourceControlAction::Off) {
                        if ((parsed.metric == CoverageSourceMetric::All
                                && std::ranges::any_of(active,
                                    [](const auto& item) {
                                        return item.has_value();
                                    }))
                            || (parsed.metric != CoverageSourceMetric::All
                                && active[0U])) {
                            return reject(
                                CoverageSourceControlError::ConflictingAllMetric,
                                line_begin + *comment, line_number);
                        }
                        if (active[metric_index]) {
                            return reject(CoverageSourceControlError::DuplicateOff,
                                line_begin + *comment, line_number);
                        }
                        const auto next = newline == std::string_view::npos
                            ? source.size()
                            : newline + 1U;
                        active[metric_index] = ActiveExclusion {
                            next, parsed.reason, line_number
                        };
                    } else {
                        if (!active[metric_index]) {
                            return reject(CoverageSourceControlError::UnmatchedOn,
                                line_begin + *comment, line_number);
                        }
                        const auto& opened = *active[metric_index];
                        result.exclusions.push_back({ parsed.metric,
                            opened.begin_offset, line_begin + *comment,
                            opened.reason, opened.line, line_number });
                        active[metric_index].reset();
                    }
                    result.directives.push_back({ parsed.action, parsed.metric,
                        std::move(parsed.reason), line_begin + *comment,
                        line_number });
                }
            }
            if (newline == std::string_view::npos)
                break;
            line_begin = newline + 1U;
            ++line_number;
        }
        for (std::size_t index = 0U; index < active.size(); ++index) {
            if (!active[index])
                continue;
            result.exclusions.push_back({
                static_cast<CoverageSourceMetric>(index),
                active[index]->begin_offset, source.size(),
                std::move(active[index]->reason), active[index]->line, 0U });
        }
        std::ranges::sort(result.exclusions, [](const auto& lhs, const auto& rhs) {
            if (lhs.begin_offset != rhs.begin_offset)
                return lhs.begin_offset < rhs.begin_offset;
            if (lhs.end_offset != rhs.end_offset)
                return lhs.end_offset < rhs.end_offset;
            return lhs.metric < rhs.metric;
        });
        return result;
    } catch (...) {
        return reject(CoverageSourceControlError::ResourceLimit, 0U, 0U);
    }
}

const CoverageSourceExclusion* coverage_source_exclusion_at(
    const std::span<const CoverageSourceExclusion> exclusions,
    const CoverageSourceMetric metric,
    const std::size_t source_offset) noexcept
{
    const auto found = std::ranges::find_if(exclusions, [&](const auto& item) {
        return (item.metric == CoverageSourceMetric::All
                   || item.metric == metric)
            && source_offset >= item.begin_offset
            && source_offset < item.end_offset;
    });
    return found == exclusions.end() ? nullptr : &*found;
}

std::string_view coverage_source_metric_name(
    const CoverageSourceMetric metric) noexcept
{
    switch (metric) {
    case CoverageSourceMetric::All:
        return "all";
    case CoverageSourceMetric::Statement:
        return "statement";
    case CoverageSourceMetric::Branch:
        return "branch";
    case CoverageSourceMetric::Line:
        return "line";
    case CoverageSourceMetric::Condition:
        return "condition";
    case CoverageSourceMetric::Expression:
        return "expression";
    case CoverageSourceMetric::Toggle:
        return "toggle";
    case CoverageSourceMetric::FsmState:
        return "fsm_state";
    case CoverageSourceMetric::FsmTransition:
        return "fsm_transition";
    case CoverageSourceMetric::SystemVerilogCoverpoint:
        return "coverpoint";
    case CoverageSourceMetric::SystemVerilogCross:
        return "cross";
    case CoverageSourceMetric::PslDirective:
        return "psl_directive";
    case CoverageSourceMetric::PslProperty:
        return "psl_property";
    }
    return "invalid";
}

std::optional<CoverageSourceMetric> coverage_source_metric_from_name(
    const std::string_view name) noexcept
{
    return parse_metric(name);
}

} // namespace fsim::frontend
