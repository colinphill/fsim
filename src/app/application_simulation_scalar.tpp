// SPDX-License-Identifier: Apache-2.0
// Included by application_simulation.cpp after Simulation::Impl is complete.

const PackedLogic4& Simulation::read_signal(const SignalId signal) const {
  return impl_->interpreter->signal_value(signal);
}

runtime::SystemVerilogScalarValue Simulation::read_scalar_signal(
    const SignalId signal) const {
  return impl_->interpreter->scalar_signal_value(signal);
}

std::vector<runtime::simir::SystemVerilogScalarSignalSnapshot>
Simulation::scalar_signal_snapshots() const {
  return impl_->interpreter->scalar_signal_snapshots();
}

PackedLogic4 Simulation::read_process_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const {
  return impl_->interpreter->read_debug_local(process, local_index);
}

runtime::SystemVerilogScalarValue Simulation::read_process_scalar_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const {
  return impl_->interpreter->read_debug_scalar_local(process, local_index);
}

void Simulation::deposit_signal(
    const SignalId signal,
    PackedLogic4 value) {
  impl_->validate_external_value(signal, value, "deposit");
  impl_->interpreter->deposit_signal(signal, std::move(value));
}

void Simulation::deposit_scalar_signal(
    const SignalId signal,
    runtime::SystemVerilogScalarValue value) {
  if (value.kind == runtime::SystemVerilogScalarKind::Chandle
      && value.bits != 0) {
    value.bits = impl_->chandle_registry.alias(value.bits);
  }
  impl_->interpreter->deposit_scalar_signal(signal, value);
}

void Simulation::force_signal(
    const SignalId signal,
    PackedLogic4 value) {
  impl_->validate_external_value(signal, value, "force");
  impl_->interpreter->force_signal(signal, std::move(value));
}

void Simulation::force_scalar_signal(
    const SignalId signal,
    runtime::SystemVerilogScalarValue value) {
  if (value.kind == runtime::SystemVerilogScalarKind::Chandle
      && value.bits != 0) {
    value.bits = impl_->chandle_registry.alias(value.bits);
  }
  impl_->interpreter->force_scalar_signal(signal, value);
}

void Simulation::set_signal_change_hook(SignalChangeHook hook) {
  impl_->signal_change_hook = std::move(hook);
}

std::uint64_t Simulation::add_signal_change_hook(SignalChangeHook hook) {
  if (!hook) {
    throw std::invalid_argument("signal change observer cannot be empty");
  }
  if (impl_->next_signal_observer == 0) {
    throw std::overflow_error("signal change observer token space exhausted");
  }
  const auto token = impl_->next_signal_observer++;
  impl_->signal_observers.emplace(token, std::move(hook));
  return token;
}

void Simulation::remove_signal_change_hook(const std::uint64_t token) noexcept {
  impl_->signal_observers.erase(token);
}

void Simulation::set_scalar_signal_change_hook(
    ScalarSignalChangeHook hook) {
  impl_->scalar_signal_change_hook = std::move(hook);
}

std::uint64_t Simulation::add_scalar_signal_change_hook(
    ScalarSignalChangeHook hook) {
  if (!hook) {
    throw std::invalid_argument{"scalar signal observer cannot be empty"};
  }
  if (impl_->next_scalar_signal_observer == 0) {
    throw std::overflow_error{
        "scalar signal observer token space exhausted"};
  }
  const auto token = impl_->next_scalar_signal_observer++;
  impl_->scalar_signal_observers.emplace(token, std::move(hook));
  return token;
}

void Simulation::remove_scalar_signal_change_hook(
    const std::uint64_t token) noexcept {
  impl_->scalar_signal_observers.erase(token);
}

runtime::SystemVerilogChandleRegistry&
Simulation::chandle_registry() noexcept {
  return impl_->chandle_registry;
}

const runtime::SystemVerilogChandleRegistry&
Simulation::chandle_registry() const noexcept {
  return impl_->chandle_registry;
}
