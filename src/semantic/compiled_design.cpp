// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

namespace fsim::semantic {
namespace {

std::string_view normalized_library(const std::string_view library)
{
    return library.empty() ? std::string_view { "work" } : library;
}

bool extended_vhdl_identifier(const std::string_view value)
{
    return value.size() >= 2U && value.front() == '\\'
        && value.back() == '\\';
}

bool same_vhdl_identifier(
    const std::string_view left, const std::string_view right)
{
    if (extended_vhdl_identifier(left)
        || extended_vhdl_identifier(right)) {
        return left == right;
    }
    const auto lower_ascii = [](const unsigned char character) {
        return character >= 'A' && character <= 'Z'
            ? static_cast<unsigned char>(character - 'A' + 'a')
            : character;
    };
    return left.size() == right.size()
        && std::ranges::equal(left, right,
            [&](const unsigned char lhs, const unsigned char rhs) {
                return lower_ascii(lhs) == lower_ascii(rhs);
            });
}

std::string canonical_vhdl_identifier(const std::string_view value)
{
    if (extended_vhdl_identifier(value)) {
        return std::string { value };
    }
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(static_cast<char>(
            byte >= 'A' && byte <= 'Z'
                ? byte - 'A' + 'a'
                : byte));
    }
    return result;
}

std::size_t hash_vhdl_identifier(const std::string_view value) noexcept
{
    constexpr std::size_t offset = 1469598103934665603ULL;
    constexpr std::size_t prime = 1099511628211ULL;
    const bool extended = extended_vhdl_identifier(value);
    auto result = offset;
    for (const auto character : value) {
        auto byte = static_cast<unsigned char>(character);
        if (!extended && byte >= 'A' && byte <= 'Z') {
            byte = static_cast<unsigned char>(byte - 'A' + 'a');
        }
        result ^= byte;
        result *= prime;
    }
    return result;
}

std::string_view vhdl_spelling(const vhdl::Name& name)
{
    return name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
}

std::vector<std::string_view> vhdl_name_parts(
    const std::string_view name)
{
    std::vector<std::string_view> result;
    std::size_t begin { };
    while (begin < name.size()) {
        const auto end = name.find('.', begin);
        const auto part = name.substr(begin,
            end == std::string_view::npos
                ? std::string_view::npos
                : end - begin);
        if (part.empty()) {
            return { };
        }
        result.push_back(part);
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return result;
}

constexpr std::uint8_t missing_record = 0U;
constexpr std::uint8_t systemverilog_record_language = 1U;
constexpr std::uint8_t vhdl_record_language = 2U;
constexpr std::uint8_t duplicate_record = 3U;

template <typename HirRecord>
const HirRecord* find_unique_by_id(
    const std::span<const HirRecord> records, const auto id) noexcept
{
    const HirRecord* result = nullptr;
    for (const auto& record : records) {
        if (record.id != id) {
            continue;
        }
        if (result != nullptr) {
            return nullptr;
        }
        result = &record;
    }
    return result;
}

template <typename Identity, typename Positions,
    typename SystemVerilogRecords, typename VhdlRecords>
auto find_language_record(
    const std::span<const Identity> identities,
    const Positions positions,
    const bool use_indexes,
    const auto id,
    SystemVerilogRecords&& systemverilog_records,
    VhdlRecords&& vhdl_records) noexcept
{
    using SystemVerilogSpan = decltype(systemverilog_records());
    using VhdlSpan = decltype(vhdl_records());
    using SystemVerilogRecord =
        std::remove_const_t<typename SystemVerilogSpan::element_type>;
    using VhdlRecord =
        std::remove_const_t<typename VhdlSpan::element_type>;
    using View = CompiledLanguageView<SystemVerilogRecord, VhdlRecord>;

    if (!id.valid() || id.value() >= identities.size()
        || identities[id.value()].id != id) {
        return std::optional<View> { };
    }
    if (use_indexes && id.value() < positions.size()) {
        const auto& indexed = positions[id.value()];
        if (indexed.language == systemverilog_record_language) {
            const auto* record = static_cast<const SystemVerilogRecord*>(
                indexed.address);
            if (record != nullptr && record->id == id) {
                return std::optional<View> { View { record, nullptr } };
            }
            return std::optional<View> { };
        }
        if (indexed.language == vhdl_record_language) {
            const auto* record = static_cast<const VhdlRecord*>(
                indexed.address);
            if (record != nullptr && record->id == id) {
                return std::optional<View> { View { nullptr, record } };
            }
            return std::optional<View> { };
        }
        return std::optional<View> { };
    }

    const auto systemverilog = systemverilog_records();
    const auto vhdl = vhdl_records();
    const auto* systemverilog_record = find_unique_by_id(systemverilog, id);
    const auto* vhdl_record = find_unique_by_id(vhdl, id);
    if ((systemverilog_record != nullptr) == (vhdl_record != nullptr)) {
        return std::optional<View> { };
    }
    return std::optional<View> {
        View { systemverilog_record, vhdl_record } };
}

std::optional<UnitKind> semantic_kind(const sv::UnitKind kind) noexcept
{
    switch (kind) {
    case sv::UnitKind::module:
        return UnitKind::verilog_module;
    case sv::UnitKind::package:
        return UnitKind::systemverilog_package;
    case sv::UnitKind::interface:
        return UnitKind::systemverilog_interface;
    case sv::UnitKind::program:
        return UnitKind::systemverilog_program;
    case sv::UnitKind::configuration:
        return UnitKind::systemverilog_configuration;
    case sv::UnitKind::bind:
        return UnitKind::systemverilog_bind;
    case sv::UnitKind::compilation_unit:
        return UnitKind::systemverilog_compilation_unit;
    }
    return std::nullopt;
}

std::optional<UnitKind> semantic_kind(const vhdl::UnitKind kind) noexcept
{
    switch (kind) {
    case vhdl::UnitKind::entity:
        return UnitKind::vhdl_entity;
    case vhdl::UnitKind::architecture:
        return UnitKind::vhdl_architecture;
    case vhdl::UnitKind::configuration:
        return UnitKind::vhdl_configuration;
    case vhdl::UnitKind::package:
        return UnitKind::vhdl_package;
    case vhdl::UnitKind::context:
        return UnitKind::vhdl_context;
    case vhdl::UnitKind::psl_verification_unit:
        return UnitKind::vhdl_psl_verification_unit;
    }
    return std::nullopt;
}

bool same_identity(const Unit& identity, const sv::Unit& unit) noexcept
{
    return identity.id == unit.id
        && identity.scope == unit.scope
        && semantic_kind(unit.kind) == identity.kind
        && normalized_library(identity.library)
            == normalized_library(unit.library)
        && identity.name == unit.name
        && identity.secondary_name.empty()
        && identity.source == unit.source
        && identity.origin == unit.origin;
}

bool same_identity(const Unit& identity, const vhdl::Unit& unit) noexcept
{
    return identity.id == unit.id
        && identity.scope == unit.scope
        && semantic_kind(unit.kind) == identity.kind
        && normalized_library(identity.library)
            == normalized_library(unit.library)
        && identity.name == unit.name
        && identity.secondary_name == unit.primary_name
        && identity.source == unit.source
        && identity.origin == unit.origin;
}

bool language_matches(
    const Language requested, const Language actual) noexcept
{
    return requested == actual;
}

bool lookup_name_matches(const Unit& unit, const std::string_view name,
    const std::string_view secondary_name) noexcept
{
    if (unit.kind == UnitKind::vhdl_architecture) {
        return unit.secondary_name == name && unit.name == secondary_name;
    }
    return unit.name == name && unit.secondary_name == secondary_name;
}

bool lookup_identity_matches(const Unit& unit, const UnitKind kind,
    const std::string_view library, const std::string_view name,
    const std::string_view secondary_name) noexcept
{
    return unit.kind == kind
        && normalized_library(unit.library) == normalized_library(library)
        && lookup_name_matches(unit, name, secondary_name);
}

template <typename HirInstance>
std::vector<std::optional<const HirInstance*>> index_instances(
    const std::vector<HirInstance>& instances,
    const std::size_t identity_count)
{
    std::vector<std::optional<const HirInstance*>> result(identity_count);
    for (const auto& instance : instances) {
        if (!instance.id.valid() || instance.id.value() >= result.size()) {
            continue;
        }
        auto& indexed = result[instance.id.value()];
        if (indexed.has_value()) {
            indexed = nullptr;
        } else {
            indexed = &instance;
        }
    }
    return result;
}

std::string claim_systemverilog_instance(
    const CompiledDesign& design,
    const InstanceId id,
    const ScopeId scope,
    const std::vector<std::optional<const sv::Instance*>>& instances,
    std::vector<std::size_t>& claims)
{
    if (!id.valid() || id.value() >= claims.size()) {
        return "SystemVerilog instance owner contains an out-of-range "
               "InstanceId";
    }
    const auto* instance = instances[id.value()].value_or(nullptr);
    if (instance == nullptr) {
        return "SystemVerilog instance owner references a missing or "
               "duplicate HIR instance ID "
            + std::to_string(id.value());
    }
    if (instance->scope != scope
        || id.value() >= design.semantics.instances().size()
        || design.semantics.instances()[id.value()].scope != scope) {
        return "SystemVerilog instance ID " + std::to_string(id.value())
            + " is claimed by a foreign unit or generate scope";
    }
    if (++claims[id.value()] != 1U) {
        return "SystemVerilog instance ID " + std::to_string(id.value())
            + " is claimed more than once";
    }
    return { };
}

bool valid_scope_owner(
    const CompiledDesign& design,
    const ScopeId scope,
    const UnitId unit,
    const std::optional<ScopeId> parent)
{
    if (!scope.valid() || scope.value() >= design.semantics.scopes().size()) {
        return false;
    }
    const auto& record = design.semantics.scopes()[scope.value()];
    return record.unit == unit && record.parent == parent;
}

std::string claim_vhdl_instance(
    const CompiledDesign& design,
    const InstanceId id,
    const ScopeId scope,
    const std::vector<std::optional<const vhdl::Instance*>>& instances,
    std::vector<std::size_t>& claims)
{
    if (!id.valid() || id.value() >= claims.size()) {
        return "VHDL instance owner contains an out-of-range InstanceId";
    }
    const auto* instance = instances[id.value()].value_or(nullptr);
    if (instance == nullptr) {
        return "VHDL instance owner references a missing or duplicate HIR "
               "instance ID "
            + std::to_string(id.value());
    }
    if (instance->scope != scope
        || id.value() >= design.semantics.instances().size()
        || design.semantics.instances()[id.value()].scope != scope) {
        return "VHDL instance ID " + std::to_string(id.value())
            + " is claimed by a foreign unit or generate scope";
    }
    if (++claims[id.value()] != 1U) {
        return "VHDL instance ID " + std::to_string(id.value())
            + " is claimed more than once";
    }
    return { };
}

std::string claim_systemverilog_generate_instances(
    const CompiledDesign& design,
    const UnitId unit,
    const ScopeId parent,
    const sv::GenerateRegion& region,
    const std::vector<std::optional<const sv::Instance*>>& instances,
    std::vector<std::size_t>& claims)
{
    // An unscoped generate wrapper shares its enclosing scope. Named and
    // nested generate blocks own a child scope.
    if (region.scope != parent
        && !valid_scope_owner(design, region.scope, unit, parent)) {
        return "SystemVerilog generate region has a foreign scope owner";
    }
    for (const auto id : region.instances) {
        if (auto error = claim_systemverilog_instance(
                design, id, region.scope, instances, claims);
            !error.empty()) {
            return error;
        }
    }
    for (const auto& nested : region.nested) {
        // Conditional and selection alternatives are represented beneath the
        // selector region so selection remains deterministic, but their
        // language scopes are siblings of the selected branch. Preserve that
        // source-language ownership instead of requiring an artificial scope
        // nesting relationship.
        auto nested_parent = region.scope;
        if (nested.scope.valid()
            && nested.scope != region.scope
            && nested.scope.value() < design.semantics.scopes().size()
            && design.semantics.scopes()[nested.scope.value()].parent
                == parent) {
            nested_parent = parent;
        }
        if (auto error = claim_systemverilog_generate_instances(
                design, unit, nested_parent, nested, instances, claims);
            !error.empty()) {
            return error;
        }
    }
    return { };
}

std::string systemverilog_instance_ownership_error(
    const CompiledDesign& design)
{
    std::vector<std::size_t> claims(design.semantics.instances().size());
    const auto instances = index_instances(
        design.systemverilog_hir.instances(), claims.size());
    for (const auto& unit : design.systemverilog_hir.units()) {
        if (!unit.scope.valid()
            || unit.scope.value() >= design.semantics.scopes().size()
            || design.semantics.scopes()[unit.scope.value()].unit != unit.id) {
            return "SystemVerilog unit has a foreign semantic scope owner";
        }
        for (const auto id : unit.instances) {
            if (auto error = claim_systemverilog_instance(
                    design, id, unit.scope, instances, claims);
                !error.empty()) {
                return error;
            }
        }
        for (const auto& bind : unit.binds) {
            for (const auto id : bind.instances) {
                if (auto error = claim_systemverilog_instance(
                        design, id, unit.scope, instances, claims);
                    !error.empty()) {
                    return error;
                }
            }
        }
        for (const auto& generate : unit.generates) {
            if (auto error = claim_systemverilog_generate_instances(
                    design, unit.id, unit.scope, generate,
                    instances, claims);
                !error.empty()) {
                return error;
            }
        }
    }
    for (const auto& instance : design.systemverilog_hir.instances()) {
        if (!instance.id.valid()
            || instance.id.value() >= claims.size()
            || claims[instance.id.value()] != 1U) {
            return "SystemVerilog instance ID "
                + std::to_string(instance.id.value())
                + " is not claimed exactly once by a unit, bind, or generate region";
        }
    }
    return { };
}

std::string claim_vhdl_generate_instances(
    const CompiledDesign& design,
    const UnitId unit,
    const ScopeId parent,
    const vhdl::GenerateRegion& region,
    const std::vector<std::optional<const vhdl::Instance*>>& instances,
    std::vector<std::size_t>& claims)
{
    if (!valid_scope_owner(design, region.scope, unit, parent)) {
        return "VHDL generate region has a foreign scope owner";
    }
    for (const auto id : region.instances) {
        if (auto error = claim_vhdl_instance(
                design, id, region.scope, instances, claims);
            !error.empty()) {
            return error;
        }
    }
    for (const auto& nested : region.nested) {
        if (auto error = claim_vhdl_generate_instances(
                design, unit, region.scope, nested, instances, claims);
            !error.empty()) {
            return error;
        }
    }
    return { };
}

} // namespace

std::size_t CompiledDesign::LookupIndexes::VhdlIdentifierHash::operator()(
    const std::string_view value) const noexcept
{
    return hash_vhdl_identifier(value);
}

bool CompiledDesign::LookupIndexes::VhdlIdentifierEqual::operator()(
    const std::string_view left, const std::string_view right) const noexcept
{
    return same_vhdl_identifier(left, right);
}

std::string vhdl_instance_ownership_error(const CompiledDesign& design)
{
    std::vector<std::size_t> claims(design.semantics.instances().size());
    const auto instances = index_instances(
        design.vhdl_hir.instances(), claims.size());
    for (const auto& unit : design.vhdl_hir.units()) {
        if (!unit.scope.valid()
            || unit.scope.value() >= design.semantics.scopes().size()
            || design.semantics.scopes()[unit.scope.value()].unit != unit.id) {
            return "VHDL unit has a foreign semantic scope owner";
        }
        for (const auto id : unit.instances) {
            if (auto error = claim_vhdl_instance(
                    design, id, unit.scope, instances, claims);
                !error.empty()) {
                return error;
            }
        }
        for (const auto& generate : unit.generates) {
            if (auto error = claim_vhdl_generate_instances(
                    design, unit.id, unit.scope, generate,
                    instances, claims);
                !error.empty()) {
                return error;
            }
        }
    }
    for (const auto& instance : design.vhdl_hir.instances()) {
        if (!instance.id.valid()
            || instance.id.value() >= claims.size()
            || claims[instance.id.value()] != 1U) {
            return "VHDL instance ID "
                + std::to_string(instance.id.value())
                + " is not claimed exactly once by a unit or generate region";
        }
    }
    return { };
}

CompiledDesign::CompiledDesign(Model semantic_model, sv::Hir systemverilog,
    vhdl::Hir vhdl, std::vector<CompiledDependency> dependencies,
    std::vector<CompiledReference> references)
    : semantics { std::move(semantic_model) }
    , systemverilog_hir { std::move(systemverilog) }
    , vhdl_hir { std::move(vhdl) }
    , dependencies_ { std::move(dependencies) }
    , references_ { std::move(references) }
{
    refresh_lookup_indexes();
}

CompiledDesign::CompiledDesign(const CompiledDesign& other)
    : semantics { other.semantics }
    , systemverilog_hir { other.systemverilog_hir }
    , vhdl_hir { other.vhdl_hir }
    , dependencies_ { other.dependencies_ }
    , references_ { other.references_ }
{
    refresh_lookup_indexes();
}

CompiledDesign::CompiledDesign(CompiledDesign&& other)
    : semantics { [&other]() -> Model&& {
          if (!other.lookup_indexes_current()) {
              other.lookup_indexes_ = LookupIndexes { };
          }
          return std::move(other.semantics);
      }() }
    , systemverilog_hir { std::move(other.systemverilog_hir) }
    , vhdl_hir { std::move(other.vhdl_hir) }
    , dependencies_ { std::move(other.dependencies_) }
    , references_ { std::move(other.references_) }
    , lookup_indexes_ { std::move(other.lookup_indexes_) }
{
    lookup_indexes_.owner = this;
    other.lookup_indexes_.initialized = false;
    other.lookup_indexes_.owner = nullptr;
}

CompiledDesign& CompiledDesign::operator=(const CompiledDesign& other)
{
    if (this == &other) {
        return *this;
    }
    CompiledDesign copy { other };
    *this = std::move(copy);
    return *this;
}

CompiledDesign& CompiledDesign::operator=(CompiledDesign&& other)
{
    if (this == &other) {
        return *this;
    }
    if (!other.lookup_indexes_current()) {
        other.lookup_indexes_ = LookupIndexes { };
    }
    lookup_indexes_ = LookupIndexes { };
    semantics = std::move(other.semantics);
    systemverilog_hir = std::move(other.systemverilog_hir);
    vhdl_hir = std::move(other.vhdl_hir);
    dependencies_ = std::move(other.dependencies_);
    references_ = std::move(other.references_);
    lookup_indexes_ = std::move(other.lookup_indexes_);
    lookup_indexes_.owner = this;
    other.lookup_indexes_.initialized = false;
    other.lookup_indexes_.owner = nullptr;
    return *this;
}

bool CompiledDesign::lookup_indexes_current() const noexcept
{
    if (!lookup_indexes_.initialized
        || lookup_indexes_.owner != this
        || lookup_indexes_.systemverilog_revision
            != systemverilog_hir.revision()
        || lookup_indexes_.vhdl_revision != vhdl_hir.revision()) {
        return false;
    }
    return true;
}

void CompiledDesign::refresh_lookup_indexes()
{
    LookupIndexes rebuilt;
    const auto build = [](RecordPositions& positions,
                           const std::size_t identity_count,
                           const auto& systemverilog_records,
                           const auto& vhdl_records) {
        positions.records.assign(identity_count, RecordPositions::Record { });
        const auto index_language = [identity_count](auto& index,
                                        const auto& records,
                                        const std::uint8_t language) {
            for (std::size_t position = 0; position < records.size();
                 ++position) {
                const auto id = records[position].id;
                if (!id.valid() || id.value() >= identity_count) {
                    continue;
                }
                auto& indexed = index[id.value()];
                if (indexed.language == missing_record) {
                    indexed.address = &records[position];
                    indexed.language = language;
                } else {
                    indexed.address = nullptr;
                    indexed.language = duplicate_record;
                }
            }
        };
        index_language(positions.records,
            systemverilog_records, systemverilog_record_language);
        index_language(
            positions.records, vhdl_records, vhdl_record_language);
    };

    build(rebuilt.units, semantics.units().size(),
        systemverilog_hir.units(), vhdl_hir.units());
    build(rebuilt.declarations, semantics.declarations().size(),
        systemverilog_hir.declarations(), vhdl_hir.declarations());
    build(rebuilt.types, semantics.types().size(),
        systemverilog_hir.types(), vhdl_hir.types());
    build(rebuilt.expressions, semantics.expression_identities().size(),
        systemverilog_hir.expressions(), vhdl_hir.expressions());
    build(rebuilt.statements, semantics.statement_identities().size(),
        systemverilog_hir.statements(), vhdl_hir.statements());
    build(rebuilt.processes, semantics.process_identities().size(),
        systemverilog_hir.processes(), vhdl_hir.processes());
    build(rebuilt.instances, semantics.instances().size(),
        systemverilog_hir.instances(), vhdl_hir.instances());
    rebuilt.systemverilog_declarations_by_scope.resize(
        semantics.scopes().size());
    for (const auto& declaration : systemverilog_hir.declarations()) {
        if (!declaration.scope.valid()
            || declaration.scope.value()
                >= rebuilt.systemverilog_declarations_by_scope.size()) {
            continue;
        }
        rebuilt.systemverilog_declarations_by_scope[
            declaration.scope.value()].push_back(declaration.id);
    }
    rebuilt.vhdl_declarations_by_scope.resize(semantics.scopes().size());
    for (const auto& declaration : vhdl_hir.declarations()) {
        if (declaration.form == vhdl::DeclarationForm::type
            || declaration.form == vhdl::DeclarationForm::subtype
            || declaration.form
                == vhdl::DeclarationForm::generic_type) {
            rebuilt.vhdl_type_declarations_by_name[declaration.name]
                .push_back(declaration.id);
        }
        if (!declaration.scope.valid()
            || declaration.scope.value()
                >= rebuilt.vhdl_declarations_by_scope.size()) {
            continue;
        }
        rebuilt.vhdl_declarations_by_scope[declaration.scope.value()]
            .push_back(declaration.id);
    }
    rebuilt.vhdl_imports_by_unit.resize(semantics.units().size());
    std::vector<std::vector<const CompiledReference*>>
        package_references_by_owner(rebuilt.vhdl_imports_by_unit.size());
    for (const auto& reference : references_) {
        if (reference.kind != CompiledReferenceKind::package
            || !reference.owner.valid()
            || reference.owner.value() >= package_references_by_owner.size()) {
            continue;
        }
        package_references_by_owner[reference.owner.value()].push_back(
            &reference);
    }
    for (const auto& unit : vhdl_hir.units()) {
        if (!unit.id.valid()
            || unit.id.value() >= rebuilt.vhdl_imports_by_unit.size()) {
            continue;
        }
        auto& imports = rebuilt.vhdl_imports_by_unit[unit.id.value()];
        for (const auto* reference :
             package_references_by_owner[unit.id.value()]) {
            for (const auto& item : unit.context) {
                if (item.kind != vhdl::ContextKind::use_clause) {
                    continue;
                }
                for (const auto& selected : item.selected_names) {
                    const auto parts = vhdl_name_parts(
                        vhdl_spelling(selected));
                    if (parts.size() != 2U && parts.size() != 3U) {
                        continue;
                    }
                    const bool qualified = parts.size() == 3U;
                    const auto specified_library = qualified
                        ? std::string_view { parts.front() }
                        : normalized_library(unit.library);
                    const auto library = same_vhdl_identifier(
                            specified_library, "work")
                        ? normalized_library(unit.library)
                        : specified_library;
                    const auto package = qualified
                        ? std::string_view { parts[1] }
                        : std::string_view { parts.front() };
                    if (!same_vhdl_identifier(
                            library, reference->library)
                        || !same_vhdl_identifier(
                            package, reference->name)) {
                        continue;
                    }
                    CompiledVhdlImport imported {
                        canonical_vhdl_identifier(reference->library),
                        canonical_vhdl_identifier(reference->name),
                        canonical_vhdl_identifier(parts.back()),
                    };
                    const auto duplicate = std::ranges::find_if(
                        imports,
                        [&](const CompiledVhdlImport& item) {
                            return item.library == imported.library
                                && item.package == imported.package
                                && item.member == imported.member;
                        });
                    if (duplicate == imports.end()) {
                        imports.push_back(std::move(imported));
                    }
                }
            }
        }
    }
    const auto indexed_vhdl_declaration = [&](const DeclarationId id)
        -> const vhdl::Declaration* {
        if (!id.valid()
            || id.value() >= rebuilt.declarations.records.size()) {
            return nullptr;
        }
        const auto& record = rebuilt.declarations.records[id.value()];
        return record.language == vhdl_record_language
            ? static_cast<const vhdl::Declaration*>(record.address)
            : nullptr;
    };
    const auto indexed_vhdl_type = [&](const TypeId id)
        -> const vhdl::TypeDefinition* {
        if (!id.valid() || id.value() >= rebuilt.types.records.size()) {
            return nullptr;
        }
        const auto& record = rebuilt.types.records[id.value()];
        return record.language == vhdl_record_language
            ? static_cast<const vhdl::TypeDefinition*>(record.address)
            : nullptr;
    };
    const auto index_vhdl_package_member = [&](const vhdl::Unit& package,
                                               const std::string_view name,
                                               const DeclarationId member) {
        rebuilt.vhdl_package_members[name].push_back(
            { normalized_library(package.library), package.name,
                package.id, member });
    };
    for (const auto& package : vhdl_hir.units()) {
        if (package.kind != vhdl::UnitKind::package
            || !package.primary_name.empty()) {
            continue;
        }
        for (const auto declaration_id : package.declarations) {
            const auto* declaration = indexed_vhdl_declaration(
                declaration_id);
            if (declaration == nullptr) {
                continue;
            }
            index_vhdl_package_member(
                package, declaration->name, declaration_id);
            if (!declaration->declared_type) {
                continue;
            }
            const auto* type = indexed_vhdl_type(
                *declaration->declared_type);
            if (type == nullptr) {
                continue;
            }
            for (const auto& literal : type->enumeration_literals) {
                index_vhdl_package_member(
                    package, literal.spelling, literal.declaration);
            }
            for (const auto& unit : type->physical_units) {
                index_vhdl_package_member(
                    package, unit.name, unit.declaration);
            }
        }
    }
    rebuilt.systemverilog_revision = systemverilog_hir.revision();
    rebuilt.vhdl_revision = vhdl_hir.revision();
    rebuilt.owner = this;
    rebuilt.initialized = true;
    lookup_indexes_ = std::move(rebuilt);
}

bool CompiledDesign::apply_linked_vhdl_expression_annotations(
    std::vector<CompiledVhdlExpressionAnnotation>& annotations)
{
    const auto& current_expressions = vhdl_hir.expressions();
    if (!lookup_indexes_current()
        || annotations.size() != current_expressions.size()) {
        return false;
    }
    for (std::size_t position = 0; position < annotations.size();
        ++position) {
        if (annotations[position].expression
                != current_expressions[position].id
            || (annotations[position].name_resolution
                && !current_expressions[position].referenced_name)) {
            return false;
        }
    }

    // This scoped write changes no expression identity, vector position, or
    // other index-driving collection. The mutable accessor advances the HIR
    // revision, so retain the already-current index addresses by updating
    // only their VHDL revision stamp after the annotation writes complete.
    auto& expressions = vhdl_hir.mutable_expressions();
    for (std::size_t position = 0; position < annotations.size();
        ++position) {
        auto& expression = expressions[position];
        auto& annotation = annotations[position];
        expression.builtin_operator = annotation.builtin_operator;
        if (!annotation.name_resolution) {
            continue;
        }
        auto& referenced_name = *expression.referenced_name;
        referenced_name.selected = annotation.name_resolution->selected;
        referenced_name.overloads.swap(
            annotation.name_resolution->overloads);
    }
    lookup_indexes_.vhdl_revision = vhdl_hir.revision();
    return true;
}

std::span<const CompiledDependency>
CompiledDesign::dependencies() const noexcept
{
    return dependencies_;
}

std::span<const CompiledReference> CompiledDesign::references() const noexcept
{
    return references_;
}

std::span<const Unit> CompiledDesign::units() const noexcept
{
    return semantics.units();
}

std::span<const sv::Unit>
CompiledDesign::systemverilog_units() const noexcept
{
    return systemverilog_hir.units();
}

std::span<const vhdl::Unit> CompiledDesign::vhdl_units() const noexcept
{
    return vhdl_hir.units();
}

std::optional<CompiledUnitView>
CompiledDesign::find_unit(const UnitId id) const noexcept
{
    if (!id.valid() || id.value() >= semantics.units().size()) {
        return std::nullopt;
    }
    const auto& identity = semantics.units()[id.value()];
    if (identity.id != id) {
        return std::nullopt;
    }
    const auto record = find_language_record(
        std::span { semantics.units() },
        std::span { lookup_indexes_.units.records },
        lookup_indexes_current(), id,
        [&] { return std::span { systemverilog_hir.units() }; },
        [&] { return std::span { vhdl_hir.units() }; });
    if (!record) {
        return std::nullopt;
    }
    const auto* systemverilog = record->systemverilog;
    const auto* vhdl = record->vhdl;
    if (systemverilog != nullptr) {
        if ((identity.language != Language::verilog
                && identity.language != Language::system_verilog)
            || !same_identity(identity, *systemverilog)) {
            return std::nullopt;
        }
    } else if (identity.language != Language::vhdl
        || !same_identity(identity, *vhdl)) {
        return std::nullopt;
    }
    return CompiledUnitView {
        identity.language, &identity, systemverilog, vhdl };
}

std::optional<CompiledUnitView> CompiledDesign::find_unit(
    const UnitKind kind, const std::string_view library,
    const std::string_view name,
    const std::string_view secondary_name) const noexcept
{
    std::optional<CompiledUnitView> result;
    bool result_is_external = false;
    for (const auto& unit : semantics.units()) {
        if (!lookup_identity_matches(
                unit, kind, library, name, secondary_name)) {
            continue;
        }
        const auto candidate = find_unit(unit.id);
        if (!candidate) {
            return std::nullopt;
        }
        const auto candidate_is_external = candidate->systemverilog != nullptr
            && candidate->systemverilog->external;
        if (!result) {
            result = candidate;
            result_is_external = candidate_is_external;
            continue;
        }
        if (result_is_external && !candidate_is_external) {
            result = candidate;
            result_is_external = false;
            continue;
        }
        if (candidate_is_external) {
            continue;
        }
        return std::nullopt;
    }
    return result;
}

std::vector<CompiledUnitView> CompiledDesign::find_units(
    const UnitKind kind, const std::string_view library,
    const std::string_view name,
    const std::string_view secondary_name) const noexcept
{
    std::vector<CompiledUnitView> result;
    for (const auto& unit : semantics.units()) {
        if (!lookup_identity_matches(
                unit, kind, library, name, secondary_name)) {
            continue;
        }
        const auto candidate = find_unit(unit.id);
        if (!candidate) {
            return {};
        }
        result.push_back(*candidate);
    }
    return result;
}

std::optional<CompiledUnitView> CompiledDesign::find_unit(
    const Language language, const std::string_view library,
    const std::string_view name,
    const std::string_view secondary_name) const
{
    std::optional<CompiledUnitView> result;
    bool result_is_external = false;
    for (const auto& unit : semantics.units()) {
        if (!language_matches(language, unit.language)
            || normalized_library(unit.library)
                != normalized_library(library)
            || !lookup_name_matches(unit, name, secondary_name)) {
            continue;
        }
        const auto candidate = find_unit(unit.id);
        if (!candidate) {
            return std::nullopt;
        }
        const auto candidate_is_external = candidate->systemverilog != nullptr
            && candidate->systemverilog->external;
        if (!result) {
            result = candidate;
            result_is_external = candidate_is_external;
            continue;
        }
        if (result_is_external && !candidate_is_external) {
            result = candidate;
            result_is_external = false;
            continue;
        }
        if (candidate_is_external) {
            continue;
        }
        return std::nullopt;
    }
    return result;
}

std::optional<CompiledDeclarationView>
CompiledDesign::find_declaration(const DeclarationId id) const noexcept
{
    return find_language_record(
        std::span { semantics.declarations() },
        std::span { lookup_indexes_.declarations.records },
        lookup_indexes_current(), id,
        [&] { return std::span { systemverilog_hir.declarations() }; },
        [&] { return std::span { vhdl_hir.declarations() }; });
}

std::optional<CompiledTypeView>
CompiledDesign::find_type(const TypeId id) const noexcept
{
    return find_language_record(
        std::span { semantics.types() },
        std::span { lookup_indexes_.types.records },
        lookup_indexes_current(), id,
        [&] { return std::span { systemverilog_hir.types() }; },
        [&] { return std::span { vhdl_hir.types() }; });
}

std::optional<CompiledExpressionView>
CompiledDesign::find_expression(const ExpressionId id) const noexcept
{
    return find_language_record(
        std::span { semantics.expression_identities() },
        std::span { lookup_indexes_.expressions.records },
        lookup_indexes_current(), id,
        [&] { return std::span { systemverilog_hir.expressions() }; },
        [&] { return std::span { vhdl_hir.expressions() }; });
}

std::optional<CompiledStatementView>
CompiledDesign::find_statement(const StatementId id) const noexcept
{
    return find_language_record(
        std::span { semantics.statement_identities() },
        std::span { lookup_indexes_.statements.records },
        lookup_indexes_current(), id,
        [&] { return std::span { systemverilog_hir.statements() }; },
        [&] { return std::span { vhdl_hir.statements() }; });
}

std::optional<CompiledProcessView>
CompiledDesign::find_process(const ProcessId id) const noexcept
{
    return find_language_record(
        std::span { semantics.process_identities() },
        std::span { lookup_indexes_.processes.records },
        lookup_indexes_current(), id,
        [&] { return std::span { systemverilog_hir.processes() }; },
        [&] { return std::span { vhdl_hir.processes() }; });
}

std::optional<CompiledInstanceView>
CompiledDesign::find_instance(const InstanceId id) const noexcept
{
    return find_language_record(
        std::span { semantics.instances() },
        std::span { lookup_indexes_.instances.records },
        lookup_indexes_current(), id,
        [&] { return std::span { systemverilog_hir.instances() }; },
        [&] { return std::span { vhdl_hir.instances() }; });
}

std::optional<std::span<const DeclarationId>>
CompiledDesign::systemverilog_declarations_in_scope(
    const ScopeId scope) const noexcept
{
    if (!lookup_indexes_current() || !scope.valid()
        || scope.value()
            >= lookup_indexes_.systemverilog_declarations_by_scope.size()) {
        return std::nullopt;
    }
    return std::span<const DeclarationId> {
        lookup_indexes_.systemverilog_declarations_by_scope[scope.value()] };
}

std::optional<std::span<const DeclarationId>>
CompiledDesign::vhdl_declarations_in_scope(
    const ScopeId scope) const noexcept
{
    if (!lookup_indexes_current() || !scope.valid()
        || scope.value()
            >= lookup_indexes_.vhdl_declarations_by_scope.size()) {
        return std::nullopt;
    }
    return std::span<const DeclarationId> {
        lookup_indexes_.vhdl_declarations_by_scope[scope.value()] };
}

std::optional<std::span<const DeclarationId>>
CompiledDesign::vhdl_type_declarations_named(
    const std::string_view name) const noexcept
{
    if (!lookup_indexes_current()) {
        return std::nullopt;
    }
    const auto found
        = lookup_indexes_.vhdl_type_declarations_by_name.find(name);
    if (found == lookup_indexes_.vhdl_type_declarations_by_name.end()) {
        return std::span<const DeclarationId> { };
    }
    return std::span<const DeclarationId> { found->second };
}

std::optional<std::span<const CompiledVhdlImport>>
CompiledDesign::vhdl_linked_imports(const UnitId owner) const noexcept
{
    if (!lookup_indexes_current() || !owner.valid()
        || owner.value() >= lookup_indexes_.vhdl_imports_by_unit.size()) {
        return std::nullopt;
    }
    return std::span<const CompiledVhdlImport> {
        lookup_indexes_.vhdl_imports_by_unit[owner.value()] };
}

std::optional<std::span<const CompiledVhdlPackageMemberIndexEntry>>
CompiledDesign::vhdl_package_members(
    const std::string_view member) const noexcept
{
    if (!lookup_indexes_current()) {
        return std::nullopt;
    }
    const auto found = lookup_indexes_.vhdl_package_members.find(member);
    if (found == lookup_indexes_.vhdl_package_members.end()) {
        return std::span<const CompiledVhdlPackageMemberIndexEntry> { };
    }
    return std::span<const CompiledVhdlPackageMemberIndexEntry> {
        found->second };
}

bool CompiledDesign::valid() const
{
    if (!semantics.valid()
        || !sv::top_level_hir_collections_well_formed(semantics,
            { systemverilog_hir.instances(), systemverilog_hir.udps(),
                systemverilog_hir.dpi_declarations(),
                systemverilog_hir.covergroup_instances() })
        || !vhdl::top_level_hir_collections_well_formed(semantics,
            { vhdl_hir.declarations(), vhdl_hir.overload_sets(),
                vhdl_hir.instances() })) {
        return false;
    }
    std::set<UnitId> language_units;
    for (const auto& unit : systemverilog_hir.units()) {
        if (!language_units.insert(unit.id).second
            || !find_unit(unit.id)) {
            return false;
        }
    }
    for (const auto& unit : vhdl_hir.units()) {
        if (!language_units.insert(unit.id).second
            || !find_unit(unit.id)) {
            return false;
        }
    }
    for (const auto& unit : semantics.units()) {
        if (unit.language != Language::systemc
            && !language_units.contains(unit.id)) {
            return false;
        }
    }
    if (!systemverilog_instance_ownership_error(*this).empty()
        || !vhdl_instance_ownership_error(*this).empty()) {
        return false;
    }
    for (const auto& dependency : dependencies_) {
        if (!dependency.owner.valid()
            || dependency.owner.value() >= semantics.units().size()
            || dependency.logical_name.empty()) {
            return false;
        }
    }
    for (const auto& reference : references_) {
        if (!reference.owner.valid()
            || reference.owner.value() >= semantics.units().size()
            || reference.name.empty()
            || (reference.target
                && reference.target->value() >= semantics.units().size())) {
            return false;
        }
    }
    return true;
}

ValidatedCompiledDesign::ValidatedCompiledDesign(
    const CompiledDesign& design) noexcept
    : design_ { &design }
{
}

const CompiledDesign& ValidatedCompiledDesign::design() const noexcept
{
    return *design_;
}

bool ValidatedCompiledDesign::can_validate(
    const CompiledDesign& design) noexcept
{
    return design.lookup_indexes_current();
}

std::optional<ValidatedCompiledDesign>
validate_compiled_design(const CompiledDesign& design)
{
    return ValidatedCompiledDesign::can_validate(design) && design.valid()
        ? std::optional { ValidatedCompiledDesign { design } }
        : std::nullopt;
}

Model& CompiledDesign::mutable_semantics() noexcept
{
    return semantics;
}

sv::Hir& CompiledDesign::mutable_systemverilog() noexcept
{
    return systemverilog_hir;
}

vhdl::Hir& CompiledDesign::mutable_vhdl() noexcept
{
    return vhdl_hir;
}

std::vector<CompiledDependency>&
CompiledDesign::mutable_dependencies() noexcept
{
    return dependencies_;
}

std::vector<CompiledReference>& CompiledDesign::mutable_references() noexcept
{
    return references_;
}

} // namespace fsim::semantic
