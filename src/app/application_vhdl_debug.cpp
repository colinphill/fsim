// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace fsim::app {
namespace {

    std::atomic<std::uint64_t> next_vhdl_debug_simulation { 1U };

    [[nodiscard]] std::optional<runtime::VhdlVhpiSourceLocation> source_location(
        const semantic::Model& model,
        const std::optional<semantic::SourceSpanId> source)
    {
        if (!source || source->value() >= model.source_spans().size()) {
            return std::nullopt;
        }
        const auto& span = model.source_spans()[source->value()];
        if (span.file.value() >= model.source_files().size()) {
            return std::nullopt;
        }
        return runtime::VhdlVhpiSourceLocation {
            model.source_files()[span.file.value()].physical_name,
            span.begin.line, span.begin.column
        };
    }

    [[nodiscard]] const VhdlUnitProvenance* unit_provenance(
        const std::span<const VhdlUnitProvenance> provenance,
        const semantic::UnitId unit)
    {
        const auto found = std::ranges::find(
            provenance, unit,
            &VhdlUnitProvenance::unit);
        return found == provenance.end()
            ? nullptr
            : &*found;
    }

    [[nodiscard]] std::vector<runtime::VhdlVhpiPackageProvenance>
    vhpi_packages(const VhdlUnitProvenance& provenance)
    {
        std::vector<runtime::VhdlVhpiPackageProvenance> result;
        result.reserve(provenance.package_dependencies.size());
        for (const auto& package : provenance.package_dependencies) {
            result.push_back({ package.standard,
                package.predefined_environment, package.package,
                package.revision, package.source_digest });
        }
        return result;
    }

    [[nodiscard]] bool basic_identifier(const std::string_view value)
    {
        if (value.empty()
            || std::isalpha(static_cast<unsigned char>(value.front())) == 0
            || value.back() == '_') {
            return false;
        }
        bool underscore { };
        for (const auto character : value) {
            if (character == '_') {
                if (underscore) {
                    return false;
                }
                underscore = true;
                continue;
            }
            if (std::isalnum(static_cast<unsigned char>(character)) == 0) {
                return false;
            }
            underscore = false;
        }
        return true;
    }

    [[nodiscard]] std::string vhpi_name(const std::string_view value)
    {
        if (basic_identifier(value)
            || (value.size() >= 3U && value.front() == '\\'
                && value.back() == '\\')) {
            return std::string { value };
        }
        std::string result { "\\" };
        for (const auto character : value) {
            result.push_back(character);
            if (character == '\\') {
                result.push_back(character);
            }
        }
        result.push_back('\\');
        return result;
    }

    [[nodiscard]] std::vector<std::string> path_segments(
        const std::string_view path)
    {
        std::vector<std::string> result;
        std::size_t begin { };
        bool extended { };
        for (std::size_t index = 0U; index < path.size(); ++index) {
            if (path[index] == '\\') {
                if (extended && index + 1U < path.size()
                    && path[index + 1U] == '\\') {
                    ++index;
                    continue;
                }
                extended = !extended;
                continue;
            }
            if (path[index] == '.' && !extended) {
                result.emplace_back(path.substr(begin, index - begin));
                begin = index + 1U;
            }
        }
        result.emplace_back(path.substr(begin));
        return result;
    }

    [[nodiscard]] std::string occurrence_path(
        const std::string_view instance_path,
        const std::string_view local_path)
    {
        if (local_path.empty() || local_path == instance_path) {
            return std::string { instance_path };
        }
        if (local_path.starts_with(instance_path)
            && local_path.size() > instance_path.size()
            && local_path[instance_path.size()] == '.') {
            return std::string { local_path };
        }
        return std::string { instance_path } + '.' + std::string { local_path };
    }

    [[nodiscard]] std::string leaf_path_name(const std::string_view path)
    {
        const auto segments = path_segments(path);
        return segments.empty() ? std::string { path } : segments.back();
    }

    [[nodiscard]] std::string parent_path_name(const std::string_view path)
    {
        auto segments = path_segments(path);
        if (segments.size() <= 1U) {
            return { };
        }
        segments.pop_back();
        std::string result;
        for (const auto& segment : segments) {
            if (!result.empty()) {
                result += '.';
            }
            result += segment;
        }
        return result;
    }

    [[nodiscard]] runtime::VhdlVhpiObjectKind declaration_kind(
        const semantic::vhdl::Declaration& declaration)
    {
        using Form = semantic::vhdl::DeclarationForm;
        using Object = semantic::vhdl::ObjectClass;
        if (declaration.form == Form::type) {
            return runtime::VhdlVhpiObjectKind::Type;
        }
        if (declaration.form == Form::subtype) {
            return runtime::VhdlVhpiObjectKind::Subtype;
        }
        switch (declaration.object_class) {
        case Object::signal:
            return runtime::VhdlVhpiObjectKind::Signal;
        case Object::variable:
            return runtime::VhdlVhpiObjectKind::Variable;
        case Object::file:
            return runtime::VhdlVhpiObjectKind::File;
        case Object::constant:
            return runtime::VhdlVhpiObjectKind::Constant;
        }
        return runtime::VhdlVhpiObjectKind::Constant;
    }

    [[nodiscard]] VhdlDebugDeclarationKind debug_kind(
        const semantic::vhdl::Declaration& declaration,
        const semantic::vhdl::TypeDefinition* type)
    {
        using Object = semantic::vhdl::ObjectClass;
        using Type = semantic::vhdl::TypeForm;
        if (type) {
            if (type->form == Type::file) {
                return VhdlDebugDeclarationKind::file;
            }
            if (type->form == Type::access) {
                return VhdlDebugDeclarationKind::access_type;
            }
            if (type->form == Type::protected_type
                || type->form == Type::protected_body) {
                return VhdlDebugDeclarationKind::protected_type;
            }
            if (type->form == Type::physical) {
                return VhdlDebugDeclarationKind::physical_type;
            }
        }
        switch (declaration.object_class) {
        case Object::signal:
            return VhdlDebugDeclarationKind::signal;
        case Object::variable:
            return VhdlDebugDeclarationKind::variable;
        case Object::file:
            return VhdlDebugDeclarationKind::file;
        case Object::constant:
            return VhdlDebugDeclarationKind::constant;
        }
        return VhdlDebugDeclarationKind::other;
    }

    [[nodiscard]] std::string_view debug_kind_name(
        const VhdlDebugDeclarationKind kind)
    {
        switch (kind) {
        case VhdlDebugDeclarationKind::signal:
            return "signal";
        case VhdlDebugDeclarationKind::variable:
            return "variable";
        case VhdlDebugDeclarationKind::constant:
            return "constant";
        case VhdlDebugDeclarationKind::file:
            return "file";
        case VhdlDebugDeclarationKind::access_type:
            return "access-type";
        case VhdlDebugDeclarationKind::protected_type:
            return "protected-type";
        case VhdlDebugDeclarationKind::physical_type:
            return "physical-type";
        case VhdlDebugDeclarationKind::other:
            return "other";
        }
        return "other";
    }

    [[nodiscard]] std::string_view psl_outcome_name(
        const runtime::VhdlPslAttemptOutcome outcome)
    {
        switch (outcome) {
        case runtime::VhdlPslAttemptOutcome::pending:
            return "pending";
        case runtime::VhdlPslAttemptOutcome::pass:
            return "pass";
        case runtime::VhdlPslAttemptOutcome::failure:
            return "failure";
        case runtime::VhdlPslAttemptOutcome::vacuous:
            return "vacuous";
        case runtime::VhdlPslAttemptOutcome::aborted:
            return "aborted";
        }
        return "pending";
    }

    [[nodiscard]] std::string_view psl_kind_name(
        const runtime::VhdlPslDirectiveKind kind)
    {
        switch (kind) {
        case runtime::VhdlPslDirectiveKind::assertion:
            return "assert";
        case runtime::VhdlPslDirectiveKind::assumption:
            return "assume";
        case runtime::VhdlPslDirectiveKind::restriction:
            return "restrict";
        case runtime::VhdlPslDirectiveKind::cover:
            return "cover";
        }
        return "assert";
    }

    [[nodiscard]] std::string_view coverage_kind_name(
        const ConcurrentAssertionCoverageKind kind)
    {
        switch (kind) {
        case ConcurrentAssertionCoverageKind::assertion:
            return "assert";
        case ConcurrentAssertionCoverageKind::assumption:
            return "assume";
        case ConcurrentAssertionCoverageKind::cover:
            return "cover";
        case ConcurrentAssertionCoverageKind::restriction:
            return "restrict";
        }
        return "assert";
    }

    [[nodiscard]] std::string type_name(
        const semantic::vhdl::Declaration& declaration,
        const semantic::vhdl::TypeDefinition* type)
    {
        if (type && !type->name.empty()) {
            return type->name;
        }
        if (declaration.subtype) {
            return declaration.subtype->type_mark.spelling;
        }
        return { };
    }

    [[nodiscard]] const semantic::vhdl::Unit* unit_for(
        const semantic::vhdl::Hir& hir, const semantic::UnitId id)
    {
        const auto found = std::ranges::find(hir.units(), id,
            &semantic::vhdl::Unit::id);
        return found == hir.units().end() ? nullptr : &*found;
    }

    [[nodiscard]] const semantic::vhdl::Declaration* declaration_for(
        const semantic::vhdl::Hir& hir, const semantic::DeclarationId id)
    {
        const auto found = std::ranges::find(hir.declarations(), id,
            &semantic::vhdl::Declaration::id);
        return found == hir.declarations().end() ? nullptr : &*found;
    }

    [[nodiscard]] const semantic::vhdl::TypeDefinition* type_for(
        const semantic::vhdl::Hir& hir,
        const semantic::vhdl::Declaration& declaration)
    {
        if (!declaration.declared_type) {
            return nullptr;
        }
        const auto found = std::ranges::find(hir.types(), *declaration.declared_type,
            &semantic::vhdl::TypeDefinition::id);
        return found == hir.types().end() ? nullptr : &*found;
    }

    [[nodiscard]] const semantic::vhdl::Process* vhdl_process_for(
        const semantic::vhdl::Hir& hir,
        const semantic::design::ProcessOccurrence& process)
    {
        const auto expected_name = leaf_path_name(process.name);
        if (process.source_process) {
            const auto identified = std::ranges::find(hir.processes(),
                *process.source_process, &semantic::vhdl::Process::id);
            if (identified != hir.processes().end()
                && identified->name == expected_name) {
                return &*identified;
            }
        }
        const semantic::vhdl::Process* result { };
        for (const auto& candidate : hir.processes()) {
            if (candidate.name != expected_name) {
                continue;
            }
            if (result) {
                return nullptr;
            }
            result = &candidate;
        }
        return result;
    }

    void add_payload(std::size_t& payload, const std::string_view value,
        const std::size_t maximum)
    {
        if (value.size() > maximum - payload) {
            throw VhdlDebugError { "VHDL debug payload ceiling exceeded" };
        }
        payload += value.size();
    }

} // namespace

namespace application_detail {

    std::unique_ptr<runtime::VhdlVhpiObjectRegistry> make_vhdl_debug_registry(
        const BuiltProject& project)
    {
        auto identity = next_vhdl_debug_simulation.fetch_add(
            1U, std::memory_order_relaxed);
        if (identity == 0U) {
            identity = next_vhdl_debug_simulation.fetch_add(
                1U, std::memory_order_relaxed);
        }
        auto registry
            = std::make_unique<runtime::VhdlVhpiObjectRegistry>(identity);
        if (!registry->valid()) {
            throw VhdlDebugError { "VHDL VHPI registry identity exhausted" };
        }
        struct ScopeProvenance {
            std::string standard;
            std::string predefined_environment;
            std::string compatibility_profile;
            std::vector<runtime::VhdlVhpiPackageProvenance> packages;
        };
        std::map<std::string, ScopeProvenance, std::less<>> provenance_by_path;
        for (const auto& specialization : project.design_ir.specializations()) {
            if (specialization.language != semantic::Language::vhdl) {
                continue;
            }
            const auto* provenance = unit_provenance(
                project.vhdl_unit_provenance, specialization.unit);
            if (!provenance) {
                continue;
            }
            const auto& instance = project.design_ir.instances().at(
                specialization.instance.value());
            provenance_by_path.insert_or_assign(instance.path,
                ScopeProvenance { provenance->standard,
                    provenance->predefined_environment,
                    provenance->compatibility_profile,
                    vhpi_packages(*provenance) });
        }
        std::map<std::string, fsim_vhpi_handle_v1, std::less<>> handles;
        const auto ensure_scope = [&](const std::string_view path)
            -> fsim_vhpi_handle_v1 {
            if (const auto found = handles.find(path); found != handles.end()) {
                return found->second;
            }
            fsim_vhpi_handle_v1 parent { };
            std::string prefix;
            for (const auto& segment : path_segments(path)) {
                if (!prefix.empty()) {
                    prefix += '.';
                }
                prefix += segment;
                if (const auto found = handles.find(prefix);
                    found != handles.end()) {
                    parent = found->second;
                    continue;
                }
                runtime::VhdlVhpiObjectDescriptor descriptor;
                descriptor.kind = parent == 0U
                    ? runtime::VhdlVhpiObjectKind::Root
                    : runtime::VhdlVhpiObjectKind::Region;
                descriptor.parent = parent;
                const auto name = vhpi_name(segment);
                descriptor.name = name;
                if (const auto provenance = provenance_by_path.find(prefix);
                    provenance != provenance_by_path.end()) {
                    descriptor.language_standard
                        = provenance->second.standard;
                    descriptor.predefined_environment
                        = provenance->second.predefined_environment;
                    descriptor.compatibility_profile
                        = provenance->second.compatibility_profile;
                    descriptor.package_dependencies
                        = provenance->second.packages;
                }
                const auto created = registry->create_object(descriptor);
                if (!created) {
                    throw VhdlDebugError {
                        "failed to publish VHDL VHPI scope '" + prefix + "'"
                    };
                }
                parent = created.value;
                handles.emplace(prefix, parent);
            }
            return parent;
        };
        std::set<std::uint32_t> vhdl_specializations;
        for (const auto& specialization : project.design_ir.specializations()) {
            if (specialization.language != semantic::Language::vhdl) {
                continue;
            }
            vhdl_specializations.insert(specialization.id.value());
            const auto& instance = project.design_ir.instances().at(
                specialization.instance.value());
            const auto parent = ensure_scope(instance.path);
            const auto* unit = unit_for(project.vhdl_hir, specialization.unit);
            if (!unit) {
                continue;
            }
            for (const auto declaration_id : unit->declarations) {
                const auto* declaration = declaration_for(
                    project.vhdl_hir, declaration_id);
                if (!declaration || declaration->name.empty()) {
                    continue;
                }
                const auto path = instance.path + "." + declaration->name;
                if (handles.contains(path)) {
                    continue;
                }
                runtime::VhdlVhpiObjectDescriptor descriptor;
                descriptor.kind = declaration_kind(*declaration);
                descriptor.parent = parent;
                const auto name = vhpi_name(declaration->name);
                descriptor.name = name;
                descriptor.source = source_location(
                    project.semantics, declaration->source);
                const auto created = registry->create_object(descriptor);
                if (!created) {
                    throw VhdlDebugError {
                        "failed to publish VHDL VHPI declaration '" + path + "'"
                    };
                }
                handles.emplace(path, created.value);
            }
        }
        for (const auto& object : project.design_ir.objects()) {
            if (!vhdl_specializations.contains(object.specialization.value())) {
                continue;
            }
            const auto& specialization = project.design_ir.specializations().at(
                object.specialization.value());
            const auto& instance = project.design_ir.instances().at(
                specialization.instance.value());
            const auto path = occurrence_path(instance.path, object.path);
            const auto parent_path = parent_path_name(path);
            auto parent = ensure_scope(
                parent_path.empty() ? instance.path : parent_path);
            if (object.parent_object) {
                const auto& parent_object = project.design_ir.objects().at(
                    object.parent_object->value());
                const auto& parent_specialization
                    = project.design_ir.specializations().at(
                        parent_object.specialization.value());
                const auto& parent_instance = project.design_ir.instances().at(
                    parent_specialization.instance.value());
                const auto parent_object_path = occurrence_path(
                    parent_instance.path, parent_object.path);
                if (const auto found = handles.find(parent_object_path);
                    found != handles.end()) {
                    parent = found->second;
                }
            }
            if (handles.contains(path)) {
                continue;
            }
            runtime::VhdlVhpiObjectDescriptor descriptor;
            descriptor.kind = object.kind == semantic::design::ObjectKind::signal
                ? runtime::VhdlVhpiObjectKind::Signal
                : object.kind == semantic::design::ObjectKind::protected_object
                    || object.kind == semantic::design::ObjectKind::protected_member
                ? runtime::VhdlVhpiObjectKind::Variable
                : runtime::VhdlVhpiObjectKind::Variable;
            descriptor.parent = parent;
            const auto name = vhpi_name(leaf_path_name(path));
            descriptor.name = name;
            descriptor.source = source_location(project.semantics, object.source);
            const auto created = registry->create_object(descriptor);
            if (!created) {
                if (created.error == runtime::VhdlVhpiObjectError::DuplicateName) {
                    const auto existing = registry->find_child(parent, name);
                    if (existing && existing.value.parent == parent
                        && existing.value.kind == descriptor.kind) {
                        handles.emplace(path, existing.value.handle);
                        continue;
                    }
                }
                throw VhdlDebugError {
                    "failed to publish VHDL VHPI object '" + path
                    + "' with error "
                    + std::to_string(static_cast<unsigned>(created.error))
                    + " under handle " + std::to_string(parent)
                    + " as '" + name + "'"
                };
            }
            handles.emplace(path, created.value);
        }
        for (const auto& process : project.design_ir.processes()) {
            if (!vhdl_specializations.contains(process.specialization.value())) {
                continue;
            }
            const auto& specialization = project.design_ir.specializations().at(
                process.specialization.value());
            const auto& instance = project.design_ir.instances().at(
                specialization.instance.value());
            const auto path = occurrence_path(instance.path, process.name);
            if (handles.contains(path)) {
                continue;
            }
            runtime::VhdlVhpiObjectDescriptor descriptor;
            descriptor.kind = runtime::VhdlVhpiObjectKind::Process;
            const auto parent_path = parent_path_name(path);
            descriptor.parent = ensure_scope(
                parent_path.empty() ? instance.path : parent_path);
            const auto name = vhpi_name(leaf_path_name(path));
            descriptor.name = name;
            const auto* source_process
                = vhdl_process_for(project.vhdl_hir, process);
            descriptor.source = source_location(project.semantics,
                source_process
                    ? std::optional { source_process->source }
                    : process.source);
            const auto created = registry->create_object(descriptor);
            if (!created) {
                throw VhdlDebugError {
                    "failed to publish VHDL VHPI process '" + path + "'"
                };
            }
            handles.emplace(path, created.value);
        }
        return registry;
    }

} // namespace application_detail

std::vector<std::string> Simulation::vhdl_provenance_comments() const
{
    std::vector<std::string> result;
    for (const auto& specialization : design_ir().specializations()) {
        if (specialization.language != semantic::Language::vhdl) {
            continue;
        }
        const auto* provenance = unit_provenance(
            vhdl_unit_provenance(), specialization.unit);
        if (!provenance) {
            continue;
        }
        const auto& instance = design_ir().instances().at(
            specialization.instance.value());
        std::ostringstream comment;
        comment << "fsim-vhdl-scope path=" << instance.path << " unit="
                << specialization.library << ':' << specialization.name
                << " source="
                << (specialization.source
                        ? specialization.source->value()
                        : std::numeric_limits<std::uint32_t>::max())
                << " standard=" << provenance->standard
                << " environment=" << provenance->predefined_environment
                << " profile=" << provenance->compatibility_profile;
        for (const auto& package : provenance->package_dependencies) {
            comment << " package=" << package.package << '@'
                    << package.revision;
        }
        result.push_back(std::move(comment).str());
    }
    return result;
}

VhdlDebugSnapshot Simulation::vhdl_debug_snapshot(
    const VhdlDebugLimits limits) const
{
    if (limits.maximum_records == 0U
        || limits.maximum_payload_bytes == 0U
        || limits.maximum_formatted_bytes == 0U) {
        throw VhdlDebugError { "VHDL debug limits must be nonzero" };
    }
    VhdlDebugSnapshot result;
    result.simulation_identity = vhdl_vhpi_objects().simulation_identity();
    result.time = now();
    result.delta = delta();
    std::size_t records { };
    std::size_t payload { };
    const auto reserve = [&](const std::size_t count) {
        if (count > limits.maximum_records - records) {
            throw VhdlDebugError { "VHDL debug record ceiling exceeded" };
        }
        records += count;
    };
    const auto& hir = vhdl_hir();
    std::set<std::string, std::less<>> declaration_paths;
    for (const auto& specialization : design_ir().specializations()) {
        if (specialization.language != semantic::Language::vhdl) {
            continue;
        }
        const auto& instance = design_ir().instances().at(
            specialization.instance.value());
        reserve(1U);
        VhdlDebugScope scope;
        scope.path = instance.path;
        scope.library = specialization.library;
        scope.unit = specialization.name;
        if (const auto* provenance = unit_provenance(
                vhdl_unit_provenance(), specialization.unit)) {
            scope.standard = provenance->standard;
            scope.predefined_environment
                = provenance->predefined_environment;
            scope.compatibility_profile
                = provenance->compatibility_profile;
            scope.package_dependencies
                = provenance->package_dependencies;
        }
        scope.source_span = specialization.source
            ? specialization.source->value()
            : std::numeric_limits<std::uint32_t>::max();
        if (const auto handle = vhdl_vhpi_objects().find(scope.path)) {
            scope.vhpi_handle = handle.value.handle;
        }
        add_payload(payload, scope.path, limits.maximum_payload_bytes);
        add_payload(payload, scope.library, limits.maximum_payload_bytes);
        add_payload(payload, scope.unit, limits.maximum_payload_bytes);
        add_payload(payload, scope.standard, limits.maximum_payload_bytes);
        add_payload(payload, scope.predefined_environment,
            limits.maximum_payload_bytes);
        add_payload(payload, scope.compatibility_profile,
            limits.maximum_payload_bytes);
        for (const auto& package : scope.package_dependencies) {
            add_payload(payload, package.package,
                limits.maximum_payload_bytes);
            add_payload(payload, package.revision,
                limits.maximum_payload_bytes);
        }
        result.scopes.push_back(std::move(scope));

        const auto* unit = unit_for(hir, specialization.unit);
        if (!unit) {
            continue;
        }
        for (const auto declaration_id : unit->declarations) {
            const auto* declaration = declaration_for(hir, declaration_id);
            if (!declaration || declaration->name.empty()) {
                continue;
            }
            reserve(1U);
            const auto* type = type_for(hir, *declaration);
            VhdlDebugDeclaration item;
            item.path = instance.path + "." + declaration->name;
            item.name = declaration->name;
            item.type = type_name(*declaration, type);
            item.kind = debug_kind(*declaration, type);
            item.source_span = declaration->source.value();
            item.external_alias
                = declaration->form == semantic::vhdl::DeclarationForm::alias;
            if (const auto handle = vhdl_vhpi_objects().find(item.path)) {
                item.vhpi_handle = handle.value.handle;
            }
            const auto object = std::ranges::find_if(
                design_ir().objects(), [&](const auto& candidate) {
                    if (candidate.specialization != specialization.id) {
                        return false;
                    }
                    return occurrence_path(instance.path, candidate.path)
                        == item.path;
                });
            if (object != design_ir().objects().end()) {
                item.driver_count = static_cast<std::uint32_t>(
                    std::ranges::count(design_ir().drivers(), object->id,
                        &semantic::design::Driver::object));
                if (object->kind == semantic::design::ObjectKind::signal) {
                    item.value = read_signal(static_cast<runtime::simir::SignalId>(
                                                 object->runtime_index))
                                     .to_msb_string();
                    item.live_value = true;
                }
            }
            add_payload(payload, item.path, limits.maximum_payload_bytes);
            add_payload(payload, item.name, limits.maximum_payload_bytes);
            add_payload(payload, item.type, limits.maximum_payload_bytes);
            add_payload(payload, item.value, limits.maximum_payload_bytes);
            declaration_paths.insert(item.path);
            result.declarations.push_back(std::move(item));
        }
        for (const auto process_id : specialization.processes) {
            const auto& process = design_ir().processes().at(process_id.value());
            reserve(1U);
            VhdlDebugProcess item;
            item.path = occurrence_path(instance.path, process.name);
            const auto* source_process = vhdl_process_for(hir, process);
            const auto source = source_process
                ? std::optional { source_process->source }
                : process.source;
            item.source_span = source
                ? source->value()
                : std::numeric_limits<std::uint32_t>::max();
            item.postponed = source_process && source_process->postponed;
            if (const auto handle = vhdl_vhpi_objects().find(item.path)) {
                item.vhpi_handle = handle.value.handle;
            }
            add_payload(payload, item.path, limits.maximum_payload_bytes);
            result.processes.push_back(std::move(item));
        }
    }
    for (const auto& object : design_ir().objects()) {
        const auto& specialization = design_ir().specializations().at(
            object.specialization.value());
        const auto& instance = design_ir().instances().at(
            specialization.instance.value());
        const auto path = occurrence_path(instance.path, object.path);
        if (specialization.language != semantic::Language::vhdl
            || declaration_paths.contains(path)) {
            continue;
        }
        reserve(1U);
        VhdlDebugDeclaration item;
        item.path = path;
        item.name = object.name;
        item.type = object.external_type.empty()
            ? object.type.spelling
            : object.external_type;
        item.kind = object.kind == semantic::design::ObjectKind::signal
            ? VhdlDebugDeclarationKind::signal
            : object.kind == semantic::design::ObjectKind::protected_object
                || object.kind == semantic::design::ObjectKind::protected_member
            ? VhdlDebugDeclarationKind::protected_type
            : VhdlDebugDeclarationKind::other;
        item.source_span = object.source
            ? object.source->value()
            : std::numeric_limits<std::uint32_t>::max();
        item.driver_count = static_cast<std::uint32_t>(
            std::ranges::count(design_ir().drivers(), object.id,
                &semantic::design::Driver::object));
        item.external_alias = object.parent_object.has_value();
        if (object.kind == semantic::design::ObjectKind::signal) {
            item.value = read_signal(static_cast<runtime::simir::SignalId>(
                                         object.runtime_index))
                             .to_msb_string();
            item.live_value = true;
        }
        if (const auto handle = vhdl_vhpi_objects().find(item.path)) {
            item.vhpi_handle = handle.value.handle;
        }
        add_payload(payload, item.path, limits.maximum_payload_bytes);
        add_payload(payload, item.name, limits.maximum_payload_bytes);
        add_payload(payload, item.type, limits.maximum_payload_bytes);
        add_payload(payload, item.value, limits.maximum_payload_bytes);
        declaration_paths.insert(item.path);
        result.declarations.push_back(std::move(item));
    }
    result.psl_attempts = vhdl_psl_attempts();
    reserve(result.psl_attempts.size());
    for (const auto& attempt : result.psl_attempts) {
        add_payload(payload, attempt.monitor, limits.maximum_payload_bytes);
        add_payload(payload, attempt.instance_identity,
            limits.maximum_payload_bytes);
        add_payload(payload, attempt.clock_identity,
            limits.maximum_payload_bytes);
    }
    result.psl_coverage = vhdl_psl_coverage();
    reserve(result.psl_coverage.size());
    for (const auto& coverage : result.psl_coverage) {
        add_payload(payload, coverage.name, limits.maximum_payload_bytes);
        add_payload(payload, coverage.instance_identity,
            limits.maximum_payload_bytes);
    }
    if (now() != result.time || delta() != result.delta) {
        throw VhdlDebugError {
            "VHDL simulation advanced during debug snapshot capture"
        };
    }
    return result;
}

std::string format_vhdl_debug_snapshot(const VhdlDebugSnapshot& snapshot,
    const std::size_t maximum_bytes)
{
    if (maximum_bytes == 0U) {
        throw VhdlDebugError { "VHDL debug formatted-byte limit must be nonzero" };
    }
    std::ostringstream output;
    output << "vhdl simulation=" << snapshot.simulation_identity << " time="
           << snapshot.time << " delta=" << snapshot.delta << '\n';
    for (const auto& scope : snapshot.scopes) {
        output << "scope " << scope.path << " unit=" << scope.library << ':'
               << scope.unit << " source=" << scope.source_span
               << " standard=" << scope.standard
               << " environment=" << scope.predefined_environment
               << " profile=" << scope.compatibility_profile;
        for (const auto& package : scope.package_dependencies) {
            output << " package=" << package.package << '@'
                   << package.revision;
        }
        output << " vhpi=" << scope.vhpi_handle << '\n';
    }
    for (const auto& declaration : snapshot.declarations) {
        output << "object " << declaration.path << " kind="
               << debug_kind_name(declaration.kind) << " type="
               << declaration.type << " drivers=" << declaration.driver_count
               << " alias=" << declaration.external_alias << " source="
               << declaration.source_span << " value="
               << (declaration.live_value ? declaration.value : "<opaque>")
               << " vhpi=" << declaration.vhpi_handle << '\n';
    }
    for (const auto& process : snapshot.processes) {
        output << "process " << process.path
               << (process.postponed ? " postponed" : " active") << " vhpi="
               << process.vhpi_handle << " source=" << process.source_span
               << '\n';
    }
    for (const auto& attempt : snapshot.psl_attempts) {
        output << "psl " << attempt.monitor << " instance="
               << attempt.instance_identity << " attempt=" << attempt.attempt
               << " kind=" << psl_kind_name(attempt.directive_kind)
               << " samples=" << attempt.start_sample << ':'
               << attempt.end_sample << " time=" << attempt.end_time << ':'
               << attempt.end_delta << " outcome="
               << psl_outcome_name(attempt.outcome) << " source="
               << attempt.source_span << '\n';
    }
    for (const auto& coverage : snapshot.psl_coverage) {
        output << "coverage " << coverage.name << " instance="
               << coverage.instance_identity << " kind="
               << coverage_kind_name(coverage.kind) << " attempts="
               << coverage.attempts << " pass=" << coverage.passes
               << " failure=" << coverage.failures << " vacuous="
               << coverage.vacuous << " aborted=" << coverage.aborted
               << " source=" << coverage.source_span << '\n';
    }
    auto result = output.str();
    if (result.size() > maximum_bytes) {
        throw VhdlDebugError { "VHDL debug formatted-byte ceiling exceeded" };
    }
    return result;
}

} // namespace fsim::app
