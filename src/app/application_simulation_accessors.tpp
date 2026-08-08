// SPDX-License-Identifier: Apache-2.0
// Included by application_simulation.cpp after Simulation::Impl is complete.

const elaboration::ElaboratedDesign& Simulation::design() const noexcept {
  return runtime_adapter();
}

const elaboration::ElaboratedDesign&
Simulation::runtime_adapter() const noexcept {
  return impl_->built.design;
}

const semantic::design::DesignIr& Simulation::design_ir() const noexcept {
  return impl_->built.design_ir;
}

const semantic::Model& Simulation::semantics() const noexcept {
  return impl_->built.semantics;
}

const std::vector<MappedLibraryProvenance>&
Simulation::mapped_libraries() const noexcept {
  return impl_->built.mapped_libraries;
}

std::string_view Simulation::time_resolution() const noexcept {
  return impl_->built.time_resolution;
}

std::optional<SignalId> Simulation::find_signal(
    const std::string_view path) const noexcept {
  const auto found = std::ranges::find_if(
      impl_->built.design_ir.objects(), [&](const auto& object) {
        return design_object_is_signal_bearing(object)
            && object.path == path
            && object.runtime_index
                <= std::numeric_limits<SignalId>::max();
      });
  return found == impl_->built.design_ir.objects().end()
      ? std::nullopt
      : std::optional<SignalId>{
            static_cast<SignalId>(found->runtime_index)};
}

const PackedLogic4& Simulation::read_driver(
    const runtime::simir::ProcessId process,
    const SignalId signal) const {
  return impl_->interpreter->driver_value(process, signal);
}

std::string Simulation::read_process_string_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const {
  return impl_->interpreter->read_debug_string_local(
      process, local_index);
}

runtime::simir::ContainerValue
Simulation::read_process_container_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const {
  return impl_->interpreter->read_debug_container_local(
      process, local_index);
}

const std::string& Simulation::read_string_object(
    const runtime::simir::StringObjectId object) const {
  return impl_->interpreter->string_object_value(object);
}

const runtime::simir::ContainerValue&
Simulation::read_container_object(
    const runtime::simir::ContainerObjectId object) const {
  return impl_->interpreter->container_object_value(object);
}

const std::vector<frontend::SystemVerilogClassSpecialization>&
Simulation::class_specializations() const noexcept {
  return impl_->built.systemverilog_class_specializations;
}

runtime::SystemVerilogClassHeap& Simulation::class_heap() noexcept {
  return impl_->class_heap;
}

const runtime::SystemVerilogClassHeap&
Simulation::class_heap() const noexcept {
  return impl_->class_heap;
}

runtime::SystemVerilogClassStaticStore&
Simulation::class_static_store() noexcept {
  return impl_->class_static_store;
}

const runtime::SystemVerilogClassStaticStore&
Simulation::class_static_store() const noexcept {
  return impl_->class_static_store;
}

runtime::SystemVerilogClassMethodRuntime&
Simulation::class_methods() noexcept {
  return impl_->class_methods;
}

const runtime::SystemVerilogClassMethodRuntime&
Simulation::class_methods() const noexcept {
  return impl_->class_methods;
}

runtime::SystemVerilogUvmObjectService& Simulation::uvm_objects() noexcept {
  return impl_->uvm_objects;
}

const runtime::SystemVerilogUvmObjectService&
Simulation::uvm_objects() const noexcept {
  return impl_->uvm_objects;
}

runtime::SystemVerilogUvmComponentService&
Simulation::uvm_components() noexcept {
  return impl_->uvm_components;
}

const runtime::SystemVerilogUvmComponentService&
Simulation::uvm_components() const noexcept {
  return impl_->uvm_components;
}

runtime::SystemVerilogUvmRegistryService& Simulation::uvm_registry() noexcept {
  return impl_->uvm_registry;
}

const runtime::SystemVerilogUvmRegistryService&
Simulation::uvm_registry() const noexcept {
  return impl_->uvm_registry;
}

runtime::SystemVerilogUvmFactoryService& Simulation::uvm_factory() noexcept {
  return impl_->uvm_factory;
}

const runtime::SystemVerilogUvmFactoryService&
Simulation::uvm_factory() const noexcept {
  return impl_->uvm_factory;
}

runtime::SystemVerilogUvmResourcePoolService&
Simulation::uvm_resources() noexcept {
  return impl_->uvm_resources;
}

const runtime::SystemVerilogUvmResourcePoolService&
Simulation::uvm_resources() const noexcept {
  return impl_->uvm_resources;
}

runtime::SystemVerilogUvmConfigDbService& Simulation::uvm_config_db() noexcept {
  return impl_->uvm_config_db;
}

const runtime::SystemVerilogUvmConfigDbService&
Simulation::uvm_config_db() const noexcept {
  return impl_->uvm_config_db;
}

runtime::SystemVerilogUvmCommandLineService&
Simulation::uvm_command_line() noexcept {
  return impl_->uvm_command_line;
}

const runtime::SystemVerilogUvmCommandLineService&
Simulation::uvm_command_line() const noexcept {
  return impl_->uvm_command_line;
}

runtime::SystemVerilogUvmReportService& Simulation::uvm_reports() noexcept {
  return impl_->uvm_reports;
}

const runtime::SystemVerilogUvmReportService&
Simulation::uvm_reports() const noexcept {
  return impl_->uvm_reports;
}

runtime::SystemVerilogClassHandle Simulation::allocate_class(
    const std::string_view specialization_identity,
    const std::string_view declared_type) {
  if (impl_->lifecycle == Impl::Lifecycle::finished
      || impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error{"simulation class heap is no longer mutable"};
  }
  return impl_->allocate_class(specialization_identity, declared_type);
}

runtime::SystemVerilogClassHandle Simulation::allocate_uvm_object(
    const std::string_view specialization_identity,
    std::string name,
    const std::string_view declared_type) {
  if (impl_->lifecycle == Impl::Lifecycle::finished
      || impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error{"simulation class heap is no longer mutable"};
  }
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
    std::string identity) {
  if (impl_->lifecycle == Impl::Lifecycle::finished
      || impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error{"simulation class heap is no longer mutable"};
  }
  return impl_->uvm_components.create_root(std::move(identity));
}

runtime::SystemVerilogClassHandle Simulation::allocate_uvm_component(
    const std::string_view specialization_identity,
    std::string name,
    const runtime::SystemVerilogClassHandle parent,
    const runtime::SystemVerilogUvmRootHandle root,
    const std::string_view declared_type) {
  if (impl_->lifecycle == Impl::Lifecycle::finished
      || impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error{"simulation class heap is no longer mutable"};
  }
  std::array actuals{
      runtime::PackedLogic4(64),
      runtime::PackedLogic4::from_aval_bval(64, parent, 0)};
  std::array string_actuals{std::move(name), std::string{}};
  return impl_->construct_class(
      specialization_identity, declared_type, actuals, string_actuals,
      std::span<const std::string>{}, "$api", root);
}
