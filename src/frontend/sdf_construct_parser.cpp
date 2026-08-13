// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {
namespace {

    [[nodiscard]] bool ascii_iequals(const std::string_view left,
        const std::string_view right) noexcept
    {
        if (left.size() != right.size())
            return false;
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (std::toupper(static_cast<unsigned char>(left[index]))
                != std::toupper(static_cast<unsigned char>(right[index]))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string decode_string(const std::string_view spelling)
    {
        if (spelling.size() < 2U || spelling.front() != '"'
            || spelling.back() != '"') {
            return std::string { spelling };
        }
        std::string result;
        result.reserve(spelling.size() - 2U);
        bool escaped = false;
        for (std::size_t index = 1U; index + 1U < spelling.size(); ++index) {
            const auto character = spelling[index];
            if (!escaped && character == '\\') {
                escaped = true;
            } else {
                result.push_back(character);
                escaped = false;
            }
        }
        return result;
    }

    [[nodiscard]] SdfConstructKind construct_kind(
        const std::string_view spelling) noexcept
    {
        static constexpr auto kinds = std::to_array<
            std::pair<std::string_view, SdfConstructKind>>({
            { "CELL", SdfConstructKind::Cell },
            { "CELLTYPE", SdfConstructKind::CellType },
            { "INSTANCE", SdfConstructKind::Instance },
            { "CORRELATION", SdfConstructKind::Correlation },
            { "DELAY", SdfConstructKind::Delay },
            { "ABSOLUTE", SdfConstructKind::Absolute },
            { "INCREMENT", SdfConstructKind::Increment },
            { "COND", SdfConstructKind::Conditional },
            { "CONDELSE", SdfConstructKind::ConditionalElse },
            { "IOPATH", SdfConstructKind::Iopath },
            { "RETAIN", SdfConstructKind::Retain },
            { "INTERCONNECT", SdfConstructKind::Interconnect },
            { "NETDELAY", SdfConstructKind::NetDelay },
            { "PORT", SdfConstructKind::Port },
            { "DEVICE", SdfConstructKind::Device },
            { "PATHPULSE", SdfConstructKind::PathPulse },
            { "PATHPULSEPERCENT", SdfConstructKind::PathPulsePercent },
            { "GLOBALPATHPULSE", SdfConstructKind::PathPulsePercent },
            { "MIPD", SdfConstructKind::Mipd },
            { "TIMINGCHECK", SdfConstructKind::TimingCheck },
            { "NEGATIVE", SdfConstructKind::Negative },
            { "SETUP", SdfConstructKind::Setup },
            { "HOLD", SdfConstructKind::Hold },
            { "SETUPHOLD", SdfConstructKind::SetupHold },
            { "RECOVERY", SdfConstructKind::Recovery },
            { "REMOVAL", SdfConstructKind::Removal },
            { "RECREM", SdfConstructKind::RecRem },
            { "SKEW", SdfConstructKind::Skew },
            { "BIDIRECTSKEW", SdfConstructKind::BidirectSkew },
            { "WIDTH", SdfConstructKind::Width },
            { "PERIOD", SdfConstructKind::Period },
            { "NOCHANGE", SdfConstructKind::NoChange },
            { "TIMINGENV", SdfConstructKind::TimingEnvironment },
            { "PATHCONSTRAINT", SdfConstructKind::PathConstraint },
            { "PERIODCONSTRAINT", SdfConstructKind::PeriodConstraint },
            { "SKEWCONSTRAINT", SdfConstructKind::SkewConstraint },
            { "SUM", SdfConstructKind::Sum },
            { "DIFF", SdfConstructKind::Diff },
            { "ARRIVAL", SdfConstructKind::Arrival },
            { "DEPARTURE", SdfConstructKind::Departure },
            { "SLACK", SdfConstructKind::Slack },
            { "WAVEFORM", SdfConstructKind::Waveform },
            { "EXCEPTION", SdfConstructKind::Exception },
            { "LABEL", SdfConstructKind::Label },
            { "NAME", SdfConstructKind::Name },
            { "SCOND", SdfConstructKind::StampCondition },
            { "CCOND", SdfConstructKind::CheckCondition },
            { "POSEDGE", SdfConstructKind::Edge },
            { "NEGEDGE", SdfConstructKind::Edge },
        });
        const auto found = std::ranges::find_if(kinds, [&](const auto& entry) {
            return ascii_iequals(spelling, entry.first);
        });
        return found == kinds.end() ? SdfConstructKind::Unknown : found->second;
    }

    [[nodiscard]] bool is_timing_check_kind(const SdfConstructKind kind) noexcept
    {
        switch (kind) {
        case SdfConstructKind::Setup:
        case SdfConstructKind::Hold:
        case SdfConstructKind::SetupHold:
        case SdfConstructKind::Recovery:
        case SdfConstructKind::Removal:
        case SdfConstructKind::RecRem:
        case SdfConstructKind::Skew:
        case SdfConstructKind::BidirectSkew:
        case SdfConstructKind::Width:
        case SdfConstructKind::Period:
        case SdfConstructKind::NoChange:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool is_sdf21_timing_check_kind(
        const SdfConstructKind kind) noexcept
    {
        switch (kind) {
        case SdfConstructKind::Setup:
        case SdfConstructKind::Hold:
        case SdfConstructKind::SetupHold:
        case SdfConstructKind::Recovery:
        case SdfConstructKind::Skew:
        case SdfConstructKind::Width:
        case SdfConstructKind::Period:
        case SdfConstructKind::NoChange:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool is_sdf21_constraint_kind(
        const SdfConstructKind kind) noexcept
    {
        return kind == SdfConstructKind::PathConstraint
            || kind == SdfConstructKind::Sum
            || kind == SdfConstructKind::Diff
            || kind == SdfConstructKind::SkewConstraint;
    }

    [[nodiscard]] bool is_timing_environment_kind(
        const SdfConstructKind kind) noexcept
    {
        switch (kind) {
        case SdfConstructKind::PathConstraint:
        case SdfConstructKind::PeriodConstraint:
        case SdfConstructKind::SkewConstraint:
        case SdfConstructKind::Sum:
        case SdfConstructKind::Diff:
        case SdfConstructKind::Arrival:
        case SdfConstructKind::Departure:
        case SdfConstructKind::Slack:
        case SdfConstructKind::Waveform:
        case SdfConstructKind::Exception:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool is_delay_definition_kind(
        const SdfConstructKind kind) noexcept
    {
        switch (kind) {
        case SdfConstructKind::Conditional:
        case SdfConstructKind::ConditionalElse:
        case SdfConstructKind::Iopath:
        case SdfConstructKind::Interconnect:
        case SdfConstructKind::NetDelay:
        case SdfConstructKind::Port:
        case SdfConstructKind::Device:
        case SdfConstructKind::Mipd:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool is_port_atom(const SdfSyntaxAtom& atom) noexcept
    {
        return atom.kind == SdfTokenKind::Identifier
            || atom.kind == SdfTokenKind::EscapedIdentifier;
    }

    [[nodiscard]] std::size_t child_count(const SdfSyntaxNode& node,
        const SdfConstructKind kind) noexcept
    {
        return static_cast<std::size_t>(std::ranges::count(
            node.children, kind, &SdfSyntaxNode::kind));
    }

    [[nodiscard]] std::size_t edge_prefix_size(
        const SdfSyntaxNode& node) noexcept
    {
        if (node.atoms.size() < 2U)
            return 0U;
        const auto& first = node.atoms[0].spelling;
        if (ascii_iequals(first, "01") || ascii_iequals(first, "10")
            || ascii_iequals(first, "0z") || ascii_iequals(first, "z1")
            || ascii_iequals(first, "1z") || ascii_iequals(first, "z0")) {
            return 1U;
        }
        if (node.atoms.size() >= 3U && first == "0"
            && ascii_iequals(node.atoms[1].spelling, "z")) {
            return 2U;
        }
        return 0U;
    }

    class SdfConstructParser {
    public:
        SdfConstructParser(SdfFile& file, std::vector<Diagnostic>& diagnostics)
            : file_(file)
            , diagnostics_(diagnostics)
        {
        }

        void run()
        {
            if (!file_.has_revision)
                return;
            for (const auto& form : file_.body_forms) {
                if (!ascii_iequals(form.keyword_spelling, "CELL"))
                    continue;
                auto root = parse_form(form);
                parse_cell(std::move(root));
            }
        }

    private:
        struct Frame {
            SdfSyntaxNode node;
            SourceLocation begin;
        };

        struct CellState {
            bool saw_cell_type { };
            bool saw_instance { };
            bool saw_correlation { };
            bool saw_declaration { };
            std::size_t instance_count { };
        };

        [[nodiscard]] bool is_sdf21() const noexcept
        {
            return file_.revision == SdfRevision::Sdf21;
        }

        [[nodiscard]] std::string hierarchy_divider() const
        {
            const auto* divider = file_.find_header(SdfHeaderKind::Divider);
            return divider && !divider->canonical_value.empty()
                ? divider->canonical_value
                : ".";
        }

        void diagnose(std::string code, std::string message,
            const SourceSpan& span)
        {
            diagnostics_.push_back(Diagnostic { DiagnosticSeverity::Error,
                std::move(code), std::move(message), span, { } });
        }

        void begin_frame(std::vector<Frame>& stack, std::size_t& index,
            const std::size_t end) const
        {
            const auto first = index;
            const auto begin = file_.tokens[index++].span.begin;
            while (index < end
                && file_.tokens[index].kind == SdfTokenKind::Comment) {
                ++index;
            }
            Frame frame;
            frame.node.first_token = first;
            frame.begin = begin;
            if (index >= end
                || file_.tokens[index].kind == SdfTokenKind::RightParenthesis) {
                frame.node.kind = SdfConstructKind::Value;
                stack.push_back(std::move(frame));
                return;
            }

            const auto& head = file_.tokens[index++];
            if (head.kind == SdfTokenKind::Keyword
                || head.kind == SdfTokenKind::Identifier
                || head.kind == SdfTokenKind::EscapedIdentifier) {
                frame.node.keyword_spelling = head.spelling;
                frame.node.kind = construct_kind(head.spelling);
                if (frame.node.kind == SdfConstructKind::Unknown) {
                    frame.node.atoms.push_back(
                        SdfSyntaxAtom { head.kind, head.spelling, head.span });
                }
            } else {
                frame.node.kind = SdfConstructKind::Value;
                frame.node.atoms.push_back(
                    SdfSyntaxAtom { head.kind, head.spelling, head.span });
            }
            stack.push_back(std::move(frame));
        }

        [[nodiscard]] SdfSyntaxNode parse_form(const SdfRawForm& form) const
        {
            const auto end = std::min(form.first_token + form.token_count,
                file_.tokens.size());
            std::size_t index = form.first_token;
            std::vector<Frame> stack;
            stack.reserve(std::min<std::size_t>(form.token_count / 2U + 1U, 4096U));
            if (index >= end
                || file_.tokens[index].kind != SdfTokenKind::LeftParenthesis) {
                SdfSyntaxNode invalid;
                invalid.span = form.span;
                return invalid;
            }
            begin_frame(stack, index, end);

            while (index < end && stack.size() != 0U) {
                const auto& token = file_.tokens[index];
                if (token.kind == SdfTokenKind::Comment) {
                    ++index;
                    continue;
                }
                if (token.kind == SdfTokenKind::LeftParenthesis) {
                    begin_frame(stack, index, end);
                    continue;
                }
                if (token.kind == SdfTokenKind::RightParenthesis) {
                    auto completed = std::move(stack.back().node);
                    const auto begin = stack.back().begin;
                    stack.pop_back();
                    ++index;
                    completed.token_count = index - completed.first_token;
                    completed.span = SourceSpan { file_.source_name, begin, token.span.end,
                        file_.source_name, { } };
                    if (stack.size() == 0U)
                        return completed;
                    stack.back().node.children.push_back(std::move(completed));
                    continue;
                }
                stack.back().node.atoms.push_back(
                    SdfSyntaxAtom { token.kind, token.spelling, token.span });
                ++index;
            }
            SdfSyntaxNode invalid;
            invalid.span = form.span;
            return invalid;
        }

        [[nodiscard]] bool valid_instance(const SdfSyntaxNode& node,
            SdfCell& cell)
        {
            if (!node.children.empty())
                return false;
            if (node.atoms.empty()) {
                cell.instance_kind = SdfInstanceSelectorKind::Empty;
                cell.instance_spellings.emplace_back();
                return true;
            }
            if (node.atoms.size() == 1U
                && node.atoms.front().kind == SdfTokenKind::Star) {
                cell.instance_kind = SdfInstanceSelectorKind::Wildcard;
                cell.instance_spelling = "*";
                cell.instance_spellings.push_back("*");
                return true;
            }
            if (!std::ranges::all_of(node.atoms, is_port_atom))
                return false;
            cell.instance_kind = SdfInstanceSelectorKind::Exact;
            for (const auto& atom : node.atoms)
                cell.instance_spelling += atom.spelling;
            cell.instance_spellings.push_back(cell.instance_spelling);
            return !cell.instance_spelling.empty();
        }

        void parse_cell_type(SdfSyntaxNode& child, SdfCell& cell,
            CellState& state)
        {
            if (state.saw_cell_type || state.saw_instance
                || state.saw_declaration) {
                diagnose("FSIM-SDF-PARSE-019",
                    "CELLTYPE must be the first CELL child", child.span);
            }
            state.saw_cell_type = true;
            if (child.atoms.size() != 1U || !child.children.empty()
                || child.atoms.front().kind != SdfTokenKind::String) {
                diagnose("FSIM-SDF-PARSE-012",
                    "CELLTYPE requires exactly one quoted string", child.span);
                return;
            }
            cell.cell_type_spelling = child.atoms.front().spelling;
            cell.cell_type = decode_string(cell.cell_type_spelling);
        }

        void parse_instance(SdfSyntaxNode& child, SdfCell& cell,
            CellState& state)
        {
            if (!state.saw_cell_type || state.saw_declaration
                || state.saw_correlation) {
                diagnose("FSIM-SDF-PARSE-019",
                    "INSTANCE must follow CELLTYPE and precede timing forms",
                    child.span);
            }
            SdfCell parsed;
            if (!valid_instance(child, parsed)) {
                diagnose("FSIM-SDF-PARSE-013",
                    "INSTANCE requires an empty, exact, or sole '*' selector",
                    child.span);
                return;
            }
            if (state.instance_count == 0U) {
                cell.instance_kind = parsed.instance_kind;
                cell.instance_spelling = parsed.instance_spelling;
                cell.instance_spellings = std::move(parsed.instance_spellings);
                cell.wildcard_requires_physical_primitive = is_sdf21()
                    && cell.instance_kind == SdfInstanceSelectorKind::Wildcard;
            } else if (!is_sdf21()) {
                diagnose(file_.revision == SdfRevision::Sdf30
                        ? "FSIM-SDF-30-002"
                        : "FSIM-SDF-PARSE-019",
                    "SDF 3.0 and 4.0 CELL requires exactly one INSTANCE form",
                    child.span);
            } else if (cell.instance_kind != SdfInstanceSelectorKind::Exact
                || parsed.instance_kind != SdfInstanceSelectorKind::Exact) {
                diagnose("FSIM-SDF-21-002",
                    "repeated SDF 2.1 INSTANCE forms must all be exact path segments",
                    child.span);
            } else {
                cell.instance_spelling += hierarchy_divider();
                cell.instance_spelling += parsed.instance_spelling;
                cell.instance_spellings.push_back(parsed.instance_spelling);
            }
            state.saw_instance = true;
            ++state.instance_count;
        }

        void parse_correlation(SdfSyntaxNode child, SdfCell& cell,
            CellState& state)
        {
            if (!is_sdf21()) {
                diagnose("FSIM-SDF-PARSE-014",
                    "CORRELATION is not legal in the SDF 4.0 syntax profile",
                    child.span);
            } else if (!state.saw_instance || state.saw_correlation
                || state.saw_declaration) {
                diagnose("FSIM-SDF-21-002",
                    "CORRELATION must occur once after INSTANCE forms",
                    child.span);
            }
            const bool valid_count = child.atoms.size() == 1U
                || child.atoms.size() == 2U || child.atoms.size() == 4U;
            const bool valid_values = valid_count
                && child.atoms.front().kind == SdfTokenKind::String
                && std::ranges::all_of(child.atoms | std::views::drop(1U),
                    [](const SdfSyntaxAtom& atom) {
                        return atom.kind == SdfTokenKind::Number;
                    });
            if (!child.children.empty() || !valid_values) {
                diagnose("FSIM-SDF-21-002",
                    "CORRELATION requires a string and zero, one, or three factors",
                    child.span);
            }
            state.saw_correlation = true;
            cell.declarations.push_back(std::move(child));
        }

        void parse_declaration(SdfSyntaxNode child, SdfCell& cell,
            CellState& state)
        {
            state.saw_declaration = true;
            if (!state.saw_cell_type || !state.saw_instance) {
                diagnose("FSIM-SDF-PARSE-019",
                    "CELL timing forms require preceding CELLTYPE and INSTANCE",
                    child.span);
            }
            const bool legal = child.kind == SdfConstructKind::Delay
                || child.kind == SdfConstructKind::TimingCheck
                || (!is_sdf21()
                    && (child.kind == SdfConstructKind::TimingEnvironment
                        || child.kind == SdfConstructKind::Label));
            if (!legal && !is_sdf21()) {
                diagnose("FSIM-SDF-PARSE-014",
                    "construct is not legal directly inside CELL", child.span);
            }
            cell.declarations.push_back(std::move(child));
        }

        void parse_cell(SdfSyntaxNode root)
        {
            if (root.kind != SdfConstructKind::Cell || !root.atoms.empty()) {
                diagnose("FSIM-SDF-PARSE-011",
                    "CELL form has an invalid root or direct value", root.span);
                return;
            }
            SdfCell cell;
            cell.span = root.span;
            CellState state;
            for (auto& child : root.children) {
                if (child.kind == SdfConstructKind::CellType) {
                    parse_cell_type(child, cell, state);
                } else if (child.kind == SdfConstructKind::Instance) {
                    parse_instance(child, cell, state);
                } else if (child.kind == SdfConstructKind::Correlation) {
                    parse_correlation(std::move(child), cell, state);
                } else {
                    parse_declaration(std::move(child), cell, state);
                }
            }
            if (!state.saw_cell_type || !state.saw_instance) {
                diagnose("FSIM-SDF-PARSE-011",
                    "CELL requires exactly one CELLTYPE followed by one INSTANCE",
                    root.span);
            }
            validate_declarations(cell.declarations);
            file_.cells.push_back(std::move(cell));
        }

        void normalize_contextual_children(SdfSyntaxNode& node) const
        {
            for (auto& child : node.children) {
                const bool edge_context = node.kind == SdfConstructKind::Iopath
                    || is_timing_check_kind(node.kind)
                    || node.kind == SdfConstructKind::SkewConstraint;
                const auto edge_prefix = edge_context ? edge_prefix_size(child) : 0U;
                if (edge_prefix != 0U
                    && child.atoms.size() == edge_prefix + 1U
                    && is_port_atom(child.atoms.back())) {
                    child.kind = SdfConstructKind::Edge;
                    child.keyword_spelling.clear();
                    for (std::size_t index = 0; index < edge_prefix; ++index)
                        child.keyword_spelling += child.atoms[index].spelling;
                    child.atoms.erase(child.atoms.begin(),
                        child.atoms.begin()
                            + static_cast<std::ptrdiff_t>(edge_prefix));
                } else if (node.kind == SdfConstructKind::Label
                    && (child.kind == SdfConstructKind::Unknown
                        || child.kind == SdfConstructKind::Name)) {
                    child.kind = SdfConstructKind::LabelEntry;
                } else if (child.kind == SdfConstructKind::Unknown
                    && (node.kind == SdfConstructKind::Conditional
                        || node.kind == SdfConstructKind::StampCondition
                        || node.kind == SdfConstructKind::CheckCondition
                        || node.kind == SdfConstructKind::ConditionExpression)) {
                    child.kind = SdfConstructKind::ConditionExpression;
                } else if (child.kind == SdfConstructKind::Unknown
                    && (node.kind == SdfConstructKind::Sum
                        || node.kind == SdfConstructKind::Diff)) {
                    child.kind = SdfConstructKind::ConstraintPath;
                }
            }
        }

        [[nodiscard]] bool child_allowed(const SdfConstructKind parent,
            const SdfConstructKind child) const noexcept
        {
            if (parent == SdfConstructKind::Cell) {
                return child == SdfConstructKind::Delay
                    || child == SdfConstructKind::TimingCheck
                    || (is_sdf21() && child == SdfConstructKind::Correlation)
                    || (!is_sdf21()
                        && (child == SdfConstructKind::TimingEnvironment
                            || child == SdfConstructKind::Label));
            }
            if (parent == SdfConstructKind::Delay) {
                return child == SdfConstructKind::Absolute
                    || child == SdfConstructKind::Increment
                    || child == SdfConstructKind::PathPulse
                    || child == SdfConstructKind::PathPulsePercent;
            }
            if (parent == SdfConstructKind::Absolute
                || parent == SdfConstructKind::Increment) {
                return is_delay_definition_kind(child)
                    && (!is_sdf21()
                        || child == SdfConstructKind::Iopath
                        || child == SdfConstructKind::Conditional
                        || child == SdfConstructKind::Interconnect
                        || child == SdfConstructKind::NetDelay
                        || child == SdfConstructKind::Port
                        || child == SdfConstructKind::Device);
            }
            if (parent == SdfConstructKind::Iopath) {
                return child == SdfConstructKind::Edge
                    || (!is_sdf21() && child == SdfConstructKind::Retain)
                    || child == SdfConstructKind::Value;
            }
            if (parent == SdfConstructKind::Conditional
                || parent == SdfConstructKind::ConditionalElse) {
                return child == SdfConstructKind::Iopath
                    || child == SdfConstructKind::ConditionExpression;
            }
            if (parent == SdfConstructKind::Interconnect
                || parent == SdfConstructKind::NetDelay
                || parent == SdfConstructKind::Port
                || parent == SdfConstructKind::Device
                || parent == SdfConstructKind::PathPulse
                || parent == SdfConstructKind::PathPulsePercent
                || parent == SdfConstructKind::Mipd
                || parent == SdfConstructKind::Retain) {
                return child == SdfConstructKind::Value
                    || (parent == SdfConstructKind::NetDelay
                        && child == SdfConstructKind::Instance);
            }
            if (parent == SdfConstructKind::TimingCheck) {
                return is_sdf21()
                    ? is_sdf21_timing_check_kind(child)
                        || is_sdf21_constraint_kind(child)
                    : is_timing_check_kind(child)
                        || child == SdfConstructKind::Negative;
            }
            if (parent == SdfConstructKind::Negative) {
                return is_timing_check_kind(child);
            }
            if (is_timing_check_kind(parent)) {
                return child == SdfConstructKind::Edge
                    || child == SdfConstructKind::Conditional
                    || child == SdfConstructKind::StampCondition
                    || child == SdfConstructKind::CheckCondition
                    || child == SdfConstructKind::ConditionExpression
                    || child == SdfConstructKind::Value;
            }
            if (parent == SdfConstructKind::TimingEnvironment) {
                return is_timing_environment_kind(child);
            }
            if (is_timing_environment_kind(parent)) {
                return child == SdfConstructKind::Value
                    || child == SdfConstructKind::Edge
                    || child == SdfConstructKind::Conditional
                    || child == SdfConstructKind::Exception
                    || child == SdfConstructKind::Name
                    || child == SdfConstructKind::ConstraintPath;
            }
            if (parent == SdfConstructKind::Label) {
                return child == SdfConstructKind::LabelEntry;
            }
            if (parent == SdfConstructKind::LabelEntry) {
                return child == SdfConstructKind::Value;
            }
            if (parent == SdfConstructKind::StampCondition
                || parent == SdfConstructKind::CheckCondition
                || parent == SdfConstructKind::ConditionExpression) {
                return child == SdfConstructKind::ConditionExpression;
            }
            return false;
        }

        [[nodiscard]] bool valid_value(const SdfSyntaxNode& node) const noexcept
        {
            if (!node.children.empty())
                return false;
            if (node.atoms.empty())
                return true;
            std::size_t colon_count = 0U;
            bool segment_has_number = false;
            for (const auto& atom : node.atoms) {
                if (atom.kind == SdfTokenKind::Colon) {
                    ++colon_count;
                    segment_has_number = false;
                } else if (atom.kind == SdfTokenKind::Number && !segment_has_number) {
                    segment_has_number = true;
                } else {
                    return false;
                }
            }
            return (colon_count == 0U && node.atoms.size() == 1U)
                || (colon_count == 2U && node.atoms.size() <= 5U);
        }

        [[nodiscard]] static bool wrapper_kind(
            const SdfConstructKind kind) noexcept
        {
            switch (kind) {
            case SdfConstructKind::Delay:
            case SdfConstructKind::Absolute:
            case SdfConstructKind::Increment:
            case SdfConstructKind::TimingCheck:
            case SdfConstructKind::Negative:
            case SdfConstructKind::TimingEnvironment:
            case SdfConstructKind::Label:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] static bool condition_kind(
            const SdfConstructKind kind) noexcept
        {
            return kind == SdfConstructKind::Conditional
                || kind == SdfConstructKind::ConditionalElse
                || kind == SdfConstructKind::StampCondition
                || kind == SdfConstructKind::CheckCondition;
        }

        [[nodiscard]] static bool delay_leaf_kind(
            const SdfConstructKind kind) noexcept
        {
            switch (kind) {
            case SdfConstructKind::Iopath:
            case SdfConstructKind::Retain:
            case SdfConstructKind::Interconnect:
            case SdfConstructKind::NetDelay:
            case SdfConstructKind::Port:
            case SdfConstructKind::Device:
            case SdfConstructKind::PathPulse:
            case SdfConstructKind::PathPulsePercent:
            case SdfConstructKind::Mipd:
                return true;
            default:
                return false;
            }
        }

        void validate_wrapper(const SdfSyntaxNode& node)
        {
            if (!node.atoms.empty() || node.children.empty()) {
                diagnose("FSIM-SDF-PARSE-015",
                    std::string { to_string(node.kind) }
                        + " requires one or more nested constructs and no direct values",
                    node.span);
            }
        }

        void validate_delay_leaf(const SdfSyntaxNode& node)
        {
            const auto values = child_count(node, SdfConstructKind::Value);
            const auto port_count = static_cast<std::size_t>(
                std::ranges::count_if(node.atoms, is_port_atom));
            bool valid = true;
            switch (node.kind) {
            case SdfConstructKind::Iopath:
                valid = port_count + child_count(node, SdfConstructKind::Edge) == 2U
                    && values >= 1U;
                break;
            case SdfConstructKind::Interconnect:
                valid = port_count == 2U && values >= 1U;
                break;
            case SdfConstructKind::NetDelay:
            case SdfConstructKind::Port:
            case SdfConstructKind::Mipd:
                valid = port_count == 1U && values >= 1U;
                break;
            case SdfConstructKind::Device:
                valid = port_count <= 1U && values >= 1U;
                break;
            case SdfConstructKind::PathPulse:
            case SdfConstructKind::PathPulsePercent:
                valid = (port_count == 0U || port_count == 2U)
                    && values >= 1U && values <= 2U;
                break;
            case SdfConstructKind::Retain:
                valid = node.atoms.empty() && values >= 1U;
                break;
            default:
                return;
            }
            if (!valid) {
                diagnose("FSIM-SDF-PARSE-015",
                    std::string { to_string(node.kind) }
                        + " has an invalid port or delay-value arity",
                    node.span);
            }
        }

        void validate_condition(const SdfSyntaxNode& node,
            const SdfConstructKind parent)
        {
            if (node.kind == SdfConstructKind::ConditionalElse) {
                if (!node.atoms.empty() || node.children.size() != 1U
                    || node.children.front().kind != SdfConstructKind::Iopath) {
                    diagnose("FSIM-SDF-PARSE-018",
                        "CONDELSE requires exactly one nested IOPATH", node.span);
                }
                return;
            }
            if (node.kind == SdfConstructKind::StampCondition
                || node.kind == SdfConstructKind::CheckCondition) {
                if (node.atoms.empty()
                    && child_count(node, SdfConstructKind::ConditionExpression) == 0U) {
                    diagnose("FSIM-SDF-PARSE-018",
                        std::string { to_string(node.kind) }
                            + " requires one nonempty condition expression",
                        node.span);
                }
                return;
            }
            const bool timing_port = is_timing_check_kind(parent);
            const auto condition_groups = child_count(node, SdfConstructKind::ConditionExpression);
            const auto iopaths = child_count(node, SdfConstructKind::Iopath);
            const bool valid = timing_port
                ? ((!node.atoms.empty() && condition_groups != 0U)
                      || node.atoms.size() >= 2U)
                    && iopaths == 0U
                : (!node.atoms.empty() || condition_groups != 0U)
                    && iopaths == 1U
                    && node.children.size() == condition_groups + iopaths;
            if (!valid) {
                diagnose("FSIM-SDF-PARSE-018",
                    timing_port
                        ? "timing-check COND requires a condition and a port selector"
                        : "delay COND requires a condition and one nested IOPATH",
                    node.span);
            }
            const auto quoted = std::ranges::find_if(node.atoms, [](const auto& atom) {
                return atom.kind == SdfTokenKind::String;
            });
            if (quoted != node.atoms.end() && quoted != node.atoms.begin()) {
                diagnose("FSIM-SDF-PARSE-018",
                    "optional COND label string must precede the condition", quoted->span);
            }
        }

        void validate_edge(const SdfSyntaxNode& node)
        {
            if (node.children.size() != 0U || node.atoms.size() != 1U
                || !is_port_atom(node.atoms.front())) {
                diagnose("FSIM-SDF-PARSE-017",
                    "edge selector requires exactly one port instance", node.span);
            }
        }

        void validate_iopath_order(const SdfSyntaxNode& node)
        {
            bool saw_retain = false;
            bool saw_value = false;
            for (const auto& child : node.children) {
                if (child.kind == SdfConstructKind::Edge
                    && (saw_retain || saw_value)) {
                    diagnose("FSIM-SDF-PARSE-019",
                        "IOPATH edge selector must precede RETAIN and delay values",
                        child.span);
                } else if (child.kind == SdfConstructKind::Retain) {
                    if (saw_value) {
                        diagnose("FSIM-SDF-PARSE-019",
                            "IOPATH RETAIN must precede delay values", child.span);
                    }
                    saw_retain = true;
                } else if (child.kind == SdfConstructKind::Value) {
                    saw_value = true;
                }
            }
        }

        void validate_timing_check(const SdfSyntaxNode& node)
        {
            std::size_t ports = static_cast<std::size_t>(
                std::ranges::count_if(node.atoms, is_port_atom));
            ports += child_count(node, SdfConstructKind::Edge);
            ports += child_count(node, SdfConstructKind::Conditional);
            const auto values = child_count(node, SdfConstructKind::Value);
            std::size_t expected_ports = 2U;
            std::size_t minimum_values = 1U;
            std::size_t maximum_values = 1U;
            switch (node.kind) {
            case SdfConstructKind::SetupHold:
            case SdfConstructKind::RecRem:
            case SdfConstructKind::BidirectSkew:
            case SdfConstructKind::NoChange:
                minimum_values = 2U;
                maximum_values = 2U;
                break;
            case SdfConstructKind::Width:
                expected_ports = 1U;
                maximum_values = 2U;
                break;
            case SdfConstructKind::Period:
                expected_ports = 1U;
                break;
            default:
                break;
            }
            if (ports != expected_ports || values < minimum_values
                || values > maximum_values) {
                diagnose("FSIM-SDF-PARSE-015",
                    std::string { to_string(node.kind) }
                        + " has invalid port or timing-value arity",
                    node.span);
            }
        }

        void validate_timing_environment(const SdfSyntaxNode& node)
        {
            if (node.kind == SdfConstructKind::Sum
                || node.kind == SdfConstructKind::Diff) {
                const auto paths = child_count(node,
                    SdfConstructKind::ConstraintPath);
                const auto values = child_count(node, SdfConstructKind::Value);
                const bool valid_paths = node.kind == SdfConstructKind::Diff
                    ? paths == 2U
                    : paths >= 2U;
                if (!node.atoms.empty() || !valid_paths
                    || values == 0U || values > 2U) {
                    diagnose("FSIM-SDF-PARSE-015",
                        std::string { to_string(node.kind) }
                            + " requires governed constraint paths and one or two values",
                        node.span);
                }
                return;
            }
            if (node.kind == SdfConstructKind::Exception) {
                if (node.atoms.empty() && node.children.empty()) {
                    diagnose("FSIM-SDF-PARSE-015",
                        "EXCEPTION requires at least one selector", node.span);
                }
                return;
            }
            auto ports = static_cast<std::size_t>(
                std::ranges::count_if(node.atoms, is_port_atom));
            ports += child_count(node, SdfConstructKind::Edge);
            const auto values = child_count(node, SdfConstructKind::Value);
            const auto minimum_ports = node.kind == SdfConstructKind::PathConstraint ? 2U : 1U;
            if (ports < minimum_ports || values == 0U) {
                diagnose("FSIM-SDF-PARSE-015",
                    std::string { to_string(node.kind) }
                        + " has invalid selector or constraint-value arity",
                    node.span);
            }
        }

        void validate_label_entry(const SdfSyntaxNode& node)
        {
            if (node.atoms.empty()
                || child_count(node, SdfConstructKind::Value) == 0U) {
                diagnose("FSIM-SDF-PARSE-015",
                    "LABEL entry requires a name and at least one value", node.span);
            }
        }

        void validate_node(SdfSyntaxNode& node, const SdfConstructKind parent)
        {
            normalize_contextual_children(node);
            if (node.kind == SdfConstructKind::Unknown) {
                diagnose("FSIM-SDF-PARSE-020",
                    "unsupported SDF construct '" + node.keyword_spelling + "'",
                    node.span);
                return;
            }
            if (!child_allowed(parent, node.kind)) {
                if (!is_sdf21()) {
                    diagnose("FSIM-SDF-PARSE-014",
                        std::string { to_string(node.kind) } + " is not legal inside "
                            + to_string(parent),
                        node.span);
                }
            }
            if (wrapper_kind(node.kind)) {
                validate_wrapper(node);
                return;
            }
            if (condition_kind(node.kind)) {
                validate_condition(node, parent);
                return;
            }
            if (node.kind == SdfConstructKind::ConditionExpression) {
                if (node.atoms.empty() && node.children.empty()) {
                    diagnose("FSIM-SDF-PARSE-018",
                        "condition group may not be empty", node.span);
                }
                return;
            }
            if (delay_leaf_kind(node.kind)) {
                validate_delay_leaf(node);
                if (node.kind == SdfConstructKind::Iopath) {
                    validate_iopath_order(node);
                }
                return;
            }
            if (node.kind == SdfConstructKind::Edge) {
                validate_edge(node);
                return;
            }
            if (node.kind == SdfConstructKind::Value) {
                if (!valid_value(node)) {
                    diagnose("FSIM-SDF-PARSE-016",
                        "delay or constraint value must be empty, decimal, or min:typ:max",
                        node.span);
                }
                return;
            }
            if (is_timing_check_kind(node.kind)) {
                validate_timing_check(node);
                return;
            }
            if (is_timing_environment_kind(node.kind)) {
                validate_timing_environment(node);
                return;
            }
            if (node.kind == SdfConstructKind::LabelEntry) {
                validate_label_entry(node);
                return;
            }
            if (node.kind == SdfConstructKind::ConstraintPath
                && (node.atoms.size() < 2U || !node.children.empty())) {
                diagnose("FSIM-SDF-PARSE-015",
                    "constraint path requires two port instances", node.span);
            }
        }

        void validate_declarations(std::vector<SdfSyntaxNode>& declarations)
        {
            struct Pending {
                SdfSyntaxNode* node;
                SdfConstructKind parent;
            };
            std::vector<Pending> pending;
            for (auto& declaration : declarations) {
                pending.push_back(Pending { &declaration, SdfConstructKind::Cell });
            }
            while (!pending.empty()) {
                const auto item = pending.back();
                pending.pop_back();
                validate_node(*item.node, item.parent);
                for (auto& child : item.node->children) {
                    pending.push_back(Pending { &child, item.node->kind });
                }
            }
        }

        SdfFile& file_;
        std::vector<Diagnostic>& diagnostics_;
    };

} // namespace

void parse_sdf_constructs(SdfFile& file,
    std::vector<Diagnostic>& diagnostics)
{
    SdfConstructParser(file, diagnostics).run();
}

const char* to_string(const SdfConstructKind kind) noexcept
{
    static constexpr auto names = std::to_array<const char*>({
        "CELL",
        "CELLTYPE",
        "INSTANCE",
        "CORRELATION",
        "DELAY",
        "ABSOLUTE",
        "INCREMENT",
        "COND",
        "CONDELSE",
        "IOPATH",
        "RETAIN",
        "INTERCONNECT",
        "NETDELAY",
        "PORT",
        "DEVICE",
        "PATHPULSE",
        "PATHPULSEPERCENT",
        "MIPD",
        "TIMINGCHECK",
        "NEGATIVE",
        "SETUP",
        "HOLD",
        "SETUPHOLD",
        "RECOVERY",
        "REMOVAL",
        "RECREM",
        "SKEW",
        "BIDIRECTSKEW",
        "WIDTH",
        "PERIOD",
        "NOCHANGE",
        "TIMINGENV",
        "PATHCONSTRAINT",
        "PERIODCONSTRAINT",
        "SKEWCONSTRAINT",
        "SUM",
        "DIFF",
        "ARRIVAL",
        "DEPARTURE",
        "SLACK",
        "WAVEFORM",
        "EXCEPTION",
        "LABEL",
        "NAME",
        "label-entry",
        "constraint-path",
        "SCOND",
        "CCOND",
        "condition-expression",
        "edge",
        "value",
        "unknown",
    });
    const auto index = static_cast<std::size_t>(kind);
    return index < names.size() ? names[index] : "unknown";
}

const char* to_string(const SdfInstanceSelectorKind kind) noexcept
{
    switch (kind) {
    case SdfInstanceSelectorKind::Empty:
        return "empty";
    case SdfInstanceSelectorKind::Exact:
        return "exact";
    case SdfInstanceSelectorKind::Wildcard:
        return "wildcard";
    }
    return "empty";
}

} // namespace fsim::frontend
