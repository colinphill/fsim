// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::semantic {

template <typename Tag>
class Id final {
public:
    constexpr Id() noexcept = default;

    [[nodiscard]] static constexpr Id from_index(
        const std::uint32_t value) noexcept {
        return Id{value};
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value_ != invalid_value;
    }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return value_;
    }

    friend constexpr auto operator<=>(const Id&, const Id&) = default;

private:
    static constexpr std::uint32_t invalid_value =
        std::numeric_limits<std::uint32_t>::max();

    explicit constexpr Id(const std::uint32_t value) noexcept : value_(value) {}

    std::uint32_t value_{invalid_value};
};

struct SourceFileTag;
struct SourceSpanTag;
struct ExpansionTag;
struct OriginTag;
struct LibraryTag;
struct UnitTag;
struct ScopeTag;
struct DeclarationTag;
struct TypeTag;
struct ValueTag;
struct ExpressionTag;
struct StatementTag;
struct InstanceTag;
struct ProcessTag;
struct PortTag;
struct DriverTag;

using SourceFileId = Id<SourceFileTag>;
using SourceSpanId = Id<SourceSpanTag>;
using ExpansionId = Id<ExpansionTag>;
using OriginId = Id<OriginTag>;
using LibraryId = Id<LibraryTag>;
using UnitId = Id<UnitTag>;
using ScopeId = Id<ScopeTag>;
using DeclarationId = Id<DeclarationTag>;
using TypeId = Id<TypeTag>;
using ValueId = Id<ValueTag>;
using ExpressionId = Id<ExpressionTag>;
using StatementId = Id<StatementTag>;
using InstanceId = Id<InstanceTag>;
using ProcessId = Id<ProcessTag>;
using PortId = Id<PortTag>;
using DriverId = Id<DriverTag>;

struct SourcePosition {
    std::uint64_t offset{};
    std::uint32_t line{1};
    std::uint32_t column{1};

    friend constexpr auto operator<=>(
        const SourcePosition&, const SourcePosition&) = default;
};

struct SourceFile {
    SourceFileId id;
    std::string physical_name;
    std::string content_digest;
};

struct Expansion {
    ExpansionId id;
    std::optional<ExpansionId> parent;
    std::string description;
};

struct SourceSpan {
    SourceSpanId id;
    SourceFileId file;
    std::string logical_name;
    SourcePosition begin;
    SourcePosition end;
    std::optional<ExpansionId> expansion;
};

enum class OriginKind : std::uint8_t {
    parsed,
    intrinsic,
    generated,
    specialized,
    systemc,
};

struct Origin {
    OriginId id;
    OriginKind kind{OriginKind::parsed};
    SourceSpanId source;
    std::optional<OriginId> parent;
    std::string detail;
};

enum class Language : std::uint8_t {
    vhdl,
    verilog,
    system_verilog,
    systemc,
};

enum class UnitKind : std::uint8_t {
    vhdl_entity,
    vhdl_architecture,
    vhdl_configuration,
    vhdl_package,
    vhdl_context,
    systemverilog_package,
    systemverilog_interface,
    verilog_module,
    systemc_factory,
    systemverilog_program,
};

enum class TypeKind : std::uint8_t {
    declaration,
    subtype,
    alias,
    implicit,
};

enum class ValueKind : std::uint8_t {
    parameter,
    port,
    signal,
    variable,
    function,
    task,
    procedure,
    enumeration_literal,
    implicit,
};

enum class DeclarationKind : std::uint8_t {
    type,
    subtype,
    constant,
    generic,
    port,
    signal,
    variable,
    file,
    alias,
    function,
    procedure,
    task,
    component,
    package_instance,
    configuration,
    generate,
    enumeration_literal,
    implicit,
};

struct TypeReference {
    TypeId target;
    SourceSpanId source;
    std::string spelling;

    [[nodiscard]] bool resolved() const noexcept { return target.valid(); }
};

struct ValueReference {
    ValueId target;
    SourceSpanId source;
    std::string spelling;

    [[nodiscard]] bool resolved() const noexcept { return target.valid(); }
};

struct Declaration {
    DeclarationId id;
    ScopeId scope;
    DeclarationKind kind{DeclarationKind::implicit};
    std::string name;
    SourceSpanId source;
    OriginId origin;
};

struct ExpressionIdentity {
    ExpressionId id;
    ScopeId scope;
    SourceSpanId source;
    OriginId origin;
};

struct StatementIdentity {
    StatementId id;
    ScopeId scope;
    SourceSpanId source;
    OriginId origin;
};

struct ProcessIdentity {
    ProcessId id;
    ScopeId scope;
    std::string name;
    SourceSpanId source;
    OriginId origin;
};

struct Scope {
    ScopeId id;
    UnitId unit;
    std::optional<ScopeId> parent;
    std::string name;
    SourceSpanId source;
    OriginId origin;
};

struct Unit {
    UnitId id;
    ScopeId scope;
    Language language{Language::system_verilog};
    UnitKind kind{UnitKind::verilog_module};
    std::string library;
    std::string name;
    std::string secondary_name;
    SourceSpanId source;
    OriginId origin;
};

struct Type {
    TypeId id;
    ScopeId scope;
    TypeKind kind{TypeKind::declaration};
    std::string name;
    TypeReference base;
    SourceSpanId source;
    OriginId origin;
};

struct Value {
    ValueId id;
    ScopeId scope;
    ValueKind kind{ValueKind::variable};
    std::string name;
    TypeReference type;
    SourceSpanId source;
    OriginId origin;
};

struct Instance {
    InstanceId id;
    ScopeId scope;
    std::string name;
    std::string target;
    SourceSpanId source;
    OriginId origin;
};

struct ModelRecords {
    std::vector<SourceFile> source_files;
    std::vector<Expansion> expansions;
    std::vector<SourceSpan> source_spans;
    std::vector<Origin> origins;
    std::vector<Scope> scopes;
    std::vector<Unit> units;
    std::vector<Type> types;
    std::vector<Value> values;
    std::vector<Instance> instances;
    std::vector<Declaration> declarations;
    std::vector<ExpressionIdentity> expression_identities;
    std::vector<StatementIdentity> statement_identities;
    std::vector<ProcessIdentity> process_identities;
};

/// Owning, deterministic semantic metadata store.
///
/// IDs are dense vector indices assigned in caller-supplied canonical order.
/// Records own all strings and never retain pointers or views into parser
/// storage. Identical file, expansion, and span records are interned; semantic
/// entities intentionally remain declaration occurrences and are not deduped.
class Model final {
public:
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ModelRecords records() const;
    [[nodiscard]] static std::optional<Model> from_records(
        ModelRecords records);

    [[nodiscard]] SourceFileId intern_source_file(
        std::string physical_name,
        std::string content_digest = {});
    [[nodiscard]] std::optional<SourceFileId> find_source_file(
        std::string_view physical_name) const noexcept;
    [[nodiscard]] ExpansionId intern_expansion(
        std::span<const std::string> outermost_to_innermost);
    [[nodiscard]] SourceSpanId intern_source_span(
        SourceFileId file,
        std::string logical_name,
        SourcePosition begin,
        SourcePosition end,
        std::optional<ExpansionId> expansion = std::nullopt);
    [[nodiscard]] OriginId add_origin(
        OriginKind kind,
        SourceSpanId source,
        std::optional<OriginId> parent = std::nullopt,
        std::string detail = {});
    [[nodiscard]] UnitId add_unit(
        Language language,
        UnitKind kind,
        std::string library,
        std::string name,
        std::string secondary_name,
        SourceSpanId source,
        OriginId origin);
    [[nodiscard]] ScopeId add_scope(
        UnitId unit,
        std::optional<ScopeId> parent,
        std::string name,
        SourceSpanId source,
        OriginId origin);
    [[nodiscard]] TypeId add_type(
        ScopeId scope,
        TypeKind kind,
        std::string name,
        TypeReference base,
        SourceSpanId source,
        OriginId origin);
    [[nodiscard]] ValueId add_value(
        ScopeId scope,
        ValueKind kind,
        std::string name,
        TypeReference type,
        SourceSpanId source,
        OriginId origin);
    [[nodiscard]] InstanceId add_instance(
        ScopeId scope,
        std::string name,
        std::string target,
        SourceSpanId source,
        OriginId origin);
    [[nodiscard]] DeclarationId add_declaration(
        ScopeId scope,
        DeclarationKind kind,
        std::string name,
        SourceSpanId source,
        OriginId origin);
    [[nodiscard]] ExpressionId add_expression_identity(
        ScopeId scope,
        SourceSpanId source,
        OriginId origin);
    [[nodiscard]] StatementId add_statement_identity(
        ScopeId scope,
        SourceSpanId source,
        OriginId origin);
    [[nodiscard]] ProcessId add_process_identity(
        ScopeId scope,
        std::string name,
        SourceSpanId source,
        OriginId origin);

    [[nodiscard]] const std::vector<SourceFile>& source_files() const noexcept;
    [[nodiscard]] const std::vector<Expansion>& expansions() const noexcept;
    [[nodiscard]] const std::vector<SourceSpan>& source_spans() const noexcept;
    [[nodiscard]] const std::vector<Origin>& origins() const noexcept;
    [[nodiscard]] const std::vector<Scope>& scopes() const noexcept;
    [[nodiscard]] const std::vector<Unit>& units() const noexcept;
    [[nodiscard]] const std::vector<Type>& types() const noexcept;
    [[nodiscard]] const std::vector<Value>& values() const noexcept;
    [[nodiscard]] const std::vector<Instance>& instances() const noexcept;
    [[nodiscard]] const std::vector<Declaration>& declarations() const noexcept;
    [[nodiscard]] const std::vector<ExpressionIdentity>&
    expression_identities() const noexcept;
    [[nodiscard]] const std::vector<StatementIdentity>&
    statement_identities() const noexcept;
    [[nodiscard]] const std::vector<ProcessIdentity>&
    process_identities() const noexcept;

private:
    struct FileKey {
        std::string physical_name;
        std::string content_digest;
        friend auto operator<=>(const FileKey&, const FileKey&) = default;
    };
    struct ExpansionKey {
        std::optional<ExpansionId> parent;
        std::string description;
        friend auto operator<=>(const ExpansionKey&, const ExpansionKey&) = default;
    };
    struct SpanKey {
        SourceFileId file;
        std::string logical_name;
        SourcePosition begin;
        SourcePosition end;
        std::optional<ExpansionId> expansion;
        friend auto operator<=>(const SpanKey&, const SpanKey&) = default;
    };

    std::vector<SourceFile> source_files_;
    std::vector<Expansion> expansions_;
    std::vector<SourceSpan> source_spans_;
    std::vector<Origin> origins_;
    std::vector<Scope> scopes_;
    std::vector<Unit> units_;
    std::vector<Type> types_;
    std::vector<Value> values_;
    std::vector<Instance> instances_;
    std::vector<Declaration> declarations_;
    std::vector<ExpressionIdentity> expression_identities_;
    std::vector<StatementIdentity> statement_identities_;
    std::vector<ProcessIdentity> process_identities_;
    std::map<FileKey, SourceFileId> source_file_by_key_;
    std::map<std::string, SourceFileId, std::less<>> source_file_by_name_;
    std::map<ExpansionKey, ExpansionId> expansion_by_key_;
    std::map<SpanKey, SourceSpanId> source_span_by_key_;
};

} // namespace fsim::semantic
