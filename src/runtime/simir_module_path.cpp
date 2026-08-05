// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {
namespace {

PackedLogic4 resize_unsigned(
    const PackedLogic4& value,
    const std::size_t width,
    const bool sign_extend = false) {
  if (width == 0) {
    throw std::invalid_argument{"a module-path expression has zero width"};
  }
  const auto extension = sign_extend && !value.empty()
      ? value.get(value.width() - 1U) : Logic4::zero;
  auto result = PackedLogic4{width, extension};
  if (value.is_logic9()) result = result.promoted_to_logic9();
  const auto copied = std::min(width, value.width());
  for (std::size_t bit = 0; bit < copied; ++bit) {
    if (value.is_logic9()) {
      result.set_logic9(bit, value.get_logic9(bit));
    } else {
      result.set(bit, value.get(bit));
    }
  }
  return result;
}

bool valid_expression_enum(const ModulePathExpressionNode& node) {
  return node.operation <= ModulePathExpressionOperator::concatenate
      && node.binary <= BinaryOperator::vhdl_match_equal
      && node.logical <= LogicalBinaryOperator::logical_or
      && node.shift <= ShiftOperator::rotate_right
      && node.reduction <= ReductionOperator::one_hot_or_zero;
}

}  // namespace

std::optional<SimulationTick> module_path_transition_delay(
    const Logic4 before,
    const Logic4 after,
    const std::span<const SimulationTick> delays) {
  if (before == after) return std::nullopt;
  if (delays.size() != 1 && delays.size() != 2 && delays.size() != 3
      && delays.size() != 6 && delays.size() != 12) {
    throw std::invalid_argument{"invalid module-path transition-delay table"};
  }
  if (delays.size() == 1) return delays[0];
  const auto rise = delays[0];
  const auto fall = delays[1];
  const auto turnoff = delays.size() == 2
      ? std::min(rise, fall) : delays[2];
  const std::array<SimulationTick, 6> direct_delays{
      rise,
      fall,
      turnoff,
      delays.size() >= 6 ? delays[3] : rise,
      delays.size() >= 6 ? delays[4] : turnoff,
      delays.size() >= 6 ? delays[5] : fall};
  const auto direct = [&](const Logic4 from, const Logic4 to)
      -> std::optional<SimulationTick> {
    if (from == Logic4::zero && to == Logic4::one) return direct_delays[0];
    if (from == Logic4::one && to == Logic4::zero) return direct_delays[1];
    if (from == Logic4::zero && to == Logic4::z) return direct_delays[2];
    if (from == Logic4::z && to == Logic4::one) return direct_delays[3];
    if (from == Logic4::one && to == Logic4::z) return direct_delays[4];
    if (from == Logic4::z && to == Logic4::zero) return direct_delays[5];
    if (delays.size() == 12) {
      if (from == Logic4::zero && to == Logic4::x) return delays[6];
      if (from == Logic4::x && to == Logic4::one) return delays[7];
      if (from == Logic4::one && to == Logic4::x) return delays[8];
      if (from == Logic4::x && to == Logic4::zero) return delays[9];
      if (from == Logic4::x && to == Logic4::z) return delays[10];
      if (from == Logic4::z && to == Logic4::x) return delays[11];
    }
    return std::nullopt;
  };
  if (const auto selected = direct(before, after)) return selected;
  if (before == Logic4::zero && after == Logic4::x) {
    return std::min(direct_delays[0], direct_delays[2]);
  }
  if (before == Logic4::x && after == Logic4::one) {
    return std::max(direct_delays[0], direct_delays[3]);
  }
  if (before == Logic4::one && after == Logic4::x) {
    return std::min(direct_delays[1], direct_delays[4]);
  }
  if (before == Logic4::x && after == Logic4::zero) {
    return std::max(direct_delays[1], direct_delays[5]);
  }
  if (before == Logic4::x && after == Logic4::z) {
    return std::max(direct_delays[2], direct_delays[4]);
  }
  return std::min(direct_delays[3], direct_delays[5]);
}

void validate_module_path_expression(
    const ModulePathExpression& expression,
    const std::span<const Signal> signals) {
  if (expression.empty()) return;
  if (expression.root >= expression.nodes.size()
      || expression.nodes.size()
          > maximum_module_path_expression_storage_bytes
              / sizeof(ModulePathExpressionNode)) {
    throw std::invalid_argument{"invalid SimIR module-path expression root"};
  }
  std::size_t operand_bytes{};
  for (std::size_t index = 0; index < expression.nodes.size(); ++index) {
    const auto& node = expression.nodes[index];
    if (node.width == 0 || !valid_expression_enum(node)
        || !std::ranges::all_of(
            node.operands,
            [&](const std::uint32_t operand) { return operand < index; })) {
      throw std::invalid_argument{"invalid SimIR module-path expression node"};
    }
    if (node.operands.size()
        > (maximum_module_path_expression_storage_bytes - operand_bytes)
            / sizeof(std::uint32_t)) {
      throw std::length_error{
          "SimIR module-path expression exceeds its storage budget"};
    }
    operand_bytes += node.operands.size() * sizeof(std::uint32_t);
    const auto expected_operands = [&]() -> std::optional<std::size_t> {
      switch (node.operation) {
        case ModulePathExpressionOperator::constant:
        case ModulePathExpressionOperator::terminal: return 0;
        case ModulePathExpressionOperator::bit_not:
        case ModulePathExpressionOperator::logical_not:
        case ModulePathExpressionOperator::reduction: return 1;
        case ModulePathExpressionOperator::binary:
        case ModulePathExpressionOperator::logical_binary:
        case ModulePathExpressionOperator::shift: return 2;
        case ModulePathExpressionOperator::conditional: return 3;
        case ModulePathExpressionOperator::concatenate: return std::nullopt;
      }
      return std::nullopt;
    }();
    if ((expected_operands && node.operands.size() != *expected_operands)
        || (node.operation == ModulePathExpressionOperator::concatenate
            && node.operands.empty())) {
      throw std::invalid_argument{
          "invalid SimIR module-path expression operand count"};
    }
    if (node.operation == ModulePathExpressionOperator::constant
        && node.constant.width() != node.width) {
      throw std::invalid_argument{
          "invalid SimIR module-path expression constant"};
    }
    if (node.operation == ModulePathExpressionOperator::terminal) {
      if (node.terminal.signal >= signals.size()
          || node.terminal.width != node.width
          || node.terminal.width == 0
          || node.terminal.offset
              > signals[node.terminal.signal].initial_value.width()
          || node.terminal.width
              > signals[node.terminal.signal].initial_value.width()
                  - node.terminal.offset) {
        throw std::invalid_argument{
            "invalid SimIR module-path expression terminal"};
      }
    }
  }
}

PackedLogic4 Interpreter::Impl::evaluate_module_path_expression(
    const ModulePathExpression& expression) const {
  if (expression.empty()) {
    throw std::invalid_argument{"cannot evaluate an empty module-path expression"};
  }
  std::vector<PackedLogic4> values;
  values.reserve(expression.nodes.size());
  for (const auto& node : expression.nodes) {
    const auto operand = [&](const std::size_t index) -> const PackedLogic4& {
      return values.at(node.operands.at(index));
    };
    PackedLogic4 value;
    switch (node.operation) {
      case ModulePathExpressionOperator::constant:
        value = node.constant;
        break;
      case ModulePathExpressionOperator::terminal:
        value = extract_value(
            signals.at(node.terminal.signal).initial_value,
            node.terminal.offset,
            node.terminal.width);
        break;
      case ModulePathExpressionOperator::bit_not:
        value = unary_not(operand(0));
        break;
      case ModulePathExpressionOperator::logical_not:
        value = logical_not(operand(0));
        break;
      case ModulePathExpressionOperator::reduction:
        value = reduce_value(node.reduction, operand(0));
        break;
      case ModulePathExpressionOperator::binary: {
        const auto width = std::max(
            operand(0).width(), operand(1).width());
        value = binary_value(
            node.binary,
            resize_unsigned(
                operand(0), width,
                expression.nodes[node.operands[0]].is_signed),
            resize_unsigned(
                operand(1), width,
                expression.nodes[node.operands[1]].is_signed));
        break;
      }
      case ModulePathExpressionOperator::logical_binary:
        value = logical_binary(node.logical, operand(0), operand(1));
        break;
      case ModulePathExpressionOperator::shift:
        value = shift_value(node.shift, operand(0), operand(1), false);
        break;
      case ModulePathExpressionOperator::conditional:
        value = conditional_value(
            operand(0),
            resize_unsigned(
                operand(1), node.width,
                expression.nodes[node.operands[1]].is_signed),
            resize_unsigned(
                operand(2), node.width,
                expression.nodes[node.operands[2]].is_signed));
        break;
      case ModulePathExpressionOperator::concatenate: {
        std::vector<PackedLogic4> operands;
        operands.reserve(node.operands.size());
        for (const auto id : node.operands) operands.push_back(values.at(id));
        value = concatenate_values(operands, node.width);
        break;
      }
    }
    values.push_back(resize_unsigned(value, node.width, node.is_signed));
  }
  return values.at(expression.root);
}

void Interpreter::Impl::evaluate_module_timing_checks(
    const SignalId changed,
    const PackedLogic4& before,
    const PackedLogic4& after) {
  const auto symbol = [](const Logic4 value) {
    switch (value) {
      case Logic4::zero: return '0';
      case Logic4::one: return '1';
      case Logic4::x: return 'x';
      case Logic4::z: return 'z';
    }
    return 'x';
  };
  const auto transition_matches = [&](const ModuleTimingEvent& event) {
    if (event.terminal.signal != changed) return false;
    const auto old_value = before.get(event.terminal.offset);
    const auto new_value = after.get(event.terminal.offset);
    bool edge = false;
    if (!event.edge_descriptors.empty()) {
      const std::array spelling{symbol(old_value), symbol(new_value)};
      edge = std::ranges::any_of(
          event.edge_descriptors,
          [&](const std::string& descriptor) {
            return descriptor.size() == 2
                && std::tolower(
                       static_cast<unsigned char>(descriptor[0]))
                    == spelling[0]
                && std::tolower(
                       static_cast<unsigned char>(descriptor[1]))
                    == spelling[1];
          });
    } else if (event.edge == ModulePathEdge::posedge) {
      edge = edge_matches(EdgeKind::posedge, old_value, new_value);
    } else if (event.edge == ModulePathEdge::negedge) {
      edge = edge_matches(EdgeKind::negedge, old_value, new_value);
    } else {
      edge = old_value != new_value;
    }
    return edge;
  };
  const auto expression_enabled = [&](const ModulePathExpression& expression) {
    return expression.empty()
        || truth_value(evaluate_module_path_expression(expression))
            == Logic4::one;
  };
  const auto matches = [&](const ModuleTimingEvent& event) {
    return transition_matches(event)
        && expression_enabled(event.condition);
  };
  const auto now = scheduler.now();
  for (std::size_t index = 0;
       index < module_timing_checks.size(); ++index) {
    const auto& check = module_timing_checks[index];
    auto& state = module_timing_check_states[index];
    const bool raw_reference = transition_matches(check.reference);
    const bool reference = matches(check.reference);
    const bool raw_data = check.data
        && transition_matches(*check.data);
    const bool data = check.data && matches(*check.data);
    const auto delayed_copy = [&](const ModuleTimingEvent& source,
                                  const std::optional<ModulePathTerminal> target,
                                  const SimulationTick delay) {
      if (!target || source.terminal.signal != changed
          || before.get(source.terminal.offset)
              == after.get(source.terminal.offset)) {
        return;
      }
      if (delay > std::numeric_limits<SimulationTick>::max() - now) {
        throw std::overflow_error{
            "simulation time overflow scheduling delayed timing signal"};
      }
      const auto value = after.get(source.terminal.offset);
      scheduler.schedule_at(
          now + delay,
          SchedulerPhase::update,
          static_cast<StableOrder>(index),
          [owner = this, target = *target, value](Scheduler&) {
            owner->stage_update_slice(
                target.signal, PackedLogic4{1, value}, target.offset);
          });
    };
    if (check.kind == ModuleTimingCheckKind::setuphold) {
      delayed_copy(
          check.reference,
          check.delayed_reference,
          check.limits[0] < 0
              ? static_cast<SimulationTick>(-check.limits[0]) : 0);
      delayed_copy(
          *check.data,
          check.delayed_data,
          check.limits[1] < 0
              ? static_cast<SimulationTick>(-check.limits[1]) : 0);
    } else if (check.kind == ModuleTimingCheckKind::recrem) {
      delayed_copy(
          check.reference,
          check.delayed_reference,
          check.limits[1] < 0
              ? static_cast<SimulationTick>(-check.limits[1]) : 0);
      delayed_copy(
          *check.data,
          check.delayed_data,
          check.limits[0] < 0
              ? static_cast<SimulationTick>(-check.limits[0]) : 0);
    }
    bool violation = false;
    const auto positive_elapsed = [&](
        const std::optional<SimulationTick> stamp)
        -> std::optional<std::int64_t> {
      if (!stamp || now - *stamp
          > static_cast<SimulationTick>(
              std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
      }
      return static_cast<std::int64_t>(now - *stamp);
    };
    const auto too_recent = [&](const std::optional<SimulationTick> stamp,
                                const std::int64_t limit) {
      const auto elapsed = positive_elapsed(stamp);
      return elapsed && *elapsed < limit;
    };
    const bool timestamp_enabled =
        expression_enabled(check.timestamp_condition);
    const bool timecheck_enabled =
        expression_enabled(check.timecheck_condition);
    const auto in_open_window = [](
        const std::int64_t offset,
        const std::int64_t before_limit,
        const std::int64_t after_limit) {
      return offset > -before_limit && offset < after_limit;
    };
    const auto schedule_deadline = [&](const std::int64_t limit) {
      scheduler.cancel(state.deadline);
      state.active = true;
      state.deadline = scheduler.schedule_after_cancelable(
          static_cast<SimulationTick>(limit),
          SchedulerPhase::postponed,
          static_cast<StableOrder>(index),
          [owner = this, index](Scheduler&) {
            auto& deadline_state =
                owner->module_timing_check_states[index];
            if (!deadline_state.active) return;
            owner->report_module_timing_violation(index);
            if (!owner->module_timing_checks[index].remain_active) {
              deadline_state.active = false;
            }
          });
    };
    switch (check.kind) {
      case ModuleTimingCheckKind::setup:
        if (reference) {
          violation = too_recent(state.last_data, check.limits[0]);
        }
        break;
      case ModuleTimingCheckKind::recovery:
        if (data) {
          violation = too_recent(state.last_reference, check.limits[0]);
        }
        break;
      case ModuleTimingCheckKind::hold:
        if (data) {
          violation = too_recent(state.last_reference, check.limits[0]);
        }
        break;
      case ModuleTimingCheckKind::removal:
        if (reference) {
          violation = too_recent(state.last_data, check.limits[0]);
        }
        break;
      case ModuleTimingCheckKind::skew:
        if (reference && state.last_data) {
          const auto elapsed = positive_elapsed(state.last_data);
          violation = elapsed && *elapsed > check.limits[0];
        }
        if (data && state.last_reference) {
          const auto elapsed = positive_elapsed(state.last_reference);
          violation = violation
              || (elapsed && *elapsed > check.limits[0]);
        }
        break;
      case ModuleTimingCheckKind::timeskew:
        if (raw_reference && !reference && !check.remain_active) {
          scheduler.cancel(state.deadline);
          state.active = false;
        }
        if (reference) {
          scheduler.cancel(state.deadline);
          state.active = true;
          state.last_reference = now;
          if (!check.event_based) {
            schedule_deadline(check.limits[0]);
          }
        }
        if (data && state.active) {
          scheduler.cancel(state.deadline);
          const auto elapsed = positive_elapsed(state.last_reference);
          violation = check.event_based && elapsed
              && *elapsed > check.limits[0];
          if (!violation || !check.remain_active) state.active = false;
        }
        break;
      case ModuleTimingCheckKind::period:
        if (reference) {
          violation = too_recent(state.last_reference, check.limits[0]);
        }
        break;
      case ModuleTimingCheckKind::width:
        if (reference) {
          const auto elapsed = positive_elapsed(
              state.last_terminal_change);
          violation = elapsed && *elapsed < check.limits[0]
              && (!check.threshold
                  || static_cast<SimulationTick>(*elapsed)
                      > *check.threshold);
        }
        break;
      case ModuleTimingCheckKind::setuphold:
        if (reference && timecheck_enabled) {
          const auto elapsed = positive_elapsed(state.last_data);
          violation = elapsed
              && in_open_window(
                  -*elapsed, check.limits[0], check.limits[1]);
        }
        if (data && timecheck_enabled) {
          const auto elapsed = positive_elapsed(state.last_reference);
          violation = violation || (elapsed
              && in_open_window(
                  *elapsed, check.limits[0], check.limits[1]));
        }
        break;
      case ModuleTimingCheckKind::recrem:
        if (reference && timecheck_enabled) {
          const auto elapsed = positive_elapsed(state.last_data);
          violation = elapsed
              && in_open_window(
                  *elapsed, check.limits[0], check.limits[1]);
        }
        if (data && timecheck_enabled) {
          const auto elapsed = positive_elapsed(state.last_reference);
          violation = violation || (elapsed
              && in_open_window(
                  -*elapsed, check.limits[0], check.limits[1]));
        }
        break;
      case ModuleTimingCheckKind::fullskew:
        if (((raw_reference && !reference)
             || (raw_data && !data))
            && !check.remain_active) {
          scheduler.cancel(state.deadline);
          state.active = false;
        }
        if (reference && state.last_data) {
          const auto elapsed = positive_elapsed(state.last_data);
          violation = elapsed && *elapsed > check.limits[1];
        }
        if (data && state.last_reference) {
          const auto elapsed = positive_elapsed(state.last_reference);
          violation = violation
              || (elapsed && *elapsed > check.limits[0]);
        }
        if (reference || data) {
          scheduler.cancel(state.deadline);
          if (violation && !check.remain_active) {
            state.active = false;
          } else if (!check.event_based) {
            schedule_deadline(
                reference ? check.limits[0] : check.limits[1]);
          } else {
            state.active = true;
          }
        }
        break;
      case ModuleTimingCheckKind::nochange:
        if (data && state.last_reference) {
          const auto elapsed = positive_elapsed(state.last_reference);
          violation = elapsed
              && *elapsed >= check.limits[0]
              && *elapsed <= check.limits[1];
        }
        if (reference && state.last_data) {
          const auto elapsed = positive_elapsed(state.last_data);
          violation = violation || (elapsed
              && -*elapsed >= check.limits[0]
              && -*elapsed <= check.limits[1]);
        }
        break;
    }
    if (reference && timestamp_enabled) state.last_reference = now;
    if (data && timestamp_enabled) state.last_data = now;
    if (check.reference.terminal.signal == changed) {
      state.last_terminal_change = now;
    }
    if (violation) report_module_timing_violation(index);
  }
}

void Interpreter::Impl::report_module_timing_violation(
    const std::size_t check_index) {
  const auto& check = module_timing_checks.at(check_index);
  if (report_hook) {
    report_hook(
        0,
        "Verilog specify timing-check violation",
        AssertionSeverity::error,
        check.source,
        scheduler.now(),
        scheduler.delta());
  }
  if (!check.notifier) return;
  const auto current = signals[*check.notifier].initial_value.get(0);
  const auto toggled = current == Logic4::zero
      ? Logic4::one : current == Logic4::one
          ? Logic4::zero : Logic4::x;
  stage_update(*check.notifier, PackedLogic4{1U, toggled});
}

}  // namespace fsim::runtime::simir
