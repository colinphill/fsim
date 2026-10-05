// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"
#include "../runtime/simir_process_program.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fsim::elaboration::detail {

struct RuntimeProcessProgramRow {
    std::uint32_t template_id { };
    runtime::simir::ProcessInstanceProgram instance;
};

struct RuntimeProcessProgramTable {
    std::vector<std::shared_ptr<const runtime::simir::ProcessProgramTemplate>>
        templates;
    std::vector<RuntimeProcessProgramRow> rows;
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
};

} // namespace fsim::elaboration::detail
