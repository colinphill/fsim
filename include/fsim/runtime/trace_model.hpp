// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace fsim::runtime {

struct TraceScopeId {
    std::uint64_t value { };
    friend bool operator==(TraceScopeId, TraceScopeId) = default;
};

struct TraceSignalId {
    std::uint64_t value { };
    friend bool operator==(TraceSignalId, TraceSignalId) = default;
};

struct TraceTypeId {
    std::uint64_t value { };
    friend bool operator==(TraceTypeId, TraceTypeId) = default;
};

struct TraceSourceId {
    std::uint64_t value { };
    friend bool operator==(TraceSourceId, TraceSourceId) = default;
};

enum class TraceSourceKind : std::uint8_t {
    Hdl,
    SystemC,
    Uvm,
    Debugger,
    Callback,
    Internal
};

enum class TraceLanguage : std::uint8_t {
    Unknown,
    Verilog,
    SystemVerilog,
    Vhdl,
    SystemC
};

struct TraceSourceMetadata {
    TraceSourceKind kind { TraceSourceKind::Hdl };
    TraceLanguage language { TraceLanguage::Unknown };
    std::string root_identity;
    std::string library;
    std::string owner_identity;
};

enum class TraceTypeKind : std::uint8_t {
    Packed,
    SystemVerilogScalar,
    SystemVerilogString,
    Enumeration,
  VhdlPhysical,
  VhdlTime,
  VhdlLogic9,
  TypedLeaf
};

enum class TraceDeclarationKind : std::uint8_t {
    Variable,
    Alias
};

enum class TraceRegion : std::uint8_t {
    Snapshot,
    Preponed,
    Active,
    Inactive,
    NonblockingAssign,
    Observed,
    Reactive,
    Postponed,
    Callback
};

struct TraceScopeDeclaration {
    TraceScopeId id;
    TraceScopeId parent;
    TraceSourceId source;
    std::string name;
    std::string path;
};

struct TraceTypeDeclaration {
    TraceTypeId id;
    TraceTypeKind kind { TraceTypeKind::Packed };
    std::size_t width { };
    SystemVerilogScalarKind scalar_kind { SystemVerilogScalarKind::None };
    std::string canonical_metadata;
};

struct TraceSourceDeclaration {
    TraceSourceId id;
    TraceSourceKind kind { TraceSourceKind::Hdl };
    TraceLanguage language { TraceLanguage::Unknown };
    std::string root_identity;
    std::string library;
    std::string owner_identity;
    std::string canonical_name;
};

struct TraceVariableDeclaration {
    TraceSignalId id;
    TraceScopeId scope;
    TraceTypeId type;
    TraceSourceId source;
    std::string hierarchical_name;
    std::string reference;
};

struct TraceAliasDeclaration {
    TraceSignalId id;
    TraceScopeId scope;
    TraceSignalId target;
    TraceSourceId source;
    std::string hierarchical_name;
    std::string reference;
};

struct TraceDeclarationEntry {
    TraceSignalId id;
    TraceDeclarationKind kind { TraceDeclarationKind::Variable };
};

class TraceDeclarationModel final {
public:
    TraceDeclarationModel(const TraceDeclarationModel&) = default;
    TraceDeclarationModel(TraceDeclarationModel&&) noexcept = default;
    TraceDeclarationModel& operator=(const TraceDeclarationModel&) = default;
    TraceDeclarationModel& operator=(TraceDeclarationModel&&) noexcept = default;
    ~TraceDeclarationModel();

    [[nodiscard]] std::span<const TraceScopeDeclaration> scopes() const noexcept;
    [[nodiscard]] std::span<const TraceTypeDeclaration> types() const noexcept;
    [[nodiscard]] std::span<const TraceSourceDeclaration> sources() const noexcept;
    [[nodiscard]] std::span<const TraceVariableDeclaration> variables() const noexcept;
    [[nodiscard]] std::span<const TraceAliasDeclaration> aliases() const noexcept;
    [[nodiscard]] std::span<const TraceDeclarationEntry> entries() const noexcept;
    [[nodiscard]] const TraceVariableDeclaration& variable(TraceSignalId id) const;
    [[nodiscard]] const TraceAliasDeclaration& alias(TraceSignalId id) const;
    [[nodiscard]] const TraceTypeDeclaration& type(TraceTypeId id) const;
    [[nodiscard]] const TraceSourceDeclaration& source(TraceSourceId id) const;

private:
    struct Impl;
    explicit TraceDeclarationModel(std::shared_ptr<const Impl> impl);
    std::shared_ptr<const Impl> impl_;
    friend class TraceDeclarationBuilder;
};

class TraceDeclarationBuilder final {
public:
    TraceDeclarationBuilder();
    ~TraceDeclarationBuilder();
    TraceDeclarationBuilder(TraceDeclarationBuilder&&) noexcept;
    TraceDeclarationBuilder& operator=(TraceDeclarationBuilder&&) noexcept;
    TraceDeclarationBuilder(const TraceDeclarationBuilder&) = delete;
    TraceDeclarationBuilder& operator=(const TraceDeclarationBuilder&) = delete;

    [[nodiscard]] TraceSignalId add_variable(
        std::string_view hierarchical_name,
        std::size_t width,
        SystemVerilogScalarKind scalar_kind = SystemVerilogScalarKind::None,
        TraceSourceKind source_kind = TraceSourceKind::Hdl);
    [[nodiscard]] TraceSignalId add_typed_variable(
        std::string_view hierarchical_name,
        TraceTypeKind type_kind,
        std::size_t width,
        SystemVerilogScalarKind scalar_kind,
        std::string_view canonical_metadata,
        TraceSourceKind source_kind = TraceSourceKind::Hdl);
    [[nodiscard]] TraceSignalId add_typed_variable(
        std::string_view hierarchical_name,
        TraceTypeKind type_kind,
        std::size_t width,
        SystemVerilogScalarKind scalar_kind,
        std::string_view canonical_metadata,
        const TraceSourceMetadata& source_metadata);
    [[nodiscard]] TraceSignalId add_alias(
        std::string_view hierarchical_name,
        TraceSignalId target,
        TraceSourceKind source_kind = TraceSourceKind::Hdl);
    [[nodiscard]] TraceSignalId add_alias(
        std::string_view hierarchical_name,
        TraceSignalId target,
        const TraceSourceMetadata& source_metadata);
    [[nodiscard]] TraceDeclarationModel freeze() &&;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct TraceEvent {
    TraceSignalId signal;
    SimulationTick time { };
    std::uint64_t delta { };
    TraceRegion region { TraceRegion::Active };
    std::uint64_t sequence { };
};

[[nodiscard]] bool trace_event_precedes(
    const TraceEvent& left,
    const TraceEvent& right) noexcept;

} // namespace fsim::runtime
