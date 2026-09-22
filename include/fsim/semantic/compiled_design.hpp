// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "fsim/semantic/vhdl_hir.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::semantic {

enum class CompiledReferenceKind : std::uint8_t {
    package,
    import,
    entity,
    architecture,
    module,
    class_declaration,
    configuration,
    bind,
    context,
};

/// One deterministic link edge retained by a compiled HIR bundle. Names are
/// canonical logical-library identities; physical checkout paths never
/// participate in linking or artifact identity.
struct CompiledReference {
    CompiledReferenceKind kind { CompiledReferenceKind::module };
    UnitId owner;
    std::string library;
    std::string name;
    std::string secondary_name;
    SourceSpanId source;
    std::optional<UnitId> target;

    friend bool operator==(const CompiledReference&,
        const CompiledReference&) = default;
};

struct CompiledDependency {
    UnitId owner;
    std::string logical_name;
    std::string content_digest;

    friend bool operator==(const CompiledDependency&,
        const CompiledDependency&) = default;
};

/// Canonical package visibility retained in the compiled-design lookup
/// indexes. The strings are owned by the index so resolver reads remain
/// allocation-free after linking and normalization.
struct CompiledVhdlImport {
    std::string library;
    std::string package;
    std::string member;
};

/// One package member retained in the non-owning compiled-design lookup
/// indexes. The declaration remains owned by the VHDL HIR.
struct CompiledVhdlPackageMemberIndexEntry {
    std::string_view library;
    std::string_view package;
    UnitId template_unit;
    DeclarationId member;
};

/// Non-owning language dispatch returned by CompiledDesign. Exactly one unit
/// pointer is present. Shared elaboration code may inspect this view but may
/// not retain or mutate it.
struct CompiledUnitView {
    Language language { Language::system_verilog };
    const Unit* identity { };
    const sv::Unit* systemverilog { };
    const vhdl::Unit* vhdl { };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return identity != nullptr
            && (systemverilog != nullptr) != (vhdl != nullptr);
    }
};

template <typename SystemVerilogRecord, typename VhdlRecord>
struct CompiledLanguageView {
    const SystemVerilogRecord* systemverilog { };
    const VhdlRecord* vhdl { };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return (systemverilog != nullptr) != (vhdl != nullptr);
    }
};

using CompiledDeclarationView =
    CompiledLanguageView<sv::Declaration, vhdl::Declaration>;
using CompiledTypeView =
    CompiledLanguageView<sv::TypeDefinition, vhdl::TypeDefinition>;
using CompiledExpressionView =
    CompiledLanguageView<sv::Expression, vhdl::Expression>;
using CompiledStatementView =
    CompiledLanguageView<sv::Statement, vhdl::Statement>;
using CompiledProcessView =
    CompiledLanguageView<sv::Process, vhdl::Process>;
using CompiledInstanceView =
    CompiledLanguageView<sv::Instance, vhdl::Instance>;

class SpecializedHirUnit;
class ValidatedCompiledDesign;

/// Complete parser-independent compilation product. The semantic model owns
/// every shared identity; both language HIRs and all link metadata use only
/// those identities and owned strings.
class CompiledDesign {
public:
    CompiledDesign() = default;
    CompiledDesign(const CompiledDesign&);
    CompiledDesign(CompiledDesign&&);
    CompiledDesign& operator=(const CompiledDesign&);
    CompiledDesign& operator=(CompiledDesign&&);
    CompiledDesign(Model semantics, sv::Hir systemverilog,
        vhdl::Hir vhdl,
        std::vector<CompiledDependency> dependencies = {},
        std::vector<CompiledReference> references = {});

    [[nodiscard]] std::span<const CompiledDependency>
    dependencies() const noexcept;
    [[nodiscard]] std::span<const CompiledReference>
    references() const noexcept;
    [[nodiscard]] std::span<const Unit> units() const noexcept;
    [[nodiscard]] std::span<const sv::Unit>
    systemverilog_units() const noexcept;
    [[nodiscard]] std::span<const vhdl::Unit> vhdl_units() const noexcept;

    [[nodiscard]] std::optional<CompiledUnitView>
    find_unit(UnitId id) const noexcept;
    /// For a VHDL architecture, name is the entity and secondary_name is the
    /// architecture. Other unit kinds use their stored primary and secondary
    /// identities directly.
    [[nodiscard]] std::optional<CompiledUnitView> find_unit(
        UnitKind kind, std::string_view library,
        std::string_view name,
        std::string_view secondary_name = {}) const noexcept;
    /// Returns every unit with this exact semantic identity. Unlike find_unit,
    /// this preserves external declarations and ambiguous definitions so
    /// callers that diagnose collisions can inspect the complete candidate set.
    [[nodiscard]] std::vector<CompiledUnitView> find_units(
        UnitKind kind, std::string_view library,
        std::string_view name,
        std::string_view secondary_name = {}) const noexcept;
    [[nodiscard]] std::optional<CompiledUnitView> find_unit(
        Language language, std::string_view library,
        std::string_view name,
        std::string_view secondary_name = {}) const;
    [[nodiscard]] std::optional<CompiledDeclarationView>
    find_declaration(DeclarationId id) const noexcept;
    [[nodiscard]] std::optional<CompiledTypeView>
    find_type(TypeId id) const noexcept;
    [[nodiscard]] std::optional<CompiledExpressionView>
    find_expression(ExpressionId id) const noexcept;
    [[nodiscard]] std::optional<CompiledStatementView>
    find_statement(StatementId id) const noexcept;
    [[nodiscard]] std::optional<CompiledProcessView>
    find_process(ProcessId id) const noexcept;
    [[nodiscard]] std::optional<CompiledInstanceView>
    find_instance(InstanceId id) const noexcept;
    /// Return the compiled declaration identities owned by one semantic
    /// scope. A disengaged result means that the non-owning lookup indexes are
    /// stale, so callers which must remain correct during mutation fall back
    /// to the complete HIR collection.
    [[nodiscard]] std::optional<std::span<const DeclarationId>>
    systemverilog_declarations_in_scope(ScopeId scope) const noexcept;
    [[nodiscard]] std::optional<std::span<const DeclarationId>>
    vhdl_declarations_in_scope(ScopeId scope) const noexcept;
    /// Return every VHDL type, subtype, or generic-type declaration with this
    /// identifier. An engaged empty span proves that no lexical or imported
    /// lookup can attach nominal type identity for the name.
    [[nodiscard]] std::optional<std::span<const DeclarationId>>
    vhdl_type_declarations_named(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::span<const CompiledVhdlImport>>
    vhdl_linked_imports(UnitId owner) const noexcept;
    [[nodiscard]] std::optional<
        std::span<const CompiledVhdlPackageMemberIndexEntry>>
    vhdl_package_members(std::string_view member) const noexcept;
    [[nodiscard]] bool valid() const;

    Model& mutable_semantics() noexcept;
    sv::Hir& mutable_systemverilog() noexcept;
    vhdl::Hir& mutable_vhdl() noexcept;
    std::vector<CompiledDependency>& mutable_dependencies() noexcept;
    std::vector<CompiledReference>& mutable_references() noexcept;
    /// Rebuild non-owning dense-ID indexes after a completed HIR mutation
    /// phase and before sharing the design with parallel readers.
    void refresh_lookup_indexes();

    Model semantics;
    sv::Hir systemverilog_hir;
    vhdl::Hir vhdl_hir;

private:
    friend class SpecializedHirUnit;
    friend class ValidatedCompiledDesign;

    static constexpr std::uint8_t systemverilog_index_language = 1U;
    static constexpr std::uint8_t vhdl_index_language = 2U;

    struct RecordPositions {
        struct Record {
            const void* address { };
            std::uint8_t language { };
        };
        std::vector<Record> records;
    };

    struct LookupIndexes {
        struct VhdlIdentifierHash {
            using is_transparent = void;

            [[nodiscard]] std::size_t operator()(
                std::string_view) const noexcept;
        };

        struct VhdlIdentifierEqual {
            using is_transparent = void;

            [[nodiscard]] bool operator()(
                std::string_view, std::string_view) const noexcept;
        };

        bool initialized { };
        const CompiledDesign* owner { };
        std::uint64_t systemverilog_revision { };
        std::uint64_t vhdl_revision { };
        RecordPositions units;
        RecordPositions declarations;
        RecordPositions types;
        RecordPositions expressions;
        RecordPositions statements;
        RecordPositions processes;
        RecordPositions instances;
        std::vector<std::vector<DeclarationId>>
            systemverilog_declarations_by_scope;
        std::vector<std::vector<DeclarationId>>
            vhdl_declarations_by_scope;
        std::unordered_map<std::string_view,
            std::vector<DeclarationId>,
            VhdlIdentifierHash, VhdlIdentifierEqual>
            vhdl_type_declarations_by_name;
        std::vector<std::vector<CompiledVhdlImport>>
            vhdl_imports_by_unit;
        std::unordered_map<std::string_view,
            std::vector<CompiledVhdlPackageMemberIndexEntry>,
            VhdlIdentifierHash, VhdlIdentifierEqual>
            vhdl_package_members;
    };

    template <typename SystemVerilogRecord, typename VhdlRecord,
        typename Identity>
    [[nodiscard]] CompiledLanguageView<SystemVerilogRecord, VhdlRecord>
    indexed_language_record(const RecordPositions& positions,
        const Identity id) const noexcept
    {
        if (!id.valid() || id.value() >= positions.records.size()) {
            return { };
        }
        const auto& indexed = positions.records[id.value()];
        if (indexed.language == systemverilog_index_language) {
            return { static_cast<const SystemVerilogRecord*>(
                         indexed.address),
                nullptr };
        }
        if (indexed.language == vhdl_index_language) {
            return { nullptr,
                static_cast<const VhdlRecord*>(indexed.address) };
        }
        return { };
    }

    [[nodiscard]] bool lookup_indexes_current() const noexcept;
    [[nodiscard]] CompiledDeclarationView
    indexed_declaration(const DeclarationId id) const noexcept
    {
        return indexed_language_record<sv::Declaration, vhdl::Declaration>(
            lookup_indexes_.declarations, id);
    }
    [[nodiscard]] CompiledTypeView
    indexed_type(const TypeId id) const noexcept
    {
        return indexed_language_record<
            sv::TypeDefinition, vhdl::TypeDefinition>(
            lookup_indexes_.types, id);
    }
    [[nodiscard]] CompiledExpressionView
    indexed_expression(const ExpressionId id) const noexcept
    {
        return indexed_language_record<sv::Expression, vhdl::Expression>(
            lookup_indexes_.expressions, id);
    }
    [[nodiscard]] CompiledStatementView
    indexed_statement(const StatementId id) const noexcept
    {
        return indexed_language_record<sv::Statement, vhdl::Statement>(
            lookup_indexes_.statements, id);
    }
    [[nodiscard]] CompiledProcessView
    indexed_process(const ProcessId id) const noexcept
    {
        return indexed_language_record<sv::Process, vhdl::Process>(
            lookup_indexes_.processes, id);
    }
    [[nodiscard]] CompiledInstanceView
    indexed_instance(const InstanceId id) const noexcept
    {
        return indexed_language_record<sv::Instance, vhdl::Instance>(
            lookup_indexes_.instances, id);
    }

    std::vector<CompiledDependency> dependencies_;
    std::vector<CompiledReference> references_;
    LookupIndexes lookup_indexes_;
};

/// Proof that one immutable compiled design passed its complete structural
/// validation with current lookup indexes at an elaboration boundary. The
/// token borrows the design and is intentionally constructible only through
/// validate_compiled_design().
class ValidatedCompiledDesign final {
public:
    [[nodiscard]] const CompiledDesign& design() const noexcept;

private:
    friend std::optional<ValidatedCompiledDesign>
    validate_compiled_design(const CompiledDesign&);

    [[nodiscard]] static bool can_validate(
        const CompiledDesign&) noexcept;
    explicit ValidatedCompiledDesign(const CompiledDesign&) noexcept;

    const CompiledDesign* design_ { };
};

[[nodiscard]] std::optional<ValidatedCompiledDesign>
validate_compiled_design(const CompiledDesign&);

/// Empty when every VHDL instance record is claimed exactly once by its owning
/// unit or nested generate region and the semantic scope chain agrees.
[[nodiscard]] std::string vhdl_instance_ownership_error(
    const CompiledDesign&);

} // namespace fsim::semantic
