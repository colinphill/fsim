// SPDX-License-Identifier: Apache-2.0
// Included in the public section of runtime::simir::Interpreter.

  using ScalarSignalChangeHook = std::function<void(
      SignalId, const SystemVerilogScalarValue&, SimulationTick)>;

  void deposit_scalar_signal(
      SignalId signal, SystemVerilogScalarValue value);
  void force_scalar_signal(
      SignalId signal, SystemVerilogScalarValue value);
  void schedule_scalar_signal_at(
      SignalId signal,
      SystemVerilogScalarValue value,
      SimulationTick time,
      StableOrder order = 0);
  void schedule_scalar_signal_after(
      SignalId signal,
      SystemVerilogScalarValue value,
      SimulationTick delay,
      StableOrder order = 0);

  [[nodiscard]] SystemVerilogScalarValue scalar_signal_value(
      SignalId signal) const;
  [[nodiscard]] std::vector<SystemVerilogScalarSignalSnapshot>
  scalar_signal_snapshots() const;
  [[nodiscard]] SystemVerilogScalarValue read_debug_scalar_local(
      ProcessId process, std::size_t local_index) const;
  void write_debug_scalar_local(
      ProcessId process,
      std::size_t local_index,
      SystemVerilogScalarValue value);
  void set_scalar_signal_change_hook(ScalarSignalChangeHook hook);
