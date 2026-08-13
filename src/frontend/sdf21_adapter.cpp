// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <cctype>
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
            const auto lhs = static_cast<unsigned char>(left[index]);
            const auto rhs = static_cast<unsigned char>(right[index]);
            if (std::toupper(lhs) != std::toupper(rhs))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool old_timing_check(const SdfConstructKind kind) noexcept
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

    [[nodiscard]] bool old_constraint(const SdfConstructKind kind) noexcept
    {
        return kind == SdfConstructKind::PathConstraint
            || kind == SdfConstructKind::Sum || kind == SdfConstructKind::Diff
            || kind == SdfConstructKind::SkewConstraint;
    }

    [[nodiscard]] bool old_delay_leaf(const SdfConstructKind kind) noexcept
    {
        switch (kind) {
        case SdfConstructKind::Iopath:
        case SdfConstructKind::Conditional:
        case SdfConstructKind::Interconnect:
        case SdfConstructKind::NetDelay:
        case SdfConstructKind::Port:
        case SdfConstructKind::Device:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool allowed_child(const SdfConstructKind parent,
        const SdfConstructKind child) noexcept
    {
        if (parent == SdfConstructKind::Cell) {
            return child == SdfConstructKind::Correlation
                || child == SdfConstructKind::Delay
                || child == SdfConstructKind::TimingCheck;
        }
        if (parent == SdfConstructKind::Delay) {
            return child == SdfConstructKind::PathPulse
                || child == SdfConstructKind::PathPulsePercent
                || child == SdfConstructKind::Absolute
                || child == SdfConstructKind::Increment;
        }
        if (parent == SdfConstructKind::Absolute
            || parent == SdfConstructKind::Increment) {
            return old_delay_leaf(child);
        }
        if (parent == SdfConstructKind::Iopath) {
            return child == SdfConstructKind::Edge
                || child == SdfConstructKind::Value;
        }
        if (parent == SdfConstructKind::Conditional) {
            return child == SdfConstructKind::ConditionExpression
                || child == SdfConstructKind::Iopath;
        }
        if (parent == SdfConstructKind::Interconnect
            || parent == SdfConstructKind::Port
            || parent == SdfConstructKind::Device
            || parent == SdfConstructKind::PathPulse
            || parent == SdfConstructKind::PathPulsePercent) {
            return child == SdfConstructKind::Value;
        }
        if (parent == SdfConstructKind::NetDelay) {
            return child == SdfConstructKind::Value
                || child == SdfConstructKind::Instance;
        }
        if (parent == SdfConstructKind::TimingCheck) {
            return old_timing_check(child) || old_constraint(child);
        }
        if (old_timing_check(parent)) {
            return child == SdfConstructKind::Edge
                || child == SdfConstructKind::Conditional
                || child == SdfConstructKind::ConditionExpression
                || child == SdfConstructKind::Value;
        }
        if (parent == SdfConstructKind::PathConstraint
            || parent == SdfConstructKind::SkewConstraint) {
            return child == SdfConstructKind::Value
                || child == SdfConstructKind::Edge
                || child == SdfConstructKind::Conditional;
        }
        if (parent == SdfConstructKind::Sum || parent == SdfConstructKind::Diff) {
            return child == SdfConstructKind::ConstraintPath
                || child == SdfConstructKind::Value;
        }
        if (parent == SdfConstructKind::ConditionExpression) {
            return child == SdfConstructKind::ConditionExpression;
        }
        return false;
    }

    [[nodiscard]] std::size_t count_children(const SdfSyntaxNode& node,
        const SdfConstructKind kind) noexcept
    {
        return static_cast<std::size_t>(
            std::ranges::count(node.children, kind, &SdfSyntaxNode::kind));
    }

    [[nodiscard]] std::size_t port_atoms(const SdfSyntaxNode& node) noexcept
    {
        return static_cast<std::size_t>(
            std::ranges::count_if(node.atoms, [](const SdfSyntaxAtom& atom) {
                return atom.kind == SdfTokenKind::Identifier
                    || atom.kind == SdfTokenKind::EscapedIdentifier;
            }));
    }

    [[nodiscard]] bool governed_delay_list_size(const std::size_t size) noexcept
    {
        return size == 1U || size == 2U || size == 3U || size == 6U
            || size == 12U;
    }

    class Sdf21Adapter {
    public:
        Sdf21Adapter(SdfFile& file, std::vector<Diagnostic>& diagnostics)
            : file_(file)
            , diagnostics_(diagnostics)
        {
        }

        void run()
        {
            file_.revision_adapter = SdfRevisionAdapter::Sdf21;
            std::vector<Pending> pending;
            for (auto& cell : file_.cells) {
                for (auto& declaration : cell.declarations) {
                    pending.push_back(Pending { &declaration, SdfConstructKind::Cell });
                }
            }
            while (!pending.empty()) {
                const auto item = pending.back();
                pending.pop_back();
                inspect(*item.node, item.parent);
                for (auto& child : item.node->children) {
                    pending.push_back(Pending { &child, item.node->kind });
                }
            }
            if (saw_single_ && saw_triple_) {
                diagnose("FSIM-SDF-21-003",
                    "SDF 2.1 may not mix single delay values and min:typ:max triples",
                    mixed_value_span_);
            }
        }

    private:
        struct Pending {
            SdfSyntaxNode* node;
            SdfConstructKind parent;
        };

        void diagnose(std::string code, std::string message,
            const SourceSpan& span)
        {
            diagnostics_.push_back(Diagnostic { DiagnosticSeverity::Error,
                std::move(code), std::move(message), span, { } });
        }

        void inspect_value(const SdfSyntaxNode& node)
        {
            if (node.atoms.empty())
                return;
            const auto colons = static_cast<std::size_t>(std::ranges::count(
                node.atoms, SdfTokenKind::Colon, &SdfSyntaxAtom::kind));
            if (colons == 0U) {
                saw_single_ = true;
            } else if (colons == 2U) {
                saw_triple_ = true;
            }
            if (mixed_value_span_.source_name.empty())
                mixed_value_span_ = node.span;
        }

        void inspect_delay_list(const SdfSyntaxNode& node)
        {
            switch (node.kind) {
            case SdfConstructKind::Iopath:
            case SdfConstructKind::Interconnect:
            case SdfConstructKind::NetDelay:
            case SdfConstructKind::Port:
            case SdfConstructKind::Device:
                break;
            default:
                return;
            }
            const auto values = count_children(node, SdfConstructKind::Value);
            if (!governed_delay_list_size(values)) {
                diagnose("FSIM-SDF-21-004",
                    std::string { to_string(node.kind) }
                        + " requires 1, 2, 3, 6, or 12 SDF 2.1 delay values",
                    node.span);
            }
        }

        void inspect_constraint(const SdfSyntaxNode& node)
        {
            const auto values = count_children(node, SdfConstructKind::Value);
            const auto paths = count_children(node, SdfConstructKind::ConstraintPath);
            bool valid = true;
            switch (node.kind) {
            case SdfConstructKind::PathConstraint:
                valid = port_atoms(node) >= 2U && values == 2U;
                break;
            case SdfConstructKind::Sum:
                valid = paths >= 2U && (values == 1U || values == 2U);
                break;
            case SdfConstructKind::Diff:
                valid = paths == 2U && (values == 1U || values == 2U);
                break;
            case SdfConstructKind::SkewConstraint:
                valid = port_atoms(node) + count_children(node, SdfConstructKind::Edge)
                        == 1U
                    && values == 1U;
                break;
            default:
                return;
            }
            if (!valid) {
                diagnose("FSIM-SDF-21-005",
                    std::string { to_string(node.kind) }
                        + " has invalid SDF 2.1 constraint arity",
                    node.span);
            }
        }

        void inspect(const SdfSyntaxNode& node, const SdfConstructKind parent)
        {
            if (!allowed_child(parent, node.kind)) {
                diagnose("FSIM-SDF-21-001",
                    std::string { to_string(node.kind) }
                        + " is not part of the SDF 2.1 construct profile",
                    node.span);
            }
            if (node.kind == SdfConstructKind::PathPulsePercent
                && !ascii_iequals(node.keyword_spelling, "GLOBALPATHPULSE")) {
                diagnose("FSIM-SDF-21-001",
                    "SDF 2.1 uses GLOBALPATHPULSE rather than PATHPULSEPERCENT",
                    node.span);
            }
            if (node.kind == SdfConstructKind::Conditional
                && std::ranges::any_of(node.atoms, [](const SdfSyntaxAtom& atom) {
                       return atom.kind == SdfTokenKind::String;
                   })) {
                diagnose("FSIM-SDF-21-001",
                    "symbolic condition labels are not part of SDF 2.1", node.span);
            }
            if (node.kind == SdfConstructKind::Value)
                inspect_value(node);
            inspect_delay_list(node);
            inspect_constraint(node);
        }

        SdfFile& file_;
        std::vector<Diagnostic>& diagnostics_;
        bool saw_single_ { };
        bool saw_triple_ { };
        SourceSpan mixed_value_span_;
    };

} // namespace

void adapt_sdf21(SdfFile& file, std::vector<Diagnostic>& diagnostics)
{
    if (!file.has_revision || file.revision != SdfRevision::Sdf21)
        return;
    Sdf21Adapter(file, diagnostics).run();
}

const char* to_string(const SdfRevisionAdapter adapter) noexcept
{
    switch (adapter) {
    case SdfRevisionAdapter::None:
        return "none";
    case SdfRevisionAdapter::Sdf21:
        return "sdf21";
    case SdfRevisionAdapter::Sdf30:
        return "sdf30";
    }
    return "none";
}

} // namespace fsim::frontend
