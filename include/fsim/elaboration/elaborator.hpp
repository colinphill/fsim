// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_driver_inventory.hpp"
#include "fsim/runtime/vpi_types.hpp"
#include "fsim/semantic/hierarchy_path.hpp"
#include "fsim/systemc_abi.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {
class ProcessProgramView;
struct ProcessInstanceProgram;
struct StaticKernelRuntimeSpec;
struct ProcessProgramTemplate;
}

namespace fsim::semantic {
class CompiledDesign;
}

namespace fsim::semantic::sv {
class ClassSpecializationIndex;
struct ClassSpecialization;
}

namespace fsim::elaboration {

namespace detail {
class ElaboratedDesignProcessAccess;
class RuntimeProcessProgramTableBuilder;
struct RuntimeProcessProgramTable;
}

struct ElaborationResult;
struct CoverageHirContext;
class Lowerer;
class HierarchyBuilder;

struct Binding {
    std::string instance;
    std::optional<std::string> target;
    std::optional<std::string> resolver;
};

/// A port of an SDF entry, with the bit or range its index selects.
struct TimingGenericPort {
    std::string name;
    bool indexed {};
    std::int64_t left {};
    std::int64_t right {};

    friend bool operator==(const TimingGenericPort&,
        const TimingGenericPort&) = default;
};

/// The location of one source in the multiple source mode: the delays of an
/// interconnect delay generic that the source's INTERCONNECT entries and the
/// PORT entries left, in femtoseconds (5.2.7.1.5).
struct VitalWireSourceLocation {
    std::string source_instance;
    std::string source_port;
    std::vector<std::int64_t> slots;
};

/// One element of an interconnect delay generic (tipd_) in the multiple
/// source mode (the whole generic when it is scalar): `slots` is the
/// element's value, which a source that no INTERCONNECT entry names keeps,
/// and `sources` the locations of the sources the entries name for it. An
/// element that no second source reached has no sources and keeps the
/// single source behaviour (5.2.7.1.5).
struct VitalWireElement {
    std::vector<std::int64_t> slots;
    std::vector<VitalWireSourceLocation> sources;
};

/// An interconnect delay generic of an instance with the multiple source mode
/// on at least one element. A vector delay generic (`VitalDelayArrayType*`)
/// has one element for each index of its range, the first at index `left`;
/// the scalar generic has one element.
struct VitalWireLocations {
    std::string instance;
    std::string generic;
    bool vector_form {};
    std::int64_t left {};
    std::vector<VitalWireElement> elements;
};

/// One backannotation of a timing generic from SDF data (IEEE 1076.4-2000
/// 5.1.1, 5.2). The annotator names the generic by the VITAL SDF map; the
/// elaborator, which knows the generic's declared type, applies the value
/// before the instance is lowered, so the model starts with the annotated
/// state (5.1: the state at the beginning of simulation is the same however
/// the annotation was made).
struct TimingGenericAnnotation {
    /// Elaborated instance path, the root alias first.
    std::string instance;
    /// The generic name the VITAL SDF map constructs (5.2.7).
    std::string generic;
    /// INCREMENT adds to the generic's current value; ABSOLUTE replaces it
    /// (5.2.7.1.1).
    bool increment {};
    /// The delval_list or rvalue after the minimum, typical or maximum
    /// selection, in femtoseconds, extended to one, two, three, six or
    /// twelve entries (5.2.6); an empty entry is an SDF null value that
    /// leaves its transition unchanged.
    std::vector<std::optional<std::int64_t>> values;
    /// Where the SDF entry is, for the diagnostics.
    std::string source;
    /// The ports of the SDF entry that make up the generic name (one or two,
    /// in SDF order), with their bit or range index; a vector delay generic
    /// takes the element each index selects (5.2.7.3.3).
    std::vector<TimingGenericPort> ports;
    /// The RETAIN values of an IOPATH, up to three (5.2.6, Table 2).
    std::vector<std::optional<std::int64_t>> retain;
    /// An INTERCONNECT entry (5.2.7.1.4), with the elaborated path of the
    /// instance that holds its source port and the source port's name (with
    /// its select). Two sources of one generic make the multiple source mode
    /// of 5.2.7.1.5.
    bool interconnect {};
    std::string source_instance;
    std::string source_port;

    friend bool operator==(const TimingGenericAnnotation&,
        const TimingGenericAnnotation&) = default;
};

/// One independently selected simulation root. `target` follows the same
/// qualified or inferred spelling as the legacy singular top; `alias` is the
/// stable first hierarchy component visible to tracing and debugging.
struct Root {
    std::string target;
    std::string alias;
    // Generic and parameter overrides for this root (`--generic`), applied
    // to the generics the root declares by that name.
    std::vector<std::pair<std::string, std::string>> generics {};
    // Backannotation of VITAL timing generics from SDF data, applied to the
    // VHDL instances below this root as they are elaborated.
    std::vector<TimingGenericAnnotation> timing_generics {};
};

/// Parser-independent identity of a class made visible by one selected
/// SystemVerilog generate branch. The CompiledDesign supplied to elaboration
/// owns the declaration and every executable member referenced by this
/// record.
struct SelectedSystemVerilogClass {
    semantic::UnitId unit;
    semantic::DeclarationId generate_owner;
    semantic::ScopeId declaration_scope;
    semantic::OriginId origin;
    std::string declaration_identity;

    friend bool operator==(
        const SelectedSystemVerilogClass&,
        const SelectedSystemVerilogClass&) = default;
};

struct PackedTypeMetadata;

struct PackedMemberMetadata {
    std::string name;
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    std::string spelling;
    std::optional<frontend::PackedRange> packed_range;
    bool is_signed { };
    std::uint64_t lsb_offset { };
    frontend::SourceSpan span;
    std::vector<PackedTypeMetadata> nested_types;

    [[nodiscard]] std::optional<std::uint64_t> width() const noexcept;
};

struct VhdlArrayDimensionMetadata {
    std::string index_subtype;
    frontend::SourceSpan index_span;
    std::optional<frontend::IntegerRange> index_base_range;
    std::optional<frontend::IntegerRange> range;
    bool null { };
    std::uint64_t stride { };
    bool unconstrained { };
};

struct VhdlArrayMetadata {
    std::string index_subtype;
    frontend::SourceSpan index_span;
    std::optional<frontend::IntegerRange> index_base_range;
    std::string element_spelling;
    std::string element_named_type;
    frontend::SourceSpan element_span;
    frontend::ValueDomain element_domain { frontend::ValueDomain::Unknown };
    bool unconstrained { };
    std::optional<std::uint64_t> flat_width;
    std::vector<VhdlArrayDimensionMetadata> dimensions;
    std::vector<PackedTypeMetadata> element_types;
};

struct VhdlAccessMetadata {
    std::vector<PackedTypeMetadata> designated_types;
    frontend::SourceSpan designated_span;
    std::uint32_t handle_width { 32U };
    std::uint32_t maximum_objects {
        std::numeric_limits<std::uint32_t>::max()
    };
    bool nullable { true };
    bool owns_designated_object { true };
    bool deallocate_releases_storage { true };
    bool reclaim_when_unreachable { };
    bool simulation_lifetime { true };
};

struct VhdlPhysicalUnitMetadata {
    std::string name;
    std::optional<std::int64_t> scale_factor;
    frontend::SourceSpan span;
};

struct VhdlPhysicalMetadata {
    std::vector<VhdlPhysicalUnitMetadata> units;
    std::optional<frontend::IntegerRange> resolved_range;
};

/// Concrete type metadata retained across elaboration and DesignIR
/// construction. It contains recursive layout and source identity, but cannot
/// own syntax expressions, statements, or parser type nodes.
struct PackedTypeMetadata {
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    std::string spelling;
    frontend::SystemVerilogScalarKind systemverilog_scalar {
        frontend::SystemVerilogScalarKind::None
    };
    std::string systemverilog_net_type;
    std::string systemverilog_resolution_function;
    std::optional<frontend::PackedRange> packed_range;
    bool is_signed { };
    std::string named_type;
    frontend::SourceSpan named_type_span;
    std::optional<frontend::IntegerRange> integer_range;
    std::optional<frontend::IntegerRange> integer_base_range;
    std::uint8_t vhdl_integer_storage_width { };
    std::string nominal_type;
    std::string vhdl_type_declaration;
    std::string vhdl_resolution_function;
    std::vector<std::string> enumeration_literals;
    std::optional<frontend::EnumerationRange> enumeration_range;
    std::optional<frontend::EnumerationRange> enumeration_base_range;
    std::shared_ptr<VhdlArrayMetadata> vhdl_array;
    std::shared_ptr<VhdlAccessMetadata> vhdl_access;
    std::shared_ptr<VhdlPhysicalMetadata> vhdl_physical;
    std::vector<PackedMemberMetadata> packed_members;
    frontend::PackedAggregateKind packed_aggregate {
        frontend::PackedAggregateKind::None
    };

    PackedTypeMetadata() = default;
    PackedTypeMetadata(
        const frontend::ValueDomain domain_value,
        std::string spelling_value,
        std::optional<frontend::PackedRange> range_value,
        const bool signed_value)
        : domain(domain_value)
        , spelling(std::move(spelling_value))
        , packed_range(std::move(range_value))
        , is_signed(signed_value)
    {
    }

    [[nodiscard]] std::optional<std::uint64_t> width() const noexcept
    {
        if (systemverilog_scalar
            == frontend::SystemVerilogScalarKind::ShortReal) {
            return 32U;
        }
        if (systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Real
            || systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Realtime
            || systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Time
            || systemverilog_scalar
                == frontend::SystemVerilogScalarKind::Chandle) {
            return 64U;
        }
        if (vhdl_array && vhdl_array->flat_width) {
            return vhdl_array->flat_width;
        }
        if (packed_range) {
            return packed_range->width();
        }
        if (vhdl_array || !packed_members.empty()) {
            return std::nullopt;
        }
        switch (domain) {
        case frontend::ValueDomain::Bit2:
        case frontend::ValueDomain::Logic4:
        case frontend::ValueDomain::Logic9:
        case frontend::ValueDomain::Boolean:
            return 1U;
        case frontend::ValueDomain::Integer:
            return vhdl_integer_storage_width != 0U
                ? std::optional<std::uint64_t> {
                      vhdl_integer_storage_width }
                : std::optional<std::uint64_t> { 32U };
        case frontend::ValueDomain::String:
        case frontend::ValueDomain::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }
};

inline std::optional<std::uint64_t>
PackedMemberMetadata::width() const noexcept
{
    if (!nested_types.empty()) {
        return nested_types.front().width();
    }
    if (packed_range) {
        return packed_range->width();
    }
    switch (domain) {
    case frontend::ValueDomain::Bit2:
    case frontend::ValueDomain::Logic4:
    case frontend::ValueDomain::Logic9:
    case frontend::ValueDomain::Boolean:
        return 1U;
    case frontend::ValueDomain::Integer:
        return 32U;
    case frontend::ValueDomain::String:
    case frontend::ValueDomain::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

struct ExternalPort {
    std::uint64_t handle { };
    std::string name;
    PackedTypeMetadata type;
    frontend::PortDirection direction {
        frontend::PortDirection::Unknown
    };
    std::uint64_t bound_object { };
};

struct ExternalSensitivity {
    std::uint64_t object { };
    // Store untrusted ABI metadata as an integer so validation can inspect an
    // out-of-range value without first performing an undefined C++ enum load.
    std::uint32_t edge { FSIM_SC_ANY_EDGE };
};

struct ExternalProcess {
    std::uint64_t handle { };
    std::string name;
    fsim_sc_process_entry_v1 entry { };
    void* user { };
    std::vector<ExternalSensitivity> sensitivity;
};

struct ExternalInternalSignal {
    std::uint64_t handle { };
    std::string name;
    PackedTypeMetadata type;
    runtime::PackedLogic4 initial_value;
};

struct ExternalExport {
    std::uint64_t handle { };
    std::string name;
    PackedTypeMetadata type;
    std::uint64_t bound_object { };
    bool writable { true };
};

/// Immutable, ABI-neutral description produced by one constructed SystemC
/// elaboration factory. `path` is the full DesignIR instance path and `target`
/// is the canonical `systemc:plugin.factory` manifest spelling.
struct SystemCInstanceDescription {
    std::string path;
    std::string target;
    std::uint64_t handle { };
    std::uint64_t parent { };
    std::vector<std::pair<std::string, std::int64_t>>
        construction_values;
    std::vector<ExternalPort> ports;
    std::vector<ExternalProcess> processes;
    std::vector<ExternalInternalSignal> internal_signals;
    std::vector<ExternalExport> exports;
    std::vector<SystemCInstanceDescription> native_children;
    // Canonical typed construction values supplied by the authoritative HDL
    // specialization path. Kept trailing for source compatibility with
    // aggregate descriptions produced by existing plug-in bridges.
    std::vector<std::pair<std::string, std::string>>
        construction_identity_values;
    // A native child that stands for an HDL design unit (SystemC source
    // instantiating SystemVerilog or VHDL): the unit, spelled like a
    // project top, and its integer parameter or generic actuals.
    struct HdlChild {
        std::string unit;
        std::vector<std::pair<std::string, std::int64_t>> actuals;
    };
    std::optional<HdlChild> hdl;
};

struct SystemCConstructionParameter {
    std::string name;
    fsim_sc_construction_type_v1 type {
        FSIM_SC_CONSTRUCTION_INTEGER
    };
    std::optional<std::int64_t> default_value;
};

struct SystemCFactoryCandidate {
    std::string library;
    std::string name;
    std::string target;
};

/// Application-owned bridge used by the authoritative hierarchy walk to
/// inspect a registered factory schema and construct an HDL-bound SystemC
/// instance only after source-language actuals have been canonicalized.
class SystemCFactoryProvider {
public:
    virtual ~SystemCFactoryProvider() = default;

    [[nodiscard]] virtual std::vector<SystemCFactoryCandidate>
    candidates() const = 0;

    // Logical-library availability is distinct from exporting at least one
    // factory. The default preserves source compatibility for providers which
    // expose only candidate records.
    [[nodiscard]] virtual std::vector<std::string> libraries() const
    {
        std::vector<std::string> result;
        for (const auto& candidate : candidates()) {
            if (std::ranges::find(result, candidate.library)
                == result.end()) {
                result.push_back(candidate.library);
            }
        }
        return result;
    }

    [[nodiscard]] virtual std::optional<
        std::vector<SystemCConstructionParameter>>
    schema(std::string_view target, std::string& error) = 0;

    [[nodiscard]] virtual std::optional<SystemCInstanceDescription>
    instantiate(
        std::string_view path,
        std::string_view target,
        std::span<const std::pair<std::string, std::int64_t>>
            construction_values,
        std::string& error) = 0;
};

/// A user-defined system task or function that a foreign library registered
/// before elaboration (vpi_register_systf, IEEE 1800-2023 38.36). A call of
/// one lowers to a foreign call boundary instead of a built-in system task.
struct UserSystemTask {
    std::string name;
    bool function { };
    /// Result width of a function (vpiIntFunc 32, vpiSizedFunc by sizetf).
    std::uint32_t width { 1U };
    bool is_signed { };
};

/// Identity prefix, after the DPI function prefix, of a user system task call
/// boundary. The full identity is
/// `$vpi:<name>|<scope>|<file>|<line>|<column>`.
inline constexpr std::string_view user_system_call_identity_prefix {
    "$vpi:" };

/// Identity prefix, after the DPI function prefix, of a call of a foreign
/// VHDL subprogram's VHPI execution function. The full identity is
/// `$vhpi:<library>\t<model>\t<name>\t<declaration>\t<scope>\t<width>`, and
/// each actual name is `<formal>\t<declaration>`. A standard direct
/// binding (IEEE 1076-2019 20.2.4.3) adds `\t=<execution function>`: its
/// library is the object library path ("null" for a null specifier) and its
/// model the subprogram designator.
inline constexpr std::string_view vhdl_foreign_call_identity_prefix {
    "$vhpi:" };

/// Suffix of the name of the placeholder process of a foreign VHDL
/// architecture's instance, after the instance path (IEEE 1076-2019 20.4).
inline constexpr std::string_view vhdl_foreign_architecture_process_suffix {
    ".$foreign" };

/// Parser-free compiled-HIR elaboration. Unsupported residual structures are
/// diagnosed instead of being reconstructed as syntax nodes.
[[nodiscard]] ElaborationResult elaborate(
    const semantic::CompiledDesign& compiled,
    std::string_view top);
[[nodiscard]] ElaborationResult elaborate(
    const semantic::CompiledDesign& compiled,
    std::span<const Root> roots,
    std::span<const Binding> bindings,
    std::span<const SystemCInstanceDescription> systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    std::span<const std::string> search_libraries,
    const CoverageHirContext* coverage = nullptr,
    std::span<const UserSystemTask> user_system_tasks = { });

struct Diagnostic {
    std::string code;
    std::string message;
    frontend::SourceSpan span;
    // The unknown system task or function a lowering failure names, for
    // cbUnresolvedSystf (IEEE 1800-2023 38.36.2).
    std::string system_call {};
};

/// The name checks of one VHDL architecture without elaborating what it
/// instantiates (compile-time analysis of architectures with instances).
[[nodiscard]] std::vector<Diagnostic> validate_vhdl_architecture_names(
    const semantic::CompiledDesign& compiled,
    semantic::UnitId architecture);

struct VhdlModeViewElementBinding {
    std::string formal_path;
    std::string actual_path;
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
    frontend::SourceSpan source;
    // Explicit packed-storage endpoint after every record selection and
    // concrete array index has been elaborated. A view never creates separate
    // storage: each leaf addresses the associated signal directly.
    runtime::simir::SignalId signal { };
    std::uint64_t lsb_offset { };
    std::uint64_t width { };
};

struct VhdlModeViewBinding {
    std::string formal;
    std::string view;
    frontend::VhdlModeViewIndicationKind kind {
        frontend::VhdlModeViewIndicationKind::record
    };
    std::vector<VhdlModeViewElementBinding> elements;
    frontend::SourceSpan source;
};

// The array of named events a signal is an element of (IEEE 1800-2023 7.4,
// 37.43): the declared name and the array's typespec, whose range gives the
// elements their indexes in declaration order, and the element's position
// in that order.
struct VpiEventArrayDeclaration {
    std::string name;
    std::shared_ptr<const runtime::SystemVerilogVpiTypespec> typespec { };
    std::uint32_t position { };
};
struct SignalInfo {
    runtime::simir::SignalId id { };
    std::string name;
    std::size_t width { };
    std::string type_name;
    frontend::ValueDomain source_domain { frontend::ValueDomain::Unknown };
    frontend::SystemVerilogScalarKind systemverilog_scalar {
        frontend::SystemVerilogScalarKind::None
    };
    std::string systemverilog_net_type;
    bool is_signed { };
    std::optional<frontend::PackedRange> packed_range;
    // VHDL-only type metadata is large (physical-type metadata alone exceeds
    // one KiB) and absent from the overwhelmingly common scalar/SV signal.
    // Box it so every SignalInfo does not reserve storage for inactive
    // language-specific alternatives.
    std::shared_ptr<VhdlArrayMetadata> vhdl_array;
    std::shared_ptr<VhdlAccessMetadata> vhdl_access;
    std::shared_ptr<VhdlPhysicalMetadata> vhdl_physical;
    std::vector<PackedMemberMetadata> packed_members;
    std::optional<frontend::IntegerRange> integer_range;
    std::string nominal_type;
    std::vector<std::string> enumeration_literals;
    std::optional<frontend::EnumerationRange> enumeration_range;
    bool is_port { };
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
    frontend::SourceSpan declaration_span;
    // A composite signal can cross several view-based VHDL boundaries. Each
    // binding retains the declaration-order formal-to-actual leaf map; Batch
    // 184 materializes these leaves as directional runtime endpoints.
    std::vector<VhdlModeViewBinding> vhdl_mode_view_bindings;
    runtime::simir::ResolutionKind resolution {
        runtime::simir::ResolutionKind::none
    };
    // The declared SystemVerilog type of a structure, union, enumeration or
    // packed array of them, as VPI describes it (IEEE 1800-2023 37.17,
    // 37.18, 37.26); empty for every other signal.
    std::shared_ptr<const runtime::SystemVerilogVpiTypeDescriptor>
        vpi_declared_type { };
    // The declaration's typespec as VPI describes it (IEEE 1800-2023
    // 37.25); empty when its type has no typespec model.
    std::shared_ptr<const runtime::SystemVerilogVpiTypespec>
        vpi_typespec { };
    // Set on each element of an array of named events.
    std::shared_ptr<const VpiEventArrayDeclaration> vpi_event_array { };
};

/// The signal descriptions of a design. While elaboration builds the design
/// the table grows in fixed blocks, so a reference to a description stays
/// valid when later signals are added (lowering adds implicit and package
/// constant signals while it holds descriptions). `seal` moves the
/// descriptions into one contiguous vector for the finished design.
class SignalInfoTable final {
public:
    SignalInfoTable() = default;
    explicit SignalInfoTable(std::vector<SignalInfo> sealed) noexcept;
    SignalInfoTable(const SignalInfoTable& other);
    SignalInfoTable(SignalInfoTable&& other) noexcept;
    SignalInfoTable& operator=(const SignalInfoTable& other);
    SignalInfoTable& operator=(SignalInfoTable&& other) noexcept;
    ~SignalInfoTable();

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
    [[nodiscard]] SignalInfo& operator[](const std::size_t index) noexcept
    {
        return sealed_flag_ ? sealed_[index]
                            : blocks_[index >> block_bits][index & block_mask];
    }
    [[nodiscard]] const SignalInfo& operator[](
        const std::size_t index) const noexcept
    {
        return sealed_flag_ ? sealed_[index]
                            : blocks_[index >> block_bits][index & block_mask];
    }
    [[nodiscard]] SignalInfo& at(std::size_t index);
    [[nodiscard]] const SignalInfo& at(std::size_t index) const;
    void push_back(const SignalInfo& signal);
    void push_back(SignalInfo&& signal);
    void pop_back();

    /// True once the descriptions are contiguous.
    [[nodiscard]] bool sealed() const noexcept { return sealed_flag_; }
    void seal();
    /// The contiguous descriptions; the table must be sealed.
    [[nodiscard]] const std::vector<SignalInfo>& contiguous() const;
    [[nodiscard]] std::vector<SignalInfo> to_vector() const;
    [[nodiscard]] std::vector<SignalInfo> take_vector() &&;

    template <typename Table, typename Value>
    class basic_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = SignalInfo;
        using difference_type = std::ptrdiff_t;
        using pointer = Value*;
        using reference = Value&;
        basic_iterator() = default;
        basic_iterator(Table* table, const std::size_t index) noexcept
            : table_ { table }, index_ { index }
        {
        }
        [[nodiscard]] reference operator*() const noexcept
        {
            return (*table_)[index_];
        }
        [[nodiscard]] pointer operator->() const noexcept
        {
            return &(*table_)[index_];
        }
        basic_iterator& operator++() noexcept
        {
            ++index_;
            return *this;
        }
        basic_iterator operator++(int) noexcept
        {
            auto previous = *this;
            ++index_;
            return previous;
        }
        [[nodiscard]] friend bool operator==(
            const basic_iterator& left, const basic_iterator& right) noexcept
        {
            return left.index_ == right.index_;
        }

    private:
        Table* table_ { };
        std::size_t index_ { };
    };
    using iterator = basic_iterator<SignalInfoTable, SignalInfo>;
    using const_iterator
        = basic_iterator<const SignalInfoTable, const SignalInfo>;
    [[nodiscard]] iterator begin() noexcept { return { this, 0U }; }
    [[nodiscard]] iterator end() noexcept { return { this, size_ }; }
    [[nodiscard]] const_iterator begin() const noexcept
    {
        return { this, 0U };
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return { this, size_ };
    }

private:
    static constexpr std::size_t block_bits = 8U;
    static constexpr std::size_t block_size = std::size_t { 1 } << block_bits;
    static constexpr std::size_t block_mask = block_size - 1U;
    void clear_blocks() noexcept;
    SignalInfo& append_slot();

    // Uninitialized storage for `block_size` descriptions each; the first
    // `size_` slots in order hold constructed descriptions.
    std::vector<SignalInfo*> blocks_;
    std::vector<SignalInfo> sealed_;
    std::size_t size_ { };
    bool sealed_flag_ { };
};

/// Read access to signal descriptions in a table or a contiguous range.
class SignalInfoView final {
public:
    SignalInfoView(const SignalInfoTable& table) noexcept
        : table_ { &table }, size_ { table.size() }
    {
    }
    SignalInfoView(const std::span<const SignalInfo> signals) noexcept
        : span_ { signals }, size_ { signals.size() }
    {
    }
    SignalInfoView(const std::vector<SignalInfo>& signals) noexcept
        : SignalInfoView { std::span<const SignalInfo> { signals } }
    {
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] const SignalInfo& operator[](
        const std::size_t index) const noexcept
    {
        return table_ != nullptr ? (*table_)[index] : span_[index];
    }

private:
    const SignalInfoTable* table_ { };
    std::span<const SignalInfo> span_;
    std::size_t size_ { };
};

/// One explicit cross-language scalar or vector boundary conversion.
///
/// Packed values store the leftmost declared element at the most-significant
/// ordinal independently of source-language index spelling. Compatible
/// ordinal and Logic4/Logic9 boundaries share one scheduler signal. Width,
/// signedness, Boolean, and integer conversions use a separately owned formal
/// signal and deterministic adapter process. This record retains both
/// language-local profiles and the connection's ownership and source identity
/// for diagnostics, debugging, and cache projection.
enum class BoundaryConversionKind : std::uint8_t {
    ordinal_alias,
    width_adapter,
    signedness_adapter,
    width_signedness_adapter,
    boolean_adapter,
    integer_adapter,
    state_domain_alias,
};

struct BoundaryConversionInfo {
    BoundaryConversionKind kind { BoundaryConversionKind::ordinal_alias };
    std::string path;
    runtime::simir::SignalId formal_signal { };
    runtime::simir::SignalId actual_signal { };
    std::optional<runtime::simir::ProcessId> process;
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
    std::size_t formal_width { };
    std::size_t actual_width { };
    frontend::ValueDomain formal_domain { frontend::ValueDomain::Unknown };
    frontend::ValueDomain actual_domain { frontend::ValueDomain::Unknown };
    bool formal_signed { };
    bool actual_signed { };
    bool state_domain_changed { };
    std::optional<frontend::PackedRange> formal_range;
    std::optional<frontend::PackedRange> actual_range;
    std::optional<frontend::IntegerRange> formal_integer_range;
    std::optional<frontend::IntegerRange> actual_integer_range;
    frontend::SourceSpan connection_span;
    frontend::SourceSpan formal_span;
    frontend::SourceSpan actual_span;
};

struct StringObjectInfo {
    runtime::simir::StringObjectId id { };
    std::string name;
    frontend::SourceSpan declaration_span;
    bool is_port { };
    frontend::PortDirection direction {
        frontend::PortDirection::Unknown
    };
};

/// The name prefix of the container object that holds a class's static
/// array, queue or associative array of values; the storage identity (the
/// property's, or `<specialization>::<name>` for a specialization with
/// statics of its own) follows (IEEE 1800-2023 8.9, 8.25.1).
inline constexpr std::string_view class_static_container_prefix {
    "$class-static:"
};

struct ContainerObjectInfo {
    runtime::simir::ContainerObjectId id { };
    std::string name;
    runtime::simir::ContainerType type;
    frontend::SourceSpan declaration_span;
    bool is_port { };
    frontend::PortDirection direction {
        frontend::PortDirection::Unknown
    };
    std::optional<runtime::simir::ContainerSliceAlias> slice_alias;
    // A SystemVerilog unpacked array of nets, and the declared type of its
    // elements when VPI describes it as a structure, union, enumeration or
    // packed array of them (IEEE 1800-2023 37.17, 37.18).
    bool net { };
    std::shared_ptr<const runtime::SystemVerilogVpiTypeDescriptor>
        vpi_element_type { };
    // The array's typespec (IEEE 1800-2023 37.25).
    std::shared_ptr<const runtime::SystemVerilogVpiTypespec>
        vpi_typespec { };
};

using VhdlProtectedObjectId = std::uint32_t;

struct VhdlProtectedMemberInfo {
    std::string name;
    bool is_signed { };
    std::size_t offset { };
    std::size_t width { };
    runtime::simir::ContainerObjectId storage { };
    frontend::SourceSpan declaration_span;
};

/// One constructed VHDL shared variable of protected type. Private members
/// use independently addressable global container objects, while this record
/// preserves their common object identity and declaration-ordered layout.
struct VhdlProtectedObjectInfo {
    VhdlProtectedObjectId id { };
    std::string name;
    std::string type_name;
    std::string nominal_type;
    std::vector<VhdlProtectedMemberInfo> members;
    frontend::SourceSpan declaration_span;
};

using SpecializationId = std::uint32_t;

/// The ordinal offset of an object declared after others in the same
/// declaration (`signal a, b : t;`), whose points share its span.
[[nodiscard]] constexpr std::uint64_t coverage_object_ordinal_base(
    const std::uint32_t declared_together) noexcept
{
    return std::uint64_t { declared_together } << 32U;
}

/// The reason of a state machine's informational other-transition row.
inline constexpr std::string_view kCoverageFsmOtherTransitionReason {
    "informational: changes between states that no case arm assigns"
};

/// The reason of a memory's toggle row: memories are excluded by default.
inline constexpr std::string_view kCoverageToggleMemoryReason {
    "memory: arrays of words are excluded from toggle coverage by default"
};

/// One object of an instance that a generated monitor process covers for
/// toggles: its declaration, width in bits, and position among the objects
/// declared with it.
struct CoverageToggleObject {
    semantic::SourceSpanId declaration;
    std::uint32_t width { };
    std::uint32_t declared_together { };
    // Non-empty for an object reported as excluded, as one uncounted row.
    std::string exclusion_reason;
    // The object's name, for external exclusions.
    std::string name;

    friend bool operator==(
        const CoverageToggleObject&, const CoverageToggleObject&) = default;
};

/// A transition between two distinct states of a covered state machine, by
/// state index.
struct CoverageFsmTransition {
    std::uint32_t from { };
    std::uint32_t to { };

    friend auto operator<=>(
        const CoverageFsmTransition&, const CoverageFsmTransition&) = default;
};

/// A state machine of an instance that a generated monitor process covers:
/// the state object's declaration, signal and position among the objects
/// declared with it, the value of each state (in enumeration order), and
/// the legal transitions the case arms assign.
struct CoverageFsmMachine {
    semantic::SourceSpanId declaration;
    runtime::simir::SignalId signal { };
    std::uint32_t declared_together { };
    std::vector<std::uint64_t> values;
    std::vector<CoverageFsmTransition> transitions;
    // The state object's name, for external exclusions.
    std::string name;

    friend bool operator==(
        const CoverageFsmMachine&, const CoverageFsmMachine&) = default;
};

/// One elaborated design-unit occurrence and its directly owned processes.
///
/// Parameterized VHDL and Verilog/SystemVerilog occurrences carry canonical
/// generic/parameter values after constant evaluation. Each occurrence remains
/// an explicit specialization record so process ownership and cache identity
/// never depend on recovering hierarchy from process-name strings.
/// A typedef an instance declares, as a typespec (IEEE 1800-2023 37.5, 37.12
/// vpiTypedef; 37.25); empty when its type has no typespec model.
struct VpiTypedefInfo {
    std::string name;
    std::shared_ptr<const runtime::SystemVerilogVpiTypespec> typespec { };
};

/// A parameter or local parameter a generate block declares, with its
/// elaborated value (IEEE 1800-2023 27.5, 37.28, 37.85).
struct VpiGenerateParameterInfo {
    std::string name;
    std::string identity;
    semantic::DeclarationId declaration;
    bool local { };
    // The typespec of the typedef the parameter is declared with (37.28
    // vpiTypespec); null for a built-in type.
    std::shared_ptr<const runtime::SystemVerilogVpiTypespec> typespec { };
};

/// One generate block occurrence of an instance: its path relative to the
/// instance (`blk`, `loop[2].inner`) and the parameters it declares (IEEE
/// 1800-2023 27.6, 37.85).
struct VpiGenerateScopeInfo {
    std::string path;
    // The generate region the elaborator selected for the occurrence, which
    // tells the branches of an if that share one scope name apart (27.5).
    semantic::DeclarationId region;
    std::vector<VpiGenerateParameterInfo> parameters;
    // The typedefs the block declares, in declaration order (IEEE 1800-2023
    // 37.85 detail 5).
    std::vector<VpiTypedefInfo> typedefs;
};

/// A package parameter or local parameter with its elaborated value (IEEE
/// 1800-2023 3.12.1, 26.2, 37.10, 37.28). The package is named by its
/// library and name, `lib::name`.
struct VpiPackageParameterInfo {
    std::string package;
    std::string name;
    std::string identity;
    semantic::DeclarationId declaration;
    bool local { };
    // The typespec of the parameter's type (37.28 vpiTypespec); null for a
    // built-in type.
    std::shared_ptr<const runtime::SystemVerilogVpiTypespec> typespec { };
    // A class parameter (vpi_class_parameters): the class specialization
    // whose value it records, empty for the class declaration's own; and
    // whether it is a type parameter, whose typespec is the type it names
    // and which has no value (IEEE 1800-2023 37.28, 37.31, 37.32).
    std::string specialization { };
    bool type_parameter { };
};

/// A typedef a package or `$unit` declares, as a typespec (IEEE 1800-2023
/// 3.12.1, 37.10 vpiTypedef). The package is named by its library and name,
/// `lib::name`.
/// The type a cast `T'(x)` to a named type resolved to, as a typespec (IEEE
/// 1800-2023 6.24.1, 37.59 details 3 and 5): the compiler resolved the name
/// where the cast is, through the visibility rules of 26.3. `expression` is
/// the HIR cast expression.
struct VpiCastTypespecInfo {
    semantic::ExpressionId expression;
    std::shared_ptr<const runtime::SystemVerilogVpiTypespec> typespec { };
};

struct VpiPackageTypedefInfo {
    std::string package;
    std::string name;
    std::shared_ptr<const runtime::SystemVerilogVpiTypespec> typespec { };
    // A class property's typespec is recorded for the class declaration
    // (empty) and for each specialization of it, by its identity (IEEE
    // 1800-2023 37.32, 37.33): a class parameter an actual reads has its
    // value there.
    std::string specialization { };
};

// A call of a user task or function in an instance's unit with the callable
// the lowerer bound it to (IEEE 1800-2023 37.42 vpiFunction, 37.38
// vpiTask): the call expression (or the task call statement), the callable's
// declaration, and the path of the instance that owns the callable; empty
// for one of the caller's own instance or of a package.
struct VpiCalleeInfo {
    bool statement { };
    std::uint32_t id { };
    semantic::DeclarationId declaration;
    std::string instance;
    // The DPI import a call is bound to (IEEE 1800-2023 35.5.4, 37.40);
    // `declaration` is then unset.
    semantic::OriginId dpi { };
};

struct SpecializationInfo {
    SpecializationId id { };
    std::string unit;
    std::string instance;
    // Exact semantic provenance for compiled-HIR hierarchy occurrences.
    // Transitional syntax-only callers may leave these unset while their
    // lowering path is ported.
    std::optional<semantic::UnitId> source_unit;
    std::optional<semantic::InstanceId> source_instance;
    std::optional<semantic::SourceSpanId> source_span;
    std::optional<semantic::OriginId> origin;
    std::vector<runtime::simir::ProcessId> processes;
    std::string source;
    // Additional source roots that define this specialization's interface,
    // such as a VHDL entity paired with an architecture in another file.
    std::vector<std::string> source_dependencies;
    frontend::Language language { frontend::Language::SystemVerilog2017 };
    std::string library { "work" };
    bool is_cell { };
    // Canonical name/value pairs after frontend generic/parameter evaluation.
    // Empty for unparameterized units.
    std::vector<std::pair<std::string, std::string>> parameter_values;
    // Versioned semantic identities used by the native-object cache when a
    // display value alone would lose width, signedness, state, or type
    // metadata. Empty entries fall back to parameter_values.
    std::vector<std::pair<std::string, std::string>>
        parameter_identity_values;
    // Direct provenance for processes lowered from language HIR. Synthetic
    // concurrent, boundary, timing, and foreign processes intentionally do
    // not appear here; callers must not infer semantic ownership by position
    // in `processes`. Kept trailing for source compatibility with existing
    // aggregate descriptions.
    std::vector<std::pair<runtime::simir::ProcessId, semantic::ProcessId>>
        semantic_processes;
    // The source extent of the continuous assignment, net declaration
    // assignment or gate each concurrent SystemVerilog process was lowered
    // from, so that the VPI driver of a net finds the statement that made it
    // (IEEE 1800-2023 37.21).
    std::vector<std::pair<runtime::simir::ProcessId, semantic::SourceSpanId>>
        statement_sources;
    // Objects whose toggles generated monitor processes count, under code
    // coverage.
    std::vector<CoverageToggleObject> coverage_toggle_objects;
    // State machines whose states and transitions generated monitor
    // processes count, under code coverage.
    std::vector<CoverageFsmMachine> coverage_fsm_machines;
    // Concurrent and continuous assignments whose statements count, under
    // code coverage, in lowering order.
    std::vector<semantic::StatementId> coverage_concurrent_statements;
    // The typedefs the instance declares, in declaration order.
    std::vector<VpiTypedefInfo> vpi_typedefs;
    // The typespec of each cast to a named type in the instance's unit.
    std::vector<VpiCastTypespecInfo> vpi_cast_typespecs;
    // Each let expression of the instance's unit with the body expression
    // of the let declaration it names, resolved as the lowerer expands it
    // (IEEE 1800-2023 11.12, 37.57 vpiLetExpr).
    std::vector<std::pair<semantic::ExpressionId, semantic::ExpressionId>>
        vpi_let_uses;
    // The callable each user task or function call of the instance's unit
    // is bound to, as the lowerer bound it.
    std::vector<VpiCalleeInfo> vpi_callees;
    // The typespecs of named formal and result types of the instance's
    // tasks and functions, keyed "<callable>/<formal>" ("<callable>/" for
    // the result) (37.41).
    std::vector<VpiTypedefInfo> vpi_formal_typespecs;
    // The typespec of each value parameter whose declared type names a
    // typedef, by parameter name (IEEE 1800-2023 37.28 vpiTypespec).
    std::vector<VpiTypedefInfo> vpi_parameter_typespecs;
    // The typespec of each port whose declared type names a typedef, by
    // port name: a port that shares its actual's runtime signal keeps its
    // own declared type (IEEE 1800-2023 37.14, 37.17).
    std::vector<VpiTypedefInfo> vpi_port_typespecs;
    // The typespec of each user-defined nettype the instance declares, by
    // nettype name: the data type the nettype carries (IEEE 1800-2023 37.23
    // vpiTypespec).
    std::vector<VpiTypedefInfo> vpi_nettype_typespecs;
    // The generate block occurrences of the instance, with the parameters
    // each declares (IEEE 1800-2023 27.6, 37.85).
    std::vector<VpiGenerateScopeInfo> vpi_generate_scopes;
    // The parameters of the design's packages, recorded once with the first
    // SystemVerilog instance (IEEE 1800-2023 3.12.1, 37.10).
    std::vector<VpiPackageParameterInfo> vpi_package_parameters;
    // The typedefs of the design's packages and `$unit`, recorded once with
    // the parameters above (IEEE 1800-2023 3.12.1, 37.10).
    std::vector<VpiPackageTypedefInfo> vpi_package_typedefs;
    // The same for the tasks and functions of the design's packages.
    std::vector<VpiPackageTypedefInfo> vpi_package_formal_typespecs;
    // The typespec of each class property that is an array or a container:
    // `package` holds the class's declaration identity (IEEE 1800-2023
    // 37.17, 37.20, 37.25), recorded once with the packages.
    std::vector<VpiPackageTypedefInfo> vpi_class_property_typespecs;
    // The typedefs each class declares, as the typespecs a use of each
    // would have (`package` holds the class's declaration identity): the
    // declaration's, and each specialization's whose value parameters all
    // have integral values (IEEE 1800-2023 37.25, 37.32 vpiTypedef),
    // recorded once with the packages.
    std::vector<VpiPackageTypedefInfo> vpi_class_typedefs;
    // The parameters of each class's parameter port list, as for a package
    // (`package` holds the class's declaration identity): the declaration's
    // value and typespec, and each specialization's (IEEE 1800-2023 37.28,
    // 37.31, 37.32), recorded once with the packages.
    std::vector<VpiPackageParameterInfo> vpi_class_parameters;
    // The block and generate statement occurrences a VHDL instance
    // elaborated, in elaboration order: the path relative to the instance
    // (`g[1]`, `h[0].e[3]`, `k`) and the declaration of the region whose
    // occurrence it is: the generate statement for each value of a for
    // generate statement's range and for the first alternative of an if
    // generate statement, else the region of the chosen alternative (the
    // else branch of an if generate statement, an alternative of a case
    // generate statement), or a block statement; also an occurrence that
    // holds no signal, instance or process (IEEE 1076-2019 14.5.2, 14.5.3,
    // 19.4.2).
    std::vector<std::pair<std::string, semantic::DeclarationId>>
        vhdl_generate_occurrences;
};

using UdpTableId = std::uint32_t;

/// One declaration-normalized Verilog user-defined primitive table.
///
/// The table is stored once per canonical logical-library identity. Every UDP
/// instance references its stable ID/digest through specialization provenance.
struct UdpTableInfo {
    UdpTableId id { };
    std::string identity;
    std::string digest;
    bool sequential { };
    std::optional<frontend::VerilogUdpOutputSymbol> initial_output;
    // The output name first, then the input names in port order.
    std::vector<std::string> terminals;
    std::vector<frontend::VerilogUdpTableRow> rows;
    // The source text of each table entry, such as "0 ? : 1" (IEEE 1800-2023
    // 29.3, 37.36 table entry).
    std::vector<std::string> row_text;
};

struct SystemCPortInfo {
    std::string name;
    std::uint64_t native_handle { };
    runtime::simir::SignalId signal { };
};

struct SystemCEventInfo {
    std::string name;
    std::uint64_t native_handle { };
    runtime::simir::SignalId signal { };
};

struct SystemCPrimitiveChannelInfo {
    std::string name;
    std::uint64_t native_handle { };
};

struct SystemCSignalInfo {
    std::string name;
    std::uint64_t native_handle { };
    runtime::simir::SignalId signal { };
};

struct SystemCExportInfo {
    std::string name;
    std::uint64_t native_handle { };
    runtime::simir::SignalId signal { };
    bool writable { true };
};

struct SystemCInstanceInfo {
    std::uint32_t id { };
    std::string target;
    std::string instance;
    std::uint64_t native_handle { };
    std::vector<std::pair<std::string, std::int64_t>>
        construction_values;
    std::vector<std::pair<std::string, std::string>>
        construction_identity_values;
    std::vector<SystemCPortInfo> ports;
    std::vector<SystemCEventInfo> events;
    std::vector<SystemCPrimitiveChannelInfo> primitive_channels;
    std::vector<SystemCSignalInfo> internal_signals;
    std::vector<SystemCExportInfo> exports;
};

struct SystemCProcessInfo {
    runtime::simir::ProcessId process { };
    std::uint64_t native_handle { };
};

enum class SystemCNamedObjectKind : std::uint8_t {
    module,
    port,
    foreign_child,
    process,
    event,
    primitive_channel,
    signal,
    export_object,
};

/// One debug-visible object from a compiled SystemC hierarchy. Names and
/// parent names are common DesignIR paths; value-bearing aliases retain their
/// distinct object identity while sharing the referenced dense signal.
struct SystemCNamedObjectInfo {
    SystemCNamedObjectKind kind { SystemCNamedObjectKind::module };
    std::uint64_t native_handle { };
    std::string name;
    std::string parent;
    std::string type_name;
    std::optional<runtime::simir::SignalId> signal;
    std::optional<runtime::simir::ProcessId> process;
    runtime::simir::SourceLocation source;
};

using VerilogSpecifyPathId = std::uint32_t;

struct VerilogSpecifyTerminalInfo {
    runtime::simir::SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    friend bool operator==(
        const VerilogSpecifyTerminalInfo&,
        const VerilogSpecifyTerminalInfo&) = default;
};

/// One instance-local, specialization-normalized Verilog module path.
/// Conditions and destination data sources are retained only as
/// parser-independent scheduler programs after specialization.
struct VerilogSpecifyPathInfo {
    VerilogSpecifyPathId id { };
    std::string identity;
    std::string instance;
    std::vector<VerilogSpecifyTerminalInfo> sources;
    std::vector<VerilogSpecifyTerminalInfo> destinations;
    std::vector<runtime::simir::ProcessId> drivers;
    runtime::simir::ModulePathExpression condition_program;
    runtime::simir::ModulePathExpression data_source_program;
    std::uint32_t selection_group { };
    frontend::VerilogModulePathKind kind {
        frontend::VerilogModulePathKind::Parallel
    };
    frontend::VerilogSpecifyEdge source_edge {
        frontend::VerilogSpecifyEdge::None
    };
    frontend::VerilogPathPolarity polarity {
        frontend::VerilogPathPolarity::None
    };
    frontend::VerilogPulseStyle pulse_style {
        frontend::VerilogPulseStyle::Onevent
    };
    bool show_cancelled { };
    std::optional<runtime::SimulationTick> pulse_reject_limit;
    std::optional<runtime::SimulationTick> pulse_error_limit;
    std::vector<runtime::SimulationTick> pulse_reject_delays;
    std::vector<runtime::SimulationTick> pulse_error_delays;
    std::vector<runtime::SimulationTick> retain_delays;
    bool conditional { };
    bool ifnone { };
    std::vector<runtime::SimulationTick> delays;
    /// The minimum, typical and maximum value of each delay in `delays`,
    /// when the path writes a minimum:typical:maximum triple or names a
    /// specparam that is one (IEEE 1800-2023 30.4.1); empty otherwise.
    /// `delays` holds the one the delay mode selected. SDF annotation
    /// replaces `delays` and clears these.
    std::vector<runtime::SimulationTick> minimum_delays;
    std::vector<runtime::SimulationTick> typical_delays;
    std::vector<runtime::SimulationTick> maximum_delays;
    frontend::SourceSpan source;
};

enum class VerilogPrimitiveKind : std::uint8_t {
    gate,
    switch_device,
    pull,
    udp,
};

/// One terminal of an elaborated primitive instance (IEEE 1800-2023 37.35
/// prim term). A terminal that is a static selection of a signal has a
/// nonzero width; any other expression (a constant or a concatenation) keeps
/// its source text.
struct VerilogPrimitiveTerminalInfo {
    frontend::VerilogPrimitiveTerminalDirection direction {
        frontend::VerilogPrimitiveTerminalDirection::Input
    };
    runtime::simir::SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    std::string expression;
    // The HIR expression of a terminal that is not a static selection of a
    // signal (a constant or a concatenation), for the VPI expr object (IEEE
    // 1800-2023 37.35 prim term vpiExpr).
    std::optional<std::uint32_t> hir_expression;
};

/// One transition delay of a primitive in simulation ticks. A delay without
/// a minimum:typical:maximum triple repeats its value in all three.
struct VerilogPrimitiveDelayInfo {
    runtime::SimulationTick minimum { };
    runtime::SimulationTick typical { };
    runtime::SimulationTick maximum { };
};

/// One elaborated gate, switch, pull or UDP instance (IEEE 1800-2023 28, 29,
/// 37.35). An element of an instance array is one record.
struct VerilogPrimitiveInfo {
    // The hierarchical path of the scope that holds the instance.
    std::string instance;
    // Empty for an unnamed instance; an array element is named "a[1]".
    std::string name;
    std::string full_name;
    // The name of the instance array an element belongs to.
    std::string array_name;
    VerilogPrimitiveKind kind { VerilogPrimitiveKind::gate };
    // The primitive keyword, or the UDP name.
    std::string keyword;
    // The UDP table of a UDP instance.
    std::optional<UdpTableId> udp_table;
    bool array_member { };
    std::int64_t array_index { };
    std::int64_t array_left { };
    std::int64_t array_right { };
    // In LRM order. A UDP instance has the output, then the inputs.
    std::vector<VerilogPrimitiveTerminalInfo> terminals;
    // Rise, fall and turn-off delays as written; empty without a delay.
    std::vector<VerilogPrimitiveDelayInfo> delays;
    // The drive strengths as frontend::VerilogStrength ranks.
    std::optional<std::uint8_t> drive_zero;
    std::optional<std::uint8_t> drive_one;
    // The processes whose delayed writes (WriteInertial) carry the
    // primitive's output delay: what vpi_put_delays retimes. Empty for an
    // instance without a delay or whose delay is not simulated.
    std::vector<runtime::simir::ProcessId> drivers;
    frontend::SourceSpan source;
};

enum class SdfNetDelayKind : std::uint8_t {
    interconnect,
    port,
};

/// One end of an SDF net delay: the port or net an INTERCONNECT or PORT
/// annotation names. `instance` is the SDF cell instance the annotation is
/// in and `object` the hierarchical path of the port or net. `select_width`
/// is the width of a bit or part select, 0 for the whole object, and
/// `select_left`/`select_right` its bounds as the HDL indexes them.
struct SdfNetDelayTerminal {
    std::string instance;
    std::string object;
    runtime::simir::SignalId signal { };
    bool port { };
    std::uint32_t select_width { };
    std::int64_t select_left { };
    std::int64_t select_right { };
};

/// An interconnect or port delay that SDF annotation created (IEEE
/// 1800-2023 37.37, 37.45). An unannotated design has none. `delays` are the
/// twelve transition delays in ticks in the order of a module path
/// (IEEE 1800-2023 30.5.1 Table 30-2). A port delay has one terminal:
/// `destination` repeats `source`. `instance` is the instance the SDF cell
/// names, the module the delay device belongs to.
struct SdfNetDelayInfo {
    SdfNetDelayKind kind { SdfNetDelayKind::interconnect };
    std::string instance;
    std::string identity;
    SdfNetDelayTerminal source;
    SdfNetDelayTerminal destination;
    std::vector<runtime::SimulationTick> delays;
    /// The pulse reject and error limit of each of the twelve transitions,
    /// in ticks and in the order of `delays`, or empty when the SDF gives
    /// none: each limit then equals its delay (an inertial delay) and an
    /// error limit that is not given equals the reject limit (IEEE 1364-2005
    /// 14.6, 16.2.4; IEEE 1497-2001 delval with pulse limits).
    std::vector<runtime::SimulationTick> reject;
    std::vector<runtime::SimulationTick> error;
    /// Set when annotation split the load port's net (split_sdf_net_delays):
    /// the instance behind `destination` reads `load_signal`, a delayed copy
    /// of `destination.signal` that `driver` writes with an interconnect-delay
    /// write (WriteNetDelay), whose twelve delays and pulse limits
    /// vpi_put_delays retimes.
    /// Records that end at the same load port share one driver.
    bool delayed { };
    runtime::simir::SignalId load_signal { };
    runtime::simir::ProcessId driver { };
    /// The copy drivers of every run of bits the record is the effective
    /// record of (a later record that names some of its bits splits the runs,
    /// IEEE 1364-2005 16.3); empty when the record owns no run, which then
    /// keeps `driver` alone. `driver` is the run of the record's first bit.
    std::vector<runtime::simir::ProcessId> run_drivers;
    /// An INCREMENT record that names bits of a load an earlier record of the
    /// same source and port also names, with other terminals: the split adds
    /// its delays to the bits' current ones (IEEE 1364-2005 16.3). An
    /// INCREMENT of the same terminals is added to the earlier record when
    /// the records are made.
    bool increment { };
    /// The drivers a put on the record retimes: its runs, else `driver`.
    [[nodiscard]] std::vector<runtime::simir::ProcessId> drivers() const
    {
        return run_drivers.empty()
            ? std::vector<runtime::simir::ProcessId> { driver } : run_drivers;
    }
};

/// A read of a signal that a process wrote with a hierarchical name
/// (IEEE 1800-2023 23.6, 23.8: `$display(top.u1.a)`), kept so that SDF
/// annotation can give the read the delayed value of a load port when it
/// names one (IEEE 1364-2005 16.2.4: a reference to an annotated port,
/// wherever it occurs, provides the delayed signal value). Only the reads
/// through a hierarchical name are recorded, so a design without one has
/// none. `reg` is the destination register of the process's ReadSignal
/// operation, which no later rewrite of the operation list moves.
struct HierarchicalSignalRead {
    runtime::simir::ProcessId process { };
    runtime::simir::RegisterId reg { };
    runtime::simir::SignalId signal { };
    /// The process waits on its reads (an implicit event list), so the
    /// delayed copy also wakes it.
    bool implicit { };
    /// The full hierarchical path the name resolved to.
    std::string path;
    /// The name is in the event control of the process (`@(u1.a)`,
    /// `@(posedge u1.clk)`): `reg` is unused, and the process waits on the
    /// load's delayed copy in place of the net.
    bool sensitivity { };
};

struct ElaboratedDesignState {
    std::string top;
    std::vector<std::string> roots;
    std::vector<SignalInfo> signal_info;
    std::vector<BoundaryConversionInfo> boundary_conversions;
    std::vector<runtime::simir::Signal> signals;
    std::vector<StringObjectInfo> string_object_info;
    std::vector<runtime::simir::StringObject> string_objects;
    std::vector<ContainerObjectInfo> container_object_info;
    std::vector<runtime::simir::ContainerObject> container_objects;
    std::vector<runtime::simir::ContainerSignalAlias>
        container_signal_aliases;
    std::vector<runtime::simir::ContainerElementSignalAlias>
        container_element_signal_aliases;
    std::vector<runtime::simir::ContainerAggregateSignalAlias>
        container_aggregate_signal_aliases;
    std::vector<VhdlProtectedObjectInfo> vhdl_protected_object_info;
    std::vector<runtime::simir::Process> processes;
    std::vector<SpecializationInfo> specializations;
    std::vector<UdpTableInfo> udp_tables;
    std::vector<VerilogSpecifyPathInfo> verilog_specify_paths;
    std::vector<runtime::simir::ModuleTimingCheck> verilog_timing_checks;
    std::vector<SystemCInstanceInfo> systemc_instances;
    std::vector<SystemCProcessInfo> systemc_processes;
    std::vector<SystemCNamedObjectInfo> systemc_objects;
    std::vector<std::pair<std::string, runtime::simir::SignalId>> signal_names;
    std::vector<std::pair<std::string, runtime::simir::StringObjectId>>
        string_names;
    std::vector<std::pair<std::string, runtime::simir::ContainerObjectId>>
        container_names;
    std::optional<CodeCoverageInventory> code_coverage_inventory;
    std::optional<runtime::simir::SignalDriverInventory>
        signal_driver_inventory;
    std::vector<VerilogPrimitiveInfo> verilog_primitives;
    std::vector<SdfNetDelayInfo> sdf_net_delays;
    /// The value of each virtual interface handle and the path of the
    /// interface instance it denotes, sorted by path (37.29 vpiActual).
    std::vector<std::pair<std::string, std::uint64_t>>
        systemverilog_interface_handles;
    /// The reads of signals through hierarchical names, ordered by process
    /// and register.
    std::vector<HierarchicalSignalRead> hierarchical_reads;
    /// ElaboratedDesign::uses_simulation_control(), computed once when the
    /// process rows are final.
    bool uses_simulation_control { };
    /// ElaboratedDesign::class_container_types(), sorted by identity.
    std::vector<std::pair<std::string, runtime::simir::ContainerType>>
        class_container_types { };
};

/// One pure acyclic continuous-assignment cone selected for A2 fusion. The
/// sink keeps its ProcessId and runs `fused`; the other members stay installed
/// with their original programs but never execute while the cone is fused.
struct ConeFusionPlanCone {
    runtime::simir::ProcessId sink { };
    /// Topological order, including the sink.
    std::vector<runtime::simir::ProcessId> members;
    std::vector<runtime::simir::SignalId> internal_signals;
    std::vector<runtime::simir::SignalId> boundary_inputs;
    runtime::simir::Process fused;
};

struct ConeFusionPlan {
    std::vector<ConeFusionPlanCone> cones;
    /// Sorted members that never execute while fused (sinks excluded).
    std::vector<runtime::simir::ProcessId> dormant;
    bool disabled { };
    const char* disabled_reason { "" };
    std::size_t candidate_processes { };
    std::size_t internal_nets { };
};

/// One census row: how many processes stay on the host for one reason.
struct StaticKernelHostReason {
    std::string reason;
    std::size_t count { };
    std::string example;
};

/// What code outside the SimIR processes needs from the plan, as the
/// application knows it before planning (docs/engine-v4-semantics.md S19,
/// S21). Foreign libraries (VPI, VHPI, DPI) and traces need nothing here:
/// they read, watch and write kernel-owned signals at run time
/// (Interpreter::watch_signal_changes, StaticKernel::note_external_write),
/// so their runs use the default plan and its kernel image. The planner
/// derives the design-local needs itself (module paths, timing checks and
/// VCD control operations).
struct StaticKernelAccessPlan {
    /// The application admits the kernel for this run although cone fusion
    /// is off, because this plan replaces the whole-design gate (switch
    /// FSIM_STATIC_KERNEL_ACCESS_PLAN=0 restores the gates).
    bool admit { };
    /// Code coverage is on. Statement, branch and line counters are host
    /// data bumped by CodeCoverageHit operations, which the kernel does not
    /// evaluate: processes with such operations stay host processes.
    bool coverage { };
    /// The source-level debugger (or a client that may stop and examine
    /// any process) drives the run: every process is a behavioral thread or
    /// a hosted process, so each can stop at a statement, and the members'
    /// debug points are executable. Signals stay kernel-owned: the debugger
    /// examines, deposits, forces and watches them at run time, as a foreign
    /// library does.
    bool debug { };
    /// A VPI or PLI library is loaded: the primitive drivers' delayed writes
    /// are slotted WriteDelayed operations (bind_primitive_delay_slots), so
    /// the kernel plan differs from the default one.
    bool delay_slots { };
    /// A client other than the design itself can write a std_logic value at
    /// run time: a VPI, VHPI, PLI or DPI library, a Tcl session or the
    /// debugger. Their writes can bring 'H', 'L', 'W' or '-' (the kernel's
    /// registers then carry weak masks, CompiledBody::weak).
    bool weak_sources { };

    /// Key text for persisted plans; empty for the default plan.
    [[nodiscard]] std::string identity() const
    {
        if (!coverage && !debug && !delay_slots
            && !weak_sources) {
            return { };
        }
        std::string text { "access" };
        text += coverage ? ";coverage" : "";
        text += debug ? ";debug" : "";
        text += delay_slots ? ";delay-slots" : "";
        text += weak_sources ? ";weak-sources" : "";
        return text;
    }
};

/// Engine v4 static kernel plan (docs/simulation-engine-v4-plan.md). The
/// runtime specification is opaque to callers; the counts are diagnostics.
struct StaticKernelPlan {
    bool disabled { };
    const char* disabled_reason { "" };
    runtime::simir::ProcessId host { };
    /// The stub the scheduler runs for the host: [WaitSensitivity, Jump 0].
    runtime::simir::Process host_program;
    std::shared_ptr<runtime::simir::StaticKernelRuntimeSpec> spec;
    std::size_t members { };
    std::size_t owned_signals { };
    std::size_t boundary_inputs { };
    std::size_t boundary_outputs { };
    std::size_t owned_containers { };
    /// Census, planned with FSIM_PROFILE_PHASES only: the valid processes,
    /// those outside the kernel (`host`), and the first reason each of them
    /// stays on the host, aggregated.
    std::size_t processes { };
    std::size_t host_processes { };
    std::vector<StaticKernelHostReason> host_reasons;
    /// Hosted processes (StaticKernelMemberKind::hosted): kernel-scheduled,
    /// executed by the reference evaluator. They count as kernel-owned (not in
    /// `host_processes`); `hosted_reasons` aggregates why each is not native.
    /// `hosted_processes` is always set, the rows with FSIM_PROFILE_PHASES.
    std::size_t hosted_processes { };
    std::vector<StaticKernelHostReason> hosted_reasons;
    /// Lazy members: dead combinational logic, evaluated only when read.
    std::size_t lazy_members { };
    /// VHDL members the planner admits to delta collapsing.
    std::size_t collapse_members { };
};

class ElaboratedDesign final {
public:
    ElaboratedDesign() = default;
    explicit ElaboratedDesign(const std::span<const Root> roots)
    {
        roots_.reserve(roots.size());
        for (const auto& root : roots) {
            roots_.push_back(root.alias);
        }
        if (!roots_.empty()) {
            top_ = roots_.front();
        }
    }

    [[nodiscard]] const std::string& top() const noexcept;
    [[nodiscard]] const std::vector<std::string>& roots() const noexcept;
    /// The value of a virtual interface handle to the hierarchical path of
    /// the interface instance it denotes (IEEE 1800-2023 25.9, 37.29
    /// vpiActual); the elaborator's own table, moved here when lowering is
    /// over.
    [[nodiscard]] const std::unordered_map<std::string, std::uint64_t>&
    systemverilog_interface_handles() const noexcept
    {
        return systemverilog_interface_handles_;
    }
    /// A procedural `$sdf_annotate` call found while lowering: the SDF file
    /// and the hierarchical scope its cells are relative to (IEEE 1364-2005
    /// 17.4.6 / IEEE 1800-2017 32.5). Elaboration applies it as an SDF
    /// annotation; the requests are not retained in the design state.
    struct SdfAnnotationRequest {
        std::string file;
        std::string scope;
    };
    [[nodiscard]] const std::vector<SdfAnnotationRequest>&
    sdf_annotation_requests() const noexcept
    {
        return sdf_annotation_requests_;
    }
    /// Foreign system tasks and functions known to this elaboration. They
    /// are an elaboration input only and are not retained in the design
    /// state; the calls carry everything the runtime needs.
    void set_user_system_tasks(std::vector<UserSystemTask> tasks)
    {
        user_system_tasks_ = std::move(tasks);
    }
    [[nodiscard]] const UserSystemTask* user_system_task(
        std::string_view name) const noexcept
    {
        for (const auto& task : user_system_tasks_) {
            if (task.name == name) {
                return &task;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const semantic::HierarchyPathTable&
    hierarchy_paths() const noexcept;
    /// Accept an extended table only when every existing ID still names the
    /// same path. DesignIR may append derived paths after elaboration.
    [[nodiscard]] bool rebind_path_table(
        semantic::HierarchyPathTable paths);
    /// Reassign path-keyed indexes when the destination table has the same
    /// spellings under different IDs, as in a canonical artifact table.
    [[nodiscard]] bool remap_path_table(
        semantic::HierarchyPathTable paths);
    [[nodiscard]] const std::vector<SignalInfo>& signals() const;
    [[nodiscard]] const std::vector<BoundaryConversionInfo>&
    boundary_conversions() const noexcept;
    [[nodiscard]] const std::vector<StringObjectInfo>&
    string_objects() const noexcept;
    [[nodiscard]] const std::deque<ContainerObjectInfo>&
    container_objects() const noexcept;
    [[nodiscard]] const std::vector<runtime::simir::ContainerElementSignalAlias>&
    container_element_signal_aliases() const noexcept;
    [[nodiscard]] const std::vector<runtime::simir::ContainerAggregateSignalAlias>&
    container_aggregate_signal_aliases() const noexcept;
    [[nodiscard]] const std::vector<VhdlProtectedObjectInfo>&
    vhdl_protected_objects() const noexcept;
    /// For a run with a VPI or PLI library: turn the delayed writes of every
    /// primitive driver (VerilogPrimitiveInfo::drivers) into slotted
    /// WriteDelayed operations, so vpi_put_delays and acc_replace_delays can
    /// retime them (IEEE 1800-2023 38.32). Materializes the process programs
    /// (their templates are no longer shared), and only when a primitive has
    /// a driver. Runs without a library never call it and keep the constant
    /// WriteInertial operations. Returns the operations changed.
    std::size_t bind_primitive_delay_slots();
    /// Whether a process runs $save, $incsave, $restart or $reset (IEEE
    /// 1800-2023 D.7-D.9). Such a run keeps the host's time steps, so the
    /// save point (the end of a time slot) is reached at the right time.
    /// Computed once, when elaboration freezes the process rows, and kept in
    /// the design artifact: a run does not scan the processes for it.
    [[nodiscard]] bool uses_simulation_control() const noexcept
    {
        return uses_simulation_control_;
    }
    /// Whether any signal holds nine values (ValueKind::logic9): only those
    /// can take a weak value ('H', 'L', 'W', '-') a foreign client writes.
    [[nodiscard]] bool has_nine_value_signals() const noexcept;
    /// Number of runtime processes without materializing artifact-backed rows.
    [[nodiscard]] std::size_t process_count() const noexcept;
    /// Materialize and return the public Process facade. This may allocate for
    /// a design decoded into private runtime rows and therefore may throw.
    [[nodiscard]] const std::vector<runtime::simir::Process>& processes() const;
    [[nodiscard]] const std::vector<SpecializationInfo>&
    specializations() const noexcept;
    [[nodiscard]] const std::vector<UdpTableInfo>&
    udp_tables() const noexcept;
    [[nodiscard]] const std::vector<VerilogSpecifyPathInfo>&
    verilog_specify_paths() const noexcept;
    [[nodiscard]] const std::vector<runtime::simir::ModuleTimingCheck>&
    verilog_timing_checks() const noexcept;
    [[nodiscard]] const std::vector<VerilogPrimitiveInfo>&
    verilog_primitives() const noexcept;
    [[nodiscard]] const std::vector<SdfNetDelayInfo>&
    sdf_net_delays() const noexcept;
    [[nodiscard]] const std::vector<HierarchicalSignalRead>&
    hierarchical_reads() const noexcept;
    /// The kernel's container type of each class property that holds a
    /// fixed-size array of values, by property identity: an object holds
    /// its elements from construction (IEEE 1800-2023 7.4, 8.7).
    [[nodiscard]] const std::unordered_map<std::string,
        runtime::simir::ContainerType>&
    class_container_types() const noexcept;
    /// Notes the container type the lowerer gives a class property.
    void record_class_container_type(
        std::string identity, runtime::simir::ContainerType type);
    [[nodiscard]] const std::vector<SystemCInstanceInfo>&
    systemc_instances() const noexcept;
    [[nodiscard]] const std::vector<SystemCProcessInfo>&
    systemc_processes() const noexcept;
    [[nodiscard]] const std::vector<SystemCNamedObjectInfo>&
    systemc_objects() const noexcept;
    [[nodiscard]] const std::optional<CodeCoverageInventory>&
    code_coverage_inventory() const noexcept;
    [[nodiscard]] const std::optional<runtime::simir::SignalDriverInventory>&
    signal_driver_inventory() const noexcept;
    [[nodiscard]] CoverageInventoryValidationResult
    attach_code_coverage_inventory(
        std::span<const CoverageInventorySource> sources,
        std::span<const CoverageInstanceInventoryDraft> instances,
        CoverageInventoryLimits limits = { }) noexcept;
    [[nodiscard]] CoverageInventoryValidationResult
    bind_hir_code_coverage_hits() noexcept;
    [[nodiscard]] std::optional<runtime::simir::SignalId> find_signal(
        std::string_view name) const noexcept;
    /// Return every debug-visible signal path in lexical order. Boundary-port
    /// aliases may refer to the same dense signal ID as their connected
    /// parent signal.
    [[nodiscard]] std::vector<std::pair<std::string, runtime::simir::SignalId>>
    signal_paths() const;
    /// Return every debug-visible string object path in lexical order.
    /// String port aliases may refer to the same object ID as their connected
    /// parent object.
    [[nodiscard]] std::vector<std::pair<
        std::string, runtime::simir::StringObjectId>>
    string_paths() const;
    [[nodiscard]] std::optional<runtime::simir::ContainerObjectId>
    find_container(std::string_view name) const noexcept;
    /// Return every debug-visible container path in lexical order. Container
    /// port aliases may refer to the same bounded object ID as their connected
    /// parent object.
    [[nodiscard]] std::vector<std::pair<
        std::string, runtime::simir::ContainerObjectId>>
    container_paths() const;

    [[nodiscard]] std::unique_ptr<runtime::simir::Interpreter> create_interpreter(
        runtime::SchedulerOptions options = { },
        std::uint64_t seed = 1) const &;
    /// Consume the immutable SimIR process programs when the elaborated design
    /// is no longer needed as their owner. Other design metadata remains
    /// available for runtime services and native-executor setup.
    [[nodiscard]] std::unique_ptr<runtime::simir::Interpreter> create_interpreter(
        runtime::SchedulerOptions options = { },
        std::uint64_t seed = 1) &&;
    /// Same as the consuming overload, installing the planned fused cones.
    [[nodiscard]] std::unique_ptr<runtime::simir::Interpreter> create_interpreter(
        runtime::SchedulerOptions options,
        std::uint64_t seed,
        const ConeFusionPlan& plan) &&;
    /// Plan A2 combinational cone fusion. Callers opt in only when no
    /// observer outside SimIR processes can inspect hidden internal nets.
    [[nodiscard]] ConeFusionPlan plan_cone_fusion() const;
    /// Create an interpreter whose statically schedulable processes run in
    /// the engine v4 static kernel.
    [[nodiscard]] std::unique_ptr<runtime::simir::Interpreter>
    create_interpreter(runtime::SchedulerOptions options,
        std::uint64_t seed, const StaticKernelPlan& plan) &&;
    /// Plan the static kernel. Callers opt in only when no observer outside
    /// SimIR processes can inspect kernel-owned state.
    [[nodiscard]] StaticKernelPlan plan_static_kernel(
        const StaticKernelAccessPlan& access = { }) const;

    /// Replaces the module paths and timing checks and keeps everything else,
    /// including the net delay splits and the processes a running design has
    /// handed to its interpreter: a live SDF reannotation changes only the
    /// timing of paths and checks (the delayed loads follow separately).
    void replace_module_timing(std::vector<VerilogSpecifyPathInfo> paths,
        std::vector<runtime::simir::ModuleTimingCheck> checks);

    [[nodiscard]] ElaboratedDesignState state() const &;
    /// Transfer the complete portable state out of a design that has reached
    /// its final ownership boundary. This avoids duplicating every process
    /// program while publishing a standalone design artifact.
    [[nodiscard]] ElaboratedDesignState state() &&;
    [[nodiscard]] static std::optional<ElaboratedDesign> from_state(
        ElaboratedDesignState state);

private:
    friend struct ElaborationResult;
    friend class Lowerer;
    friend class detail::ElaboratedDesignProcessAccess;

    /// Positions, in insertion order, of the container signal aliases and
    /// element signal aliases that name `object`. The index extends over
    /// entries appended since the last query; code that erases from or
    /// replaces either vector calls invalidate_container_alias_indexes().
    [[nodiscard]] std::span<const std::uint32_t> container_alias_positions(
        runtime::simir::ContainerObjectId object) const;
    [[nodiscard]] std::span<const std::uint32_t>
    container_element_alias_positions(
        runtime::simir::ContainerObjectId object) const;
    void invalidate_container_alias_indexes() noexcept;

    /// Positions in `specializations_`, in insertion order, of the
    /// specializations whose instance path is `instance` (exact) or whose
    /// ASCII-lowercased instance path is `lowered`. The indexes extend over
    /// entries appended since the last query; instance paths are fixed
    /// before an entry is appended.
    [[nodiscard]] std::span<const std::uint32_t> specialization_positions(
        std::string_view instance) const;
    [[nodiscard]] std::span<const std::uint32_t>
    specialization_positions_lowercase(std::string_view lowered) const;
    /// The first specialization whose instance path is `instance`, or null.
    [[nodiscard]] const SpecializationInfo* find_specialization(
        std::string_view instance) const;

    [[nodiscard]] static std::optional<ElaboratedDesign>
    from_state_with_process_rows(
        ElaboratedDesignState state,
        std::shared_ptr<const detail::RuntimeProcessProgramTable> rows,
        bool require_driver_inventory);

    void populate_interpreter(
        runtime::simir::Interpreter* interpreter,
        bool validation_only,
        std::vector<runtime::simir::Process>* consumed_processes = nullptr,
        const ConeFusionPlan* plan = nullptr) const;
    void finalize_signal_driver_inventory();
    void append_process_record(runtime::simir::Process process);
    void append_process_instance_record(
        std::shared_ptr<const runtime::simir::ProcessProgramTemplate> common,
        runtime::simir::ProcessInstanceProgram instance);
    void replace_process_record(
        std::size_t process, runtime::simir::Process instance);
    void rebuild_process_record(
        std::size_t process, runtime::simir::Process program);
    [[nodiscard]] std::shared_ptr<const runtime::simir::ProcessProgramTemplate>
    intern_process_template(
        const runtime::simir::ProcessProgramView& process);
    [[nodiscard]] runtime::simir::ProcessProgramView process_view(
        std::size_t process) const noexcept;
    void finalize_process_rows();
    // Notes the callable the lowerer bound a call of instance `caller` to;
    // finalize_process_rows gives it to the caller's specialization, and a
    // call of the caller's own callables to every specialization of its
    // unit (a replayed process is not lowered again).
    void record_vpi_callee(std::string caller, VpiCalleeInfo callee);
    [[nodiscard]] bool scan_simulation_control();
    [[nodiscard]] runtime::simir::SignalDriverInventory
    compute_signal_driver_inventory() const;
    friend class HierarchyBuilder;

    struct HierarchyPathHash final {
        [[nodiscard]] std::size_t operator()(
            semantic::HierarchyPathId id) const noexcept
        {
            return id.value();
        }
    };

    void freeze_hierarchy_paths();

    std::string top_;
    std::vector<std::string> roots_;
    // Instance path to virtual interface handle, kept for VPI.
    std::unordered_map<std::string, std::uint64_t>
        systemverilog_interface_handles_;
    std::vector<SdfAnnotationRequest> sdf_annotation_requests_;
    std::vector<UserSystemTask> user_system_tasks_;
    SignalInfoTable signal_info_;
    std::vector<BoundaryConversionInfo> boundary_conversions_;
    std::vector<runtime::simir::Signal> signals_;
    std::vector<StringObjectInfo> string_object_info_;
    std::vector<runtime::simir::StringObject> string_objects_;
    // A deque: lowering allocates class static containers while bindings
    // hold references to earlier objects' types.
    std::deque<ContainerObjectInfo> container_object_info_;
    std::vector<runtime::simir::ContainerObject> container_objects_;
    std::vector<runtime::simir::ContainerSignalAlias>
        container_signal_aliases_;
    std::vector<runtime::simir::ContainerElementSignalAlias>
        container_element_signal_aliases_;
    struct ContainerAliasIndex {
        std::unordered_map<runtime::simir::ContainerObjectId,
            std::vector<std::uint32_t>>
            positions;
        std::size_t indexed { };
    };
    mutable ContainerAliasIndex container_alias_index_;
    mutable ContainerAliasIndex container_element_alias_index_;
    std::vector<runtime::simir::ContainerAggregateSignalAlias>
        container_aggregate_signal_aliases_;
    std::vector<VhdlProtectedObjectInfo> vhdl_protected_object_info_;
    mutable std::vector<runtime::simir::Process> processes_;
    // Original sink programs captured while populating a fused interpreter.
    mutable std::vector<runtime::simir::Process> fused_sink_originals_;
    mutable std::shared_ptr<const detail::RuntimeProcessProgramTable>
        process_rows_;
    std::shared_ptr<detail::RuntimeProcessProgramTableBuilder>
        process_builder_;
    bool uses_simulation_control_ { };
    std::vector<std::pair<std::string, VpiCalleeInfo>> pending_vpi_callees_;
    std::unordered_map<std::string, runtime::simir::ContainerType>
        class_container_types_;
    std::vector<SpecializationInfo> specializations_;
    struct SpecializationPathHash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(
            const std::string_view value) const noexcept
        {
            return std::hash<std::string_view> { }(value);
        }
    };
    struct SpecializationPathEqual {
        using is_transparent = void;
        [[nodiscard]] bool operator()(const std::string_view left,
            const std::string_view right) const noexcept
        {
            return left == right;
        }
    };
    struct SpecializationPathIndex {
        std::unordered_map<std::string, std::vector<std::uint32_t>,
            SpecializationPathHash, SpecializationPathEqual>
            exact;
        std::unordered_map<std::string, std::vector<std::uint32_t>,
            SpecializationPathHash, SpecializationPathEqual>
            lowercase;
        std::size_t exact_indexed { };
        std::size_t lowercase_indexed { };
    };
    mutable SpecializationPathIndex specialization_path_index_;
    std::vector<UdpTableInfo> udp_tables_;
    std::vector<VerilogSpecifyPathInfo> verilog_specify_paths_;
    std::vector<runtime::simir::ModuleTimingCheck> verilog_timing_checks_;
    std::vector<VerilogPrimitiveInfo> verilog_primitives_;
    std::vector<SdfNetDelayInfo> sdf_net_delays_;
    std::vector<HierarchicalSignalRead> hierarchical_reads_;
    std::vector<SystemCInstanceInfo> systemc_instances_;
    std::vector<SystemCProcessInfo> systemc_processes_;
    std::vector<SystemCNamedObjectInfo> systemc_objects_;
    std::optional<CodeCoverageInventory> code_coverage_inventory_;
    std::optional<runtime::simir::SignalDriverInventory>
        signal_driver_inventory_;
    semantic::HierarchyPathTable hierarchy_paths_;
    std::unordered_map<semantic::HierarchyPathId,
        runtime::simir::SignalId, HierarchyPathHash> signal_by_path_;
    std::unordered_map<semantic::HierarchyPathId,
        runtime::simir::StringObjectId, HierarchyPathHash> string_by_path_;
    std::unordered_map<semantic::HierarchyPathId,
        runtime::simir::ContainerObjectId, HierarchyPathHash>
        container_by_path_;
    // Construction drafts. Cleared before the design is published.
    std::unordered_map<std::string, runtime::simir::SignalId> signal_by_name_;
    std::unordered_map<
        std::string, runtime::simir::StringObjectId>
        string_by_name_;
    std::unordered_map<
        std::string, runtime::simir::ContainerObjectId>
        container_by_name_;
    // Construction draft: the packed dimensions (left and right bounds,
    // outermost first) of a container object's packed elements, so a select
    // within an element named hierarchically finds its bits (IEEE 1800-2017
    // 7.4.5, 23.6).
    std::unordered_map<runtime::simir::ContainerObjectId,
        std::vector<std::pair<std::int64_t, std::int64_t>>>
        container_element_packed_shape_;
    // Construction draft: the packed dimensions of a signal with more than
    // one, so a select of it named hierarchically finds its elements (IEEE
    // 1800-2017 7.4.5, 23.6).
    std::unordered_map<runtime::simir::SignalId,
        std::vector<std::pair<std::int64_t, std::int64_t>>>
        signal_packed_shape_;
    // Construction draft: the element signals of a named-event array by the
    // array's full path, as (index, signal) in declared range order, so a
    // select of an array named hierarchically (`g[1].e[i]`) finds its
    // elements (IEEE 1800-2023 15.5, 23.6).
    std::unordered_map<std::string,
        std::vector<std::pair<std::int32_t, runtime::simir::SignalId>>>
        event_array_by_name_;
    // Construction draft: the unpacked dimensions of a fixed-size unpacked
    // array signal, outermost first; its elements are equally wide and the
    // rightmost element of the last dimension is at bit 0, so a select
    // through a virtual interface handle finds an element (IEEE 1800-2017
    // 7.4, 25.9).
    std::unordered_map<runtime::simir::SignalId,
        std::vector<std::pair<std::int64_t, std::int64_t>>>
        signal_unpacked_shape_;
    // Construction draft: the members of the packed structure elements of
    // such a signal, laid out within one element, so a member of an element
    // named hierarchically finds its bits (IEEE 1800-2017 7.2.1, 23.6).
    std::unordered_map<runtime::simir::SignalId,
        std::vector<PackedMemberMetadata>>
        signal_unpacked_element_members_;
    // Construction draft: per process, the writes through a virtual
    // interface handle to each candidate instance member; the instance
    // they reach is known only at run time (IEEE 1800-2017 25.9).
    std::unordered_map<std::string,
        std::unordered_map<runtime::simir::SignalId, std::size_t>>
        virtual_interface_write_counts_;
    // Construction draft: the design's class specializations, computed
    // once on first use.
    std::shared_ptr<semantic::sv::ClassSpecializationIndex>
        class_specializations_;
};

struct ElaborationResult {
    std::optional<ElaboratedDesign> design;
    /// The delay locations of interconnect delay generics in the multiple
    /// source mode (IEEE 1076.4-2000 5.2.7.1.5), for
    /// apply_vital_wire_sources.
    std::vector<VitalWireLocations> vital_wire_locations;
    std::vector<Diagnostic> diagnostics;
    // Non-failing elaboration-time language messages remain ordered beside
    // ordinary errors without making a successful design unavailable.
    std::vector<frontend::Diagnostic> messages;
    // Generate declarations are selected only after parameters and hierarchy
    // are known. Return stable compiled-HIR identities; syntax declarations
    // never cross the elaboration boundary.
    std::vector<SelectedSystemVerilogClass>
        selected_systemverilog_classes;
    // The class specializations lowering made for types whose actuals read
    // the parameters of the design element instance that names them
    // (`C #(N) c;` in `sub #(5)`, IEEE 1800-2023 8.25, 23.2.2): the compiled
    // design's own specialization pass cannot know them.
    std::shared_ptr<const std::vector<semantic::sv::ClassSpecialization>>
        instance_class_specializations;

    [[nodiscard]] bool ok() const noexcept
    {
        return design.has_value() && diagnostics.empty();
    }
};

/// Elaborate one compiled VHDL entity/architecture or Verilog module.
///
/// VHDL names are already canonicalized in the compiled HIR. `top` accepts a
/// simple unit name or a qualified manifest spelling such as
/// `sv:work.counter` or `vhdl:work.counter(rtl)`.
} // namespace fsim::elaboration
