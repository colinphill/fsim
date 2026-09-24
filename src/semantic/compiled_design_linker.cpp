// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_linker.hpp"

#include "fsim/semantic/compiled_design_normalization.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/support/ctype_whitespace.hpp"

#include "compiled_design_linker_classes.hpp"

#include <boost/pfr/core.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::semantic {
namespace {

std::string_view normalized_library(const std::string_view library)
{
    return library.empty() ? std::string_view { "work" } : library;
}

struct Relocation {
    std::uint32_t source_file { };
    std::uint32_t expansion { };
    std::uint32_t source_span { };
    std::uint32_t origin { };
    std::uint32_t unit { };
    std::uint32_t scope { };
    std::uint32_t declaration { };
    std::uint32_t type { };
    std::uint32_t value { };
    std::uint32_t expression { };
    std::uint32_t statement { };
    std::uint32_t instance { };
    std::uint32_t process { };
};

inline constexpr auto kRemovedId
    = std::numeric_limits<std::uint32_t>::max();

struct Projection {
    std::vector<std::uint32_t> source_file;
    std::vector<std::uint32_t> expansion;
    std::vector<std::uint32_t> source_span;
    std::vector<std::uint32_t> origin;
    std::vector<std::uint32_t> unit;
    std::vector<std::uint32_t> scope;
    std::vector<std::uint32_t> declaration;
    std::vector<std::uint32_t> type;
    std::vector<std::uint32_t> value;
    std::vector<std::uint32_t> expression;
    std::vector<std::uint32_t> statement;
    std::vector<std::uint32_t> instance;
    std::vector<std::uint32_t> process;
};

template <typename T>
struct IsVector : std::false_type { };

template <typename T, typename Allocator>
struct IsVector<std::vector<T, Allocator>> : std::true_type { };

template <typename T>
struct IsOptional : std::false_type { };

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type { };

template <typename T>
struct IsPair : std::false_type { };

template <typename First, typename Second>
struct IsPair<std::pair<First, Second>> : std::true_type { };

template <typename T>
struct IsId : std::false_type { };

template <typename Tag>
struct IsId<Id<Tag>> : std::true_type {
    using tag_type = Tag;
};

template <typename Tag>
const std::vector<std::uint32_t>& mapping_for(
    const Projection& projection)
{
    if constexpr (std::same_as<Tag, SourceFileTag>) {
        return projection.source_file;
    } else if constexpr (std::same_as<Tag, ExpansionTag>) {
        return projection.expansion;
    } else if constexpr (std::same_as<Tag, SourceSpanTag>) {
        return projection.source_span;
    } else if constexpr (std::same_as<Tag, OriginTag>) {
        return projection.origin;
    } else if constexpr (std::same_as<Tag, UnitTag>) {
        return projection.unit;
    } else if constexpr (std::same_as<Tag, ScopeTag>) {
        return projection.scope;
    } else if constexpr (std::same_as<Tag, DeclarationTag>) {
        return projection.declaration;
    } else if constexpr (std::same_as<Tag, TypeTag>) {
        return projection.type;
    } else if constexpr (std::same_as<Tag, ValueTag>) {
        return projection.value;
    } else if constexpr (std::same_as<Tag, ExpressionTag>) {
        return projection.expression;
    } else if constexpr (std::same_as<Tag, StatementTag>) {
        return projection.statement;
    } else if constexpr (std::same_as<Tag, InstanceTag>) {
        return projection.instance;
    } else {
        static_assert(std::same_as<Tag, ProcessTag>);
        return projection.process;
    }
}

template <typename Tag>
bool retained(const Id<Tag> id, const Projection& projection)
{
    if (!id.valid()) {
        return false;
    }
    const auto& mapping = mapping_for<Tag>(projection);
    return id.value() < mapping.size()
        && mapping[id.value()] != kRemovedId;
}

template <typename Value>
void project(Value& value, const Projection& projection);

template <typename Tag>
void project(Id<Tag>& id, const Projection& projection)
{
    if (!retained(id, projection)) {
        id = { };
        return;
    }
    id = Id<Tag>::from_index(mapping_for<Tag>(projection)[id.value()]);
}

template <typename Value>
void project(Value& value, const Projection& projection)
{
    using Type = std::remove_cvref_t<Value>;
    if constexpr (std::is_arithmetic_v<Type> || std::is_enum_v<Type>
        || std::same_as<Type, std::string>
        || std::same_as<Type, std::filesystem::path>) {
        return;
    } else if constexpr (IsVector<Type>::value) {
        for (auto& item : value) {
            project(item, projection);
        }
        if constexpr (IsId<typename Type::value_type>::value) {
            std::erase_if(value,
                [](const auto id) { return !id.valid(); });
        }
    } else if constexpr (IsOptional<Type>::value) {
        if (value) {
            project(*value, projection);
            if constexpr (IsId<typename Type::value_type>::value) {
                if (!value->valid()) {
                    value.reset();
                }
            }
        }
    } else if constexpr (IsPair<Type>::value) {
        project(value.first, projection);
        project(value.second, projection);
    } else if constexpr (std::is_aggregate_v<Type>) {
        boost::pfr::for_each_field(
            value, [&](auto& field) { project(field, projection); });
    }
}

template <typename Tag>
std::uint32_t offset_for(const Relocation& relocation)
{
    if constexpr (std::same_as<Tag, SourceFileTag>) {
        return relocation.source_file;
    } else if constexpr (std::same_as<Tag, ExpansionTag>) {
        return relocation.expansion;
    } else if constexpr (std::same_as<Tag, SourceSpanTag>) {
        return relocation.source_span;
    } else if constexpr (std::same_as<Tag, OriginTag>) {
        return relocation.origin;
    } else if constexpr (std::same_as<Tag, UnitTag>) {
        return relocation.unit;
    } else if constexpr (std::same_as<Tag, ScopeTag>) {
        return relocation.scope;
    } else if constexpr (std::same_as<Tag, DeclarationTag>) {
        return relocation.declaration;
    } else if constexpr (std::same_as<Tag, TypeTag>) {
        return relocation.type;
    } else if constexpr (std::same_as<Tag, ValueTag>) {
        return relocation.value;
    } else if constexpr (std::same_as<Tag, ExpressionTag>) {
        return relocation.expression;
    } else if constexpr (std::same_as<Tag, StatementTag>) {
        return relocation.statement;
    } else if constexpr (std::same_as<Tag, InstanceTag>) {
        return relocation.instance;
    } else if constexpr (std::same_as<Tag, ProcessTag>) {
        return relocation.process;
    } else {
        return 0;
    }
}

template <typename Value>
void relocate(Value& value, const Relocation& relocation);

template <typename Tag>
void relocate(Id<Tag>& id, const Relocation& relocation)
{
    if (!id.valid()) {
        return;
    }
    id = Id<Tag>::from_index(id.value() + offset_for<Tag>(relocation));
}

template <typename Value>
void relocate(Value& value, const Relocation& relocation)
{
    using Type = std::remove_cvref_t<Value>;
    if constexpr (std::is_arithmetic_v<Type> || std::is_enum_v<Type>
        || std::same_as<Type, std::string>
        || std::same_as<Type, std::filesystem::path>) {
        return;
    } else if constexpr (IsVector<Type>::value) {
        for (auto& item : value) {
            relocate(item, relocation);
        }
    } else if constexpr (IsOptional<Type>::value) {
        if (value) {
            relocate(*value, relocation);
        }
    } else if constexpr (IsPair<Type>::value) {
        relocate(value.first, relocation);
        relocate(value.second, relocation);
    } else if constexpr (std::is_aggregate_v<Type>) {
        boost::pfr::for_each_field(
            value, [&](auto& field) { relocate(field, relocation); });
    }
}

struct IdLimits {
    std::size_t source_file { };
    std::size_t expansion { };
    std::size_t source_span { };
    std::size_t origin { };
    std::size_t unit { };
    std::size_t scope { };
    std::size_t declaration { };
    std::size_t type { };
    std::size_t value { };
    std::size_t expression { };
    std::size_t statement { };
    std::size_t instance { };
    std::size_t process { };
};

IdLimits id_limits(const Model& model)
{
    return {
        model.source_files().size(),
        model.expansions().size(),
        model.source_spans().size(),
        model.origins().size(),
        model.units().size(),
        model.scopes().size(),
        model.declarations().size(),
        model.types().size(),
        model.values().size(),
        model.expression_identities().size(),
        model.statement_identities().size(),
        model.instances().size(),
        model.process_identities().size(),
    };
}

template <typename Tag>
std::size_t limit_for(const IdLimits& limits)
{
    if constexpr (std::same_as<Tag, SourceFileTag>) {
        return limits.source_file;
    } else if constexpr (std::same_as<Tag, ExpansionTag>) {
        return limits.expansion;
    } else if constexpr (std::same_as<Tag, SourceSpanTag>) {
        return limits.source_span;
    } else if constexpr (std::same_as<Tag, OriginTag>) {
        return limits.origin;
    } else if constexpr (std::same_as<Tag, UnitTag>) {
        return limits.unit;
    } else if constexpr (std::same_as<Tag, ScopeTag>) {
        return limits.scope;
    } else if constexpr (std::same_as<Tag, DeclarationTag>) {
        return limits.declaration;
    } else if constexpr (std::same_as<Tag, TypeTag>) {
        return limits.type;
    } else if constexpr (std::same_as<Tag, ValueTag>) {
        return limits.value;
    } else if constexpr (std::same_as<Tag, ExpressionTag>) {
        return limits.expression;
    } else if constexpr (std::same_as<Tag, StatementTag>) {
        return limits.statement;
    } else if constexpr (std::same_as<Tag, InstanceTag>) {
        return limits.instance;
    } else if constexpr (std::same_as<Tag, ProcessTag>) {
        return limits.process;
    } else {
        return std::numeric_limits<std::size_t>::max();
    }
}

template <typename Value>
bool ids_in_range(const Value& value, const IdLimits& limits);

template <typename Tag>
bool ids_in_range(const Id<Tag> id, const IdLimits& limits)
{
    return !id.valid() || id.value() < limit_for<Tag>(limits);
}

template <typename Value>
bool ids_in_range(const Value& value, const IdLimits& limits)
{
    using Type = std::remove_cvref_t<Value>;
    if constexpr (requires { value.generated_text; }) {
        if (!sv::generated_text_kind_valid(value.generated_text)) {
            return false;
        }
    }
    if constexpr (requires { value.output_generated_text; }) {
        if (!sv::generated_text_kind_valid(value.output_generated_text)) {
            return false;
        }
    }
    if constexpr (std::same_as<Type, sv::SourceToken>) {
        return value.source.valid()
            && value.source.value() < limits.source_span;
    } else if constexpr (std::same_as<Type, sv::GeneratedTextKind>) {
        return sv::generated_text_kind_valid(value);
    } else if constexpr (std::is_arithmetic_v<Type> || std::is_enum_v<Type>
        || std::same_as<Type, std::string>
        || std::same_as<Type, std::filesystem::path>) {
        return true;
    } else if constexpr (IsVector<Type>::value) {
        return std::ranges::all_of(value,
            [&](const auto& item) { return ids_in_range(item, limits); });
    } else if constexpr (IsOptional<Type>::value) {
        return !value || ids_in_range(*value, limits);
    } else if constexpr (IsPair<Type>::value) {
        return ids_in_range(value.first, limits)
            && ids_in_range(value.second, limits);
    } else if constexpr (std::is_aggregate_v<Type>) {
        bool valid = true;
        boost::pfr::for_each_field(value, [&](const auto& field) {
            valid = valid && ids_in_range(field, limits);
        });
        return valid;
    } else {
        return true;
    }
}

template <typename Record, typename Identity, typename Predicate>
bool valid_identity_records(const std::vector<Record>& records,
    const std::vector<Identity>& identities,
    std::set<std::uint32_t>& claimed, Predicate predicate,
    const std::string_view record_kind, std::string& error)
{
    for (const auto& record : records) {
        if (!record.id.valid()) {
            error = std::string { record_kind } + " has an invalid ID";
            return false;
        }
        if (record.id.value() >= identities.size()) {
            error = std::string { record_kind } + " ID "
                + std::to_string(record.id.value())
                + " has no semantic identity";
            return false;
        }
        if (!claimed.insert(record.id.value()).second) {
            error = std::string { record_kind } + " ID "
                + std::to_string(record.id.value())
                + " is claimed by multiple HIR records";
            return false;
        }
        if (!predicate(record, identities[record.id.value()])) {
            const auto& identity = identities[record.id.value()];
            error = std::string { record_kind } + " ID "
                + std::to_string(record.id.value())
                + " does not match its semantic identity (source "
                + std::to_string(record.source.value()) + " versus "
                + std::to_string(identity.source.value()) + ", origin "
                + std::to_string(record.origin.value()) + " versus "
                + std::to_string(identity.origin.value());
            if constexpr (requires { record.scope; identity.scope; }) {
                error += ", scope " + std::to_string(record.scope.value())
                    + " versus " + std::to_string(identity.scope.value());
            }
            error += ')';
            return false;
        }
    }
    return true;
}

template <typename Identity>
bool complete_identity_records(const std::vector<Identity>& identities,
    const std::set<std::uint32_t>& claimed, const Model& semantics,
    const std::string_view identity_kind, std::string& error)
{
    for (const auto& identity : identities) {
        if (!identity.scope.valid()
            || identity.scope.value() >= semantics.scopes().size()) {
            error = std::string { identity_kind }
                + " has an invalid semantic scope";
            return false;
        }
        const auto unit = semantics.scopes()[identity.scope.value()].unit;
        if (!unit.valid() || unit.value() >= semantics.units().size()) {
            error = std::string { identity_kind }
                + " has an invalid semantic unit owner";
            return false;
        }
        if (semantics.units()[unit.value()].language == Language::systemc) {
            continue;
        }
        if (!claimed.contains(identity.id.value())) {
            error = std::string { identity_kind } + " ID "
                + std::to_string(identity.id.value())
                + " has no SystemVerilog or VHDL HIR record";
            return false;
        }
    }
    return true;
}

template <typename Value>
bool validate_ids_in_range(const Value& value, const IdLimits& limits,
    const std::string_view record_kind, std::string& error)
{
    if (ids_in_range(value, limits)) {
        return true;
    }
    error = std::string { record_kind }
        + " contains an out-of-range semantic ID";
    return false;
}

bool equivalent_origins(const Model& model, OriginId left, OriginId right)
{
    while (left != right) {
        if (!left.valid() || !right.valid()
            || left.value() >= model.origins().size()
            || right.value() >= model.origins().size()) {
            return false;
        }
        const auto& left_origin = model.origins()[left.value()];
        const auto& right_origin = model.origins()[right.value()];
        if (left_origin.kind != right_origin.kind
            || left_origin.source != right_origin.source
            || left_origin.detail != right_origin.detail
            || left_origin.parent.has_value()
                != right_origin.parent.has_value()) {
            return false;
        }
        if (!left_origin.parent) {
            return true;
        }
        left = *left_origin.parent;
        right = *right_origin.parent;
    }
    return true;
}

std::string structural_hir_error(const CompiledDesign& design)
{
    if (const auto error = vhdl_instance_ownership_error(design);
        !error.empty()) {
        return error;
    }
    if (!design.valid()) {
        return "semantic model or language HIR is invalid";
    }
    const auto limits = id_limits(design.semantics);
    std::string error;
    if (!validate_ids_in_range(design.systemverilog_hir.units(), limits,
            "SystemVerilog unit HIR", error)
        || !validate_ids_in_range(
            design.systemverilog_hir.declarations(), limits,
            "SystemVerilog declaration HIR", error)
        || !validate_ids_in_range(design.systemverilog_hir.types(), limits,
            "SystemVerilog type HIR", error)
        || !validate_ids_in_range(
            design.systemverilog_hir.expressions(), limits,
            "SystemVerilog expression HIR", error)
        || !validate_ids_in_range(
            design.systemverilog_hir.statements(), limits,
            "SystemVerilog statement HIR", error)
        || !validate_ids_in_range(
            design.systemverilog_hir.processes(), limits,
            "SystemVerilog process HIR", error)
        || !validate_ids_in_range(design.systemverilog_hir.classes(), limits,
            "SystemVerilog class HIR", error)
        || !validate_ids_in_range(
            design.systemverilog_hir.instances(), limits,
            "SystemVerilog instance HIR", error)
        || !validate_ids_in_range(design.systemverilog_hir.udps(), limits,
            "SystemVerilog UDP HIR", error)
        || !validate_ids_in_range(
            design.systemverilog_hir.dpi_declarations(), limits,
            "SystemVerilog DPI HIR", error)
        || !validate_ids_in_range(
            design.systemverilog_hir.covergroup_instances(), limits,
            "SystemVerilog covergroup-instance HIR", error)
        || !validate_ids_in_range(design.vhdl_hir.units(), limits,
            "VHDL unit HIR", error)
        || !validate_ids_in_range(design.vhdl_hir.declarations(), limits,
            "VHDL declaration HIR", error)
        || !validate_ids_in_range(design.vhdl_hir.types(), limits,
            "VHDL type HIR", error)
        || !validate_ids_in_range(design.vhdl_hir.overload_sets(), limits,
            "VHDL overload-set HIR", error)
        || !validate_ids_in_range(design.vhdl_hir.expressions(), limits,
            "VHDL expression HIR", error)
        || !validate_ids_in_range(design.vhdl_hir.statements(), limits,
            "VHDL statement HIR", error)
        || !validate_ids_in_range(design.vhdl_hir.processes(), limits,
            "VHDL process HIR", error)
        || !validate_ids_in_range(design.vhdl_hir.instances(), limits,
            "VHDL instance HIR", error)) {
        return error;
    }
    std::set<std::uint32_t> declarations;
    std::set<std::uint32_t> types;
    std::set<std::uint32_t> expressions;
    std::set<std::uint32_t> statements;
    std::set<std::uint32_t> processes;
    std::set<std::uint32_t> instances;
    const auto declaration_matches = [&](const auto& record,
                                         const auto& identity) {
        return record.scope == identity.scope
            && record.source == identity.source
            && equivalent_origins(
                design.semantics, record.origin, identity.origin);
    };
    const auto type_matches = [&](const auto& record,
                                  const auto& identity) {
        return record.source == identity.source
            && equivalent_origins(
                design.semantics, record.origin, identity.origin);
    };
    const auto instance_matches = [&](const auto& record,
                                      const auto& identity) {
        // Semantic instances retain the declaration occurrence while the
        // language HIR may retain a generated or specialized occurrence.
        // Their structural identity is the shared ID, scope, and source.
        return record.scope == identity.scope
            && record.source == identity.source;
    };
    if (!valid_identity_records(
               design.systemverilog_hir.declarations(),
               design.semantics.declarations(), declarations,
               declaration_matches, "SystemVerilog declaration", error)
        || !valid_identity_records(design.vhdl_hir.declarations(),
            design.semantics.declarations(), declarations,
            declaration_matches, "VHDL declaration", error)
        || !valid_identity_records(design.systemverilog_hir.types(),
            design.semantics.types(), types, type_matches,
            "SystemVerilog type", error)
        || !valid_identity_records(design.vhdl_hir.types(),
            design.semantics.types(), types, type_matches,
            "VHDL type", error)
        || !valid_identity_records(
            design.systemverilog_hir.expressions(),
            design.semantics.expression_identities(), expressions,
            declaration_matches, "SystemVerilog expression", error)
        || !valid_identity_records(design.vhdl_hir.expressions(),
            design.semantics.expression_identities(), expressions,
            declaration_matches, "VHDL expression", error)
        || !valid_identity_records(design.systemverilog_hir.statements(),
            design.semantics.statement_identities(), statements,
            declaration_matches, "SystemVerilog statement", error)
        || !valid_identity_records(design.vhdl_hir.statements(),
            design.semantics.statement_identities(), statements,
            declaration_matches, "VHDL statement", error)
        || !valid_identity_records(design.systemverilog_hir.processes(),
            design.semantics.process_identities(), processes,
            declaration_matches, "SystemVerilog process", error)
        || !valid_identity_records(design.vhdl_hir.processes(),
            design.semantics.process_identities(), processes,
            declaration_matches, "VHDL process", error)
        || !valid_identity_records(design.systemverilog_hir.instances(),
            design.semantics.instances(), instances,
            instance_matches, "SystemVerilog instance", error)
        || !valid_identity_records(design.vhdl_hir.instances(),
            design.semantics.instances(), instances,
            instance_matches, "VHDL instance", error)) {
        return error;
    }
    // Semantic declarations, types, expressions, and statements also model
    // compiler-provided leaves and derived facts that intentionally have no
    // language-HIR owner. Processes and instances are structural hierarchy
    // records, so every non-SystemC semantic identity must have exactly one
    // language-HIR counterpart.
    if (!complete_identity_records(
            design.semantics.process_identities(), processes,
            design.semantics, "semantic process identity", error)
        || !complete_identity_records(design.semantics.instances(),
            instances, design.semantics,
            "semantic instance identity", error)) {
        return error;
    }
    if (auto class_error = sv::class_hir_error(design.semantics,
            design.systemverilog_hir.units(),
            design.systemverilog_hir.declarations(),
            design.systemverilog_hir.statements(),
            design.systemverilog_hir.classes());
        !class_error.empty()) {
        return class_error;
    }
    return { };
}

template <typename T>
void append(std::vector<T>& destination, std::vector<T>& source)
{
    destination.insert(
        destination.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end()));
}

template <typename Record, typename Predicate>
std::vector<Record> select_records(
    const std::vector<Record>& input,
    std::vector<std::uint32_t>& mapping,
    Predicate predicate)
{
    mapping.assign(input.size(), kRemovedId);
    std::vector<Record> output;
    for (const auto& record : input) {
        if (!predicate(record)) {
            continue;
        }
        mapping.at(record.id.value())
            = static_cast<std::uint32_t>(output.size());
        output.push_back(record);
    }
    return output;
}

struct RetainedProvenance {
    std::set<std::uint32_t> source_files;
    std::set<std::uint32_t> expansions;
    std::set<std::uint32_t> source_spans;
    std::set<std::uint32_t> origins;
};

template <typename Value>
void retain_provenance(
    const Value& value, RetainedProvenance& retained);

template <typename Tag>
void retain_provenance(
    const Id<Tag> id, RetainedProvenance& retained)
{
    if (!id.valid()) {
        return;
    }
    if constexpr (std::same_as<Tag, SourceFileTag>) {
        retained.source_files.insert(id.value());
    } else if constexpr (std::same_as<Tag, ExpansionTag>) {
        retained.expansions.insert(id.value());
    } else if constexpr (std::same_as<Tag, SourceSpanTag>) {
        retained.source_spans.insert(id.value());
    } else if constexpr (std::same_as<Tag, OriginTag>) {
        retained.origins.insert(id.value());
    }
}

template <typename Value>
void retain_provenance(
    const Value& value, RetainedProvenance& retained)
{
    using Type = std::remove_cvref_t<Value>;
    if constexpr (std::is_arithmetic_v<Type> || std::is_enum_v<Type>
        || std::same_as<Type, std::string>
        || std::same_as<Type, std::filesystem::path>) {
        return;
    } else if constexpr (IsVector<Type>::value) {
        for (const auto& item : value) {
            retain_provenance(item, retained);
        }
    } else if constexpr (IsOptional<Type>::value) {
        if (value) {
            retain_provenance(*value, retained);
        }
    } else if constexpr (IsPair<Type>::value) {
        retain_provenance(value.first, retained);
        retain_provenance(value.second, retained);
    } else if constexpr (std::is_aggregate_v<Type>) {
        boost::pfr::for_each_field(value, [&](const auto& field) {
            retain_provenance(field, retained);
        });
    }
}

template <typename Records, typename Predicate>
void retain_record_provenance(
    const Records& records, Predicate predicate,
    RetainedProvenance& retained)
{
    for (const auto& record : records) {
        if (predicate(record)) {
            retain_provenance(record, retained);
        }
    }
}

template <typename UnitPredicate, typename LibraryPredicate>
ModelRecords project_model_records(
    const CompiledDesign& design,
    UnitPredicate select_unit,
    LibraryPredicate select_auxiliary_library,
    Projection& projection)
{
    const auto input = design.semantics.records();
    ModelRecords output;
    output.units = select_records(input.units, projection.unit,
        [&](const Unit& unit) {
            return select_unit(unit);
        });
    output.scopes = select_records(input.scopes, projection.scope,
        [&](const Scope& scope) {
            return retained(scope.unit, projection);
        });
    const auto retained_scope = [&](const auto& record) {
        return retained(record.scope, projection);
    };
    output.types = select_records(
        input.types, projection.type, retained_scope);
    output.values = select_records(
        input.values, projection.value, retained_scope);
    output.instances = select_records(
        input.instances, projection.instance, retained_scope);
    output.declarations = select_records(
        input.declarations, projection.declaration, retained_scope);
    output.expression_identities = select_records(
        input.expression_identities, projection.expression,
        retained_scope);
    output.statement_identities = select_records(
        input.statement_identities, projection.statement,
        retained_scope);
    output.process_identities = select_records(
        input.process_identities, projection.process,
        retained_scope);

    RetainedProvenance retained_provenance;
    retain_provenance(output, retained_provenance);
    const auto retained_id = [&](const auto id) {
        return retained(id, projection);
    };
    retain_record_provenance(
        design.systemverilog_hir.units(),
        [&](const sv::Unit& unit) { return retained_id(unit.id); },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.declarations(),
        [&](const sv::Declaration& declaration) {
            return retained_id(declaration.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.types(),
        [&](const sv::TypeDefinition& type) {
            return retained_id(type.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.expressions(),
        [&](const sv::Expression& expression) {
            return retained_id(expression.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.statements(),
        [&](const sv::Statement& statement) {
            return retained_id(statement.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.processes(),
        [&](const sv::Process& process) {
            return retained_id(process.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.classes(),
        [&](const sv::ClassDeclaration& declaration) {
            return retained_id(declaration.scope);
        },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.instances(),
        [&](const sv::Instance& instance) {
            return retained_id(instance.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.udps(),
        [&](const sv::UdpDeclaration& declaration) {
            return select_auxiliary_library(
                normalized_library(declaration.library));
        },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.dpi_declarations(),
        [&](const sv::DpiDeclaration& declaration) {
            return retained_id(declaration.owner_scope);
        },
        retained_provenance);
    retain_record_provenance(
        design.systemverilog_hir.covergroup_instances(),
        [&](const sv::CovergroupInstance& instance) {
            return retained_id(instance.owner_scope);
        },
        retained_provenance);
    retain_record_provenance(
        design.vhdl_hir.units(),
        [&](const vhdl::Unit& unit) { return retained_id(unit.id); },
        retained_provenance);
    retain_record_provenance(
        design.vhdl_hir.declarations(),
        [&](const vhdl::Declaration& declaration) {
            return retained_id(declaration.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.vhdl_hir.types(),
        [&](const vhdl::TypeDefinition& type) {
            return retained_id(type.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.vhdl_hir.overload_sets(),
        [&](const vhdl::OverloadSet& overload) {
            return retained_id(overload.scope);
        },
        retained_provenance);
    retain_record_provenance(
        design.vhdl_hir.expressions(),
        [&](const vhdl::Expression& expression) {
            return retained_id(expression.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.vhdl_hir.statements(),
        [&](const vhdl::Statement& statement) {
            return retained_id(statement.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.vhdl_hir.processes(),
        [&](const vhdl::Process& process) {
            return retained_id(process.id);
        },
        retained_provenance);
    retain_record_provenance(
        design.vhdl_hir.instances(),
        [&](const vhdl::Instance& instance) {
            return retained_id(instance.id);
        },
        retained_provenance);
    if (select_auxiliary_library("std")) {
        for (const auto& source : input.source_files) {
            const auto path = std::filesystem::path {
                source.physical_name
            };
            if (path.parent_path().filename() == "std"
                && path.parent_path().parent_path().filename()
                    == "fsim-standard") {
                retained_provenance.source_files.insert(
                    source.id.value());
            }
        }
    }

    bool changed { true };
    while (changed) {
        changed = false;
        for (const auto source : retained_provenance.source_spans) {
            if (source >= input.source_spans.size()) {
                continue;
            }
            const auto& span = input.source_spans[source];
            if (span.file.valid()
                && retained_provenance.source_files
                       .insert(span.file.value()).second) {
                changed = true;
            }
            if (span.expansion && span.expansion->valid()
                && retained_provenance.expansions
                       .insert(span.expansion->value()).second) {
                changed = true;
            }
        }
        for (const auto origin : retained_provenance.origins) {
            if (origin >= input.origins.size()) {
                continue;
            }
            const auto& record = input.origins[origin];
            if (record.source.valid()
                && retained_provenance.source_spans
                       .insert(record.source.value()).second) {
                changed = true;
            }
            if (record.parent && record.parent->valid()
                && retained_provenance.origins
                       .insert(record.parent->value()).second) {
                changed = true;
            }
        }
        for (const auto expansion : retained_provenance.expansions) {
            if (expansion >= input.expansions.size()) {
                continue;
            }
            const auto& record = input.expansions[expansion];
            if (record.parent && record.parent->valid()
                && retained_provenance.expansions
                       .insert(record.parent->value()).second) {
                changed = true;
            }
        }
    }
    output.source_files = select_records(input.source_files,
        projection.source_file, [&](const SourceFile& record) {
            return retained_provenance.source_files.contains(
                record.id.value());
        });
    output.expansions = select_records(input.expansions,
        projection.expansion, [&](const Expansion& record) {
            return retained_provenance.expansions.contains(
                record.id.value());
        });
    output.source_spans = select_records(input.source_spans,
        projection.source_span, [&](const SourceSpan& record) {
            return retained_provenance.source_spans.contains(
                record.id.value());
        });
    output.origins = select_records(input.origins,
        projection.origin, [&](const Origin& record) {
            return retained_provenance.origins.contains(
                record.id.value());
        });
    project(output, projection);
    return output;
}

template <typename Predicate>
sv::Hir project_hir(const sv::Hir& input,
    Predicate select_library,
    const Projection& projection)
{
    auto output = input;
    std::erase_if(output.mutable_units(),
        [&](const sv::Unit& unit) {
            return !retained(unit.id, projection);
        });
    std::erase_if(output.mutable_declarations(),
        [&](const sv::Declaration& declaration) {
            return !retained(declaration.id, projection);
        });
    std::erase_if(output.mutable_types(),
        [&](const sv::TypeDefinition& type) {
            return !retained(type.id, projection);
        });
    std::erase_if(output.mutable_expressions(),
        [&](const sv::Expression& expression) {
            return !retained(expression.id, projection);
        });
    std::erase_if(output.mutable_statements(),
        [&](const sv::Statement& statement) {
            return !retained(statement.id, projection);
        });
    std::erase_if(output.mutable_processes(),
        [&](const sv::Process& process) {
            return !retained(process.id, projection);
        });
    std::erase_if(output.mutable_classes(),
        [&](const sv::ClassDeclaration& declaration) {
            return !retained(declaration.scope, projection);
        });
    std::erase_if(output.mutable_instances(),
        [&](const sv::Instance& instance) {
            return !retained(instance.id, projection);
        });
    std::erase_if(output.mutable_udps(),
        [&](const sv::UdpDeclaration& declaration) {
            return !select_library(
                normalized_library(declaration.library));
        });
    std::erase_if(output.mutable_dpi_declarations(),
        [&](const sv::DpiDeclaration& declaration) {
            return !retained(declaration.owner_scope, projection);
        });
    std::erase_if(output.mutable_covergroup_instances(),
        [&](const sv::CovergroupInstance& instance) {
            return !retained(instance.owner_scope, projection);
        });
    project(output.mutable_units(), projection);
    project(output.mutable_declarations(), projection);
    project(output.mutable_types(), projection);
    project(output.mutable_expressions(), projection);
    project(output.mutable_statements(), projection);
    project(output.mutable_processes(), projection);
    project(output.mutable_classes(), projection);
    project(output.mutable_instances(), projection);
    project(output.mutable_udps(), projection);
    project(output.mutable_dpi_declarations(), projection);
    project(output.mutable_covergroup_instances(), projection);
    return output;
}

vhdl::Hir project_hir(
    const vhdl::Hir& input,
    const Projection& projection);

template <typename UnitPredicate, typename LibraryPredicate>
CompiledLinkResult project_compiled_design(
    const CompiledDesign& design, UnitPredicate select_unit,
    LibraryPredicate select_auxiliary_library,
    const std::string_view invalid_projection)
{
    if (const auto error = structural_hir_error(design);
        !error.empty()) {
        return { std::nullopt,
            "cannot project a structurally invalid compiled-HIR design: "
                + error };
    }
    Projection projection;
    auto records = project_model_records(
        design, select_unit, select_auxiliary_library, projection);
    auto model = Model::from_records(std::move(records));
    if (!model) {
        return { std::nullopt,
            "compiled-HIR library projection produced an invalid semantic model" };
    }
    auto systemverilog = project_hir(
        design.systemverilog_hir, select_auxiliary_library, projection);
    auto vhdl = project_hir(design.vhdl_hir, projection);
    CompiledDesign output {
        std::move(*model), std::move(systemverilog), std::move(vhdl)
    };
    refresh_compiled_design_metadata(output);
    if (!normalize_compiled_design(output)) {
        return { std::nullopt, std::string { invalid_projection } };
    }
    if (const auto error = structural_hir_error(output);
        !error.empty()) {
        return { std::nullopt, std::string { invalid_projection }
                + ": " + error };
    }
    return { std::move(output), { } };
}

template <typename UnitPredicate, typename LibraryPredicate>
CompiledLinkResult consume_or_project_compiled_design(
    CompiledDesign&& design, UnitPredicate select_unit,
    LibraryPredicate select_auxiliary_library,
    const std::string_view invalid_projection)
{
    const auto selects_every_record
        = std::ranges::all_of(design.units(), select_unit)
        && std::ranges::all_of(design.systemverilog_hir.udps(),
            [&](const sv::UdpDeclaration& declaration) {
                return select_auxiliary_library(
                    normalized_library(declaration.library));
            });
    if (!selects_every_record) {
        return project_compiled_design(design, select_unit,
            select_auxiliary_library, invalid_projection);
    }
    if (const auto error = structural_hir_error(design);
        !error.empty()) {
        return { std::nullopt,
            "cannot project a structurally invalid compiled-HIR design: "
                + error };
    }
    return { std::move(design), { } };
}

vhdl::Hir project_hir(
    const vhdl::Hir& input,
    const Projection& projection)
{
    auto output = input;
    std::erase_if(output.mutable_units(),
        [&](const vhdl::Unit& unit) {
            return !retained(unit.id, projection);
        });
    std::erase_if(output.mutable_declarations(),
        [&](const vhdl::Declaration& declaration) {
            return !retained(declaration.id, projection);
        });
    std::erase_if(output.mutable_types(),
        [&](const vhdl::TypeDefinition& type) {
            return !retained(type.id, projection);
        });
    std::erase_if(output.mutable_overload_sets(),
        [&](const vhdl::OverloadSet& overload) {
            return !retained(overload.scope, projection);
        });
    std::erase_if(output.mutable_expressions(),
        [&](const vhdl::Expression& expression) {
            return !retained(expression.id, projection);
        });
    std::erase_if(output.mutable_statements(),
        [&](const vhdl::Statement& statement) {
            return !retained(statement.id, projection);
        });
    std::erase_if(output.mutable_processes(),
        [&](const vhdl::Process& process) {
            return !retained(process.id, projection);
        });
    std::erase_if(output.mutable_instances(),
        [&](const vhdl::Instance& instance) {
            return !retained(instance.id, projection);
        });
    project(output.mutable_units(), projection);
    project(output.mutable_declarations(), projection);
    project(output.mutable_types(), projection);
    project(output.mutable_overload_sets(), projection);
    project(output.mutable_expressions(), projection);
    project(output.mutable_statements(), projection);
    project(output.mutable_processes(), projection);
    project(output.mutable_instances(), projection);
    return output;
}

Relocation relocation_for(const ModelRecords& records)
{
    return Relocation {
        static_cast<std::uint32_t>(records.source_files.size()),
        static_cast<std::uint32_t>(records.expansions.size()),
        static_cast<std::uint32_t>(records.source_spans.size()),
        static_cast<std::uint32_t>(records.origins.size()),
        static_cast<std::uint32_t>(records.units.size()),
        static_cast<std::uint32_t>(records.scopes.size()),
        static_cast<std::uint32_t>(records.declarations.size()),
        static_cast<std::uint32_t>(records.types.size()),
        static_cast<std::uint32_t>(records.values.size()),
        static_cast<std::uint32_t>(records.expression_identities.size()),
        static_cast<std::uint32_t>(records.statement_identities.size()),
        static_cast<std::uint32_t>(records.instances.size()),
        static_cast<std::uint32_t>(records.process_identities.size()),
    };
}

void append_records(ModelRecords& destination, ModelRecords source)
{
    append(destination.source_files, source.source_files);
    append(destination.expansions, source.expansions);
    append(destination.source_spans, source.source_spans);
    append(destination.origins, source.origins);
    append(destination.scopes, source.scopes);
    append(destination.units, source.units);
    append(destination.types, source.types);
    append(destination.values, source.values);
    append(destination.instances, source.instances);
    append(destination.declarations, source.declarations);
    append(destination.expression_identities, source.expression_identities);
    append(destination.statement_identities, source.statement_identities);
    append(destination.process_identities, source.process_identities);
}

void append_hir(sv::Hir& destination, sv::Hir source)
{
    append(destination.mutable_units(), source.mutable_units());
    append(destination.mutable_declarations(), source.mutable_declarations());
    append(destination.mutable_types(), source.mutable_types());
    append(destination.mutable_expressions(), source.mutable_expressions());
    append(destination.mutable_statements(), source.mutable_statements());
    append(destination.mutable_processes(), source.mutable_processes());
    append(destination.mutable_classes(), source.mutable_classes());
    append(destination.mutable_instances(), source.mutable_instances());
    append(destination.mutable_udps(), source.mutable_udps());
    append(destination.mutable_dpi_declarations(),
        source.mutable_dpi_declarations());
    append(destination.mutable_covergroup_instances(),
        source.mutable_covergroup_instances());
}

void append_hir(vhdl::Hir& destination, vhdl::Hir source)
{
    append(destination.mutable_units(), source.mutable_units());
    append(destination.mutable_declarations(), source.mutable_declarations());
    append(destination.mutable_types(), source.mutable_types());
    append(destination.mutable_overload_sets(), source.mutable_overload_sets());
    append(destination.mutable_expressions(), source.mutable_expressions());
    append(destination.mutable_statements(), source.mutable_statements());
    append(destination.mutable_processes(), source.mutable_processes());
    append(destination.mutable_instances(), source.mutable_instances());
}

void relocate_hir(sv::Hir& hir, const Relocation& relocation)
{
    relocate(hir.mutable_units(), relocation);
    relocate(hir.mutable_declarations(), relocation);
    relocate(hir.mutable_types(), relocation);
    relocate(hir.mutable_expressions(), relocation);
    relocate(hir.mutable_statements(), relocation);
    relocate(hir.mutable_processes(), relocation);
    relocate(hir.mutable_classes(), relocation);
    relocate(hir.mutable_instances(), relocation);
    relocate(hir.mutable_udps(), relocation);
    relocate(hir.mutable_dpi_declarations(), relocation);
    relocate(hir.mutable_covergroup_instances(), relocation);
}

void relocate_hir(vhdl::Hir& hir, const Relocation& relocation)
{
    relocate(hir.mutable_units(), relocation);
    relocate(hir.mutable_declarations(), relocation);
    relocate(hir.mutable_types(), relocation);
    relocate(hir.mutable_overload_sets(), relocation);
    relocate(hir.mutable_expressions(), relocation);
    relocate(hir.mutable_statements(), relocation);
    relocate(hir.mutable_processes(), relocation);
    relocate(hir.mutable_instances(), relocation);
}

std::string reference_name(const std::string_view text)
{
    const auto separator = text.find_last_of(".:");
    if (separator == std::string_view::npos) {
        return std::string { text };
    }
    auto begin = separator + 1U;
    while (begin < text.size() && text[begin] == ':') {
        ++begin;
    }
    return std::string { text.substr(begin) };
}

std::string reference_library(
    const std::string_view text, const std::string_view fallback)
{
    const auto separator = text.find_first_of(".:");
    if (separator == std::string_view::npos) {
        return std::string { normalized_library(fallback) };
    }
    return std::string { text.substr(0, separator) };
}

template <typename Name>
void add_reference(std::vector<CompiledReference>& output,
    const CompiledReferenceKind kind, const UnitId owner,
    const std::string_view owner_library, const Name& name,
    const SourceSpanId source, const std::string_view secondary = {})
{
    auto spelling = std::string_view { name.spelling };
    if constexpr (requires { name.canonical; }) {
        if (!name.canonical.empty()) {
            spelling = name.canonical;
        }
    }
    if (spelling.empty()) {
        return;
    }
    output.push_back(CompiledReference {
        kind,
        owner,
        reference_library(spelling, owner_library),
        reference_name(spelling),
        std::string { secondary },
        source,
        std::nullopt,
    });
}

void add_source_dependency(const CompiledDesign& design,
    std::vector<CompiledDependency>& output, const UnitId owner,
    const SourceSpanId source)
{
    if (!source.valid()
        || source.value() >= design.semantics.source_spans().size()) {
        return;
    }
    const auto& span = design.semantics.source_spans().at(source.value());
    if (!span.file.valid()
        || span.file.value() >= design.semantics.source_files().size()) {
        return;
    }
    const auto& file = design.semantics.source_files().at(span.file.value());
    output.push_back({ owner, span.logical_name, file.content_digest });
}

template <typename Item, typename Projection>
void sort_unique(std::vector<Item>& items, Projection projection)
{
    std::ranges::sort(items, {}, projection);
    items.erase(std::unique(items.begin(), items.end(),
                    [&](const Item& left, const Item& right) {
                        return projection(left) == projection(right);
                    }),
        items.end());
}

std::string lower_ascii(std::string_view text);

using UnitKey = std::tuple<Language, UnitKind, std::string,
    std::string, std::string>;

using UnitDefinitions = std::map<UnitKey, bool>;

std::string vhdl_link_name(
    const std::string_view name, const bool extended = false)
{
    const bool extended_identifier = name.size() >= 2U
        && name.front() == '\\' && name.back() == '\\';
    return extended || extended_identifier
        ? std::string { name }
        : lower_ascii(name);
}

UnitKey unit_key(const CompiledDesign& design, const Unit& unit)
{
    const auto language = unit.language == Language::verilog
        ? Language::system_verilog
        : unit.language;
    const auto library = unit.language == Language::vhdl
        ? lower_ascii(normalized_library(unit.library))
        : std::string { normalized_library(unit.library) };
    // Decoded bundles are not required to have passed through the parser's
    // canonical spelling path. Normalize VHDL basic identifiers here while
    // preserving the case-sensitive payload of extended identifiers.
    const auto vhdl_unit = unit.language == Language::vhdl
        ? std::ranges::find(
              design.vhdl_hir.units(), unit.id, &vhdl::Unit::id)
        : design.vhdl_hir.units().end();
    const auto name = unit.language == Language::vhdl
        ? vhdl_link_name(unit.name,
              vhdl_unit != design.vhdl_hir.units().end()
                  && vhdl_unit->extended_name)
        : unit.name;
    const auto secondary_name = unit.language == Language::vhdl
        ? vhdl_link_name(unit.secondary_name,
              vhdl_unit != design.vhdl_hir.units().end()
                  && vhdl_unit->extended_primary_name)
        : unit.secondary_name;
    return {
        language,
        unit.kind,
        library,
        name,
        secondary_name,
    };
}

using UdpKey = std::tuple<Language, std::string, std::string>;

UdpKey udp_key(const sv::UdpDeclaration& declaration)
{
    return { declaration.language,
        std::string { normalized_library(declaration.library) },
        declaration.name };
}

bool is_external_unit(const CompiledDesign& design, const Unit& unit)
{
    if (unit.language != Language::verilog
        && unit.language != Language::system_verilog) {
        return false;
    }
    const auto hir_unit = std::ranges::find(
        design.systemverilog_hir.units(), unit.id, &sv::Unit::id);
    return hir_unit != design.systemverilog_hir.units().end()
        && hir_unit->external;
}

std::string lower_ascii(const std::string_view text)
{
    std::string result { text };
    std::ranges::transform(result, result.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

bool same_vhdl_identifier(
    const std::string_view left, const std::string_view right)
{
    return lower_ascii(left) == lower_ascii(right);
}

const Unit* reference_owner(
    const CompiledDesign& design, const CompiledReference& reference)
{
    if (!reference.owner.valid()
        || reference.owner.value() >= design.semantics.units().size()) {
        return nullptr;
    }
    return &design.semantics.units()[reference.owner.value()];
}

bool reference_kind_matches(
    const CompiledReferenceKind kind, const Unit& unit)
{
    switch (kind) {
    case CompiledReferenceKind::package:
    case CompiledReferenceKind::import:
        return unit.kind == UnitKind::systemverilog_package
            || unit.kind == UnitKind::vhdl_package;
    case CompiledReferenceKind::entity:
        return unit.kind == UnitKind::vhdl_entity;
    case CompiledReferenceKind::architecture:
        return unit.kind == UnitKind::vhdl_architecture;
    case CompiledReferenceKind::module:
    case CompiledReferenceKind::bind:
        return unit.kind == UnitKind::verilog_module
            || unit.kind == UnitKind::systemverilog_interface
            || unit.kind == UnitKind::systemverilog_program;
    case CompiledReferenceKind::configuration:
        return unit.kind == UnitKind::vhdl_configuration
            || unit.kind == UnitKind::systemverilog_configuration;
    case CompiledReferenceKind::context:
        return unit.kind == UnitKind::vhdl_context;
    case CompiledReferenceKind::class_declaration:
        return false;
    }
    return false;
}

bool reference_name_matches(
    const CompiledReference& reference, const Unit& unit)
{
    if (!reference_kind_matches(reference.kind, unit)) {
        return false;
    }
    const auto vhdl = unit.language == Language::vhdl;
    const auto equals = [&](const std::string_view left,
                            const std::string_view right) {
        return vhdl ? same_vhdl_identifier(left, right) : left == right;
    };
    if (!equals(normalized_library(unit.library),
            normalized_library(reference.library))) {
        return false;
    }
    if (unit.kind == UnitKind::vhdl_architecture) {
        return equals(unit.secondary_name, reference.name)
            && !reference.secondary_name.empty()
            && equals(unit.name, reference.secondary_name);
    }
    return equals(unit.name, reference.name)
        && (reference.secondary_name.empty()
            || equals(unit.secondary_name, reference.secondary_name));
}

std::optional<UnitId> resolve_class_reference(
    const CompiledDesign& design, const CompiledReference& reference)
{
    const auto matches = [&](const sv::ClassDeclaration& declaration) {
        return !reference.secondary_name.empty()
            ? sv::class_declaration_identity(declaration)
                    == reference.secondary_name
                || declaration.canonical_identity
                    == reference.secondary_name
            : declaration.name == reference.name;
    };
    const auto found = std::ranges::find_if(
        design.systemverilog_hir.classes(), matches);
    if (found == design.systemverilog_hir.classes().end()
        || std::find_if(std::next(found),
               design.systemverilog_hir.classes().end(), matches)
            != design.systemverilog_hir.classes().end()) {
        return std::nullopt;
    }
    return compiled_class_owner(design, *found);
}

std::optional<UnitId> resolve_reference(
    const CompiledDesign& design, const CompiledReference& reference)
{
    if (reference.kind == CompiledReferenceKind::class_declaration) {
        return resolve_class_reference(design, reference);
    }
    const auto* owner = reference_owner(design, reference);
    const auto matches = [&](const Unit& unit) {
        if (!reference_name_matches(reference, unit)) {
            return false;
        }
        if (owner == nullptr) {
            return true;
        }
        if (owner->language == Language::vhdl) {
            return unit.language == Language::vhdl;
        }
        return unit.language == Language::verilog
            || unit.language == Language::system_verilog;
    };
    const Unit* selected = nullptr;
    auto ambiguous = false;
    auto matched_vhdl_package_declaration = false;
    auto matched_vhdl_package_body = false;
    const auto vhdl_package_declaration = [&](const Unit& unit) {
        if (unit.kind != UnitKind::vhdl_package) {
            return false;
        }
        const auto record = std::ranges::find(
            design.vhdl_hir.units(), unit.id, &vhdl::Unit::id);
        return record != design.vhdl_hir.units().end()
            && record->primary_name.empty();
    };
    for (const auto& unit : design.semantics.units()) {
        if (!matches(unit)) {
            continue;
        }
        const auto definition = !is_external_unit(design, unit);
        if (selected == nullptr) {
            selected = &unit;
            if (unit.kind == UnitKind::vhdl_package) {
                const auto declaration = vhdl_package_declaration(unit);
                matched_vhdl_package_declaration = declaration;
                matched_vhdl_package_body = !declaration;
            }
            continue;
        }
        const auto selected_definition
            = !is_external_unit(design, *selected);
        if (definition && !selected_definition) {
            selected = &unit;
            ambiguous = false;
        } else if (definition == selected_definition) {
            const auto package_pair
                = reference.kind == CompiledReferenceKind::package
                    || reference.kind == CompiledReferenceKind::import;
            if (package_pair && unit.kind == UnitKind::vhdl_package
                && selected->kind == UnitKind::vhdl_package) {
                const auto declaration = vhdl_package_declaration(unit);
                auto& matched = declaration
                    ? matched_vhdl_package_declaration
                    : matched_vhdl_package_body;
                if (matched) {
                    ambiguous = true;
                } else {
                    matched = true;
                }
                if (declaration) {
                    selected = &unit;
                }
            } else {
                ambiguous = true;
            }
        }
    }
    return selected != nullptr && !ambiguous
        ? std::optional<UnitId> { selected->id }
        : std::nullopt;
}

std::string vhdl_architecture_primary_profile_error(
    const CompiledDesign& design)
{
    for (const auto& reference : design.references()) {
        if (reference.kind != CompiledReferenceKind::entity
            || !reference.target) {
            continue;
        }
        const auto architecture = std::ranges::find(
            design.vhdl_hir.units(), reference.owner, &vhdl::Unit::id);
        const auto entity = std::ranges::find(
            design.vhdl_hir.units(), *reference.target, &vhdl::Unit::id);
        if (architecture == design.vhdl_hir.units().end()
            || entity == design.vhdl_hir.units().end()
            || architecture->kind != vhdl::UnitKind::architecture
            || entity->kind != vhdl::UnitKind::entity
            || (architecture->standard == entity->standard
                && architecture->compatibility_profile
                    == entity->compatibility_profile)) {
            continue;
        }
        return "VHDL entity '"
            + std::string { normalized_library(entity->library) }
            + "." + entity->name + "' was analyzed as "
            + entity->standard + " with compatibility profile '"
            + entity->compatibility_profile + "' but architecture '"
            + architecture->name + "' uses " + architecture->standard
            + " with compatibility profile '"
            + architecture->compatibility_profile + "'";
    }
    return { };
}

void resolve_linked_vhdl_architecture_names(CompiledDesign& design)
{
    const auto resolve_annotations = [&] {
        std::vector<CompiledVhdlExpressionAnnotation> annotations;
        const auto& expressions = design.vhdl_hir.expressions();
        annotations.reserve(expressions.size());
        for (const auto& expression : expressions) {
            CompiledVhdlExpressionAnnotation annotation;
            annotation.expression = expression.id;
            if (!expression.scope.valid()
                || expression.scope.value()
                    >= design.semantics.scopes().size()) {
                annotations.push_back(std::move(annotation));
                continue;
            }

            auto builtin_operator = vhdl::BuiltinOperatorIdentity::none;
            const auto& operation = expression.text;
            if (expression.kind == vhdl::ExpressionKind::unary
                && expression.operands.size() == 1U
                && same_vhdl_identifier(operation, "not")) {
                builtin_operator
                    = vhdl::BuiltinOperatorIdentity::ieee_std_logic_1164_not;
            } else if (expression.kind == vhdl::ExpressionKind::binary
                && expression.operands.size() == 2U) {
                if (same_vhdl_identifier(operation, "and")) {
                    builtin_operator
                        = vhdl::BuiltinOperatorIdentity::
                            ieee_std_logic_1164_and;
                } else if (same_vhdl_identifier(operation, "or")) {
                    builtin_operator
                        = vhdl::BuiltinOperatorIdentity::
                            ieee_std_logic_1164_or;
                } else if (same_vhdl_identifier(operation, "nand")) {
                    builtin_operator
                        = vhdl::BuiltinOperatorIdentity::
                            ieee_std_logic_1164_nand;
                } else if (same_vhdl_identifier(operation, "nor")) {
                    builtin_operator
                        = vhdl::BuiltinOperatorIdentity::
                            ieee_std_logic_1164_nor;
                } else if (same_vhdl_identifier(operation, "xor")) {
                    builtin_operator
                        = vhdl::BuiltinOperatorIdentity::
                            ieee_std_logic_1164_xor;
                } else if (same_vhdl_identifier(operation, "xnor")) {
                    builtin_operator
                        = vhdl::BuiltinOperatorIdentity::
                            ieee_std_logic_1164_xnor;
                }
            }
            if (builtin_operator == vhdl::BuiltinOperatorIdentity::none
                && !expression.referenced_name) {
                annotations.push_back(std::move(annotation));
                continue;
            }
            const auto unit_id
                = design.semantics.scopes()[expression.scope.value()].unit;
            const auto unit = design.find_unit(unit_id);
            if (!unit || unit->vhdl == nullptr
                || unit->vhdl->kind != vhdl::UnitKind::architecture) {
                annotations.push_back(std::move(annotation));
                continue;
            }
            const auto retained_candidate = expression.referenced_name
                && (expression.referenced_name->selected.has_value()
                    || !expression.referenced_name->overloads.empty());
            const CompiledDesignResolver resolver { design, unit_id };
            auto synthetic_name = vhdl::Name { };
            synthetic_name.spelling = operation;
            synthetic_name.canonical = operation;
            synthetic_name.source = expression.source;
            const auto& resolution_name = expression.referenced_name
                ? *expression.referenced_name
                : synthetic_name;
            auto resolved = resolver.resolve_vhdl(
                resolution_name, expression.scope);
            if (builtin_operator != vhdl::BuiltinOperatorIdentity::none
                && !retained_candidate
                && resolved.status
                    == CompiledResolutionStatus::not_found
                && resolved.candidates.empty()
                && resolver.vhdl_builtin_package_member_imported("ieee",
                    "std_logic_1164", operation, expression.scope)) {
                annotation.builtin_operator = builtin_operator;
            }
            if (expression.referenced_name) {
                annotation.name_resolution.emplace(
                    CompiledVhdlExpressionNameResolution {
                        resolved.unique(), std::move(resolved.candidates) });
            }
            annotations.push_back(std::move(annotation));
        }
        return annotations;
    };

    auto annotations = resolve_annotations();
    if (design.apply_linked_vhdl_expression_annotations(annotations)) {
        return;
    }

    // A stale lookup cache can be used safely through the resolver's linear
    // fallback, but refresh before retrying so the linker keeps its usual
    // indexed-resolution behavior.
    design.refresh_lookup_indexes();
    annotations = resolve_annotations();
    if (design.apply_linked_vhdl_expression_annotations(annotations)) {
        return;
    }

    // This is only a defensive path for a malformed identity sequence. Do
    // not restore the revision stamp unless the scoped API proves its
    // contract; finish through an ordinary mutation and full index rebuild.
    auto& expressions = design.vhdl_hir.mutable_expressions();
    for (std::size_t position = 0;
        position < std::min(expressions.size(), annotations.size());
        ++position) {
        auto& expression = expressions[position];
        auto& annotation = annotations[position];
        if (expression.id != annotation.expression) {
            continue;
        }
        expression.builtin_operator = annotation.builtin_operator;
        if (!annotation.name_resolution || !expression.referenced_name) {
            continue;
        }
        expression.referenced_name->selected
            = annotation.name_resolution->selected;
        expression.referenced_name->overloads.swap(
            annotation.name_resolution->overloads);
    }
    design.refresh_lookup_indexes();
}

CompiledReference systemverilog_configuration_reference(
    const sv::Unit& owner, const CompiledReferenceKind kind,
    const std::string_view library, const std::string_view name,
    const SourceSpanId source)
{
    return {
        kind,
        owner.id,
        std::string { normalized_library(
            library.empty() ? owner.library : library) },
        std::string { name },
        {},
        source,
        std::nullopt,
    };
}

void append_systemverilog_configuration_references(
    std::vector<CompiledReference>& references, const sv::Unit& unit)
{
    if (!unit.configuration) {
        return;
    }
    for (const auto& design : unit.configuration->designs) {
        references.push_back(systemverilog_configuration_reference(
            unit, CompiledReferenceKind::module,
            design.library, design.cell, design.source));
    }
    for (const auto& rule : unit.configuration->rules) {
        if (rule.selection != sv::ConfigurationSelectionKind::use) {
            continue;
        }
        references.push_back(systemverilog_configuration_reference(
            unit,
            rule.use_configuration
                ? CompiledReferenceKind::configuration
                : CompiledReferenceKind::module,
            rule.use_library, rule.use_cell, rule.source));
    }
}

void link_systemverilog_configurations(CompiledDesign& design)
{
    const auto has_configurations = std::ranges::any_of(
        design.systemverilog_hir.units(),
        [](const sv::Unit& unit) { return unit.configuration.has_value(); });
    if (!has_configurations) {
        return;
    }
    auto& units = design.mutable_systemverilog().mutable_units();
    design.refresh_lookup_indexes();
    for (auto& unit : units) {
        if (!unit.configuration) {
            continue;
        }
        for (auto& configuration_design :
            unit.configuration->designs) {
            configuration_design.target = resolve_reference(
                design, systemverilog_configuration_reference(
                            unit, CompiledReferenceKind::module,
                            configuration_design.library,
                            configuration_design.cell,
                            configuration_design.source));
        }
        for (auto& rule : unit.configuration->rules) {
            rule.target.reset();
            if (rule.selection
                != sv::ConfigurationSelectionKind::use) {
                continue;
            }
            rule.target = resolve_reference(
                design, systemverilog_configuration_reference(
                            unit,
                            rule.use_configuration
                                ? CompiledReferenceKind::configuration
                                : CompiledReferenceKind::module,
                            rule.use_library, rule.use_cell,
                            rule.source));
        }
    }
}

std::vector<std::string_view> selected_name_parts(
    const std::string_view selected)
{
    std::vector<std::string_view> result;
    std::size_t begin = 0;
    while (begin < selected.size()) {
        const auto end = selected.find('.', begin);
        const auto part = selected.substr(begin,
            end == std::string_view::npos
                ? std::string_view::npos
                : end - begin);
        if (!part.empty()) {
            result.push_back(part);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return result;
}

std::pair<std::string_view, std::string_view> vhdl_entity_aspect(
    const vhdl::Name& name)
{
    const auto spelling = name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
    const auto close = spelling.find_last_not_of(" \t\r\n");
    if (close == std::string_view::npos || spelling[close] != ')') {
        return { spelling, { } };
    }
    const auto open = spelling.rfind('(', close);
    if (open == std::string_view::npos) {
        return { spelling, { } };
    }
    const auto entity = support::trim_ctype_whitespace(
        spelling.substr(0, open));
    const auto architecture = support::trim_ctype_whitespace(
        spelling.substr(open + 1U, close - open - 1U));
    if (entity.empty() || architecture.empty()) {
        return { spelling, { } };
    }
    return { entity, architecture };
}

void add_vhdl_entity_instance_references(
    std::vector<CompiledReference>& references, const UnitId owner,
    const std::string_view owner_library, const vhdl::Name& target,
    const SourceSpanId source)
{
    const auto [entity, architecture] = vhdl_entity_aspect(target);
    references.push_back(CompiledReference {
        CompiledReferenceKind::entity,
        owner,
        reference_library(entity, owner_library),
        reference_name(entity),
        {},
        source,
        std::nullopt,
    });
    if (architecture.empty()) {
        return;
    }
    references.push_back(CompiledReference {
        CompiledReferenceKind::architecture,
        owner,
        reference_library(entity, owner_library),
        reference_name(entity),
        std::string { architecture },
        source,
        std::nullopt,
    });
}

void add_vhdl_use_reference(std::vector<CompiledReference>& references,
    const UnitId owner, const std::string_view owner_library,
    const vhdl::Name& name, const SourceSpanId source)
{
    const auto spelling = name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
    const auto parts = selected_name_parts(spelling);
    if (parts.size() < 2U) {
        return;
    }
    const auto qualified = parts.size() >= 3U;
    const auto specified_library = qualified
        ? std::string_view { parts[0] }
        : normalized_library(owner_library);
    const auto library = same_vhdl_identifier(specified_library, "work")
        ? normalized_library(owner_library)
        : specified_library;
    references.push_back(CompiledReference {
        CompiledReferenceKind::package,
        owner,
        lower_ascii(library),
        lower_ascii(qualified ? parts[1] : parts[0]),
        {},
        source,
        std::nullopt,
    });
}

void add_vhdl_selected_package_reference(const CompiledDesign& design,
    std::vector<CompiledReference>& references, const UnitId owner,
    const std::string_view owner_library,
    const vhdl::Expression& expression)
{
    if (expression.kind != vhdl::ExpressionKind::name) {
        return;
    }
    const auto spelling = expression.referenced_name
            && !expression.referenced_name->canonical.empty()
        ? std::string_view { expression.referenced_name->canonical }
        : std::string_view { expression.text };
    const auto parts = selected_name_parts(spelling);
    if (parts.size() != 2U && parts.size() != 3U) {
        return;
    }
    const auto qualified = parts.size() == 3U;
    const auto specified_library = qualified
        ? parts[0]
        : normalized_library(owner_library);
    const auto library = same_vhdl_identifier(specified_library, "work")
        ? normalized_library(owner_library)
        : specified_library;
    const auto package_name = qualified ? parts[1] : parts[0];
    const auto member_name = qualified ? parts[2] : parts[1];
    const auto package = std::ranges::find_if(
        design.vhdl_units(), [&](const vhdl::Unit& candidate) {
            if (candidate.kind != vhdl::UnitKind::package
                || !candidate.primary_name.empty()
                || !same_vhdl_identifier(candidate.name, package_name)
                || !same_vhdl_identifier(
                    normalized_library(candidate.library), library)) {
                return false;
            }
            return std::ranges::any_of(candidate.declarations,
                [&](const DeclarationId declaration_id) {
                    const auto declaration
                        = design.find_declaration(declaration_id);
                    return declaration && declaration->vhdl != nullptr
                        && same_vhdl_identifier(
                            declaration->vhdl->name, member_name);
                });
        });
    if (package == design.vhdl_units().end()) {
        return;
    }
    references.push_back(CompiledReference {
        CompiledReferenceKind::package,
        owner,
        lower_ascii(library),
        lower_ascii(package_name),
        {},
        expression.source,
        std::nullopt,
    });
}

void add_vhdl_context_reference(std::vector<CompiledReference>& references,
    const UnitId owner, const std::string_view owner_library,
    const vhdl::Name& name, const SourceSpanId source)
{
    const auto spelling = name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
    const auto parts = selected_name_parts(spelling);
    if (parts.size() < 2U) {
        return;
    }
    const auto specified_library = std::string_view {
        parts[parts.size() - 2U]
    };
    const auto library = same_vhdl_identifier(specified_library, "work")
        ? normalized_library(owner_library)
        : specified_library;
    references.push_back(CompiledReference {
        CompiledReferenceKind::context,
        owner,
        lower_ascii(library),
        lower_ascii(parts.back()),
        {},
        source,
        std::nullopt,
    });
}

void add_class_reference(std::vector<CompiledReference>& references,
    const UnitId owner, const std::string_view canonical_identity,
    const SourceSpanId source)
{
    if (canonical_identity.empty()) {
        return;
    }
    references.push_back(CompiledReference {
        CompiledReferenceKind::class_declaration,
        owner,
        reference_library(canonical_identity, "work"),
        reference_name(canonical_identity),
        std::string { canonical_identity },
        source,
        std::nullopt,
    });
}

void add_vhdl_binding_references(
    std::vector<CompiledReference>& references, const UnitId owner,
    const std::string_view owner_library,
    const vhdl::BindingIndication& binding)
{
    if (binding.kind == vhdl::BindingKind::entity) {
        add_reference(references, CompiledReferenceKind::entity,
            owner, owner_library, binding.entity, binding.source);
        if (!binding.architecture.empty()) {
            add_reference(references, CompiledReferenceKind::architecture,
                owner, owner_library, binding.entity, binding.source,
                binding.architecture);
        }
    } else if (binding.kind == vhdl::BindingKind::configuration) {
        add_reference(references, CompiledReferenceKind::configuration,
            owner, owner_library, binding.configuration, binding.source);
    }
}

void add_vhdl_component_references(
    std::vector<CompiledReference>& references, const UnitId owner,
    const std::string_view owner_library,
    const std::vector<vhdl::ComponentConfiguration>& components)
{
    for (const auto& component : components) {
        add_vhdl_binding_references(
            references, owner, owner_library, component.binding);
    }
}

void add_vhdl_block_references(
    std::vector<CompiledReference>& references, const UnitId owner,
    const std::string_view owner_library,
    const vhdl::BlockConfiguration& block)
{
    add_vhdl_component_references(
        references, owner, owner_library, block.components);
    for (const auto& child : block.blocks) {
        add_vhdl_block_references(
            references, owner, owner_library, child);
    }
}

bool can_append_count(const std::size_t destination,
    const std::size_t source)
{
    constexpr auto maximum
        = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    return destination <= maximum && source <= maximum
        && source <= maximum - destination;
}

bool relocation_fits(
    const ModelRecords& destination, const ModelRecords& source)
{
    return can_append_count(
               destination.source_files.size(), source.source_files.size())
        && can_append_count(
            destination.expansions.size(), source.expansions.size())
        && can_append_count(destination.source_spans.size(),
            source.source_spans.size())
        && can_append_count(
            destination.origins.size(), source.origins.size())
        && can_append_count(destination.units.size(), source.units.size())
        && can_append_count(destination.scopes.size(), source.scopes.size())
        && can_append_count(destination.declarations.size(),
            source.declarations.size())
        && can_append_count(destination.types.size(), source.types.size())
        && can_append_count(destination.values.size(), source.values.size())
        && can_append_count(destination.expression_identities.size(),
            source.expression_identities.size())
        && can_append_count(destination.statement_identities.size(),
            source.statement_identities.size())
        && can_append_count(
            destination.instances.size(), source.instances.size())
        && can_append_count(destination.process_identities.size(),
            source.process_identities.size());
}

bool register_definitions(const CompiledDesign& design,
    UnitDefinitions& units, std::set<std::string>& classes,
    std::set<UdpKey>& udps, std::string& error)
{
    for (const auto& unit : design.semantics.units()) {
        const auto definition = !is_external_unit(design, unit);
        const auto [registered, inserted]
            = units.try_emplace(unit_key(design, unit), definition);
        if (!inserted && definition && registered->second) {
            error = "compiled-HIR unit collision for logical identity '"
                + std::string { normalized_library(unit.library) }
                + "." + unit.name + "'";
            return false;
        }
        registered->second = registered->second || definition;
    }
    for (const auto& declaration : design.systemverilog_hir.classes()) {
        const auto identity = sv::class_declaration_identity(declaration);
        if (!identity.empty() && !classes.insert(identity).second) {
            error = "compiled-HIR class collision for logical identity '"
                + identity + "'";
            return false;
        }
    }
    for (const auto& declaration : design.systemverilog_hir.udps()) {
        if (!udps.insert(udp_key(declaration)).second) {
            error = "compiled-HIR UDP collision for logical identity '"
                + std::string { normalized_library(declaration.library) }
                + "." + declaration.name + "'";
            return false;
        }
    }
    return true;
}

using InputUnitKey = std::tuple<UnitKey, bool, std::string, std::string>;
using InputShapeKey = std::tuple<std::size_t, std::size_t, std::size_t,
    std::size_t, std::size_t, std::size_t, std::size_t, std::size_t,
    std::size_t, std::size_t, std::size_t, std::size_t, std::size_t>;
using CompiledInputOrderKey = std::tuple<std::vector<InputUnitKey>,
    std::vector<std::string>, std::vector<UdpKey>, InputShapeKey>;

CompiledInputOrderKey compiled_input_order_key(
    const CompiledDesign& design)
{
    std::vector<InputUnitKey> units;
    units.reserve(design.units().size());
    for (const auto& unit : design.units()) {
        std::string logical_name;
        std::string content_digest;
        if (unit.source.valid()
            && unit.source.value() < design.semantics.source_spans().size()) {
            const auto& span
                = design.semantics.source_spans()[unit.source.value()];
            logical_name = span.logical_name;
            if (span.file.valid()
                && span.file.value()
                    < design.semantics.source_files().size()) {
                content_digest = design.semantics.source_files()
                                     [span.file.value()]
                                         .content_digest;
            }
        }
        units.emplace_back(unit_key(design, unit),
            is_external_unit(design, unit),
            std::move(logical_name), std::move(content_digest));
    }
    std::ranges::sort(units);

    std::vector<std::string> classes;
    classes.reserve(design.systemverilog_hir.classes().size());
    for (const auto& declaration : design.systemverilog_hir.classes()) {
        classes.push_back(sv::class_declaration_identity(declaration));
    }
    std::ranges::sort(classes);

    std::vector<UdpKey> udps;
    udps.reserve(design.systemverilog_hir.udps().size());
    for (const auto& declaration : design.systemverilog_hir.udps()) {
        udps.push_back(udp_key(declaration));
    }
    std::ranges::sort(udps);

    const auto& records = design.semantics;
    return { std::move(units), std::move(classes), std::move(udps),
        InputShapeKey {
            records.source_files().size(), records.expansions().size(),
            records.source_spans().size(), records.origins().size(),
            records.units().size(), records.scopes().size(),
            records.declarations().size(), records.types().size(),
            records.values().size(), records.expression_identities().size(),
            records.statement_identities().size(), records.instances().size(),
            records.process_identities().size() } };
}

} // namespace

void refresh_compiled_design_metadata(CompiledDesign& design)
{
    auto& dependencies = design.mutable_dependencies();
    auto& references = design.mutable_references();
    dependencies.clear();
    references.clear();
    for (const auto& unit : design.systemverilog_hir.units()) {
        add_source_dependency(design, dependencies, unit.id, unit.source);
        for (const auto& import : unit.imports) {
            add_reference(references, CompiledReferenceKind::import,
                unit.id, unit.library, import.package, import.source);
        }
        for (const auto& export_item : unit.exports) {
            add_reference(references, CompiledReferenceKind::package,
                unit.id, unit.library, export_item.package,
                export_item.source);
        }
        for (const auto& bind : unit.binds) {
            add_reference(references, CompiledReferenceKind::bind,
                unit.id, unit.library, bind.target, bind.source);
        }
        append_systemverilog_configuration_references(references, unit);
    }
    append_compiled_class_references(design, references);
    for (const auto& expression : design.systemverilog_hir.expressions()) {
        if (!expression.scope.valid()
            || expression.scope.value() >= design.semantics.scopes().size()) {
            continue;
        }
        add_class_reference(references,
            design.semantics.scopes()[expression.scope.value()].unit,
            expression.class_identity, expression.source);
    }
    for (const auto& unit : design.vhdl_hir.units()) {
        add_source_dependency(design, dependencies, unit.id, unit.source);
        if (unit.kind == vhdl::UnitKind::architecture
            && !unit.primary_name.empty()) {
            references.push_back(CompiledReference {
                CompiledReferenceKind::entity,
                unit.id,
                std::string { normalized_library(unit.library) },
                unit.primary_name,
                {},
                unit.source,
                std::nullopt,
            });
        }
        for (const auto& context : unit.context) {
            for (const auto& selected : context.selected_names) {
                if (context.kind == vhdl::ContextKind::use_clause) {
                    add_vhdl_use_reference(references, unit.id,
                        unit.library, selected, context.source);
                } else if (context.kind
                    == vhdl::ContextKind::context_reference) {
                    add_vhdl_context_reference(references, unit.id,
                        unit.library, selected, context.source);
                }
            }
        }
        for (const auto declaration_id : unit.declarations) {
            const auto declaration = std::ranges::find(
                design.vhdl_hir.declarations(), declaration_id,
                &vhdl::Declaration::id);
            if (declaration == design.vhdl_hir.declarations().end()
                || !declaration->package) {
                continue;
            }
            add_reference(references, CompiledReferenceKind::package,
                unit.id, unit.library,
                declaration->package->template_name,
                declaration->source);
        }
        add_vhdl_component_references(references, unit.id,
            unit.library, unit.component_configurations);
        if (unit.configuration) {
            add_vhdl_block_references(references, unit.id,
                unit.library, *unit.configuration);
        }
    }
    const auto instance_owner = [&](const ScopeId scope)
        -> const Unit* {
        if (!scope.valid()
            || scope.value() >= design.semantics.scopes().size()) {
            return nullptr;
        }
        const auto unit = design.semantics.scopes()[scope.value()].unit;
        if (!unit.valid() || unit.value() >= design.units().size()) {
            return nullptr;
        }
        return &design.units()[unit.value()];
    };
    for (const auto& expression : design.vhdl_hir.expressions()) {
        const auto* owner = instance_owner(expression.scope);
        if (owner == nullptr || owner->language != Language::vhdl) {
            continue;
        }
        add_vhdl_selected_package_reference(design, references,
            owner->id, owner->library, expression);
    }
    for (const auto& instance : design.systemverilog_hir.instances()) {
        const auto* owner = instance_owner(instance.scope);
        if (owner == nullptr || instance.udp) {
            continue;
        }
        add_reference(references, CompiledReferenceKind::module,
            owner->id, owner->library, instance.target,
            instance.source);
    }
    for (const auto& instance : design.vhdl_hir.instances()) {
        const auto* owner = instance_owner(instance.scope);
        if (owner == nullptr) {
            continue;
        }
        if (instance.configuration) {
            add_reference(references,
                CompiledReferenceKind::configuration,
                owner->id, owner->library, instance.target,
                instance.source);
        } else {
            add_vhdl_entity_instance_references(references,
                owner->id, owner->library, instance.target,
                instance.source);
        }
    }
    sort_unique(dependencies, [](const CompiledDependency& dependency) {
        return std::tuple {
            dependency.owner, dependency.logical_name,
            dependency.content_digest };
    });
    sort_unique(references, [](const CompiledReference& reference) {
        return std::tuple {
            reference.owner, reference.kind, reference.library,
            reference.name, reference.secondary_name, reference.source };
    });
    design.refresh_lookup_indexes();
    for (auto& reference : references) {
        reference.target = resolve_reference(design, reference);
    }
    link_systemverilog_configurations(design);
}

CompiledLinkResult extract_compiled_library(
    const CompiledDesign& design,
    const std::string_view library)
{
    const auto select_library = [&](const std::string_view candidate) {
        return normalized_library(candidate)
            == normalized_library(library);
    };
    return project_compiled_design(design,
        [&](const Unit& unit) {
            return select_library(unit.library);
        }, select_library,
        "compiled-HIR library projection is structurally invalid");
}

CompiledLinkResult extract_compiled_library(
    CompiledDesign&& design,
    const std::string_view library)
{
    const auto select_library = [&](const std::string_view candidate) {
        return normalized_library(candidate)
            == normalized_library(library);
    };
    return consume_or_project_compiled_design(std::move(design),
        [&](const Unit& unit) {
            return select_library(unit.library);
        }, select_library,
        "compiled-HIR library projection is structurally invalid");
}

CompiledLinkResult extract_compiled_libraries(
    const CompiledDesign& design,
    const std::span<const std::string> libraries)
{
    const auto select_library = [&](const std::string_view candidate) {
        return std::ranges::any_of(libraries,
            [&](const std::string_view selected) {
                return normalized_library(candidate)
                    == normalized_library(selected);
            });
    };
    return project_compiled_design(design,
        [&](const Unit& unit) {
            return select_library(unit.library);
        }, select_library,
        "compiled-HIR library-set projection is structurally invalid");
}

CompiledLinkResult extract_compiled_libraries(
    CompiledDesign&& design,
    const std::span<const std::string> libraries)
{
    const auto select_library = [&](const std::string_view candidate) {
        return std::ranges::any_of(libraries,
            [&](const std::string_view selected) {
                return normalized_library(candidate)
                    == normalized_library(selected);
            });
    };
    return consume_or_project_compiled_design(std::move(design),
        [&](const Unit& unit) {
            return select_library(unit.library);
        }, select_library,
        "compiled-HIR library-set projection is structurally invalid");
}

CompiledLinkResult extract_compiled_units(
    const CompiledDesign& design,
    const std::span<const UnitId> units,
    const std::span<const std::string> supporting_libraries)
{
    const auto select_supporting_library
        = [&](const std::string_view candidate) {
        return std::ranges::any_of(supporting_libraries,
            [&](const std::string_view selected) {
                return normalized_library(candidate)
                    == normalized_library(selected);
            });
    };
    return project_compiled_design(design,
        [&](const Unit& unit) {
            return std::ranges::find(units, unit.id) != units.end()
                || select_supporting_library(unit.library);
        }, select_supporting_library,
        "compiled-HIR unit projection is structurally invalid");
}

CompiledLinkResult exclude_compiled_libraries(
    const CompiledDesign& design,
    const std::span<const std::string> libraries)
{
    const auto select_library = [&](const std::string_view candidate) {
        return std::ranges::none_of(libraries,
            [&](const std::string_view excluded) {
                return normalized_library(candidate)
                    == normalized_library(excluded);
            });
    };
    return project_compiled_design(design,
        [&](const Unit& unit) {
            return select_library(unit.library);
        }, select_library,
        "compiled-HIR library exclusion is structurally invalid");
}

static CompiledLinkResult finalize_linked_design(CompiledDesign output)
{
    refresh_compiled_design_metadata(output);
    if (const auto error = vhdl_architecture_primary_profile_error(output);
        !error.empty()) {
        return { std::nullopt, error, "FSIM-FE-VHORDER-011" };
    }
    resolve_linked_vhdl_architecture_names(output);
    if (!normalize_compiled_design(output)) {
        return { std::nullopt,
            "linked compiled-HIR bundle is structurally invalid" };
    }
    if (const auto error = structural_hir_error(output);
        !error.empty()) {
        return { std::nullopt,
            "linked compiled-HIR bundle is structurally invalid: "
                + error };
    }
    return { std::move(output), { } };
}

CompiledLinkResult link_compiled_designs(
    std::vector<CompiledDesign> inputs)
{
    if (inputs.empty()) {
        return { CompiledDesign { }, { } };
    }
    for (const auto& input : inputs) {
        if (const auto error = structural_hir_error(input);
            !error.empty()) {
            return { std::nullopt,
                "cannot link a structurally invalid compiled-HIR bundle: "
                    + error };
        }
    }
    if (inputs.size() == 1U) {
        return finalize_linked_design(std::move(inputs.front()));
    }
    std::stable_sort(inputs.begin(), inputs.end(),
        [](const CompiledDesign& left, const CompiledDesign& right) {
            return compiled_input_order_key(left)
                < compiled_input_order_key(right);
        });
    CompiledDesign output = std::move(inputs.front());
    auto records = output.semantics.records();
    UnitDefinitions units;
    std::set<std::string> classes;
    std::set<UdpKey> udps;
    std::string collision;
    if (!register_definitions(
            output, units, classes, udps, collision)) {
        return { std::nullopt, std::move(collision) };
    }
    for (std::size_t index = 1; index < inputs.size(); ++index) {
        auto input = std::move(inputs[index]);
        auto input_records = input.semantics.records();
        if (!register_definitions(
                input, units, classes, udps, collision)) {
            return { std::nullopt, std::move(collision) };
        }
        if (!relocation_fits(records, input_records)) {
            return { std::nullopt,
                "compiled-HIR ID relocation exceeds the 32-bit ID space" };
        }
        const auto relocation = relocation_for(records);
        relocate(input_records, relocation);
        relocate_hir(input.systemverilog_hir, relocation);
        relocate_hir(input.vhdl_hir, relocation);
        append_records(records, std::move(input_records));
        append_hir(
            output.systemverilog_hir,
            std::move(input.systemverilog_hir));
        append_hir(output.vhdl_hir, std::move(input.vhdl_hir));
    }
    auto model = Model::from_records(std::move(records));
    if (!model) {
        return { std::nullopt,
            "compiled-HIR ID relocation produced an invalid semantic model" };
    }
    output.semantics = std::move(*model);
    return finalize_linked_design(std::move(output));
}

} // namespace fsim::semantic
