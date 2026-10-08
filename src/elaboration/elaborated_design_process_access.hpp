// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"
#include "../runtime/simir_process_program.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime::simir {
class RegionGraph;
}

namespace fsim::elaboration::detail {

struct RuntimeProcessProgramRow {
    std::uint32_t template_id { };
    runtime::simir::ProcessInstanceProgram instance;
};

struct RuntimeProcessProgramTable {
    std::vector<std::shared_ptr<const runtime::simir::ProcessProgramTemplate>>
        templates;
    std::vector<RuntimeProcessProgramRow> rows;
    /// Derived data, not part of the table's value: the design region graph
    /// last built over these rows and the signal-layout key it was built
    /// with (ElaboratedDesignProcessAccess::region_graph).
    mutable std::shared_ptr<const runtime::simir::RegionGraph> region_graph;
    mutable std::uint64_t region_graph_key { };
};

/// Mutable fresh-elaboration sink. It splits each incoming full process once
/// into the same immutable-template/instance-row representation used by
/// runtime artifacts, while the elaborator is still appending later IDs.
class RuntimeProcessProgramTableBuilder {
public:
    RuntimeProcessProgramTableBuilder();

    [[nodiscard]] std::size_t size() const noexcept;
    /// The returned view borrows a row and is invalidated by append, replace,
    /// or freeze. Callers must finish using it before mutating the builder.
    [[nodiscard]] runtime::simir::ProcessProgramView view(
        std::size_t process) const noexcept;
    [[nodiscard]] std::shared_ptr<const runtime::simir::ProcessProgramTemplate>
    intern_template(const runtime::simir::ProcessProgramView& process);
    void append(runtime::simir::Process process);
    void append(
        std::shared_ptr<const runtime::simir::ProcessProgramTemplate> common,
        runtime::simir::ProcessInstanceProgram instance);
    void replace(
        std::size_t process, runtime::simir::Process instance);
    [[nodiscard]] std::shared_ptr<const RuntimeProcessProgramTable>
    freeze() && noexcept;

private:
    [[nodiscard]] std::uint32_t intern_table_template(
        std::shared_ptr<const runtime::simir::ProcessProgramTemplate> common);

    std::shared_ptr<RuntimeProcessProgramTable> table_;
    runtime::simir::ProcessProgramTemplatePool template_pool_;
    std::unordered_map<const runtime::simir::ProcessProgramTemplate*,
        std::uint32_t> template_ids_;
};

/// While alive on this thread, loading runtime state skips the trial
/// interpreter population that validates every process program. Only for a
/// caller that populates a real interpreter from the loaded design next,
/// which validates the same programs (simulation).
class DeferredProgramValidation {
public:
    DeferredProgramValidation() noexcept;
    ~DeferredProgramValidation();
    DeferredProgramValidation(const DeferredProgramValidation&) = delete;
    DeferredProgramValidation& operator=(const DeferredProgramValidation&) = delete;

    [[nodiscard]] static bool active() noexcept;

private:
    bool previous_ { };
};

/// Engine v4 static-kernel plan persistence: elaborate --aot stores the
/// plan beside the kernel's native code, and a later simulate of the same
/// artifact restores it instead of planning (which builds the region graph).
[[nodiscard]] std::string serialize_static_kernel_plan(
    const StaticKernelPlan& plan);
/// Null when `bytes` is not a plan for `design` (wrong format, or process,
/// signal or container references out of range).
[[nodiscard]] std::optional<StaticKernelPlan> restore_static_kernel_plan(
    const ElaboratedDesign& design, std::string_view bytes);
/// The planner's identity, for persisted plans' cache keys.
[[nodiscard]] std::string static_kernel_plan_identity();

/// While alive, an artifact restored on this thread is trusted: its
/// checksums and producing build already match, so derived state it carries
/// (the signal driver inventory) is adopted rather than re-derived and
/// cross-checked. FSIM_VERIFY_ARTIFACT=1 keeps the full checks.
class TrustedArtifactLoad {
public:
    TrustedArtifactLoad() noexcept;
    ~TrustedArtifactLoad();
    TrustedArtifactLoad(const TrustedArtifactLoad&) = delete;
    TrustedArtifactLoad& operator=(const TrustedArtifactLoad&) = delete;

    [[nodiscard]] static bool active() noexcept;

private:
    bool previous_ { };
};

/// While alive, a design restored on this thread resolves its hierarchy
/// paths against `table` (the artifact's canonical path table) instead of
/// building its own and remapping onto the canonical one afterwards.
/// adopted() reports whether that happened; when a path is absent from the
/// table the design builds its own, and the caller's remap reports it.
class CanonicalHierarchyPaths {
public:
    explicit CanonicalHierarchyPaths(
        const semantic::HierarchyPathTable& table) noexcept;
    ~CanonicalHierarchyPaths();
    CanonicalHierarchyPaths(const CanonicalHierarchyPaths&) = delete;
    CanonicalHierarchyPaths& operator=(const CanonicalHierarchyPaths&) = delete;

    [[nodiscard]] bool adopted() const noexcept { return adopted_; }

    [[nodiscard]] static CanonicalHierarchyPaths* active() noexcept;
    [[nodiscard]] const semantic::HierarchyPathTable& table() const noexcept
    {
        return table_;
    }
    void mark_adopted() noexcept { adopted_ = true; }

private:
    const semantic::HierarchyPathTable& table_;
    CanonicalHierarchyPaths* previous_ { };
    bool adopted_ { };
};

/// Private bridge between the artifact decoder and ElaboratedDesign's
/// row-backed runtime representation. This type is not part of the public
/// elaboration API.
class ElaboratedDesignProcessAccess {
public:
    [[nodiscard]] static std::optional<ElaboratedDesign> from_state(
        ElaboratedDesignState state,
        std::shared_ptr<const RuntimeProcessProgramTable> rows,
        bool require_driver_inventory = false);

    /// Copy non-process runtime metadata without materializing a row-backed
    /// design. Process rows are returned separately by process_table().
    [[nodiscard]] static ElaboratedDesignState artifact_state(
        const ElaboratedDesign& design);

    /// Return the stable owner for published row views. Consumers retaining
    /// ProcessProgramViews must retain this owner too; the public processes()
    /// facade can detach the design's own row-table pointer.
    [[nodiscard]] static std::shared_ptr<const RuntimeProcessProgramTable>
    process_table(const ElaboratedDesign& design) noexcept;

    [[nodiscard]] static std::size_t process_count(
        const ElaboratedDesign& design) noexcept;

    /// Builder-backed views borrow row storage and must not cross an append,
    /// replacement, or finalize operation. Published table views are stable.
    [[nodiscard]] static runtime::simir::ProcessProgramView process_view(
        const ElaboratedDesign& design,
        std::size_t process) noexcept;

    [[nodiscard]] static bool row_backed(
        const ElaboratedDesign& design) noexcept;

    /// The region graph over every process with complete accesses, as the
    /// driver inventory and the static-kernel planner use it. A row-backed
    /// design caches it on its row table, keyed by the signal and container
    /// layout, so a loaded design builds it once.
    [[nodiscard]] static std::shared_ptr<const runtime::simir::RegionGraph>
    region_graph(const ElaboratedDesign& design);
};

} // namespace fsim::elaboration::detail
