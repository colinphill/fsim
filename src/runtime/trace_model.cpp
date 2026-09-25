// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/trace_model.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::runtime {
namespace {

    struct TracePathHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(
            const std::string_view value) const noexcept
        {
            return std::hash<std::string_view> { }(value);
        }
    };

    struct TracePathEqual {
        using is_transparent = void;

        [[nodiscard]] bool operator()(
            const std::string_view left,
            const std::string_view right) const noexcept
        {
            return left == right;
        }
    };

    template <typename Id>
    [[nodiscard]] Id next_id(const std::size_t size, const char* description)
    {
        if (size == std::numeric_limits<std::uint64_t>::max()) {
            throw std::length_error(std::string { "too many trace " } + description);
        }
        return Id { static_cast<std::uint64_t>(size + 1U) };
    }

    struct TraceName {
        std::string_view path;
        std::string_view scope;
        std::string_view reference;
    };

    [[nodiscard]] TraceName split_name(const std::string_view name)
    {
        if (name.empty()) {
            throw std::invalid_argument("trace declaration name cannot be empty");
        }
        std::size_t component_start = 0;
        while (component_start < name.size()) {
            const auto separator = name.find('.', component_start);
            const auto component_end
                = separator == std::string_view::npos ? name.size() : separator;
            if (component_end == component_start) {
                throw std::invalid_argument(
                    "trace declaration name has an empty component");
            }
            if (separator == std::string_view::npos) {
                break;
            }
            component_start = separator + 1U;
        }
        const auto separator = name.find_last_of('.');
        if (separator == name.size() - 1U) {
            throw std::invalid_argument(
                "trace declaration name has an empty component");
        }
        return {
            name,
            separator == std::string_view::npos
                ? std::string_view { }
                : name.substr(0, separator),
            separator == std::string_view::npos
                    ? name
                    : name.substr(separator + 1U)
        };
    }

    void require_source_metadata(const TraceSourceMetadata& source)
    {
        const auto valid_text = [](const std::string_view value) {
            return !value.empty()
                && value.find('\0') == std::string_view::npos;
        };
        if (source.language == TraceLanguage::Unknown) {
            if (!source.root_identity.empty() || !source.library.empty()
                || !source.owner_identity.empty()) {
                throw std::invalid_argument(
                    "trace source provenance lacks its language");
            }
            return;
        }
        if (!valid_text(source.root_identity)
            || !valid_text(source.library)
            || !valid_text(source.owner_identity)) {
            throw std::invalid_argument(
                "trace source provenance is incomplete");
        }
        if ((source.language == TraceLanguage::SystemC)
            != (source.kind == TraceSourceKind::SystemC)) {
            throw std::invalid_argument(
                "trace source language conflicts with its kind");
        }
    }

    void require_source_ownership(
        const TraceSourceMetadata& source,
        const std::string_view hierarchical_name)
    {
        if (source.language == TraceLanguage::Unknown) {
            return;
        }
        if (hierarchical_name != source.root_identity
            && !(hierarchical_name.size() > source.root_identity.size()
                && hierarchical_name.starts_with(source.root_identity)
                && hierarchical_name[source.root_identity.size()] == '.')) {
            throw std::invalid_argument(
                "trace source root does not own its declaration");
        }
    }

} // namespace

TracePathName::TracePathName(
    semantic::HierarchyPathTable paths,
    const semantic::HierarchyPathId id)
    : paths_(std::move(paths))
    , id_(id)
{
}

TracePathName::TracePathName(TracePathName&& other) noexcept
    : paths_(std::move(other.paths_))
    , id_(other.id_)
{
    other.id_ = { };
}

TracePathName& TracePathName::operator=(TracePathName&& other) noexcept
{
    if (this != &other) {
        paths_ = std::move(other.paths_);
        id_ = other.id_;
        other.id_ = { };
    }
    return *this;
}

std::string_view TracePathName::view() const
{
    return id_.valid() ? paths_.view(id_) : std::string_view { };
}

std::size_t TracePathName::size() const
{
    return view().size();
}

bool TracePathName::empty() const
{
    return view().empty();
}

const char* TracePathName::data() const
{
    return view().data();
}

std::size_t TracePathName::find(const char value) const
{
    return view().find(value);
}

std::size_t TracePathName::find(const std::string_view value) const
{
    return view().find(value);
}

bool TracePathName::starts_with(const std::string_view value) const
{
    return view().starts_with(value);
}

bool TracePathName::ends_with(const std::string_view value) const
{
    return view().ends_with(value);
}

char TracePathName::operator[](const std::size_t index) const
{
    return view()[index];
}

TracePathName::operator std::string_view() const
{
    return view();
}

TracePathName::operator std::string() const
{
    return std::string { view() };
}

bool operator==(
    const TracePathName& left,
    const TracePathName& right)
{
    return left.view() == right.view();
}

bool operator==(
    const TracePathName& left,
    const std::string_view right)
{
    return left.view() == right;
}

bool operator==(
    const TracePathName& left,
    const std::string& right)
{
    return left.view() == right;
}

bool operator==(
    const TracePathName& left,
    const char* right)
{
    return right && left.view() == std::string_view { right };
}

struct TraceDeclarationModel::Impl {
    std::vector<TraceScopeDeclaration> scopes;
    std::vector<TraceTypeDeclaration> types;
    std::vector<TraceSourceDeclaration> sources;
    std::vector<TraceVariableDeclaration> variables;
    std::vector<TraceAliasDeclaration> aliases;
    std::vector<TraceDeclarationEntry> entries;
    std::unordered_map<std::uint64_t, std::size_t> variable_indices;
    std::unordered_map<std::uint64_t, std::size_t> alias_indices;
};

struct TraceDeclarationBuilder::Impl {
    struct PathReference {
        semantic::HierarchyPathId id;
        bool is_fallback { false };
    };

    std::shared_ptr<TraceDeclarationModel::Impl> model
        = std::make_shared<TraceDeclarationModel::Impl>();
    semantic::HierarchyPathTable source_paths;
    semantic::HierarchyPathTable::Builder fallback_paths;
    std::vector<PathReference> scope_paths;
    std::vector<PathReference> source_path_refs;
    std::vector<PathReference> variable_paths;
    std::vector<PathReference> alias_paths;
    std::unordered_map<std::string_view, TraceScopeId,
        TracePathHash, TracePathEqual> scope_ids;
    std::unordered_map<std::string_view, TraceSignalId,
        TracePathHash, TracePathEqual> signal_ids;

    explicit Impl(semantic::HierarchyPathTable paths)
        : source_paths(std::move(paths))
    {
    }

    [[nodiscard]] PathReference intern_path(const std::string_view path)
    {
        if (const auto found = source_paths.find(path)) {
            return { *found, false };
        }
        return { fallback_paths.intern(path), true };
    }

    [[nodiscard]] std::string_view path_view(
        const PathReference path) const
    {
        return path.is_fallback
            ? fallback_paths.view(path.id)
            : source_paths.view(path.id);
    }

    [[nodiscard]] TraceSourceId add_source(
        const TraceSourceMetadata& metadata,
        const PathReference name)
    {
        const auto id = next_id<TraceSourceId>(model->sources.size(), "sources");
        model->sources.push_back(TraceSourceDeclaration { id,
            metadata.kind,
            metadata.language,
            metadata.root_identity,
            metadata.library,
            metadata.owner_identity,
            TracePathName { } });
        try {
            source_path_refs.push_back(name);
        } catch (...) {
            model->sources.pop_back();
            throw;
        }
        return id;
    }

    [[nodiscard]] TraceScopeId add_scopes(
        const std::string_view scope,
        const TraceSourceId source)
    {
        if (scope.empty()) {
            return { };
        }
        TraceScopeId parent;
        std::size_t component_start = 0;
        while (component_start < scope.size()) {
            const auto separator = scope.find('.', component_start);
            const auto component_end
                = separator == std::string_view::npos
                ? scope.size()
                : separator;
            const auto reference = intern_path(scope.substr(0, component_end));
            const auto path = path_view(reference);
            if (const auto found = scope_ids.find(path);
                found != scope_ids.end()) {
                parent = found->second;
            } else {
                const auto id = next_id<TraceScopeId>(
                    model->scopes.size(), "scopes");
                model->scopes.push_back(TraceScopeDeclaration { id,
                    parent,
                    source,
                    std::string {
                        scope.substr(component_start,
                            component_end - component_start) },
                    TracePathName { } });
                try {
                    scope_paths.push_back(reference);
                } catch (...) {
                    model->scopes.pop_back();
                    throw;
                }
                try {
                    scope_ids.emplace(path, id);
                } catch (...) {
                    scope_paths.pop_back();
                    model->scopes.pop_back();
                    throw;
                }
                parent = id;
            }
            if (separator == std::string_view::npos) {
                break;
            }
            component_start = separator + 1U;
        }
        return parent;
    }

    [[nodiscard]] TraceTypeId add_type(
        const std::size_t width,
        const SystemVerilogScalarKind scalar_kind,
        const TraceTypeKind requested_kind,
        const std::string_view canonical_metadata)
    {
        const auto kind = requested_kind == TraceTypeKind::Packed
                && scalar_kind != SystemVerilogScalarKind::None
            ? TraceTypeKind::SystemVerilogScalar
            : requested_kind;
        const auto found = std::ranges::find_if(
            model->types,
            [&](const auto& type) {
                return type.kind == kind && type.width == width
                    && type.scalar_kind == scalar_kind
                    && type.canonical_metadata == canonical_metadata;
            });
        if (found != model->types.end()) {
            return found->id;
        }
        const auto id = next_id<TraceTypeId>(model->types.size(), "types");
        model->types.push_back({ id, kind, width, scalar_kind,
            std::string { canonical_metadata } });
        return id;
    }

    [[nodiscard]] TraceSignalId add_signal_name(const std::string_view name)
    {
        if (signal_ids.contains(name)) {
            throw std::invalid_argument(
                "duplicate trace declaration name: " + std::string { name });
        }
        const auto id = next_id<TraceSignalId>(model->entries.size(), "signals");
        signal_ids.emplace(name, id);
        return id;
    }
};

TraceDeclarationModel::TraceDeclarationModel(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl))
{
}

TraceDeclarationModel::~TraceDeclarationModel() = default;

std::span<const TraceScopeDeclaration>
TraceDeclarationModel::scopes() const noexcept
{
    return impl_->scopes;
}

std::span<const TraceTypeDeclaration>
TraceDeclarationModel::types() const noexcept
{
    return impl_->types;
}

std::span<const TraceSourceDeclaration>
TraceDeclarationModel::sources() const noexcept
{
    return impl_->sources;
}

std::span<const TraceVariableDeclaration>
TraceDeclarationModel::variables() const noexcept
{
    return impl_->variables;
}

std::span<const TraceAliasDeclaration>
TraceDeclarationModel::aliases() const noexcept
{
    return impl_->aliases;
}

std::span<const TraceDeclarationEntry>
TraceDeclarationModel::entries() const noexcept
{
    return impl_->entries;
}

const TraceVariableDeclaration&
TraceDeclarationModel::variable(const TraceSignalId id) const
{
    const auto found = impl_->variable_indices.find(id.value);
    if (found == impl_->variable_indices.end()) {
        throw std::out_of_range("trace variable identity is not declared");
    }
    return impl_->variables[found->second];
}

const TraceAliasDeclaration&
TraceDeclarationModel::alias(const TraceSignalId id) const
{
    const auto found = impl_->alias_indices.find(id.value);
    if (found == impl_->alias_indices.end()) {
        throw std::out_of_range("trace alias identity is not declared");
    }
    return impl_->aliases[found->second];
}

const TraceTypeDeclaration&
TraceDeclarationModel::type(const TraceTypeId id) const
{
    if (id.value == 0 || id.value > impl_->types.size()) {
        throw std::out_of_range("trace type identity is not declared");
    }
    return impl_->types[static_cast<std::size_t>(id.value - 1U)];
}

const TraceSourceDeclaration&
TraceDeclarationModel::source(const TraceSourceId id) const
{
    if (id.value == 0 || id.value > impl_->sources.size()) {
        throw std::out_of_range("trace source identity is not declared");
    }
    return impl_->sources[static_cast<std::size_t>(id.value - 1U)];
}

TraceDeclarationBuilder::TraceDeclarationBuilder()
    : TraceDeclarationBuilder(semantic::HierarchyPathTable { })
{
}

TraceDeclarationBuilder::TraceDeclarationBuilder(
    semantic::HierarchyPathTable paths)
    : impl_(std::make_unique<Impl>(std::move(paths)))
{
}

TraceDeclarationBuilder::~TraceDeclarationBuilder() = default;
TraceDeclarationBuilder::TraceDeclarationBuilder(
    TraceDeclarationBuilder&&) noexcept = default;
TraceDeclarationBuilder& TraceDeclarationBuilder::operator=(
    TraceDeclarationBuilder&&) noexcept = default;

TraceSignalId TraceDeclarationBuilder::add_variable(
    const std::string_view hierarchical_name,
    const std::size_t width,
    const SystemVerilogScalarKind scalar_kind,
    const TraceSourceKind source_kind)
{
    if (!impl_) {
        throw std::logic_error("trace declaration builder was already frozen");
    }
    return add_typed_variable(
        hierarchical_name,
        scalar_kind == SystemVerilogScalarKind::None
            ? TraceTypeKind::Packed
            : TraceTypeKind::SystemVerilogScalar,
        width, scalar_kind, { }, source_kind);
}

TraceSignalId TraceDeclarationBuilder::add_typed_variable(
    const std::string_view hierarchical_name,
    const TraceTypeKind type_kind,
    const std::size_t width,
    const SystemVerilogScalarKind scalar_kind,
    const std::string_view canonical_metadata,
    const TraceSourceKind source_kind)
{
    TraceSourceMetadata source;
    source.kind = source_kind;
    return add_typed_variable(hierarchical_name, type_kind, width, scalar_kind,
        canonical_metadata, source);
}

TraceSignalId TraceDeclarationBuilder::add_typed_variable(
    const std::string_view hierarchical_name,
    const TraceTypeKind type_kind,
    const std::size_t width,
    const SystemVerilogScalarKind scalar_kind,
    const std::string_view canonical_metadata,
    const TraceSourceMetadata& source_metadata)
{
    if (!impl_) {
        throw std::logic_error("trace declaration builder was already frozen");
    }
    if ((type_kind == TraceTypeKind::SystemVerilogString && width != 0)
        || (type_kind != TraceTypeKind::SystemVerilogString && width == 0)) {
        throw std::invalid_argument("trace variable has an invalid typed width");
    }
    if (type_kind == TraceTypeKind::SystemVerilogScalar
        && scalar_kind == SystemVerilogScalarKind::None) {
        throw std::invalid_argument("trace scalar type lacks its scalar kind");
    }
    if (type_kind != TraceTypeKind::SystemVerilogScalar
        && scalar_kind != SystemVerilogScalarKind::None) {
        throw std::invalid_argument("trace non-scalar type has a scalar kind");
    }
    if ((type_kind == TraceTypeKind::Enumeration
            || type_kind == TraceTypeKind::VhdlPhysical
            || type_kind == TraceTypeKind::VhdlTime
            || type_kind == TraceTypeKind::VhdlLogic9
            || type_kind == TraceTypeKind::TypedLeaf)
        && canonical_metadata.empty()) {
        throw std::invalid_argument("trace extended type lacks canonical metadata");
    }
    require_source_metadata(source_metadata);
    const auto name = split_name(hierarchical_name);
    require_source_ownership(source_metadata, name.path);
    const auto path = impl_->intern_path(name.path);
    const auto path_view = impl_->path_view(path);
    const auto id = impl_->add_signal_name(path_view);
    const auto source = impl_->add_source(source_metadata, path);
    const auto scope = impl_->add_scopes(name.scope, source);
    const auto type = impl_->add_type(
        width, scalar_kind, type_kind, canonical_metadata);
    impl_->model->variable_indices.emplace(
        id.value, impl_->model->variables.size());
    impl_->model->variables.push_back(TraceVariableDeclaration {
        id, scope, type, source, TracePathName { },
        std::string { name.reference } });
    impl_->variable_paths.push_back(path);
    impl_->model->entries.push_back({ id, TraceDeclarationKind::Variable });
    return id;
}

TraceSignalId TraceDeclarationBuilder::add_alias(
    const std::string_view hierarchical_name,
    const TraceSignalId target,
    const TraceSourceKind source_kind)
{
    TraceSourceMetadata source;
    source.kind = source_kind;
    return add_alias(hierarchical_name, target, source);
}

TraceSignalId TraceDeclarationBuilder::add_alias(
    const std::string_view hierarchical_name,
    const TraceSignalId target,
    const TraceSourceMetadata& source_metadata)
{
    if (!impl_) {
        throw std::logic_error("trace declaration builder was already frozen");
    }
    if (!impl_->model->variable_indices.contains(target.value)) {
        throw std::invalid_argument("trace alias target is not a variable");
    }
    require_source_metadata(source_metadata);
    const auto name = split_name(hierarchical_name);
    require_source_ownership(source_metadata, name.path);
    const auto path = impl_->intern_path(name.path);
    const auto path_view = impl_->path_view(path);
    const auto id = impl_->add_signal_name(path_view);
    const auto source = impl_->add_source(source_metadata, path);
    const auto scope = impl_->add_scopes(name.scope, source);
    impl_->model->alias_indices.emplace(id.value, impl_->model->aliases.size());
    impl_->model->aliases.push_back(TraceAliasDeclaration {
        id, scope, target, source, TracePathName { },
        std::string { name.reference } });
    impl_->alias_paths.push_back(path);
    impl_->model->entries.push_back({ id, TraceDeclarationKind::Alias });
    return id;
}

TraceDeclarationModel TraceDeclarationBuilder::freeze() &&
{
    if (!impl_) {
        throw std::logic_error("trace declaration builder was already frozen");
    }
    if (impl_->scope_paths.size() != impl_->model->scopes.size()
        || impl_->source_path_refs.size() != impl_->model->sources.size()
        || impl_->variable_paths.size() != impl_->model->variables.size()
        || impl_->alias_paths.size() != impl_->model->aliases.size()) {
        throw std::logic_error("trace path references are not aligned");
    }
    auto fallback_paths = std::move(impl_->fallback_paths).freeze();
    const auto declaration_path = [&](const Impl::PathReference path) {
        const auto& owner = path.is_fallback
            ? fallback_paths
            : impl_->source_paths;
        return TracePathName { owner, path.id };
    };
    for (std::size_t index = 0; index < impl_->model->scopes.size(); ++index) {
        impl_->model->scopes[index].path
            = declaration_path(impl_->scope_paths[index]);
    }
    for (std::size_t index = 0; index < impl_->model->sources.size(); ++index) {
        impl_->model->sources[index].canonical_name
            = declaration_path(impl_->source_path_refs[index]);
    }
    for (std::size_t index = 0; index < impl_->model->variables.size(); ++index) {
        impl_->model->variables[index].hierarchical_name
            = declaration_path(impl_->variable_paths[index]);
    }
    for (std::size_t index = 0; index < impl_->model->aliases.size(); ++index) {
        impl_->model->aliases[index].hierarchical_name
            = declaration_path(impl_->alias_paths[index]);
    }
    std::shared_ptr<const TraceDeclarationModel::Impl> model = impl_->model;
    impl_.reset();
    return TraceDeclarationModel { std::move(model) };
}

bool trace_event_precedes(
    const TraceEvent& left,
    const TraceEvent& right) noexcept
{
    return std::tuple {
        left.time,
        left.delta,
        static_cast<std::uint8_t>(left.region),
        left.sequence,
        left.signal.value
    }
    < std::tuple {
          right.time,
          right.delta,
          static_cast<std::uint8_t>(right.region),
          right.sequence,
          right.signal.value
      };
}

} // namespace fsim::runtime
