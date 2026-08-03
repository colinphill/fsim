// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/model.hpp"

#include <stdexcept>
#include <utility>

namespace fsim::semantic {
namespace {

template <typename IdType>
[[nodiscard]] IdType next_id(const std::size_t size) {
    if (size >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error{"semantic identity space exhausted"};
    }
    return IdType::from_index(static_cast<std::uint32_t>(size));
}

template <typename IdType, typename Range>
void require_id(
    const IdType id,
    const Range& range,
    const char* const message) {
    if (!id.valid() || id.value() >= range.size()) {
        throw std::invalid_argument{message};
    }
}

} // namespace

SourceFileId Model::intern_source_file(
    std::string physical_name,
    std::string content_digest) {
    FileKey key{std::move(physical_name), std::move(content_digest)};
    if (const auto found = source_file_by_key_.find(key);
        found != source_file_by_key_.end()) {
        return found->second;
    }
    const auto id = next_id<SourceFileId>(source_files_.size());
    source_files_.push_back({id, key.physical_name, key.content_digest});
    source_file_by_name_.try_emplace(key.physical_name, id);
    source_file_by_key_.emplace(std::move(key), id);
    return id;
}

std::optional<SourceFileId> Model::find_source_file(
    const std::string_view physical_name) const noexcept {
    const auto found = source_file_by_name_.find(physical_name);
    return found == source_file_by_name_.end()
        ? std::nullopt
        : std::optional<SourceFileId>{found->second};
}

ExpansionId Model::intern_expansion(
    const std::span<const std::string> outermost_to_innermost) {
    std::optional<ExpansionId> parent;
    for (const auto& description : outermost_to_innermost) {
        ExpansionKey key{parent, description};
        if (const auto found = expansion_by_key_.find(key);
            found != expansion_by_key_.end()) {
            parent = found->second;
            continue;
        }
        const auto id = next_id<ExpansionId>(expansions_.size());
        expansions_.push_back({id, parent, description});
        expansion_by_key_.emplace(std::move(key), id);
        parent = id;
    }
    return parent.value_or(ExpansionId{});
}

SourceSpanId Model::intern_source_span(
    const SourceFileId file,
    std::string logical_name,
    const SourcePosition begin,
    const SourcePosition end,
    const std::optional<ExpansionId> expansion) {
    if (!file.valid() || file.value() >= source_files_.size()) {
        throw std::invalid_argument{"source span has an invalid source file"};
    }
    if (end.offset < begin.offset) {
        throw std::invalid_argument{"source span end precedes its beginning"};
    }
    if (expansion && (!expansion->valid()
        || expansion->value() >= expansions_.size())) {
        throw std::invalid_argument{"source span has an invalid expansion"};
    }
    SpanKey key{file, std::move(logical_name), begin, end, expansion};
    if (const auto found = source_span_by_key_.find(key);
        found != source_span_by_key_.end()) {
        return found->second;
    }
    const auto id = next_id<SourceSpanId>(source_spans_.size());
    source_spans_.push_back(
        {id, file, key.logical_name, begin, end, expansion});
    source_span_by_key_.emplace(std::move(key), id);
    return id;
}

OriginId Model::add_origin(
    const OriginKind kind,
    const SourceSpanId source,
    const std::optional<OriginId> parent,
    std::string detail) {
    require_id(source, source_spans_, "origin has an invalid source span");
    if (parent) {
        require_id(*parent, origins_, "origin has an invalid parent");
    }
    const auto id = next_id<OriginId>(origins_.size());
    origins_.push_back({id, kind, source, parent, std::move(detail)});
    return id;
}

UnitId Model::add_unit(
    const Language language,
    const UnitKind kind,
    std::string library,
    std::string name,
    std::string secondary_name,
    const SourceSpanId source,
    const OriginId origin) {
    require_id(source, source_spans_, "unit has an invalid source span");
    require_id(origin, origins_, "unit has an invalid origin");
    const auto unit_id = next_id<UnitId>(units_.size());
    const auto scope_id = next_id<ScopeId>(scopes_.size());
    scopes_.push_back(
        {scope_id, unit_id, std::nullopt, name, source, origin});
    units_.push_back({
        unit_id,
        scope_id,
        language,
        kind,
        std::move(library),
        std::move(name),
        std::move(secondary_name),
        source,
        origin});
    return unit_id;
}

ScopeId Model::add_scope(
    const UnitId unit,
    const std::optional<ScopeId> parent,
    std::string name,
    const SourceSpanId source,
    const OriginId origin) {
    require_id(unit, units_, "scope has an invalid unit");
    if (parent) {
        require_id(*parent, scopes_, "scope has an invalid parent");
    }
    require_id(source, source_spans_, "scope has an invalid source span");
    require_id(origin, origins_, "scope has an invalid origin");
    const auto id = next_id<ScopeId>(scopes_.size());
    scopes_.push_back(
        {id, unit, parent, std::move(name), source, origin});
    return id;
}

TypeId Model::add_type(
    const ScopeId scope,
    const TypeKind kind,
    std::string name,
    TypeReference base,
    const SourceSpanId source,
    const OriginId origin) {
    require_id(scope, scopes_, "type has an invalid scope");
    require_id(base.source, source_spans_, "type has an invalid base span");
    // Type declarations may legally reference a later declaration in the
    // same canonical batch. The builder preassigns that dense target ID.
    require_id(source, source_spans_, "type has an invalid source span");
    require_id(origin, origins_, "type has an invalid origin");
    const auto id = next_id<TypeId>(types_.size());
    types_.push_back({
        id,
        scope,
        kind,
        std::move(name),
        std::move(base),
        source,
        origin});
    return id;
}

ValueId Model::add_value(
    const ScopeId scope,
    const ValueKind kind,
    std::string name,
    TypeReference type,
    const SourceSpanId source,
    const OriginId origin) {
    require_id(scope, scopes_, "value has an invalid scope");
    require_id(type.source, source_spans_, "value has an invalid type span");
    if (type.target.valid()) {
        require_id(type.target, types_, "value has an invalid type target");
    }
    require_id(source, source_spans_, "value has an invalid source span");
    require_id(origin, origins_, "value has an invalid origin");
    const auto id = next_id<ValueId>(values_.size());
    values_.push_back({
        id,
        scope,
        kind,
        std::move(name),
        std::move(type),
        source,
        origin});
    return id;
}

InstanceId Model::add_instance(
    const ScopeId scope,
    std::string name,
    std::string target,
    const SourceSpanId source,
    const OriginId origin) {
    require_id(scope, scopes_, "instance has an invalid scope");
    require_id(source, source_spans_, "instance has an invalid source span");
    require_id(origin, origins_, "instance has an invalid origin");
    const auto id = next_id<InstanceId>(instances_.size());
    instances_.push_back({
        id,
        scope,
        std::move(name),
        std::move(target),
        source,
        origin});
    return id;
}

DeclarationId Model::add_declaration(
    const ScopeId scope,
    const DeclarationKind kind,
    std::string name,
    const SourceSpanId source,
    const OriginId origin) {
    require_id(scope, scopes_, "declaration has an invalid scope");
    require_id(source, source_spans_, "declaration has an invalid source span");
    require_id(origin, origins_, "declaration has an invalid origin");
    const auto id = next_id<DeclarationId>(declarations_.size());
    declarations_.push_back(
        {id, scope, kind, std::move(name), source, origin});
    return id;
}

ExpressionId Model::add_expression_identity(
    const ScopeId scope,
    const SourceSpanId source,
    const OriginId origin) {
    require_id(scope, scopes_, "expression has an invalid scope");
    require_id(source, source_spans_, "expression has an invalid source span");
    require_id(origin, origins_, "expression has an invalid origin");
    const auto id = next_id<ExpressionId>(expression_identities_.size());
    expression_identities_.push_back({id, scope, source, origin});
    return id;
}

StatementId Model::add_statement_identity(
    const ScopeId scope,
    const SourceSpanId source,
    const OriginId origin) {
    require_id(scope, scopes_, "statement has an invalid scope");
    require_id(source, source_spans_, "statement has an invalid source span");
    require_id(origin, origins_, "statement has an invalid origin");
    const auto id = next_id<StatementId>(statement_identities_.size());
    statement_identities_.push_back({id, scope, source, origin});
    return id;
}

ProcessId Model::add_process_identity(
    const ScopeId scope,
    std::string name,
    const SourceSpanId source,
    const OriginId origin) {
    require_id(scope, scopes_, "process has an invalid scope");
    require_id(source, source_spans_, "process has an invalid source span");
    require_id(origin, origins_, "process has an invalid origin");
    const auto id = next_id<ProcessId>(process_identities_.size());
    process_identities_.push_back(
        {id, scope, std::move(name), source, origin});
    return id;
}

const std::vector<SourceFile>& Model::source_files() const noexcept {
    return source_files_;
}
const std::vector<Expansion>& Model::expansions() const noexcept {
    return expansions_;
}
const std::vector<SourceSpan>& Model::source_spans() const noexcept {
    return source_spans_;
}
const std::vector<Origin>& Model::origins() const noexcept { return origins_; }
const std::vector<Scope>& Model::scopes() const noexcept { return scopes_; }
const std::vector<Unit>& Model::units() const noexcept { return units_; }
const std::vector<Type>& Model::types() const noexcept { return types_; }
const std::vector<Value>& Model::values() const noexcept { return values_; }
const std::vector<Instance>& Model::instances() const noexcept {
    return instances_;
}
const std::vector<Declaration>& Model::declarations() const noexcept {
    return declarations_;
}
const std::vector<ExpressionIdentity>&
Model::expression_identities() const noexcept {
    return expression_identities_;
}
const std::vector<StatementIdentity>&
Model::statement_identities() const noexcept {
    return statement_identities_;
}
const std::vector<ProcessIdentity>&
Model::process_identities() const noexcept {
    return process_identities_;
}

} // namespace fsim::semantic
