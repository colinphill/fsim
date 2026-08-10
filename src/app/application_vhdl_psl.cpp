// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <charconv>
#include <unordered_map>

namespace fsim::app::application_detail {
namespace {

    using runtime::VhdlPslAttemptOutcome;
    using runtime::VhdlPslClockedSample;
    using runtime::VhdlPslSampleValues;
    using runtime::VhdlPslTruth;
    using semantic::vhdl::PslDeclaration;
    using semantic::vhdl::PslDeclarationKind;

    [[nodiscard]] runtime::VhdlPslDirectiveKind directive_kind(
        const semantic::vhdl::PslDirectiveKind kind) noexcept
    {
        switch (kind) {
        case semantic::vhdl::PslDirectiveKind::assume:
            return runtime::VhdlPslDirectiveKind::assumption;
        case semantic::vhdl::PslDirectiveKind::restrict:
            return runtime::VhdlPslDirectiveKind::restriction;
        case semantic::vhdl::PslDirectiveKind::cover:
            return runtime::VhdlPslDirectiveKind::cover;
        case semantic::vhdl::PslDirectiveKind::assert_directive:
            return runtime::VhdlPslDirectiveKind::assertion;
        }
        return runtime::VhdlPslDirectiveKind::assertion;
    }

    [[nodiscard]] ConcurrentAssertionCoverageKind coverage_kind(
        const runtime::VhdlPslDirectiveKind kind) noexcept
    {
        switch (kind) {
        case runtime::VhdlPslDirectiveKind::assumption:
            return ConcurrentAssertionCoverageKind::assumption;
        case runtime::VhdlPslDirectiveKind::restriction:
            return ConcurrentAssertionCoverageKind::restriction;
        case runtime::VhdlPslDirectiveKind::cover:
            return ConcurrentAssertionCoverageKind::cover;
        case runtime::VhdlPslDirectiveKind::assertion:
            return ConcurrentAssertionCoverageKind::assertion;
        }
        return ConcurrentAssertionCoverageKind::assertion;
    }

    [[nodiscard]] std::string canonical(const std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (const auto character : text) {
            result.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character))));
        }
        return result;
    }

    [[nodiscard]] bool opening(const std::string_view token)
    {
        return token == "(" || token == "[" || token == "{";
    }

    [[nodiscard]] bool closing(const std::string_view token)
    {
        return token == ")" || token == "]" || token == "}";
    }

    [[nodiscard]] std::span<const std::string> trim(
        std::span<const std::string> tokens)
    {
        while (!tokens.empty() && tokens.back() == ";") {
            tokens = tokens.first(tokens.size() - 1U);
        }
        bool changed = true;
        while (changed && tokens.size() >= 2U) {
            changed = false;
            const auto pair = (tokens.front() == "(" && tokens.back() == ")")
                || (tokens.front() == "{" && tokens.back() == "}");
            if (!pair) {
                break;
            }
            std::size_t depth { };
            bool owns_all = true;
            for (std::size_t index = 0U; index < tokens.size(); ++index) {
                if (opening(tokens[index])) {
                    ++depth;
                } else if (closing(tokens[index])) {
                    --depth;
                    if (depth == 0U && index + 1U != tokens.size()) {
                        owns_all = false;
                        break;
                    }
                }
            }
            if (owns_all) {
                tokens = tokens.subspan(1U, tokens.size() - 2U);
                changed = true;
            }
        }
        return tokens;
    }

    [[nodiscard]] std::optional<std::size_t> top_level_word(
        const std::span<const std::string> tokens, const std::string_view word)
    {
        std::size_t depth { };
        for (std::size_t index = 0U; index < tokens.size(); ++index) {
            if (opening(tokens[index])) {
                ++depth;
            } else if (closing(tokens[index])) {
                if (depth != 0U) {
                    --depth;
                }
            } else if (depth == 0U && canonical(tokens[index]) == word) {
                return index;
            }
        }
        return std::nullopt;
    }

    struct SequenceResult {
        std::vector<std::size_t> matches;
        bool pending { };
    };

    struct UnitModel {
        std::unordered_map<std::string, PslDeclaration> declarations;
    };

    [[nodiscard]] bool weak_property(const UnitModel& unit,
        std::span<const std::string> tokens, const std::size_t depth = 0U)
    {
        if (depth > 256U) {
            return false;
        }
        tokens = trim(tokens);
        if (tokens.empty()) {
            return false;
        }
        if (canonical(tokens.front()) == "weak") {
            return true;
        }
        if (tokens.size() == 1U) {
            const auto found = unit.declarations.find(canonical(tokens.front()));
            return found != unit.declarations.end()
                && found->second.kind == PslDeclarationKind::property
                && weak_property(
                    unit, found->second.body_tokens, depth + 1U);
        }
        return false;
    }

    class Evaluator {
    public:
        Evaluator(const UnitModel& unit,
            const std::span<const VhdlPslClockedSample> samples,
            const VhdlPslSampleValues& current, const bool edge,
            const bool end_of_run, const std::size_t maximum_work_steps,
            std::string value_prefix = { })
            : unit_(unit)
            , samples_(samples)
            , current_(current)
            , edge_(edge)
            , end_of_run_(end_of_run)
            , maximum_work_steps_(maximum_work_steps)
            , value_prefix_(std::move(value_prefix))
        {
        }

        [[nodiscard]] VhdlPslTruth clock(
            const std::span<const std::string> tokens) const
        {
            return boolean(tokens, current_, 0U)
                ? VhdlPslTruth::true_value
                : VhdlPslTruth::false_value;
        }

        [[nodiscard]] VhdlPslAttemptOutcome property(
            std::span<const std::string> tokens, const std::size_t start,
            const std::size_t depth = 0U) const
        {
            work(tokens.size() + 1U);
            if (depth > 256U) {
                throw std::runtime_error(
                    "VHDL PSL executable declaration nesting exceeds 256");
            }
            tokens = trim(tokens);
            if (tokens.empty()) {
                return VhdlPslAttemptOutcome::failure;
            }
            if (const auto abort = top_level_word(tokens, "async_abort")) {
                if (boolean(tokens.subspan(*abort + 1U), current_, depth + 1U)) {
                    return VhdlPslAttemptOutcome::aborted;
                }
                return property(tokens.first(*abort), start, depth + 1U);
            }
            if (const auto abort = top_level_word(tokens, "sync_abort")) {
                if (edge_
                    && boolean(tokens.subspan(*abort + 1U), current_, depth + 1U)) {
                    return VhdlPslAttemptOutcome::aborted;
                }
                return property(tokens.first(*abort), start, depth + 1U);
            }
            if (const auto abort = top_level_word(tokens, "abort")) {
                if (edge_
                    && boolean(tokens.subspan(*abort + 1U), current_, depth + 1U)) {
                    return VhdlPslAttemptOutcome::aborted;
                }
                return property(tokens.first(*abort), start, depth + 1U);
            }
            if (const auto implication = implication_at(tokens)) {
                const auto antecedent = sequence(
                    tokens.first(implication->position), start, depth + 1U);
                if (antecedent.matches.empty()) {
                    return antecedent.pending
                        ? VhdlPslAttemptOutcome::pending
                        : VhdlPslAttemptOutcome::vacuous;
                }
                bool pending { antecedent.pending };
                for (const auto match : antecedent.matches) {
                    const auto consequent_start
                        = match + (implication->nonoverlapped ? 1U : 0U);
                    const auto outcome = property(tokens.subspan(
                                                      implication->position
                                                      + implication->width),
                        consequent_start, depth + 1U);
                    if (outcome == VhdlPslAttemptOutcome::failure
                        || outcome == VhdlPslAttemptOutcome::aborted) {
                        return outcome;
                    }
                    pending |= outcome == VhdlPslAttemptOutcome::pending;
                }
                return pending ? VhdlPslAttemptOutcome::pending
                               : VhdlPslAttemptOutcome::pass;
            }
            const auto first = canonical(tokens.front());
            if (first == "strong" || first == "weak") {
                return property(tokens.subspan(1U), start, depth + 1U);
            }
            if (first == "next" || first == "next_a" || first == "next_e") {
                const auto [delay, operand] = prefix_range(tokens, 1U, depth);
                if (first == "next_a" || first == "next_e") {
                    const auto available = start < samples_.size()
                        ? samples_.size() - start - 1U
                        : 0U;
                    const auto maximum = std::min(delay.second, available);
                    bool pending { delay.second > available };
                    for (std::size_t offset = delay.first;
                        offset <= maximum; ++offset) {
                        const auto outcome = offset
                                > std::numeric_limits<std::size_t>::max() - start
                            ? VhdlPslAttemptOutcome::pending
                            : property(operand, start + offset, depth + 1U);
                        if (first == "next_a"
                            && outcome == VhdlPslAttemptOutcome::failure) {
                            return outcome;
                        }
                        if (first == "next_e"
                            && outcome == VhdlPslAttemptOutcome::pass) {
                            return outcome;
                        }
                        pending |= outcome == VhdlPslAttemptOutcome::pending;
                        if (offset
                            == std::numeric_limits<std::size_t>::max()) {
                            break;
                        }
                    }
                    if (pending) {
                        return VhdlPslAttemptOutcome::pending;
                    }
                    return first == "next_a" ? VhdlPslAttemptOutcome::pass
                                             : VhdlPslAttemptOutcome::failure;
                }
                if (delay.first
                    > std::numeric_limits<std::size_t>::max() - start) {
                    return VhdlPslAttemptOutcome::pending;
                }
                return property(operand, start + delay.first, depth + 1U);
            }
            if (first == "prev") {
                return start == 0U
                    ? VhdlPslAttemptOutcome::failure
                    : property(tokens.subspan(1U), start - 1U, depth + 1U);
            }
            if (first == "eventually") {
                const auto ranged = tokens.size() > 1U && tokens[1] == "[";
                const auto parsed = ranged
                    ? prefix_range(tokens, 1U, depth)
                    : std::pair { std::pair { std::size_t { 0U },
                                      std::numeric_limits<std::size_t>::max() },
                          tokens.subspan(1U) };
                const auto& range = parsed.first;
                const auto operand = parsed.second;
                const auto available = start < samples_.size()
                    ? samples_.size() - start - 1U
                    : 0U;
                const auto maximum = range.second
                        == std::numeric_limits<std::size_t>::max()
                    ? available
                    : std::min(range.second, available);
                for (std::size_t delay = range.first; delay <= maximum; ++delay) {
                    const auto outcome = property(
                        operand, start + delay, depth + 1U);
                    if (outcome == VhdlPslAttemptOutcome::pass) {
                        return outcome;
                    }
                    if (delay == std::numeric_limits<std::size_t>::max()) {
                        break;
                    }
                }
                const auto open_horizon = range.second
                    == std::numeric_limits<std::size_t>::max();
                const auto horizon = open_horizon
                    ? std::numeric_limits<std::size_t>::max()
                    : range.second
                        > std::numeric_limits<std::size_t>::max() - start
                    ? std::numeric_limits<std::size_t>::max()
                    : start + range.second;
                return !end_of_run_
                        && (open_horizon || samples_.size() <= horizon)
                    ? VhdlPslAttemptOutcome::pending
                    : VhdlPslAttemptOutcome::failure;
            }
            if (first == "always") {
                const auto operand = tokens.subspan(1U);
                for (std::size_t sample = start; sample < samples_.size();
                    ++sample) {
                    if (property(operand, sample, depth + 1U)
                        == VhdlPslAttemptOutcome::failure) {
                        return VhdlPslAttemptOutcome::failure;
                    }
                }
                return end_of_run_ ? VhdlPslAttemptOutcome::pass
                                   : VhdlPslAttemptOutcome::pending;
            }
            if (const auto until = top_level_word(tokens, "until")) {
                for (std::size_t sample = start; sample < samples_.size();
                    ++sample) {
                    if (boolean_at(tokens.subspan(*until + 1U), sample,
                            depth + 1U)) {
                        return VhdlPslAttemptOutcome::pass;
                    }
                    if (!boolean_at(tokens.first(*until), sample, depth + 1U)) {
                        return VhdlPslAttemptOutcome::failure;
                    }
                }
                return end_of_run_ ? VhdlPslAttemptOutcome::failure
                                   : VhdlPslAttemptOutcome::pending;
            }
            if (const auto before = top_level_word(tokens, "before")) {
                for (std::size_t sample = start; sample < samples_.size();
                    ++sample) {
                    if (property(tokens.first(*before), sample, depth + 1U)
                        == VhdlPslAttemptOutcome::pass) {
                        return VhdlPslAttemptOutcome::pass;
                    }
                    if (property(tokens.subspan(*before + 1U), sample,
                            depth + 1U)
                        == VhdlPslAttemptOutcome::pass) {
                        return VhdlPslAttemptOutcome::failure;
                    }
                }
                return end_of_run_ ? VhdlPslAttemptOutcome::failure
                                   : VhdlPslAttemptOutcome::pending;
            }
            if (const auto specialized = specialized_declaration(tokens)) {
                if (specialized->declaration->kind
                    == PslDeclarationKind::property) {
                    return property(specialized->body, start, depth + 1U);
                }
                if (specialized->declaration->kind
                        == PslDeclarationKind::sequence
                    || specialized->declaration->kind
                        == PslDeclarationKind::endpoint) {
                    const auto result = sequence(
                        specialized->body, start, depth + 1U);
                    return !result.matches.empty()
                        ? VhdlPslAttemptOutcome::pass
                        : result.pending ? VhdlPslAttemptOutcome::pending
                                         : VhdlPslAttemptOutcome::failure;
                }
            }
            if (start >= samples_.size()) {
                return VhdlPslAttemptOutcome::pending;
            }
            return boolean_at(tokens, start, depth + 1U)
                ? VhdlPslAttemptOutcome::pass
                : VhdlPslAttemptOutcome::failure;
        }

    private:
        void work(const std::size_t steps) const
        {
            if (steps > maximum_work_steps_ - work_steps_) {
                throw runtime::VhdlPslResourceError {
                    runtime::VhdlPslResourceKind::temporal_steps,
                    maximum_work_steps_
                };
            }
            work_steps_ += steps;
        }

        struct Implication {
            std::size_t position { };
            std::size_t width { };
            bool nonoverlapped { };
        };

        struct Repetition {
            enum class Kind : std::uint8_t {
                consecutive,
                nonconsecutive,
                goto_repetition,
            };

            std::size_t left { };
            std::size_t right { };
            std::size_t range_begin { };
            Kind kind { Kind::consecutive };
        };

        struct SpecializedDeclaration {
            const PslDeclaration* declaration { };
            std::vector<std::string> body;
        };

        [[nodiscard]] std::optional<SpecializedDeclaration>
        specialized_declaration(
            const std::span<const std::string> tokens) const
        {
            work(tokens.size() + 1U);
            if (tokens.empty()) {
                return std::nullopt;
            }
            const auto found = unit_.declarations.find(canonical(tokens.front()));
            if (found == unit_.declarations.end()) {
                return std::nullopt;
            }
            const auto& declaration = found->second;
            work(declaration.formals.size() + declaration.body_tokens.size());
            std::vector<std::vector<std::string>> actuals;
            if (tokens.size() != 1U) {
                if (tokens.size() < 3U || tokens[1] != "("
                    || tokens.back() != ")") {
                    return std::nullopt;
                }
                std::size_t begin = 2U;
                std::size_t depth { };
                for (std::size_t index = 2U; index + 1U < tokens.size();
                    ++index) {
                    if (opening(tokens[index])) {
                        ++depth;
                    } else if (closing(tokens[index]) && depth != 0U) {
                        --depth;
                    } else if (tokens[index] == "," && depth == 0U) {
                        actuals.emplace_back(tokens.begin()
                                + static_cast<std::ptrdiff_t>(begin),
                            tokens.begin()
                                + static_cast<std::ptrdiff_t>(index));
                        begin = index + 1U;
                    }
                }
                if (begin + 1U < tokens.size()) {
                    actuals.emplace_back(tokens.begin()
                            + static_cast<std::ptrdiff_t>(begin),
                        tokens.end() - 1);
                }
            }
            if (actuals.size() > declaration.formals.size()) {
                throw std::runtime_error(
                    "VHDL PSL declaration has too many executable actuals");
            }
            std::map<std::string, std::vector<std::string>, std::less<>> values;
            for (std::size_t index = 0U; index < declaration.formals.size();
                ++index) {
                const auto& formal = declaration.formals[index];
                if (index < actuals.size()) {
                    values.emplace(formal.name, actuals[index]);
                    continue;
                }
                const auto assignment = std::ranges::find(
                    formal.profile_tokens, std::string { ":=" });
                if (assignment != formal.profile_tokens.end()) {
                    values.emplace(formal.name,
                        std::vector<std::string> { assignment + 1,
                            formal.profile_tokens.end() });
                } else if (formal.static_default) {
                    values.emplace(formal.name,
                        std::vector<std::string> {
                            std::to_string(*formal.static_default) });
                } else {
                    throw std::runtime_error("VHDL PSL formal '" + formal.name
                        + "' requires an executable actual");
                }
            }
            std::vector<std::string> body;
            for (const auto& token : declaration.body_tokens) {
                const auto replacement = values.find(canonical(token));
                if (replacement == values.end()) {
                    body.push_back(token);
                    continue;
                }
                if (replacement->second.size() > 1U) {
                    body.push_back("(");
                }
                body.insert(body.end(), replacement->second.begin(),
                    replacement->second.end());
                if (replacement->second.size() > 1U) {
                    body.push_back(")");
                }
            }
            return SpecializedDeclaration { &declaration, std::move(body) };
        }

        [[nodiscard]] std::optional<Implication> implication_at(
            const std::span<const std::string> tokens) const
        {
            std::size_t depth { };
            for (std::size_t index = 0U; index < tokens.size(); ++index) {
                if (opening(tokens[index])) {
                    ++depth;
                    continue;
                }
                if (closing(tokens[index])) {
                    if (depth != 0U) {
                        --depth;
                    }
                    continue;
                }
                if (depth != 0U) {
                    continue;
                }
                if (tokens[index] == "|->") {
                    return Implication { index, 1U, false };
                }
                if (tokens[index] == "|=>") {
                    return Implication { index, 1U, true };
                }
                if (index + 2U < tokens.size() && tokens[index] == "|"
                    && tokens[index + 1U] == "-"
                    && tokens[index + 2U] == ">") {
                    return Implication { index, 3U, false };
                }
                if (index + 1U < tokens.size() && tokens[index] == "|"
                    && (tokens[index + 1U] == "=>"
                        || tokens[index + 1U] == "->")) {
                    return Implication { index, 2U,
                        tokens[index + 1U] == "=>" };
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Repetition> repetition_at(
            const std::span<const std::string> tokens) const
        {
            std::size_t depth { };
            for (std::size_t index = 0U; index < tokens.size(); ++index) {
                if (tokens[index] != "[") {
                    if (opening(tokens[index])) {
                        ++depth;
                    } else if (closing(tokens[index]) && depth != 0U) {
                        --depth;
                    }
                    continue;
                }
                if (depth != 0U || index + 2U >= tokens.size()) {
                    ++depth;
                    continue;
                }
                std::size_t right = index + 1U;
                std::size_t bracket_depth = 1U;
                for (; right < tokens.size(); ++right) {
                    if (tokens[right] == "[") {
                        ++bracket_depth;
                    } else if (tokens[right] == "]" && --bracket_depth == 0U) {
                        break;
                    }
                }
                if (right + 1U != tokens.size()) {
                    continue;
                }
                if (tokens[index + 1U] == "*") {
                    return Repetition { index, right, index + 2U,
                        Repetition::Kind::consecutive };
                }
                if (tokens[index + 1U] == "=") {
                    return Repetition { index, right, index + 2U,
                        Repetition::Kind::nonconsecutive };
                }
                if (index + 3U < right && tokens[index + 1U] == "-"
                    && tokens[index + 2U] == ">") {
                    return Repetition { index, right, index + 3U,
                        Repetition::Kind::goto_repetition };
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::pair<std::pair<std::size_t, std::size_t>,
            std::span<const std::string>>
        prefix_range(const std::span<const std::string> tokens,
            const std::size_t position, const std::size_t depth) const
        {
            if (position >= tokens.size() || tokens[position] != "[") {
                return { { 1U, 1U }, tokens.subspan(position) };
            }
            const auto right = std::ranges::find(
                tokens.subspan(position + 1U), std::string { "]" });
            if (right == tokens.end()) {
                return { { 1U, 1U }, tokens.subspan(position) };
            }
            const auto right_position
                = static_cast<std::size_t>(right - tokens.begin());
            const auto range = numeric_range(tokens.subspan(
                                                 position + 1U, right_position - position - 1U),
                depth);
            return { range, tokens.subspan(right_position + 1U) };
        }

        [[nodiscard]] std::pair<std::size_t, std::size_t> numeric_range(
            const std::span<const std::string> tokens,
            const std::size_t depth) const
        {
            const auto colon = std::ranges::find(tokens, std::string { ":" });
            const auto separator = colon == tokens.end()
                ? tokens.size()
                : static_cast<std::size_t>(colon - tokens.begin());
            const auto minimum = number(tokens.first(separator), depth);
            const auto maximum = separator == tokens.size()
                ? minimum
                : canonical(tokens[separator + 1U]) == "inf"
                ? std::numeric_limits<std::size_t>::max()
                : number(tokens.subspan(separator + 1U), depth);
            return { minimum, maximum };
        }

        [[nodiscard]] std::size_t number(
            const std::span<const std::string> tokens,
            const std::size_t) const
        {
            if (tokens.size() != 1U) {
                return 1U;
            }
            std::size_t value { };
            const auto parsed = std::from_chars(tokens.front().data(),
                tokens.front().data() + tokens.front().size(), value);
            if (parsed.ec == std::errc { }) {
                return value;
            }
            throw std::runtime_error(
                "VHDL PSL temporal bound was not statically specialized");
        }

        [[nodiscard]] SequenceResult sequence(
            std::span<const std::string> tokens, const std::size_t start,
            const std::size_t depth) const
        {
            work(tokens.size() + 1U);
            tokens = trim(tokens);
            if (tokens.empty()) {
                return { };
            }
            if (const auto semicolon = top_level_word(tokens, ";")) {
                return concatenate(tokens, *semicolon, start, depth, false);
            }
            if (const auto colon = top_level_word(tokens, ":")) {
                return concatenate(tokens, *colon, start, depth, true);
            }
            if (const auto within = top_level_word(tokens, "within")) {
                const auto enclosing = sequence(
                    tokens.subspan(*within + 1U), start, depth + 1U);
                SequenceResult result;
                result.pending = enclosing.pending;
                for (const auto enclosing_end : enclosing.matches) {
                    for (std::size_t candidate = start;
                        candidate <= enclosing_end; ++candidate) {
                        const auto enclosed = sequence(
                            tokens.first(*within), candidate, depth + 1U);
                        result.pending |= enclosed.pending;
                        if (std::ranges::any_of(enclosed.matches,
                                [&](const std::size_t enclosed_end) {
                                    return enclosed_end <= enclosing_end;
                                })) {
                            result.matches.push_back(enclosing_end);
                            break;
                        }
                    }
                }
                return result;
            }
            if (const auto repetition = repetition_at(tokens)) {
                return repeat(tokens.first(repetition->left),
                    tokens.subspan(repetition->range_begin,
                        repetition->right - repetition->range_begin),
                    start, depth + 1U, repetition->kind);
            }
            if (const auto specialized = specialized_declaration(tokens)) {
                if (specialized->declaration->kind
                        == PslDeclarationKind::sequence
                    || specialized->declaration->kind
                        == PslDeclarationKind::endpoint) {
                    return sequence(specialized->body, start, depth + 1U);
                }
            }
            if (start >= samples_.size()) {
                return { { }, !end_of_run_ };
            }
            return boolean_at(tokens, start, depth + 1U)
                ? SequenceResult { { start }, false }
                : SequenceResult { };
        }

        [[nodiscard]] SequenceResult repeat(
            const std::span<const std::string> operand,
            const std::span<const std::string> range_tokens,
            const std::size_t start, const std::size_t depth,
            const Repetition::Kind kind) const
        {
            const auto [minimum, requested_maximum]
                = numeric_range(range_tokens, depth + 1U);
            const auto available = start < samples_.size()
                ? samples_.size() - start
                : 0U;
            const auto maximum = requested_maximum
                    == std::numeric_limits<std::size_t>::max()
                ? available
                : std::min(requested_maximum, available);
            SequenceResult result;
            bool consecutive_can_continue { };
            std::size_t observed_count { };
            if (minimum == 0U) {
                result.matches.push_back(start);
            }
            if (kind == Repetition::Kind::consecutive) {
                std::vector<std::size_t> states { start };
                for (std::size_t count = 1U;
                    count <= maximum && !states.empty(); ++count) {
                    std::vector<std::size_t> next;
                    for (const auto position : states) {
                        const auto match = sequence(
                            operand, position, depth + 1U);
                        next.insert(next.end(), match.matches.begin(),
                            match.matches.end());
                        result.pending |= match.pending;
                    }
                    states.clear();
                    for (const auto match : next) {
                        states.push_back(match + 1U);
                        if (count >= minimum) {
                            result.matches.push_back(match);
                        }
                    }
                }
                consecutive_can_continue = !states.empty();
            } else {
                for (std::size_t position = start; position < samples_.size();
                    ++position) {
                    const auto match = sequence(
                        operand, position, depth + 1U);
                    if (!match.matches.empty()) {
                        if (observed_count
                            == std::numeric_limits<std::size_t>::max()) {
                            break;
                        }
                        ++observed_count;
                        if (observed_count > requested_maximum) {
                            break;
                        }
                        if (observed_count >= minimum) {
                            result.matches.push_back(match.matches.front());
                        }
                    } else if (kind == Repetition::Kind::nonconsecutive
                        && observed_count >= minimum
                        && observed_count <= requested_maximum) {
                        // [=] admits a trailing run of cycles where the
                        // operand does not match. [->] ends exactly on the
                        // selected occurrence and therefore does not.
                        result.matches.push_back(position);
                    }
                    result.pending |= match.pending;
                }
                if (!end_of_run_
                    && kind == Repetition::Kind::nonconsecutive
                    && observed_count >= minimum
                    && observed_count <= requested_maximum) {
                    result.pending = true;
                }
            }
            if (!end_of_run_) {
                if (kind == Repetition::Kind::consecutive) {
                    result.pending |= consecutive_can_continue
                        && maximum < requested_maximum;
                } else {
                    result.pending |= observed_count < minimum
                        || observed_count < requested_maximum;
                }
            }
            std::ranges::sort(result.matches);
            result.matches.erase(
                std::unique(result.matches.begin(), result.matches.end()),
                result.matches.end());
            return result;
        }

        [[nodiscard]] SequenceResult concatenate(
            const std::span<const std::string> tokens,
            const std::size_t separator, const std::size_t start,
            const std::size_t depth, const bool fusion) const
        {
            const auto left = sequence(tokens.first(separator), start, depth + 1U);
            SequenceResult result;
            result.pending = left.pending;
            for (const auto match : left.matches) {
                const auto right = sequence(tokens.subspan(separator + 1U),
                    match + (fusion ? 0U : 1U), depth + 1U);
                result.matches.insert(
                    result.matches.end(), right.matches.begin(), right.matches.end());
                result.pending |= right.pending;
            }
            return result;
        }

        [[nodiscard]] bool boolean_at(const std::span<const std::string> tokens,
            const std::size_t sample, const std::size_t depth) const
        {
            return sample < samples_.size()
                && boolean(tokens, *samples_[sample].values, depth + 1U);
        }

        [[nodiscard]] bool boolean(std::span<const std::string> tokens,
            const VhdlPslSampleValues& values, const std::size_t depth) const
        {
            work(tokens.size() + 1U);
            if (depth > 256U) {
                throw std::runtime_error(
                    "VHDL PSL Boolean declaration nesting exceeds 256");
            }
            tokens = trim(tokens);
            if (tokens.empty()) {
                return false;
            }
            if (const auto specialized = specialized_declaration(tokens);
                specialized && specialized->declaration->kind == PslDeclarationKind::boolean) {
                return boolean(specialized->body, values, depth + 1U);
            }
            if (const auto operation = top_level_word(tokens, "or")) {
                return boolean(tokens.first(*operation), values, depth + 1U)
                    || boolean(tokens.subspan(*operation + 1U), values,
                        depth + 1U);
            }
            if (const auto operation = top_level_word(tokens, "xor")) {
                return boolean(tokens.first(*operation), values, depth + 1U)
                    != boolean(tokens.subspan(*operation + 1U), values,
                        depth + 1U);
            }
            if (const auto operation = top_level_word(tokens, "and")) {
                return boolean(tokens.first(*operation), values, depth + 1U)
                    && boolean(tokens.subspan(*operation + 1U), values,
                        depth + 1U);
            }
            if (canonical(tokens.front()) == "not") {
                return !boolean(tokens.subspan(1U), values, depth + 1U);
            }
            if (tokens.size() == 3U
                && (tokens[1] == "=" || tokens[1] == "/=")) {
                const auto left = truth(tokens[0], values, depth + 1U);
                const auto right = truth(tokens[2], values, depth + 1U);
                const auto equal = left != VhdlPslTruth::unknown
                    && right != VhdlPslTruth::unknown && left == right;
                return tokens[1] == "=" ? equal : !equal;
            }
            return truth(tokens.front(), values, depth + 1U)
                == VhdlPslTruth::true_value;
        }

        [[nodiscard]] VhdlPslTruth truth(const std::string_view spelling,
            const VhdlPslSampleValues& values, const std::size_t depth) const
        {
            work(1U);
            const auto name = canonical(spelling);
            if (name == "true" || name == "'1'") {
                return VhdlPslTruth::true_value;
            }
            if (name == "false" || name == "'0'") {
                return VhdlPslTruth::false_value;
            }
            if (const auto found = values.find(name); found != values.end()) {
                return found->second;
            }
            if (!value_prefix_.empty()) {
                const auto qualified = value_prefix_ + "::" + name;
                if (const auto found = values.find(qualified);
                    found != values.end()) {
                    return found->second;
                }
            }
            if (const auto found = unit_.declarations.find(name);
                found != unit_.declarations.end()
                && found->second.kind == PslDeclarationKind::boolean) {
                return boolean(found->second.body_tokens, values, depth + 1U)
                    ? VhdlPslTruth::true_value
                    : VhdlPslTruth::false_value;
            }
            return VhdlPslTruth::unknown;
        }

        const UnitModel& unit_;
        std::span<const VhdlPslClockedSample> samples_;
        const VhdlPslSampleValues& current_;
        bool edge_ { };
        bool end_of_run_ { };
        std::size_t maximum_work_steps_ { };
        mutable std::size_t work_steps_ { };
        std::string value_prefix_;
    };

    [[nodiscard]] VhdlPslTruth packed_truth(const runtime::PackedLogic4& value)
    {
        if (value.empty()) {
            return VhdlPslTruth::unknown;
        }
        const auto bit = value.get(0U);
        return bit == runtime::Logic4::one
            ? VhdlPslTruth::true_value
            : bit == runtime::Logic4::zero ? VhdlPslTruth::false_value
                                           : VhdlPslTruth::unknown;
    }

    [[nodiscard]] std::optional<runtime::simir::SignalId> resolve_signal(
        const elaboration::ElaboratedDesign& design, const std::string_view name,
        const std::string_view instance = { })
    {
        if (!instance.empty()) {
            if (const auto local = design.find_signal(
                    std::string { instance } + "." + std::string { name })) {
                return local;
            }
        }
        if (const auto direct = design.find_signal(name)) {
            return direct;
        }
        std::optional<runtime::simir::SignalId> result;
        const auto suffix = "." + std::string { name };
        for (const auto& [path, signal] : design.signal_paths()) {
            if (!instance.empty()
                && !(path == instance || path.starts_with(std::string { instance } + "."))) {
                continue;
            }
            if (!path.ends_with(suffix)) {
                continue;
            }
            if (result && *result != signal) {
                return std::nullopt;
            }
            result = signal;
        }
        return result;
    }

} // namespace

struct VhdlPslExecution::Impl {
    struct SignalBinding {
        std::string name;
        runtime::simir::SignalId signal { };
    };

    struct ClockBinding {
        std::string name;
        std::vector<std::string> expression;
        std::string direct_signal;
        std::size_t model { };
        std::string value_prefix;
    };

    Impl(const semantic::vhdl::Hir& hir,
        const elaboration::ElaboratedDesign& design,
        const semantic::design::DesignIr& design_ir,
        SignalReader input_reader)
        : reader(std::move(input_reader))
    {
        models.reserve(hir.units().size());
        for (const auto& unit : hir.units()) {
            UnitModel model;
            for (const auto& declaration : unit.psl_declarations) {
                if (!declaration.name.empty()) {
                    model.declarations.emplace(declaration.name, declaration);
                }
            }
            models.push_back(std::move(model));
        }
        for (std::size_t unit_index = 0U; unit_index < hir.units().size();
            ++unit_index) {
            const auto& unit = hir.units()[unit_index];
            auto& model = models[unit_index];
            std::vector<std::string> occurrences;
            for (const auto& specialization : design_ir.specializations()) {
                if (specialization.language != semantic::Language::vhdl
                    || specialization.unit != unit.id) {
                    continue;
                }
                const auto& path = design_ir.instances()
                                       .at(specialization.instance.value())
                                       .path;
                if (std::ranges::find(occurrences, path)
                    == occurrences.end()) {
                    occurrences.push_back(path);
                }
            }
            if (occurrences.empty() && !unit.psl_directives.empty()) {
                occurrences.emplace_back();
            }
            for (const auto& occurrence : occurrences) {
                for (std::size_t index = 0U; index < unit.psl_directives.size();
                    ++index) {
                    const auto& directive = unit.psl_directives[index];
                    if (!directive.analyzed_property
                        || !directive.analyzed_property->clock) {
                        continue;
                    }
                    const auto& clock = *directive.analyzed_property->clock;
                    const auto clock_identity = occurrence.empty()
                        ? clock.canonical_identity
                        : occurrence + "::" + clock.canonical_identity;
                    bind_clock(design, clock, unit_index, occurrence);
                    for (const auto& declaration : unit.psl_declarations) {
                        if (!declaration.analyzed_expression) {
                            continue;
                        }
                        for (const auto& name :
                            declaration.analyzed_expression->sampled_names) {
                            bind_sample(design, name, occurrence);
                        }
                    }
                    for (const auto& name :
                        directive.analyzed_property->sampled_names) {
                        bind_sample(design, name, occurrence);
                    }
                    runtime::VhdlPslMonitorPlan plan;
                    plan.identity = unit.library + ":" + unit.name + ":"
                        + (directive.label.empty()
                                ? "$psl$" + std::to_string(index + 1U)
                                : directive.label)
                        + (occurrences.size() > 1U ? "@" + occurrence
                                                   : std::string { });
                    plan.instance_identity = occurrence.empty()
                        ? unit.library + ":" + unit.name
                        : occurrence;
                    plan.source_span = directive.source.value();
                    plan.slot = static_cast<std::uint32_t>(index);
                    plan.directive_kind = directive_kind(directive.kind);
                    plan.clock_identity = clock_identity;
                    plan.edge = clock_identity.starts_with("fall:")
                        ? runtime::VhdlPslClockEdge::falling
                        : runtime::VhdlPslClockEdge::rising;
                    plan.strong = !weak_property(model, directive.property_tokens);
                    const auto property_tokens = directive.property_tokens;
                    plan.evaluate = [&model, property_tokens, occurrence](
                                        const runtime::VhdlPslEvaluationContext& context) {
                        return Evaluator { model, context.samples,
                            context.current_values, context.clock_edge,
                            context.end_of_run, context.maximum_temporal_steps,
                            occurrence }
                            .property(property_tokens, context.start_sample);
                    };
                    const auto coverage_index = coverage.size();
                    plan.complete = [this, coverage_index](
                                        const runtime::VhdlPslAttemptSnapshot& attempt) {
                        auto& item = coverage.at(coverage_index);
                        ++item.attempts;
                        switch (attempt.outcome) {
                        case VhdlPslAttemptOutcome::pass:
                            ++item.passes;
                            break;
                        case VhdlPslAttemptOutcome::failure:
                            ++item.failures;
                            break;
                        case VhdlPslAttemptOutcome::vacuous:
                            ++item.vacuous;
                            break;
                        case VhdlPslAttemptOutcome::aborted:
                            ++item.aborted;
                            break;
                        case VhdlPslAttemptOutcome::pending:
                            break;
                        }
                        if (completion_hook) {
                            completion_hook(attempt);
                        }
                    };
                    engine.add_monitor(std::move(plan));
                    ConcurrentAssertionCoverage item;
                    item.name = unit.library + ":" + unit.name + ":"
                        + (directive.label.empty()
                                ? "$psl$" + std::to_string(index + 1U)
                                : directive.label)
                        + (occurrences.size() > 1U ? "@" + occurrence
                                                   : std::string { });
                    item.process = occurrence.empty()
                        ? unit.library + ":" + unit.name
                        : occurrence;
                    item.instance_identity = item.process;
                    item.kind = coverage_kind(directive_kind(directive.kind));
                    item.slot = static_cast<std::uint32_t>(index);
                    item.source_span = directive.source.value();
                    coverage.push_back(std::move(item));
                }
            }
        }
    }

    void bind_clock(const elaboration::ElaboratedDesign& design,
        const semantic::vhdl::PslClock& clock, const std::size_t model,
        const std::string_view instance)
    {
        std::string direct_signal;
        if (clock.canonical_identity.starts_with("rise:")
            || clock.canonical_identity.starts_with("fall:")) {
            direct_signal = clock.canonical_identity.substr(5U);
            bind_sample(design, direct_signal, instance);
            if (!instance.empty()) {
                direct_signal = std::string { instance } + "::" + direct_signal;
            }
        } else {
            bool sampled { };
            for (const auto& token : clock.expression_tokens) {
                const auto name = canonical(token);
                if (resolve_signal(design, name, instance)) {
                    bind_sample(design, name, instance);
                    sampled = true;
                }
            }
            if (!sampled) {
                throw std::logic_error(
                    "an executable VHDL PSL clock does not resolve uniquely");
            }
        }
        const auto identity = instance.empty()
            ? clock.canonical_identity
            : std::string { instance } + "::" + clock.canonical_identity;
        const auto existing = std::ranges::find(
            clocks, identity, &ClockBinding::name);
        if (existing == clocks.end()) {
            clocks.push_back(ClockBinding { identity, clock.expression_tokens,
                std::move(direct_signal), model, std::string { instance } });
        } else if (existing->expression != clock.expression_tokens
            || existing->direct_signal != direct_signal) {
            throw std::logic_error(
                "one PSL clock identity maps to two expressions");
        }
    }

    void bind_sample(const elaboration::ElaboratedDesign& design,
        const std::string& name, const std::string_view instance)
    {
        const auto identity = instance.empty()
            ? name
            : std::string { instance } + "::" + name;
        if (std::ranges::find(samples, identity, &SignalBinding::name)
            != samples.end()) {
            return;
        }
        const auto signal = resolve_signal(design, name, instance);
        if (!signal) {
            throw std::logic_error("executable VHDL PSL sampled name '"
                + identity + "' does not resolve uniquely");
        }
        samples.push_back(SignalBinding { identity, *signal });
    }

    runtime::VhdlPslAttemptEngine engine;
    SignalReader reader;
    std::vector<UnitModel> models;
    std::vector<ClockBinding> clocks;
    std::vector<SignalBinding> samples;
    std::vector<ConcurrentAssertionCoverage> coverage;
    runtime::VhdlPslCompletionHook completion_hook;
};

VhdlPslExecution::VhdlPslExecution(const semantic::vhdl::Hir& hir,
    const elaboration::ElaboratedDesign& design,
    const semantic::design::DesignIr& design_ir, SignalReader reader)
    : impl_(std::make_unique<Impl>(
          hir, design, design_ir, std::move(reader)))
{
}

VhdlPslExecution::~VhdlPslExecution() = default;

void VhdlPslExecution::observe(
    const runtime::SimulationTick time, const std::uint64_t delta)
{
    VhdlPslSampleValues values;
    for (const auto& binding : impl_->samples) {
        values.emplace(binding.name, packed_truth(impl_->reader(binding.signal)));
    }
    std::map<std::string, VhdlPslTruth, std::less<>> clocks;
    for (const auto& binding : impl_->clocks) {
        if (!binding.direct_signal.empty()) {
            clocks.emplace(binding.name, values.at(binding.direct_signal));
            continue;
        }
        clocks.emplace(binding.name,
            Evaluator { impl_->models.at(binding.model), { }, values, false,
                false, 4'194'304U, binding.value_prefix }
                .clock(binding.expression));
    }
    impl_->engine.observe(clocks, std::move(values), time, delta);
}

void VhdlPslExecution::finish(
    const runtime::SimulationTick time, const std::uint64_t delta)
{
    impl_->engine.finish(time, delta);
}

const std::vector<runtime::VhdlPslAttemptSnapshot>& VhdlPslExecution::attempts()
    const noexcept
{
    return impl_->engine.attempts();
}

std::vector<ConcurrentAssertionCoverage> VhdlPslExecution::coverage() const
{
    return impl_->coverage;
}

void VhdlPslExecution::set_completion_hook(runtime::VhdlPslCompletionHook hook)
{
    impl_->completion_hook = std::move(hook);
}

} // namespace fsim::app::application_detail
