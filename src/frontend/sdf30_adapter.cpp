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
            if (std::toupper(static_cast<unsigned char>(left[index]))
                != std::toupper(static_cast<unsigned char>(right[index]))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::size_t child_count(const SdfSyntaxNode& node,
        const SdfConstructKind kind) noexcept
    {
        return static_cast<std::size_t>(
            std::ranges::count(node.children, kind, &SdfSyntaxNode::kind));
    }

    [[nodiscard]] bool delay_list_owner(const SdfConstructKind kind) noexcept
    {
        switch (kind) {
        case SdfConstructKind::Iopath:
        case SdfConstructKind::Interconnect:
        case SdfConstructKind::Port:
        case SdfConstructKind::Device:
        case SdfConstructKind::Retain:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool valid_delay_list_size(const std::size_t size) noexcept
    {
        return size == 1U || size == 2U || size == 3U
            || (size >= 4U && size <= 6U) || (size >= 7U && size <= 12U);
    }

    class Sdf30Adapter {
    public:
        Sdf30Adapter(SdfFile& file, std::vector<Diagnostic>& diagnostics)
            : file_(file)
            , diagnostics_(diagnostics)
        {
        }

        void run()
        {
            file_.revision_adapter = SdfRevisionAdapter::Sdf30;
            std::vector<Pending> pending;
            for (auto& cell : file_.cells) {
                for (auto& declaration : cell.declarations) {
                    pending.push_back(Pending { &declaration });
                }
            }
            while (!pending.empty()) {
                auto* node = pending.back().node;
                pending.pop_back();
                inspect(*node);
                for (auto& child : node->children)
                    pending.push_back(Pending { &child });
            }
            if (saw_single_ && saw_triple_) {
                diagnose("FSIM-SDF-30-004",
                    "SDF 3.0 may not mix single delay values and min:typ:max triples",
                    mixed_span_);
            }
        }

    private:
        struct Pending {
            SdfSyntaxNode* node;
        };

        void diagnose(std::string code, std::string message,
            const SourceSpan& span)
        {
            diagnostics_.push_back(Diagnostic { DiagnosticSeverity::Error,
                std::move(code), std::move(message), span, { } });
        }

        void inspect_profile(const SdfSyntaxNode& node)
        {
            const bool removed = node.kind == SdfConstructKind::Correlation
                || node.kind == SdfConstructKind::NetDelay
                || node.kind == SdfConstructKind::Label
                || node.kind == SdfConstructKind::Mipd;
            const bool old_pulse = node.kind == SdfConstructKind::PathPulsePercent
                && ascii_iequals(node.keyword_spelling, "GLOBALPATHPULSE");
            if (removed || old_pulse) {
                diagnose("FSIM-SDF-30-001",
                    std::string { to_string(node.kind) }
                        + " is not part of the OVI SDF 3.0 construct profile",
                    node.span);
            }
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
            if (mixed_span_.source_name.empty())
                mixed_span_ = node.span;
        }

        void inspect_delay_list(const SdfSyntaxNode& node)
        {
            if (!delay_list_owner(node.kind))
                return;
            const auto values = child_count(node, SdfConstructKind::Value);
            if (!valid_delay_list_size(values)) {
                diagnose("FSIM-SDF-30-004",
                    std::string { to_string(node.kind) }
                        + " has an invalid SDF 3.0 delay-list length",
                    node.span);
            }
        }

        void inspect(const SdfSyntaxNode& node)
        {
            inspect_profile(node);
            if (node.kind == SdfConstructKind::Value)
                inspect_value(node);
            inspect_delay_list(node);
        }

        SdfFile& file_;
        std::vector<Diagnostic>& diagnostics_;
        bool saw_single_ { };
        bool saw_triple_ { };
        SourceSpan mixed_span_;
    };

} // namespace

void adapt_sdf30(SdfFile& file, std::vector<Diagnostic>& diagnostics)
{
    if (!file.has_revision || file.revision != SdfRevision::Sdf30)
        return;
    Sdf30Adapter(file, diagnostics).run();
}

} // namespace fsim::frontend
