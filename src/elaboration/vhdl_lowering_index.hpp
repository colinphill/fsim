// SPDX-License-Identifier: Apache-2.0
//
// Design-wide facts about the VHDL HIR (and a few about the SystemVerilog
// HIR) that a hierarchy builder shares with the lowerers it creates.
#pragma once

#include "fsim/semantic/systemverilog_hir.hpp"
#include "fsim/semantic/vhdl_hir.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fsim::elaboration {

struct HirCallableBodyRecording;

/// Facts about one revision of the design's VHDL HIR, each derived on first
/// use and shared by the design's lowerers.
struct VhdlLoweringIndex {
    /// A design HIR item whose context can type a VHDL logic character
    /// literal (IEEE 1076-2008 9.3.2, 12.5): an expression, statement,
    /// declaration or type naming it directly or through an aggregate.
    struct CharacterLiteralItem {
        enum class Kind : std::uint8_t {
            expression,
            statement,
            declaration,
            type,
        };
        std::uint32_t literal { };
        Kind kind { };
        std::uint32_t position { };
    };
    const semantic::vhdl::Hir* hir { };
    std::uint64_t revision { };
    bool character_literals_built { };
    /// Sorted by literal.
    std::vector<CharacterLiteralItem> character_literals;
    /// Whether the design declares a protected type, without which no name
    /// denotes a protected method (IEEE 1076-2008 5.6.2).
    std::optional<bool> protected_types;
    /// Whether the design declares a record type, without which no function
    /// result is a partially constrained record (IEEE 1076-2008 5.3.3).
    std::optional<bool> record_types;
    /// Whether the design declares an access type, whose designated shapes
    /// the lowering learns as it allocates (IEEE 1076-2008 5.4).
    std::optional<bool> access_types;
    /// The callables whose formals a nested scope declares, by the
    /// callable's nested scope (IEEE 1076-2008 4.2.2).
    std::optional<std::unordered_map<std::uint32_t,
        std::vector<semantic::DeclarationId>>>
        callables_by_nested_scope;
    /// The package instantiation declarations, in declaration order
    /// (IEEE 1076-2008 4.9).
    std::optional<std::vector<semantic::DeclarationId>> package_instances;
    /// Positions in the HIR declarations of the shared variables with a
    /// declared subtype (IEEE 1076-2008 6.4.2.4).
    std::optional<std::vector<std::size_t>> shared_variables;
    /// Positions in the HIR statements of the loops with a parameter, by
    /// the loop's nested scope (IEEE 1076-2008 10.10).
    std::optional<std::unordered_map<std::uint32_t, std::vector<std::size_t>>>
        parameter_loops_by_scope;
    /// The sources of the external names each unit writes, by unit (IEEE
    /// 1076-2008 8.7).
    std::optional<std::unordered_map<std::uint32_t,
        std::vector<semantic::SourceSpanId>>>
        external_name_sources;

    /// Facts about one revision of the design's SystemVerilog HIR.
    struct SystemVerilogEvents {
        const semantic::sv::Hir* hir { };
        std::uint64_t revision { };
        /// Positions of the statements that wait on a condition or an event
        /// list, and of the processes with an event list (IEEE 1800-2017
        /// 9.4.2, 9.4.3).
        std::vector<std::size_t> statements;
        std::vector<std::size_t> processes;
    };
    std::optional<SystemVerilogEvents> systemverilog_events_;
    /// Lowered bodies of package-level VHDL functions, by frame key, which
    /// a later process replays instead of lowering the body again
    /// (Lowerer::record_hir_callable_body).
    std::unordered_map<std::string,
        std::shared_ptr<const HirCallableBodyRecording>>
        callable_bodies;

    /// The event-waiting statements and processes of `source`'s current
    /// revision.
    const SystemVerilogEvents& systemverilog_events(
        const semantic::sv::Hir& source)
    {
        if (systemverilog_events_ && systemverilog_events_->hir == &source
            && systemverilog_events_->revision == source.revision()) {
            return *systemverilog_events_;
        }
        auto& events = systemverilog_events_.emplace();
        events.hir = &source;
        events.revision = source.revision();
        const auto& statements = source.statements();
        for (std::size_t position = 0; position < statements.size();
             ++position) {
            const auto& statement = statements[position];
            if ((statement.kind == semantic::sv::StatementKind::wait_statement
                    && statement.condition)
                || !statement.sensitivities.empty()) {
                events.statements.push_back(position);
            }
        }
        const auto& processes = source.processes();
        for (std::size_t position = 0; position < processes.size();
             ++position) {
            if (!processes[position].sensitivities.empty()) {
                events.processes.push_back(position);
            }
        }
        return events;
    }

    /// This index for `source`'s current revision, cleared when stale.
    VhdlLoweringIndex& current(const semantic::vhdl::Hir& source)
    {
        if (hir != &source || revision != source.revision()) {
            *this = { };
            hir = &source;
            revision = source.revision();
        }
        return *this;
    }
};

} // namespace fsim::elaboration
