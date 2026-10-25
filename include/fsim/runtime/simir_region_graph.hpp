// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime::simir {

namespace region_graph_detail {
class RegionGraphProgramBuilder;
}

/// Capabilities requested by an observer, not an assertion that storage may
/// disappear. Execution must materialize the requested state before exposing it.
enum class RegionObservation : std::uint32_t {
    none = 0U,
    current = 1U << 0U,
    previous = 1U << 1U,
    drivers = 1U << 2U,
    pending = 1U << 3U,
    events = 1U << 4U,
    mutation = 1U << 5U,
    coverage = 1U << 6U,
    unknown = 1U << 7U,
};

constexpr RegionObservation operator|(RegionObservation left,
    RegionObservation right) noexcept
{
    return static_cast<RegionObservation>(static_cast<std::uint32_t>(left)
        | static_cast<std::uint32_t>(right));
}

enum class RegionDriverClass : std::uint8_t {
    undriven,
    single_whole,
    single_partial,
    disjoint_partial,
    resolved,
    unknown,
};

/// Unlike a scheduler phase, this describes the publication contract. In
/// particular VHDL projected transactions can never be folded into SV Active.
enum class RegionUpdateKind : std::uint8_t {
    generic,
    systemverilog_active,
    systemverilog_nba,
    vhdl_projected,
    systemc,
    mixed_or_unknown,
};

struct RegionSignalDescriptor {
    /// Zero is a valid empty VHDL array. Retain its identity and dependencies,
    /// but exclude its readers and writers from compiled region admission.
    std::uint32_t width { };
    ResolutionKind resolution { ResolutionKind::none };
    ValueKind value_kind { ValueKind::logic4 };
    bool implicit_driver { };
    bool external_driver { };
    bool event_variable { };
    RegionObservation observations { RegionObservation::none };
};

/// One signal that a direct container-object operation may access. `writable`
/// describes an effect of the object operation, including publication of a
/// derived aggregate view. Object element indexes are runtime registers, so
/// graph accesses conservatively cover the complete signal rather than
/// guessing a selected element.
struct RegionContainerSignalBinding {
    SignalId signal { };
    bool readable { };
    bool writable { };
    /// The object operation changes a derived signal view, but process driver
    /// ownership is represented by another binding (for example, aggregate
    /// array storage projected from its leaf signals).
    bool indirect_write { };
};

/// Static signal bindings for one ContainerObjectId. `complete` means every
/// signal effect of direct object reads/writes is represented by `bindings`;
/// a complete descriptor with no bindings is owning storage with no signal
/// bindings.
/// Cascading views and other unresolved bindings must remain incomplete.
struct RegionContainerDescriptor {
    ContainerObjectId object { };
    std::vector<RegionContainerSignalBinding> bindings;
    bool complete { };
};

/// Verified physical projection for one aggregate array signal. Offsets are
/// normalized to the least-significant bit of the proxy; each member covers
/// one complete declared-order element. This relationship adds access and
/// observation edges only. It is not a scheduler dependency edge by itself.
struct RegionSignalAliasLeaf {
    SignalId signal { };
    std::uint32_t ordinal { };
    std::uint32_t offset { };
    std::uint32_t width { };

    bool operator==(const RegionSignalAliasLeaf&) const = default;
};

struct RegionSignalAliasRange {
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };

    bool operator==(const RegionSignalAliasRange&) const = default;
};

struct RegionSignalAliasFamilyDescriptor {
    ContainerObjectId object { };
    SignalId proxy { };
    std::uint32_t width { };
    std::vector<RegionSignalAliasLeaf> leaves;
    bool complete { };
    bool proxy_readable { };
    bool proxy_writable { };

    /// Project a nonempty normalized proxy range into ordered leaf-local
    /// ranges. Width zero means from the offset through the proxy's end.
    /// Returns false for an incomplete family or a range outside its coverage.
    [[nodiscard]] bool project_range(std::uint32_t offset,
        std::uint32_t width,
        std::vector<RegionSignalAliasRange>& ranges) const;

    bool operator==(const RegionSignalAliasFamilyDescriptor&) const = default;
};

struct RegionAccess {
    ProcessId process { };
    std::uint32_t offset { };
    /// Zero width is the entire signal, including unknown dynamic selections.
    std::uint32_t width { };
    EdgeKind edge { EdgeKind::any };

    bool operator==(const RegionAccess&) const = default;
};

struct RegionSignalNode {
    RegionSignalDescriptor descriptor;
    RegionDriverClass drivers { RegionDriverClass::undriven };
    RegionObservation observations { RegionObservation::none };
    /// Current writer accesses or declared ownership could not be proven.
    bool writers_unknown { };
    /// Fork children may introduce new driver identities for these accesses.
    /// Structural regions treat this as unknown ownership. Existing guarded
    /// cohorts can use proven current owners until spawn invalidates them.
    bool dynamic_fork_writers { };
    bool partial_projected_transactions { };
    std::vector<RegionAccess> readers;
    std::vector<RegionAccess> writers;
};

struct RegionProcessNode {
    ProcessId process { };
    ProcessSchedulingDomain scheduling_domain { ProcessSchedulingDomain::generic };
    RegionUpdateKind update_kind { RegionUpdateKind::generic };
    std::vector<Sensitivity> sensitivities;
    std::vector<Sensitivity> reads;
    std::vector<Process::DriverRegion> writes;
    bool pure { };
    /// True only when every classified direct write matches one declared
    /// driver range at the value width inferred by the graph walk.
    bool operation_write_ranges_exact { };
    /// True only when at least one instruction has an unclassified or opaque
    /// signal-access effect. This is independent of `pure`.
    bool dependencies_unknown { };
    /// The first instruction whose access is opaque, when one is
    /// (diagnostics).
    std::uint32_t unknown_dependency_instruction {
        std::numeric_limits<std::uint32_t>::max()
    };
    /// Cycle classification within the pure scheduling/publication domain.
    /// Cross-domain or non-pure dependencies remain in the signal inventory.
    bool cyclic_or_dependent_on_cycle { };
};

/// One deterministic elaborated dependency graph: per-process and per-signal
/// access inventories plus aggregate-alias families. The static-kernel planner
/// and cone fusion consume it at elaboration time; the runtime does not build
/// one.
class RegionGraph {
public:
    /// process_access_complete, when supplied, is aligned with processes.
    /// False entries identify alternate executors whose signal effects are
    /// not bound to the supplied SimIR program; any such process makes the
    /// whole-graph access inventory incomplete.
    [[nodiscard]] static RegionGraph build(
        std::span<const Process* const> processes,
        std::span<const RegionSignalDescriptor> signals,
        std::span<const RegionContainerDescriptor> containers = { },
        std::span<const std::uint8_t> process_access_complete = { },
        std::span<const RegionSignalAliasFamilyDescriptor>
            signal_alias_families = { });

    [[nodiscard]] std::span<const RegionProcessNode> processes() const noexcept
    {
        return processes_;
    }
    [[nodiscard]] std::span<const RegionSignalNode> signals() const noexcept
    {
        return signals_;
    }
    [[nodiscard]] std::span<const RegionSignalAliasFamilyDescriptor>
    signal_alias_families() const noexcept
    {
        return signal_alias_families_;
    }
    /// False means an opaque process may access arbitrary signals. No signal
    /// can be hidden while this whole-graph dependency inventory is incomplete.
    [[nodiscard]] bool access_inventory_complete() const noexcept
    {
        return access_inventory_complete_;
    }

private:
    friend class region_graph_detail::RegionGraphProgramBuilder;

    std::vector<RegionProcessNode> processes_;
    std::vector<RegionSignalNode> signals_;
    std::vector<RegionSignalAliasFamilyDescriptor> signal_alias_families_;
    /// Family index for each proxy/leaf signal, or max() when unaliased.
    std::vector<std::size_t> signal_alias_family_by_signal_;
    /// True only for the aggregate proxy signal in the corresponding family.
    std::vector<std::uint8_t> signal_alias_is_proxy_;
    bool access_inventory_complete_ { true };
};

} // namespace fsim::runtime::simir
