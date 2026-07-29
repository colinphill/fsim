// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

void Interpreter::Impl::commit(SignalId signal_id, PackedLogic4 value)  {
    (void)get_signal(signal_id);
    if (driven_values[signal_id].width() != value.width()) {
      throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    value = normalize_signal_value(
        signal_id, std::move(value));
    driven_values[signal_id] = value;
    if (!forced_values[signal_id].has_value()) {
      publish(signal_id, std::move(value));
    }
  }

[[nodiscard]] PackedLogic4 Interpreter::Impl::initial_driver_value(
    const SignalId signal_id) const  {
    const auto& signal = get_signal(signal_id);
    if (signal.value_kind == ValueKind::logic9) {
      auto result = PackedLogic4{
          signal.initial_value.width(), Logic4::x};
      result.fill(Logic9::u);
      return result;
    }
    const auto initial =
        signal.resolution == ResolutionKind::sv_wire
            ? Logic4::z
            : Logic4::x;
    return PackedLogic4{signal.initial_value.width(), initial};
  }

PackedLogic4& Interpreter::Impl::driver_slot(
    const ProcessId process,
    const SignalId signal_id)  {
    auto& values = driver_values.at(signal_id);
    const auto found = values.find(process);
    if (found != values.end()) {
      return found->second;
    }
    return values
        .try_emplace(
            process, initial_driver_value(signal_id))
        .first->second;
  }

[[nodiscard]] PackedLogic4 Interpreter::Impl::resolved_driver_value(
    const SignalId signal_id) const  {
    const auto& values = driver_values.at(signal_id);
    if (values.empty()
        && !external_driver_values.at(signal_id)) {
      return get_signal(signal_id).initial_value;
    }
    std::vector<PackedLogic4> drivers;
    drivers.reserve(values.size());
    for (const auto& [process, value] : values) {
      (void)process;
      drivers.push_back(value);
    }
    if (external_driver_values.at(signal_id)) {
      drivers.push_back(
          *external_driver_values.at(signal_id));
    }
    return runtime::resolve(
        std::span<const PackedLogic4>{drivers});
  }

PackedLogic4& Interpreter::Impl::external_driver_slot(
    const SignalId signal_id)  {
    auto& value = external_driver_values.at(signal_id);
    if (!value) {
      value = initial_driver_value(signal_id);
    }
    return *value;
  }

void Interpreter::Impl::register_driver(
    const ProcessId process,
    const SignalId signal_id)  {
    const auto& signal = get_signal(signal_id);
    if (signal.resolution == ResolutionKind::none) {
      return;
    }
    auto& values = driver_values.at(signal_id);
    const auto [entry, inserted] = values.try_emplace(
        process, initial_driver_value(signal_id));
    (void)entry;
    if (!inserted) {
      return;
    }
    auto resolved = resolved_driver_value(signal_id);
    driven_values[signal_id] = resolved;
    signals[signal_id].initial_value = resolved;
    signal_last_values[signal_id] = std::move(resolved);
  }

void Interpreter::Impl::set_driver(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value)  {
    const auto& signal = get_signal(signal_id);
    if (signal.initial_value.width() != value.width()) {
      throw std::invalid_argument(
          "SimIR driver assignment width mismatch");
    }
    value = normalize_signal_value(
        signal_id, std::move(value));
    driver_slot(process, signal_id) = std::move(value);
  }

void Interpreter::Impl::commit_driver(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value)  {
    if (get_signal(signal_id).resolution
        == ResolutionKind::none) {
      commit(signal_id, std::move(value));
      return;
    }
    set_driver(process, signal_id, std::move(value));
    commit(signal_id, resolved_driver_value(signal_id));
  }

[[nodiscard]] const PackedLogic4& Interpreter::Impl::current_driver_value(
    const ProcessId process,
    const SignalId signal_id) const  {
    if (get_signal(signal_id).resolution
        == ResolutionKind::none) {
      return driven_values.at(signal_id);
    }
    const auto& values = driver_values.at(signal_id);
    const auto found = values.find(process);
    if (found == values.end()) {
      throw std::out_of_range(
          "process has no driver slot for SimIR signal");
    }
    return found->second;
  }

void Interpreter::Impl::commit_slice(
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)  {
    (void)get_signal(signal_id);
    commit(
        signal_id,
        insert_value(
            driven_values[signal_id], value, offset));
  }

void Interpreter::Impl::commit_driver_slice(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)  {
    if (get_signal(signal_id).resolution
        == ResolutionKind::none) {
      commit_slice(
          signal_id, std::move(value), offset);
      return;
    }
    const auto updated = insert_value(
        driver_slot(process, signal_id),
        value,
        offset);
    commit_driver(process, signal_id, std::move(updated));
  }

void Interpreter::Impl::schedule_update_commit()  {
    if (update_commit_scheduled) {
      return;
    }
    update_commit_scheduled = true;
    scheduler.schedule(
        SchedulerPhase::update,
        std::numeric_limits<StableOrder>::max(),
        [this](Scheduler&) {
          struct CoalescedDriverUpdate {
            SignalId signal{};
            std::optional<ProcessId> driver;
            PackedLogic4 value;
          };
          std::unordered_map<SignalId, PackedLogic4>
              unresolved_updates;
          unresolved_updates.reserve(pending_updates.size());
          std::vector<CoalescedDriverUpdate> driver_updates;
          std::set<SignalId> resolved_signals;
          for (auto& pending : pending_updates) {
            PackedLogic4* destination{};
            if (get_signal(pending.signal).resolution
                == ResolutionKind::none) {
              destination =
                  &unresolved_updates
                       .try_emplace(
                           pending.signal,
                           driven_values[pending.signal])
                       .first->second;
            } else {
              const auto found = std::find_if(
                  driver_updates.begin(),
                  driver_updates.end(),
                  [&](const CoalescedDriverUpdate& update) {
                    return update.signal == pending.signal
                        && update.driver == pending.driver;
                  });
              if (found != driver_updates.end()) {
                destination = &found->value;
              } else {
                auto initial =
                    pending.driver
                        ? driver_slot(
                              *pending.driver, pending.signal)
                        : external_driver_slot(pending.signal);
                driver_updates.push_back({
                    pending.signal,
                    pending.driver,
                    std::move(initial)});
                destination = &driver_updates.back().value;
              }
              resolved_signals.insert(pending.signal);
            }
            if (pending.offset) {
              *destination = insert_value(
                  std::move(*destination),
                  pending.value,
                  *pending.offset);
            } else {
              *destination = std::move(pending.value);
            }
          }
          pending_updates.clear();
          update_commit_scheduled = false;

          for (auto& update : driver_updates) {
            if (update.driver) {
              set_driver(
                  *update.driver,
                  update.signal,
                  std::move(update.value));
            } else {
              external_driver_slot(update.signal) =
                  std::move(update.value);
            }
          }

          std::vector<std::pair<SignalId, PackedLogic4>> updates;
          updates.reserve(
              unresolved_updates.size()
              + resolved_signals.size());
          for (auto& [signal, value] : unresolved_updates) {
            updates.emplace_back(signal, std::move(value));
          }
          for (const auto signal : resolved_signals) {
            updates.emplace_back(
                signal, resolved_driver_value(signal));
          }
          std::sort(
              updates.begin(),
              updates.end(),
              [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
              });
          for (auto& [signal, value] : updates) {
            commit(signal, std::move(value));
          }
        });
  }

void Interpreter::Impl::stage_update(
    const std::optional<ProcessId> driver,
    SignalId signal_id,
    PackedLogic4 staged_value)  {
    (void)get_signal(signal_id);
    if (driven_values[signal_id].width() != staged_value.width()) {
      throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    staged_value = normalize_signal_value(
        signal_id, std::move(staged_value));
    pending_updates.push_back(PendingUpdate{
        signal_id,
        driver,
        std::nullopt,
        std::move(staged_value)});
    schedule_update_commit();
  }

void Interpreter::Impl::stage_update(
    const SignalId signal_id,
    PackedLogic4 staged_value)  {
    stage_update(
        std::nullopt, signal_id, std::move(staged_value));
  }

void Interpreter::Impl::stage_update(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 staged_value)  {
    stage_update(
        std::optional<ProcessId>{process},
        signal_id,
        std::move(staged_value));
  }

void Interpreter::Impl::stage_update_slice(
    const std::optional<ProcessId> driver,
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)  {
    (void)get_signal(signal_id);
    const auto target_width = driven_values[signal_id].width();
    if (value.width() == 0 || offset > target_width
        || value.width() > target_width - offset) {
      throw std::invalid_argument(
          "partial update range is outside its target signal");
    }
    value = coerce_value_kind(
        std::move(value),
        get_signal(signal_id).value_kind);
    pending_updates.push_back(PendingUpdate{
        signal_id, driver, offset, std::move(value)});
    schedule_update_commit();
  }

void Interpreter::Impl::stage_update_slice(
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)  {
    stage_update_slice(
        std::nullopt,
        signal_id,
        std::move(value),
        offset);
  }

void Interpreter::Impl::stage_update_slice(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)  {
    stage_update_slice(
        std::optional<ProcessId>{process},
        signal_id,
        std::move(value),
        offset);
  }

void Interpreter::Impl::schedule_inertial(
    const ProcessId process,
    const SignalId signal,
    PackedLogic4 value,
    const std::optional<std::size_t> offset,
    const TransitionDelays& delays)  {
    (void)get_signal(signal);
    const auto target_width = driven_values[signal].width();
    if (value.width() == 0
        || value.width()
            > std::numeric_limits<std::uint32_t>::max()
        || offset.value_or(0)
            > std::numeric_limits<std::uint32_t>::max()
        || (offset
            && (*offset > target_width
                || value.width() > target_width - *offset))
        || (!offset && value.width() != target_width)) {
      throw std::invalid_argument(
          "inertial write range is outside its target signal");
    }
    const InertialDriverKey key{
        process,
        signal,
        static_cast<std::uint32_t>(offset.value_or(0)),
        static_cast<std::uint32_t>(value.width())};
    if (const auto pending = pending_inertial_writes.find(key);
        pending != pending_inertial_writes.end()) {
      if (pending->second.source_value == value) {
        return;
      }
      scheduler.cancel(pending->second.handle);
      pending_inertial_writes.erase(pending);
    }
    const auto& driver_current =
        get_signal(signal).resolution == ResolutionKind::none
            ? driven_values[signal]
            : driver_slot(process, signal);
    const auto current =
        offset
            ? extract_value(
                  driver_current, *offset, value.width())
            : driver_current;
    const auto delay = transition_delay(current, value, delays);
    if (!delay) {
      return;
    }
    auto [pending, inserted] =
        pending_inertial_writes.try_emplace(
            key,
            PendingInertialWrite{
                ScheduledTaskHandle{}, value});
    (void)inserted;
    try {
      pending->second.handle = scheduler.schedule_after_cancelable(
          *delay,
          SchedulerPhase::update,
          process,
          [this,
           key,
           process,
           signal,
           offset,
           value = std::move(value)](Scheduler&) mutable {
            pending_inertial_writes.erase(key);
            if (offset) {
              stage_update_slice(
                  process,
                  signal,
                  std::move(value),
                  *offset);
            } else {
              stage_update(
                  process, signal, std::move(value));
            }
          });
    } catch (...) {
      pending_inertial_writes.erase(pending);
      throw;
    }
  }

void Interpreter::Impl::schedule_projected_scalar_waveform(
    const ProcessId process,
    const SignalId signal,
    const std::uint32_t offset,
    const std::vector<
        std::pair<PackedLogic4, SimulationTick>>& elements,
    const SimulationTick rejection,
    const ProjectedDelayMode mode)  {
    if (elements.empty()) {
      throw std::invalid_argument(
          "a projected waveform must contain at least one element");
    }
    const auto first_delay = elements.front().second;
    if (mode == ProjectedDelayMode::inertial
        && rejection > first_delay) {
      throw std::invalid_argument(
          "projected-waveform rejection limit exceeds its first delay");
    }
    auto previous_delay = first_delay;
    for (std::size_t index = 0; index < elements.size(); ++index) {
      const auto delay = elements[index].second;
      if (index != 0 && delay <= previous_delay) {
        throw std::invalid_argument(
            "projected-waveform delays must be strictly ascending");
      }
      if (delay
          > std::numeric_limits<SimulationTick>::max()
              - scheduler.now()) {
        throw std::overflow_error(
            "simulation time overflow while scheduling projected waveform");
      }
      previous_delay = delay;
    }
    const auto first_time = scheduler.now() + first_delay;
    const ProjectedDriverKey key{process, signal, offset};
    auto [driver, inserted] =
        projected_drivers.try_emplace(key);
    (void)inserted;
    auto& transactions = driver->second.transactions;

    const auto first_deleted = std::lower_bound(
        transactions.begin(),
        transactions.end(),
        first_time,
        [](const ProjectedTransaction& transaction,
           const SimulationTick candidate) {
          return transaction.time < candidate;
        });
    for (auto transaction = first_deleted;
         transaction != transactions.end();
         ++transaction) {
      scheduler.cancel(transaction->handle);
    }
    transactions.erase(first_deleted, transactions.end());

    const auto old_count = transactions.size();
    std::vector<std::uint64_t> new_ids;
    new_ids.reserve(elements.size());
    for (const auto& [value, delay] : elements) {
      const auto id = next_projected_transaction_id++;
      new_ids.push_back(id);
      transactions.push_back(ProjectedTransaction{
          id, scheduler.now() + delay, value, {}});
    }
    if (mode == ProjectedDelayMode::inertial
        && old_count != 0) {
      std::vector<bool> marked(transactions.size(), false);
      for (std::size_t index = old_count;
           index < transactions.size();
           ++index) {
        marked[index] = true;
      }
      const auto threshold = first_time - rejection;
      for (std::size_t index = 0; index < old_count; ++index) {
        marked[index] =
            transactions[index].time < threshold;
      }
      for (std::size_t index = transactions.size() - 1;
           index-- > 0;) {
        if (!marked[index] && marked[index + 1]
            && transactions[index].value
                == transactions[index + 1].value) {
          marked[index] = true;
        }
      }

      for (std::size_t index = old_count; index-- > 0;) {
        if (!marked[index]) {
          scheduler.cancel(transactions[index].handle);
          transactions.erase(
              transactions.begin()
              + static_cast<std::ptrdiff_t>(index));
        }
      }
    }

    for (std::size_t index = 0; index < new_ids.size(); ++index) {
      const auto id = new_ids[index];
      const auto delay = elements[index].second;
      const auto pending = std::ranges::find(
          transactions, id, &ProjectedTransaction::id);
      if (pending == transactions.end()) {
        throw std::logic_error(
            "new projected transaction was not retained");
      }
      try {
        pending->handle = scheduler.schedule_after_cancelable(
          delay,
          SchedulerPhase::update,
          process,
          [this, key, id, process, signal, offset](Scheduler&) {
            const auto found_driver =
                projected_drivers.find(key);
            if (found_driver == projected_drivers.end()) {
              return;
            }
            auto& state = found_driver->second;
            const auto found_transaction = std::ranges::find(
                state.transactions,
                id,
                &ProjectedTransaction::id);
            if (found_transaction == state.transactions.end()) {
              return;
            }
            const auto committed_value =
                found_transaction->value;
            state.transactions.erase(found_transaction);
            stage_update_slice(
                process,
                signal,
                committed_value,
                offset);
          });
      } catch (...) {
        transactions.erase(pending);
        throw;
      }
    }
  }

void Interpreter::Impl::schedule_projected_waveform(
    const ProcessId process,
    const SignalId signal,
    const std::vector<ProjectedWaveformValue>& elements,
    const std::optional<std::size_t> offset,
    const SimulationTick rejection,
    const ProjectedDelayMode mode)  {
    (void)get_signal(signal);
    if (elements.empty()) {
      throw std::invalid_argument(
          "a projected waveform must contain at least one element");
    }
    const auto width = elements.front().value.width();
    for (const auto& element : elements) {
      if (element.value.width() != width) {
        throw std::invalid_argument(
            "projected-waveform element widths do not match");
      }
    }
    const auto target_width = driven_values[signal].width();
    const auto first = offset.value_or(0);
    if (width == 0
        || first > target_width
        || width > target_width - first
        || (!offset && width != target_width)
        || first > std::numeric_limits<std::uint32_t>::max()
        || width
            > std::numeric_limits<std::uint32_t>::max()
        || first + width
            > static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
      throw std::invalid_argument(
          "projected write range is outside its target signal");
    }
    std::vector<
        std::pair<PackedLogic4, SimulationTick>> scalar_elements;
    scalar_elements.reserve(elements.size());
    for (std::size_t bit = 0; bit < width; ++bit) {
      scalar_elements.clear();
      for (const auto& element : elements) {
        auto scalar = PackedLogic4{1, Logic4::x};
        if (element.value.is_logic9()) {
          scalar.fill(element.value.get_logic9(bit));
        } else {
          scalar.set(0, element.value.get(bit));
        }
        scalar_elements.emplace_back(
            std::move(scalar), element.delay);
      }
      schedule_projected_scalar_waveform(
          process,
          signal,
          static_cast<std::uint32_t>(first + bit),
          scalar_elements,
          rejection,
          mode);
    }
  }

void Interpreter::Impl::schedule_projected(
    const ProcessId process,
    const SignalId signal,
    const PackedLogic4& value,
    const std::optional<std::size_t> offset,
    const SimulationTick delay,
    const SimulationTick rejection,
    const ProjectedDelayMode mode)  {
    schedule_projected_waveform(
        process,
        signal,
        std::vector<ProjectedWaveformValue>{{value, delay}},
        offset,
        rejection,
        mode);
  }

[[noreturn]] void Interpreter::Impl::fail(const ProcessState &process,
                       const std::string &message) const  {
    throw InterpreterError(process.program.id, process.pc, message);
  }

} // namespace fsim::runtime::simir
