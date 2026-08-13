// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {
namespace {

    void append_field(std::string& output, const std::string_view value)
    {
        output += std::to_string(value.size());
        output.push_back(':');
        output += value;
    }

    void append_number(std::string& output, const std::uint64_t value)
    {
        append_field(output, std::to_string(value));
    }

    [[nodiscard]] bool ascii_iequals(const std::string_view left,
        const std::string_view right) noexcept
    {
        if (left.size() != right.size())
            return false;
        auto right_character = right.begin();
        for (const char left_character : left) {
            if (std::toupper(static_cast<unsigned char>(left_character))
                != std::toupper(static_cast<unsigned char>(*right_character++))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string cell_identity(const SdfCell& source)
    {
        std::string result = "cell";
        append_field(result, source.cell_type);
        append_field(result, to_string(source.instance_kind));
        if (source.normalized_instance)
            append_field(result, source.normalized_instance->canonical);
        else
            append_field(result, source.instance_spelling);
        append_field(result,
            source.wildcard_requires_physical_primitive ? "physical" : "general");
        return result;
    }

    [[nodiscard]] std::string profile_identity(const SdfFile& file,
        const SdfSyntaxNode& node)
    {
        if (file.revision != SdfRevision::Sdf21)
            return { };
        if (node.kind == SdfConstructKind::Correlation)
            return "sdf21:correlation";
        if (node.kind == SdfConstructKind::NetDelay)
            return "sdf21:netdelay";
        if (node.kind == SdfConstructKind::PathPulsePercent
            && ascii_iequals(node.keyword_spelling, "GLOBALPATHPULSE")) {
            return "sdf21:globalpathpulse";
        }
        return { };
    }

    [[nodiscard]] std::string source_identity(const std::uint64_t cell_id,
        const std::uint64_t node_id, const std::size_t first_token,
        const std::size_t token_count)
    {
        std::string result = "cell/" + std::to_string(cell_id) + "/node/"
            + std::to_string(node_id) + "/token/" + std::to_string(first_token)
            + '+' + std::to_string(token_count);
        return result;
    }

    [[nodiscard]] std::string node_identity(const SdfIrNode& node)
    {
        std::string result = "node";
        append_number(result, node.cell_id);
        append_number(result, node.parent_id);
        append_number(result, node.sibling_index);
        append_field(result, to_string(node.kind));
        append_field(result, node.canonical_identity);
        append_field(result, node.profile_identity);
        return result;
    }

    class IrBuilder {
    public:
        IrBuilder(const SdfFile& file, std::vector<Diagnostic>& diagnostics,
            const SdfIrLimits limits)
            : file_(file)
            , diagnostics_(diagnostics)
            , limits_(limits)
        {
        }

        [[nodiscard]] std::shared_ptr<const SdfIr> run()
        {
            if (!validate_file())
                return { };
            cells_.reserve(file_.cells.size());
            for (const auto& cell : file_.cells) {
                if (!lower_cell(cell))
                    return { };
            }
            if (!build_semantic_identity())
                return { };
            return std::shared_ptr<const SdfIr>(new SdfIr(file_.revision,
                file_.revision_adapter, file_.source_name,
                file_.normalized_timescale, std::move(cells_), std::move(nodes_),
                std::move(semantic_identity_)));
        }

    private:
        struct PendingNode {
            const SdfSyntaxNode* source { };
            std::uint64_t parent_id { };
            std::size_t sibling_index { };
            std::size_t depth { };
        };

        void diagnose(std::string code, std::string message,
            const SourceSpan& span)
        {
            diagnostics_.push_back(Diagnostic { DiagnosticSeverity::Error,
                std::move(code), std::move(message), span, { } });
        }

        [[nodiscard]] bool validate_file()
        {
            if (file_.cells.size() > limits_.max_cells) {
                diagnose("FSIM-SDF-IR-002",
                    "normalized SDF IR exceeds the configured cell limit",
                    file_.span);
                return false;
            }
            const auto* timescale = file_.find_header(SdfHeaderKind::Timescale);
            if ((timescale != nullptr) != file_.normalized_timescale.has_value()) {
                diagnose("FSIM-SDF-IR-001",
                    "normalized SDF timescale state is incomplete", file_.span);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool lower_cell(const SdfCell& source)
        {
            if (source.instance_kind == SdfInstanceSelectorKind::Exact
                && !source.normalized_instance) {
                diagnose("FSIM-SDF-IR-001",
                    "normalized SDF exact instance state is incomplete", source.span);
                return false;
            }
            if (cells_.size()
                >= static_cast<std::size_t>(
                    std::numeric_limits<std::uint64_t>::max())) {
                diagnose("FSIM-SDF-IR-002",
                    "normalized SDF IR cell identity space is exhausted", source.span);
                return false;
            }
            SdfIrCell cell;
            cell.id = static_cast<std::uint64_t>(cells_.size() + 1U);
            cell.cell_type = source.cell_type;
            cell.instance_kind = source.instance_kind;
            cell.instance = source.normalized_instance;
            cell.wildcard_requires_physical_primitive
                = source.wildcard_requires_physical_primitive;
            cell.first_node = nodes_.size();
            cell.span = source.span;
            cell.source_identity = "cell/" + std::to_string(cell.id);
            cell.canonical_identity = cell_identity(source);

            std::vector<PendingNode> pending;
            pending.reserve(source.declarations.size());
            auto sibling_index = source.declarations.size();
            for (auto declaration = source.declarations.rbegin();
                declaration != source.declarations.rend(); ++declaration) {
                --sibling_index;
                pending.push_back(
                    PendingNode { &*declaration, 0U, sibling_index, 0U });
            }
            while (!pending.empty()) {
                const auto item = pending.back();
                pending.pop_back();
                if (!lower_node(cell.id, item, pending))
                    return false;
            }
            cell.node_count = nodes_.size() - cell.first_node;
            cells_.push_back(std::move(cell));
            return true;
        }

        [[nodiscard]] bool lower_node(const std::uint64_t cell_id,
            const PendingNode& item, std::vector<PendingNode>& pending)
        {
            if (nodes_.size() >= limits_.max_nodes
                || nodes_.size()
                    >= static_cast<std::size_t>(
                        std::numeric_limits<std::uint64_t>::max())) {
                diagnose("FSIM-SDF-IR-002",
                    "normalized SDF IR exceeds the configured node limit",
                    item.source->span);
                return false;
            }
            if (item.source->canonical_identity.empty()
                || (item.source->kind == SdfConstructKind::Value
                    && !item.source->exact_value)) {
                diagnose("FSIM-SDF-IR-001",
                    "normalized SDF syntax node is incomplete", item.source->span);
                return false;
            }

            SdfIrNode node;
            node.id = static_cast<std::uint64_t>(nodes_.size() + 1U);
            node.cell_id = cell_id;
            node.parent_id = item.parent_id;
            node.sibling_index = item.sibling_index;
            node.depth = item.depth;
            node.kind = item.source->kind;
            node.canonical_atoms = item.source->canonical_atoms;
            node.exact_value = item.source->exact_value;
            node.scaled_femtoseconds = item.source->scaled_femtoseconds;
            node.first_token = item.source->first_token;
            node.token_count = item.source->token_count;
            node.span = item.source->span;
            node.source_identity = source_identity(cell_id, node.id,
                node.first_token, node.token_count);
            node.profile_identity = profile_identity(file_, *item.source);
            node.canonical_identity = item.source->canonical_identity;
            const auto node_id = node.id;
            nodes_.push_back(std::move(node));

            if (item.depth == std::numeric_limits<std::size_t>::max()) {
                diagnose("FSIM-SDF-IR-002",
                    "normalized SDF IR depth exceeds host storage", item.source->span);
                return false;
            }
            auto sibling_index = item.source->children.size();
            for (auto child = item.source->children.rbegin();
                child != item.source->children.rend(); ++child) {
                --sibling_index;
                pending.push_back(PendingNode {
                    &*child, node_id, sibling_index, item.depth + 1U });
            }
            return true;
        }

        [[nodiscard]] bool append_identity(const std::string_view value,
            const SourceSpan& span)
        {
            const auto length_digits = std::to_string(value.size()).size();
            if (semantic_identity_.size() > limits_.max_identity_bytes
                || value.size() > limits_.max_identity_bytes
                || length_digits + 1U
                    > limits_.max_identity_bytes - semantic_identity_.size()
                || value.size() > limits_.max_identity_bytes
                        - semantic_identity_.size() - length_digits - 1U) {
                diagnose("FSIM-SDF-IR-002",
                    "normalized SDF semantic identity exceeds the configured byte limit",
                    span);
                return false;
            }
            append_field(semantic_identity_, value);
            return true;
        }

        [[nodiscard]] bool build_semantic_identity()
        {
            semantic_identity_ = "sdf-ir-v1";
            if (!append_identity(file_.normalized_timescale
                        ? file_.normalized_timescale->canonical
                        : "unspecified",
                    file_.span)
                || !append_identity(std::to_string(cells_.size()), file_.span)) {
                return false;
            }
            for (const auto& cell : cells_) {
                if (!append_identity(cell.canonical_identity, cell.span))
                    return false;
            }
            if (!append_identity(std::to_string(nodes_.size()), file_.span))
                return false;
            for (const auto& node : nodes_) {
                if (!append_identity(node_identity(node), node.span))
                    return false;
            }
            return true;
        }

        const SdfFile& file_;
        std::vector<Diagnostic>& diagnostics_;
        SdfIrLimits limits_;
        std::vector<SdfIrCell> cells_;
        std::vector<SdfIrNode> nodes_;
        std::string semantic_identity_;
    };

} // namespace

SdfIr::SdfIr(const SdfRevision revision, const SdfRevisionAdapter adapter,
    std::string source_name, std::optional<SdfNormalizedTimescale> timescale,
    std::vector<SdfIrCell> cells, std::vector<SdfIrNode> nodes,
    std::string semantic_identity)
    : revision_(revision)
    , revision_adapter_(adapter)
    , source_name_(std::move(source_name))
    , timescale_(std::move(timescale))
    , cells_(std::move(cells))
    , nodes_(std::move(nodes))
    , semantic_identity_(std::move(semantic_identity))
{
}

SdfRevision SdfIr::revision() const noexcept { return revision_; }

SdfRevisionAdapter SdfIr::revision_adapter() const noexcept
{
    return revision_adapter_;
}

std::string_view SdfIr::source_name() const noexcept { return source_name_; }

const std::optional<SdfNormalizedTimescale>& SdfIr::timescale() const noexcept
{
    return timescale_;
}

std::span<const SdfIrCell> SdfIr::cells() const noexcept { return cells_; }

std::span<const SdfIrNode> SdfIr::nodes() const noexcept { return nodes_; }

const SdfIrCell* SdfIr::find_cell(const std::uint64_t id) const noexcept
{
    if (id == 0U || id > cells_.size())
        return nullptr;
    const auto& result = cells_[static_cast<std::size_t>(id - 1U)];
    return result.id == id ? &result : nullptr;
}

const SdfIrNode* SdfIr::find_node(const std::uint64_t id) const noexcept
{
    if (id == 0U || id > nodes_.size())
        return nullptr;
    const auto& result = nodes_[static_cast<std::size_t>(id - 1U)];
    return result.id == id ? &result : nullptr;
}

std::string_view SdfIr::semantic_identity() const noexcept
{
    return semantic_identity_;
}

std::string SdfIr::describe() const
{
    std::string result = "sdf-ir schema=1 revision=";
    result += to_string(revision_);
    result += " adapter=";
    result += to_string(revision_adapter_);
    result += " source=";
    append_field(result, source_name_);
    result += " cells=" + std::to_string(cells_.size());
    result += " nodes=" + std::to_string(nodes_.size());
    for (const auto& cell : cells_) {
        result += "\ncell id=" + std::to_string(cell.id) + " nodes="
            + std::to_string(cell.first_node) + '+'
            + std::to_string(cell.node_count) + " source=";
        append_field(result, cell.source_identity);
        result += " canonical=";
        append_field(result, cell.canonical_identity);
    }
    for (const auto& node : nodes_) {
        result += "\nnode id=" + std::to_string(node.id) + " cell="
            + std::to_string(node.cell_id) + " parent="
            + std::to_string(node.parent_id) + " sibling="
            + std::to_string(node.sibling_index) + " depth="
            + std::to_string(node.depth) + " kind=" + to_string(node.kind)
            + " source=";
        append_field(result, node.source_identity);
        result += " canonical=";
        append_field(result, node.canonical_identity);
        if (!node.profile_identity.empty()) {
            result += " profile=";
            append_field(result, node.profile_identity);
        }
    }
    return result;
}

bool SdfIr::semantically_equal(const SdfIr& other) const noexcept
{
    return semantic_identity_ == other.semantic_identity_;
}

void lower_sdf_ir(SdfFile& file, std::vector<Diagnostic>& diagnostics,
    const SdfIrLimits limits)
{
    file.normalized_ir.reset();
    if (has_errors(diagnostics))
        return;
    file.normalized_ir = IrBuilder(file, diagnostics, limits).run();
}

} // namespace fsim::frontend
