// SPDX-License-Identifier: Apache-2.0
// Included by application_simulation.cpp after Simulation::Impl is complete.

const elaboration::ElaboratedDesign& Simulation::design() const noexcept
{
    return runtime_adapter();
}

const elaboration::ElaboratedDesign&
Simulation::runtime_adapter() const noexcept
{
    return impl_->built.design;
}

const semantic::design::DesignIr& Simulation::design_ir() const noexcept
{
    return impl_->built.design_ir;
}

const semantic::vhdl::Hir& Simulation::vhdl_hir() const noexcept
{
    return impl_->built.vhdl_hir;
}

const semantic::Model& Simulation::semantics() const noexcept
{
    return impl_->built.semantics;
}

const std::vector<MappedLibraryProvenance>&
Simulation::mapped_libraries() const noexcept
{
    return impl_->built.mapped_libraries;
}

const std::vector<VhdlUnitProvenance>&
Simulation::vhdl_unit_provenance() const noexcept
{
    return impl_->built.vhdl_unit_provenance;
}

namespace {

[[nodiscard]] std::optional<VerilogScopeProvenance>
make_verilog_scope_provenance(
    const BuiltProject& project,
    const semantic::design::Specialization& specialization)
{
    if ((specialization.language != semantic::Language::verilog
            && specialization.language != semantic::Language::system_verilog)
        || specialization.unit.value() >= project.semantics.units().size()
        || specialization.instance.value()
            >= project.design_ir.instances().size()) {
        return std::nullopt;
    }
    const auto& unit = project.semantics.units().at(
        specialization.unit.value());
    if (unit.language != specialization.language) {
        return std::nullopt;
    }
    const auto identity = unit.library + "::" + unit.name;
    const auto revision = project.verilog_unit_revisions.find(identity);
    const auto profile
        = project.verilog_unit_compatibility_profiles.find(identity);
    if (revision == project.verilog_unit_revisions.end()
        || profile == project.verilog_unit_compatibility_profiles.end()) {
        return std::nullopt;
    }

    VerilogScopeProvenance result;
    result.path = project.design_ir.instances().at(
        specialization.instance.value()).path;
    result.unit = unit.id;
    result.source = unit.source;
    result.language = unit.language;
    result.library = unit.library;
    result.unit_name = unit.name;
    result.semantic_unit = identity;
    result.standard = frontend::to_string(revision->second);
    result.compatibility_profile = profile->second;
    if (unit.source.value() < project.semantics.source_spans().size()) {
        const auto& span = project.semantics.source_spans().at(
            unit.source.value());
        result.source_line = span.begin.line;
        result.source_column = span.begin.column;
        if (!span.logical_name.empty()) {
            result.source_path = span.logical_name;
        } else if (span.file.value()
                   < project.semantics.source_files().size()) {
            result.source_path = project.semantics.source_files().at(
                span.file.value()).physical_name;
        }
    }
    return result;
}

[[nodiscard]] bool verilog_scope_owns_path(
    const std::string_view scope,
    const std::string_view path) noexcept
{
    return path == scope
        || (path.size() > scope.size() && path.starts_with(scope)
            && path[scope.size()] == '.');
}

[[nodiscard]] std::string_view verilog_language_name(
    const semantic::Language language) noexcept
{
    return language == semantic::Language::verilog
        ? std::string_view { "verilog" }
        : std::string_view { "systemverilog" };
}

} // namespace

std::vector<VerilogScopeProvenance>
verilog_scope_provenance(const BuiltProject& project)
{
    std::vector<VerilogScopeProvenance> result;
    for (const auto& specialization : project.design_ir.specializations()) {
        if (auto provenance = make_verilog_scope_provenance(
                project, specialization)) {
            result.push_back(std::move(*provenance));
        }
    }
    std::ranges::sort(result, {}, &VerilogScopeProvenance::path);
    return result;
}

std::optional<VerilogScopeProvenance> verilog_scope_provenance(
    const BuiltProject& project,
    const std::string_view path)
{
    std::optional<VerilogScopeProvenance> result;
    for (const auto& specialization : project.design_ir.specializations()) {
        auto provenance = make_verilog_scope_provenance(
            project, specialization);
        if (provenance && verilog_scope_owns_path(provenance->path, path)
            && (!result || provenance->path.size() > result->path.size())) {
            result = std::move(provenance);
        }
    }
    return result;
}

std::vector<VerilogScopeProvenance>
Simulation::verilog_scope_provenance() const
{
    return fsim::app::verilog_scope_provenance(impl_->built);
}

std::optional<VerilogScopeProvenance>
Simulation::verilog_scope_provenance(const std::string_view path) const
{
    return fsim::app::verilog_scope_provenance(impl_->built, path);
}

std::vector<std::string> Simulation::verilog_provenance_comments() const
{
    std::vector<std::string> result;
    for (const auto& provenance : verilog_scope_provenance()) {
        std::ostringstream comment;
        comment << "fsim-verilog-scope path=" << provenance.path
                << " unit=" << provenance.library << ':'
                << provenance.unit_name << " source="
                << provenance.source.value() << " source_path="
                << provenance.source_path << " language="
                << verilog_language_name(provenance.language)
                << " standard=" << provenance.standard << " profile="
                << provenance.compatibility_profile;
        result.push_back(std::move(comment).str());
    }
    return result;
}

std::string_view Simulation::time_resolution() const noexcept
{
    return impl_->built.time_resolution;
}

std::optional<SignalId> Simulation::find_signal(
    const std::string_view path) const noexcept
{
    const auto found = std::ranges::find_if(
        impl_->built.design_ir.objects(), [&](const auto& object) {
            return design_object_is_signal_bearing(object)
                && object.path == path
                && object.runtime_index
                <= std::numeric_limits<SignalId>::max();
        });
    return found == impl_->built.design_ir.objects().end()
        ? std::nullopt
        : std::optional<SignalId> {
              static_cast<SignalId>(found->runtime_index)
          };
}

PackedLogic4 Simulation::read_driver(
    const runtime::simir::ProcessId process,
    const SignalId signal) const
{
    return impl_->interpreter->driver_value(process, signal);
}

runtime::simir::DriveStrength Simulation::read_signal_strength(
    const SignalId signal) const
{
    return impl_->interpreter->signal_strength(signal);
}

std::string Simulation::read_process_string_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const
{
    return impl_->interpreter->read_debug_string_local(
        process, local_index);
}

runtime::simir::ContainerValue
Simulation::read_process_container_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const
{
    return impl_->interpreter->read_debug_container_local(
        process, local_index);
}

const std::string& Simulation::read_string_object(
    const runtime::simir::StringObjectId object) const
{
    return impl_->interpreter->string_object_value(object);
}

const runtime::simir::ContainerValue&
Simulation::read_container_object(
    const runtime::simir::ContainerObjectId object) const
{
    return impl_->interpreter->container_object_value(object);
}

const std::vector<frontend::SystemVerilogClassSpecialization>&
Simulation::class_specializations() const noexcept
{
    return impl_->built.systemverilog_class_specializations;
}

runtime::SystemVerilogClassHeap& Simulation::class_heap() noexcept
{
    return impl_->class_heap;
}

const runtime::SystemVerilogClassHeap&
Simulation::class_heap() const noexcept
{
    return impl_->class_heap;
}

runtime::SystemVerilogClassStaticStore&
Simulation::class_static_store() noexcept
{
    return impl_->class_static_store;
}

const runtime::SystemVerilogClassStaticStore&
Simulation::class_static_store() const noexcept
{
    return impl_->class_static_store;
}

runtime::SystemVerilogClassMethodRuntime&
Simulation::class_methods() noexcept
{
    return impl_->class_methods;
}

const runtime::SystemVerilogClassMethodRuntime&
Simulation::class_methods() const noexcept
{
    return impl_->class_methods;
}

runtime::SystemVerilogUvmObjectService& Simulation::uvm_objects() noexcept
{
    return impl_->uvm_objects;
}

const runtime::SystemVerilogUvmObjectService&
Simulation::uvm_objects() const noexcept
{
    return impl_->uvm_objects;
}

runtime::SystemVerilogUvmComponentService&
Simulation::uvm_components() noexcept
{
    return impl_->uvm_components;
}

const runtime::SystemVerilogUvmComponentService&
Simulation::uvm_components() const noexcept
{
    return impl_->uvm_components;
}

runtime::SystemVerilogUvmActivityService&
Simulation::uvm_activity() noexcept
{
    return impl_->uvm_activity;
}

const runtime::SystemVerilogUvmActivityService&
Simulation::uvm_activity() const noexcept
{
    return impl_->uvm_activity;
}

runtime::SystemVerilogUvmForeignService&
Simulation::uvm_foreign() noexcept
{
    return impl_->uvm_foreign;
}

const runtime::SystemVerilogUvmForeignService&
Simulation::uvm_foreign() const noexcept
{
    return impl_->uvm_foreign;
}

runtime::SystemVerilogUvmCheckpointCaptureResult
Simulation::capture_uvm_checkpoint(
    const runtime::SystemVerilogUvmCheckpointLimits limits)
{
    runtime::SystemVerilogUvmCheckpointProvenance provenance;
    provenance.content_identity = impl_->built.cache_key;
    provenance.cache_identity = impl_->built.cache_key + ":"
        + std::string { project::to_string(impl_->built.optimization) };
    provenance.artifact_identity = impl_->built.artifact_identity;
    provenance.roots = impl_->built.design_ir.roots();
    return runtime::capture_systemverilog_uvm_checkpoint(
        impl_->uvm_foreign, std::move(provenance), limits);
}

runtime::SystemVerilogUvmPhaseService& Simulation::uvm_phases() noexcept
{
    return impl_->uvm_phases;
}

const runtime::SystemVerilogUvmPhaseService&
Simulation::uvm_phases() const noexcept
{
    return impl_->uvm_phases;
}

runtime::SystemVerilogUvmObjectionService&
Simulation::uvm_objections() noexcept
{
    return impl_->uvm_objections;
}

const runtime::SystemVerilogUvmObjectionService&
Simulation::uvm_objections() const noexcept
{
    return impl_->uvm_objections;
}

runtime::SystemVerilogUvmTlm1Service& Simulation::uvm_tlm1() noexcept
{
    return impl_->uvm_tlm1;
}

const runtime::SystemVerilogUvmTlm1Service&
Simulation::uvm_tlm1() const noexcept
{
    return impl_->uvm_tlm1;
}

runtime::SystemVerilogUvmTlm2Service& Simulation::uvm_tlm2() noexcept
{
    return impl_->uvm_tlm2;
}

const runtime::SystemVerilogUvmTlm2Service&
Simulation::uvm_tlm2() const noexcept
{
    return impl_->uvm_tlm2;
}

runtime::SystemVerilogUvmSequenceService&
Simulation::uvm_sequences() noexcept
{
    return impl_->uvm_sequences;
}

const runtime::SystemVerilogUvmSequenceService&
Simulation::uvm_sequences() const noexcept
{
    return impl_->uvm_sequences;
}

runtime::SystemVerilogUvmCallbackService&
Simulation::uvm_callbacks() noexcept
{
    return impl_->uvm_callbacks;
}

const runtime::SystemVerilogUvmCallbackService&
Simulation::uvm_callbacks() const noexcept
{
    return impl_->uvm_callbacks;
}

runtime::SystemVerilogUvmTransactionRecorderService&
Simulation::uvm_transactions() noexcept
{
    return impl_->uvm_transactions;
}

const runtime::SystemVerilogUvmTransactionRecorderService&
Simulation::uvm_transactions() const noexcept
{
    return impl_->uvm_transactions;
}

runtime::SystemVerilogUvmRegisterModelService&
Simulation::uvm_register_model() noexcept
{
    return impl_->uvm_register_model;
}

const runtime::SystemVerilogUvmRegisterModelService&
Simulation::uvm_register_model() const noexcept
{
    return impl_->uvm_register_model;
}

runtime::SystemVerilogUvmRegistryService& Simulation::uvm_registry() noexcept
{
    return impl_->uvm_registry;
}

const runtime::SystemVerilogUvmRegistryService&
Simulation::uvm_registry() const noexcept
{
    return impl_->uvm_registry;
}

runtime::SystemVerilogUvmFactoryService& Simulation::uvm_factory() noexcept
{
    return impl_->uvm_factory;
}

const runtime::SystemVerilogUvmFactoryService&
Simulation::uvm_factory() const noexcept
{
    return impl_->uvm_factory;
}

runtime::SystemVerilogUvmResourcePoolService&
Simulation::uvm_resources() noexcept
{
    return impl_->uvm_resources;
}

const runtime::SystemVerilogUvmResourcePoolService&
Simulation::uvm_resources() const noexcept
{
    return impl_->uvm_resources;
}

runtime::SystemVerilogUvmSynchronizationService&
Simulation::uvm_synchronization() noexcept
{
    return impl_->uvm_synchronization;
}

const runtime::SystemVerilogUvmSynchronizationService&
Simulation::uvm_synchronization() const noexcept
{
    return impl_->uvm_synchronization;
}

runtime::SystemVerilogUvmConfigDbService& Simulation::uvm_config_db() noexcept
{
    return impl_->uvm_config_db;
}

const runtime::SystemVerilogUvmConfigDbService&
Simulation::uvm_config_db() const noexcept
{
    return impl_->uvm_config_db;
}

runtime::SystemVerilogUvmCommandLineService&
Simulation::uvm_command_line() noexcept
{
    return impl_->uvm_command_line;
}

const runtime::SystemVerilogUvmCommandLineService&
Simulation::uvm_command_line() const noexcept
{
    return impl_->uvm_command_line;
}

runtime::SystemVerilogUvmTestRunnerService&
Simulation::uvm_test_runner() noexcept
{
    return impl_->uvm_test_runner;
}

const runtime::SystemVerilogUvmTestRunnerService&
Simulation::uvm_test_runner() const noexcept
{
    return impl_->uvm_test_runner;
}

runtime::SystemVerilogUvmReportService& Simulation::uvm_reports() noexcept
{
    return impl_->uvm_reports;
}

const runtime::SystemVerilogUvmReportService&
Simulation::uvm_reports() const noexcept
{
    return impl_->uvm_reports;
}

void Simulation::schedule_uvm_report_settings()
{
    impl_->schedule_uvm_report_settings();
}

runtime::SystemVerilogClassHandle Simulation::allocate_class(
    const std::string_view specialization_identity,
    const std::string_view declared_type)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    return impl_->allocate_class(specialization_identity, declared_type);
}

runtime::SystemVerilogClassHandle Simulation::allocate_uvm_object(
    const std::string_view specialization_identity,
    std::string name,
    const std::string_view declared_type)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    application_detail::ensure_systemverilog_uvm_object_type(
        impl_->class_specialization(specialization_identity),
        impl_->uvm_objects);
    const auto handle = impl_->allocate_class(
        specialization_identity, declared_type);
    try {
        impl_->uvm_objects.initialize(handle, std::move(name));
    } catch (...) {
        (void)impl_->class_heap.release(handle);
        throw;
    }
    return handle;
}

runtime::SystemVerilogUvmRootHandle Simulation::create_uvm_root(
    std::string identity)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    const auto root = impl_->uvm_components.create_root(std::move(identity));
    try {
        impl_->uvm_phases.participate_standard_root(root);
    } catch (...) {
        impl_->uvm_components.destroy_root(root);
        throw;
    }
    return root;
}

runtime::SystemVerilogClassHandle Simulation::allocate_uvm_component(
    const std::string_view specialization_identity,
    std::string name,
    const runtime::SystemVerilogClassHandle parent,
    const runtime::SystemVerilogUvmRootHandle root,
    const std::string_view declared_type)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    application_detail::ensure_systemverilog_uvm_object_type(
        impl_->class_specialization(specialization_identity),
        impl_->uvm_objects);
    const auto component_name = name;
    std::array actuals {
        runtime::PackedLogic4(64),
        runtime::PackedLogic4::from_aval_bval(64, parent, 0)
    };
    std::array string_actuals { std::move(name), std::string { } };
    const auto handle = impl_->construct_class(
        specialization_identity, declared_type, actuals, string_actuals,
        std::span<const std::string> { }, "$api", root);
    if (impl_->uvm_components.contains(handle))
        return handle;
    try {
        if (!impl_->uvm_objects.contains(handle)) {
            impl_->uvm_objects.initialize(handle, component_name);
        }
        impl_->uvm_components.initialize(handle, component_name, parent, root);
    } catch (...) {
        if (impl_->uvm_components.contains(handle)) {
            impl_->uvm_components.release(handle);
        } else {
            if (impl_->uvm_objects.contains(handle)) {
                impl_->uvm_objects.erase(handle);
            }
            (void)impl_->class_heap.release(handle);
        }
        throw;
    }
    return handle;
}
