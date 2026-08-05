// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <functional>

namespace fsim::elaboration {
namespace {

const frontend::Expression* expression_terminal_base(
    const frontend::Expression& expression) {
  if ((expression.kind == frontend::ExpressionKind::Index
       || expression.kind == frontend::ExpressionKind::Slice)
      && !expression.operands.empty()) {
    return expression_terminal_base(expression.operands.front());
  }
  return &expression;
}

runtime::PackedLogic4 packed_constant(
    const elaboration_detail::SystemVerilogConstantValue& constant) {
  auto result = runtime::PackedLogic4{
      constant.width, runtime::Logic4::zero};
  for (std::uint32_t bit = 0; bit < constant.width; ++bit) {
    const auto mask = std::uint64_t{1} << bit;
    if ((constant.unknown_bits & mask) != 0) {
      result.set(
          bit,
          (constant.high_impedance_bits & mask) != 0
              ? runtime::Logic4::z : runtime::Logic4::x);
    } else if ((constant.bits & mask) != 0) {
      result.set(bit, runtime::Logic4::one);
    }
  }
  return result;
}

}  // namespace

std::optional<runtime::simir::ModulePathExpression>
HierarchyBuilder::compile_verilog_specify_expression(
    const frontend::Expression& expression,
    const SignalMap& signals,
    const ConstantEnvironment& parameter_environment,
    const std::string_view role) {
  using frontend::ExpressionKind;
  using runtime::simir::BinaryOperator;
  using runtime::simir::LogicalBinaryOperator;
  using runtime::simir::ModulePathExpression;
  using runtime::simir::ModulePathExpressionNode;
  using runtime::simir::ModulePathExpressionOperator;
  using runtime::simir::ReductionOperator;
  using runtime::simir::ShiftOperator;

  ModulePathExpression program;
  bool failed{};
  const auto append = [&](ModulePathExpressionNode node)
      -> std::optional<std::uint32_t> {
    if (program.nodes.size()
        >= runtime::simir::maximum_module_path_expression_storage_bytes
            / sizeof(ModulePathExpressionNode)
        || program.nodes.size()
            > std::numeric_limits<std::uint32_t>::max()) {
      failed = true;
      return std::nullopt;
    }
    const auto id = static_cast<std::uint32_t>(program.nodes.size());
    program.nodes.push_back(std::move(node));
    return id;
  };
  std::function<std::optional<std::uint32_t>(
      const frontend::Expression&)> compile;
  compile = [&](const frontend::Expression& candidate)
      -> std::optional<std::uint32_t> {
    std::string constant_error;
    if (const auto constant =
            elaboration_detail::evaluate_systemverilog_constant_expression(
                candidate, {}, parameter_environment, constant_error)) {
      ModulePathExpressionNode node;
      node.operation = ModulePathExpressionOperator::constant;
      node.constant = packed_constant(*constant);
      node.width = constant->width;
      node.is_signed = constant->is_signed;
      return append(std::move(node));
    }

    const auto* base = expression_terminal_base(candidate);
    if (base->kind == ExpressionKind::Identifier) {
      if (const auto signal = signals.find(base->text);
          signal != signals.end()) {
        if (const auto terminal =
                elaboration_detail::resolve_verilog_specify_selection(
                    candidate,
                    signal->second,
                    design_.signal_info_.at(signal->second),
                    parameter_environment)) {
          ModulePathExpressionNode node;
          node.operation = ModulePathExpressionOperator::terminal;
          node.terminal = runtime::simir::ModulePathTerminal{
              terminal->signal, terminal->offset, terminal->width};
          node.width = terminal->width;
          node.is_signed =
              design_.signal_info_.at(signal->second).is_signed;
          return append(std::move(node));
        }
      }
    }

    const auto compile_operands = [&]()
        -> std::optional<std::vector<std::uint32_t>> {
      std::vector<std::uint32_t> result;
      result.reserve(candidate.operands.size());
      for (const auto& operand : candidate.operands) {
        const auto id = compile(operand);
        if (!id) return std::nullopt;
        result.push_back(*id);
      }
      return result;
    };
    if (candidate.kind == ExpressionKind::Unary
        && candidate.operands.size() == 1) {
      const auto source = compile(candidate.operands.front());
      if (!source) return std::nullopt;
      if (candidate.text == "+") return source;
      ModulePathExpressionNode node;
      node.operands = {*source};
      node.width = program.nodes[*source].width;
      node.is_signed = program.nodes[*source].is_signed;
      if (candidate.text == "~") {
        node.operation = ModulePathExpressionOperator::bit_not;
      } else if (candidate.text == "!") {
        node.operation = ModulePathExpressionOperator::logical_not;
        node.width = 1;
        node.is_signed = false;
      } else if (candidate.text == "&" || candidate.text == "|"
                 || candidate.text == "^" || candidate.text == "~&"
                 || candidate.text == "~|" || candidate.text == "~^"
                 || candidate.text == "^~") {
        node.operation = ModulePathExpressionOperator::reduction;
        node.width = 1;
        node.is_signed = false;
        node.reduction = candidate.text.find('&') != std::string::npos
            ? ReductionOperator::bit_and
            : candidate.text.find('|') != std::string::npos
                ? ReductionOperator::bit_or : ReductionOperator::bit_xor;
        const auto reduced = append(std::move(node));
        if (!reduced || candidate.text.front() != '~') return reduced;
        ModulePathExpressionNode invert;
        invert.operation = ModulePathExpressionOperator::bit_not;
        invert.operands = {*reduced};
        invert.width = 1;
        return append(std::move(invert));
      } else if (candidate.text == "-") {
        ModulePathExpressionNode zero;
        zero.operation = ModulePathExpressionOperator::constant;
        zero.constant = runtime::PackedLogic4{
            node.width, runtime::Logic4::zero};
        zero.width = node.width;
        zero.is_signed = node.is_signed;
        const auto zero_id = append(std::move(zero));
        if (!zero_id) return std::nullopt;
        node.operation = ModulePathExpressionOperator::binary;
        node.binary = node.is_signed
            ? BinaryOperator::subtract_signed
            : BinaryOperator::subtract_unsigned;
        node.operands = {*zero_id, *source};
      } else {
        return std::nullopt;
      }
      return append(std::move(node));
    }
    if (candidate.kind == ExpressionKind::Concatenation) {
      const auto operands = compile_operands();
      if (!operands || operands->empty()) return std::nullopt;
      std::uint64_t width{};
      for (const auto operand : *operands) {
        width += program.nodes[operand].width;
      }
      if (width > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
      }
      ModulePathExpressionNode node;
      node.operation = ModulePathExpressionOperator::concatenate;
      node.operands = *operands;
      node.width = static_cast<std::uint32_t>(width);
      return append(std::move(node));
    }
    if (candidate.kind != ExpressionKind::Binary
        || (candidate.operands.size() != 2
            && !(candidate.text == "?:"
                 && candidate.operands.size() == 3))) {
      return std::nullopt;
    }
    const auto operands = compile_operands();
    if (!operands) return std::nullopt;
    ModulePathExpressionNode node;
    node.operands = *operands;
    if (candidate.text == "?:") {
      node.operation = ModulePathExpressionOperator::conditional;
      node.width = std::max(
          program.nodes[(*operands)[1]].width,
          program.nodes[(*operands)[2]].width);
      node.is_signed = program.nodes[(*operands)[1]].is_signed
          && program.nodes[(*operands)[2]].is_signed;
      return append(std::move(node));
    }
    if (candidate.text == "&&" || candidate.text == "||") {
      node.operation = ModulePathExpressionOperator::logical_binary;
      node.logical = candidate.text == "&&"
          ? LogicalBinaryOperator::logical_and
          : LogicalBinaryOperator::logical_or;
      node.width = 1;
      return append(std::move(node));
    }
    if (candidate.text == "<<" || candidate.text == "<<<"
        || candidate.text == ">>" || candidate.text == ">>>") {
      node.operation = ModulePathExpressionOperator::shift;
      node.shift = candidate.text == "<<" || candidate.text == "<<<"
          ? ShiftOperator::logical_left
          : candidate.text == ">>>"
              ? ShiftOperator::arithmetic_right
              : ShiftOperator::logical_right;
      node.width = program.nodes[(*operands)[0]].width;
      node.is_signed = program.nodes[(*operands)[0]].is_signed;
      return append(std::move(node));
    }
    node.operation = ModulePathExpressionOperator::binary;
    const bool signed_operation = program.nodes[(*operands)[0]].is_signed
        && program.nodes[(*operands)[1]].is_signed;
    const auto select = [&](const BinaryOperator unsigned_op,
                            const BinaryOperator signed_op) {
      node.binary = signed_operation ? signed_op : unsigned_op;
    };
    bool invert{};
    bool relational{};
    if (candidate.text == "&") node.binary = BinaryOperator::bit_and;
    else if (candidate.text == "|") node.binary = BinaryOperator::bit_or;
    else if (candidate.text == "^" || candidate.text == "~^"
             || candidate.text == "^~") {
      node.binary = BinaryOperator::bit_xor;
      invert = candidate.text != "^";
    } else if (candidate.text == "+") {
      select(BinaryOperator::add_unsigned, BinaryOperator::add_signed);
    } else if (candidate.text == "-") {
      select(BinaryOperator::subtract_unsigned, BinaryOperator::subtract_signed);
    } else if (candidate.text == "*") {
      select(BinaryOperator::multiply_unsigned, BinaryOperator::multiply_signed);
    } else if (candidate.text == "**") {
      select(BinaryOperator::power_unsigned, BinaryOperator::power_signed);
    } else if (candidate.text == "/") {
      select(BinaryOperator::divide_unsigned, BinaryOperator::divide_signed);
    } else if (candidate.text == "%") {
      select(BinaryOperator::modulo_unsigned, BinaryOperator::modulo_signed);
    } else if (candidate.text == "==") node.binary = BinaryOperator::equal;
    else if (candidate.text == "!=") node.binary = BinaryOperator::not_equal;
    else if (candidate.text == "===" || candidate.text == "!==") {
      node.binary = BinaryOperator::case_equal;
      invert = candidate.text == "!==";
    } else if (candidate.text == "<") {
      select(BinaryOperator::less_unsigned, BinaryOperator::less_signed);
      relational = true;
    } else if (candidate.text == "<=") {
      select(BinaryOperator::less_equal_unsigned, BinaryOperator::less_equal_signed);
      relational = true;
    } else if (candidate.text == ">") {
      select(BinaryOperator::greater_unsigned, BinaryOperator::greater_signed);
      relational = true;
    } else if (candidate.text == ">=") {
      select(BinaryOperator::greater_equal_unsigned, BinaryOperator::greater_equal_signed);
      relational = true;
    } else {
      return std::nullopt;
    }
    const bool comparison = relational || candidate.text == "=="
        || candidate.text == "!=" || candidate.text == "==="
        || candidate.text == "!==";
    node.width = comparison ? 1U : std::max(
        program.nodes[(*operands)[0]].width,
        program.nodes[(*operands)[1]].width);
    node.is_signed = !comparison && signed_operation;
    const auto result = append(std::move(node));
    if (!result || !invert) return result;
    ModulePathExpressionNode inverse;
    inverse.operation = comparison
        ? ModulePathExpressionOperator::logical_not
        : ModulePathExpressionOperator::bit_not;
    inverse.operands = {*result};
    inverse.width = program.nodes[*result].width;
    return append(std::move(inverse));
  };

  const auto root = compile(expression);
  if (!root || failed) {
    report(
        "FSIM-ELAB-SVSPEC-008",
        "specify " + std::string{role}
            + " is not a bounded executable integral expression",
        expression.span);
    return std::nullopt;
  }
  program.root = *root;
  return program;
}

}  // namespace fsim::elaboration
