// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

const frontend::FunctionDeclaration* Lowerer::visible_function(
    const std::string_view name) const {
    const auto found = function_indices_.find(std::string{name});
    return found == function_indices_.end()
        ? nullptr
        : function_frames_[found->second].source;
}

void Lowerer::initialize_function_support() {
    function_frames_.clear();
    function_indices_.clear();
    pending_functions_.clear();
    function_dependencies_.clear();
    active_function_.reset();
    function_return_jumps_.clear();
    function_call_stack_ = {};
    function_support_initialized_ = false;
    if (functions_.empty()) {
        return;
    }
    if (functions_.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-SVFUNC-001",
            "too many visible functions for the SimIR call stack",
            functions_.front().span);
        return;
    }

    function_frames_.reserve(functions_.size());
    for (const auto& function : functions_) {
        const auto index = function_frames_.size();
        const auto [existing, inserted] =
            function_indices_.emplace(function.name, index);
        if (!inserted) {
            report(
                "FSIM-ELAB-SVFUNC-002",
                "ambiguous visible function '" + function.name + "'",
                function.span);
            (void)existing;
            continue;
        }
        FunctionFrame frame;
        frame.source = &function;
        function_frames_.push_back(std::move(frame));
    }
    function_dependencies_.resize(function_frames_.size());
    if (function_frames_.empty()) {
        return;
    }

    function_call_stack_.pointer =
        allocate_register(32, frontend::ValueDomain::Bit2);
    function_call_stack_.entries = next_register_;
    function_call_stack_.capacity =
        static_cast<std::uint32_t>(function_frames_.size());
    process_.operations.emplace_back(LoadConstant{
        function_call_stack_.pointer, unsigned_value(0, 32)});
    for (std::uint32_t index = 0;
         index < function_call_stack_.capacity; ++index) {
        const auto entry =
            allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant{entry, unsigned_value(0, 32)});
    }
    function_support_initialized_ = true;
}

Lowerer::ExpressionAttempt Lowerer::lower_user_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type*) {
    if (expression.kind != ExpressionKind::Call
        || !function_support_initialized_) {
        return ExpressionAttempt{};
    }
    const auto found = function_indices_.find(expression.text);
    if (found == function_indices_.end()) {
        return ExpressionAttempt{};
    }
    const auto function_index = found->second;
    auto& frame = function_frames_[function_index];
    const auto& function = *frame.source;
    if (expression.operands.size() != function.arguments.size()) {
        report(
            "FSIM-ELAB-SVFUNC-003",
            "function '" + function.name + "' expects "
                + std::to_string(function.arguments.size())
                + " arguments but received "
                + std::to_string(expression.operands.size()),
            expression.span);
        return std::nullopt;
    }

    if (!frame.allocated) {
        const auto return_width = function.return_type.width();
        if (!return_width || *return_width == 0
            || *return_width > 64) {
            report(
                "FSIM-ELAB-SVFUNC-004",
                "function '" + function.name
                    + "' return type must have an executable width in "
                      "[1, 64]",
                function.span);
            return std::nullopt;
        }
        frame.result = allocate_register(
            static_cast<std::size_t>(*return_width),
            function.return_type.domain);
        frame.arguments.reserve(function.arguments.size());
        for (const auto& argument : function.arguments) {
            const auto width = argument.type.width();
            if (!width || *width == 0 || *width > 64) {
                report(
                    "FSIM-ELAB-SVFUNC-004",
                    "function argument '" + argument.name
                        + "' must have an executable width in [1, 64]",
                    argument.span);
                return std::nullopt;
            }
            frame.arguments.push_back(allocate_register(
                static_cast<std::size_t>(*width),
                argument.type.domain));
        }
        frame.allocated = true;
    }

    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        const auto width = static_cast<std::size_t>(
            *formal.type.width());
        auto actual = lower_expression(
            expression.operands[index], width, &formal.type);
        if (!actual) {
            return std::nullopt;
        }
        if (register_width(*actual) != width) {
            *actual = resize_register(
                *actual,
                width,
                is_signed_expression(expression.operands[index]));
        }
        process_.operations.emplace_back(
            CopyRegister{frame.arguments[index], *actual});
    }
    const auto return_width =
        static_cast<std::size_t>(*function.return_type.width());
    process_.operations.emplace_back(LoadConstant{
        frame.result,
        default_packed_value(function.return_type, return_width)});
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call{
        frame.target.value_or(0),
        static_cast<InstructionIndex>(call_site + 1U),
        function_call_stack_});
    if (frame.target) {
        std::get<Call>(process_.operations[call_site]).target =
            *frame.target;
    } else {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_functions_.push_back(function_index);
    }
    if (active_function_) {
        function_dependencies_[*active_function_].insert(
            function_index);
    }

    const auto destination = allocate_register(
        return_width, function.return_type.domain);
    process_.operations.emplace_back(
        CopyRegister{destination, frame.result});
    if (expected_width != 0 && expected_width != return_width) {
        return resize_register(
            destination,
            expected_width,
            function.return_type.is_signed);
    }
    return destination;
}

void Lowerer::lower_function_return(const Statement& statement) {
    if (!active_function_) {
        report(
            "FSIM-ELAB-SVFUNC-005",
            "return statement is outside an executable function",
            statement.span);
        return;
    }
    auto& frame = function_frames_[*active_function_];
    const auto& function = *frame.source;
    const auto width =
        static_cast<std::size_t>(*function.return_type.width());
    if (!statement.value.valid()) {
        report(
            "FSIM-ELAB-SVFUNC-005",
            "function return requires a value",
            statement.span);
    } else {
        auto value = lower_expression(
            statement.value, width, &function.return_type);
        if (value) {
            if (register_width(*value) != width) {
                *value = resize_register(
                    *value,
                    width,
                    is_signed_expression(statement.value));
            }
            process_.operations.emplace_back(
                CopyRegister{frame.result, *value});
        }
    }
    function_return_jumps_.push_back(
        static_cast<InstructionIndex>(
            process_.operations.size()));
    process_.operations.emplace_back(Jump{});
}

void Lowerer::lower_function_body(const std::size_t function_index) {
    auto& frame = function_frames_[function_index];
    if (frame.lowered || !frame.allocated) {
        return;
    }
    frame.target = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto call_site : frame.call_sites) {
        std::get<Call>(process_.operations[call_site]).target =
            *frame.target;
    }
    frame.call_sites.clear();
    frame.lowered = true;

    auto saved_locals = std::move(locals_);
    auto saved_signed = std::move(local_signed_);
    auto saved_ranges = std::move(local_ranges_);
    auto saved_integer_ranges = std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    auto saved_loop_controls = std::move(loop_controls_);
    auto saved_return_jumps = std::move(function_return_jumps_);
    const auto saved_active = active_function_;
    locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    local_scope_ = {frame.source->name};
    loop_controls_.clear();
    function_return_jumps_.clear();
    active_function_ = function_index;

    const auto bind =
        [&](const std::string& name,
            const frontend::Type& type,
            const RegisterId register_id) {
          locals_.insert_or_assign(name, register_id);
          local_signed_.insert_or_assign(name, type.is_signed);
          local_ranges_.insert_or_assign(name, type.packed_range);
          local_integer_ranges_.insert_or_assign(
              name, type.integer_range);
          local_members_.insert_or_assign(
              name, type.packed_members);
          local_types_.insert_or_assign(name, &type);
        };
    bind(
        frame.source->name,
        frame.source->return_type,
        frame.result);
    if (const auto separator =
            frame.source->name.rfind("::");
        separator != std::string::npos) {
        bind(
            frame.source->name.substr(separator + 2),
            frame.source->return_type,
            frame.result);
    }
    for (std::size_t index = 0;
         index < frame.source->arguments.size(); ++index) {
        bind(
            frame.source->arguments[index].name,
            frame.source->arguments[index].type,
            frame.arguments[index]);
    }
    initialize_variables(frame.source->variables);
    lower_statements(frame.source->statements);
    const auto epilogue = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto jump : function_return_jumps_) {
        process_.operations[jump] = Jump{epilogue};
    }
    process_.operations.emplace_back(
        Return{function_call_stack_});

    active_function_ = saved_active;
    function_return_jumps_ = std::move(saved_return_jumps);
    loop_controls_ = std::move(saved_loop_controls);
    local_scope_ = std::move(saved_scope);
    local_types_ = std::move(saved_types);
    local_members_ = std::move(saved_members);
    local_integer_ranges_ =
        std::move(saved_integer_ranges);
    local_ranges_ = std::move(saved_ranges);
    local_signed_ = std::move(saved_signed);
    locals_ = std::move(saved_locals);
}

void Lowerer::diagnose_function_cycles() {
    std::vector<std::uint8_t> state(
        function_frames_.size(), 0);
    const auto visit =
        [&](const auto& self, const std::size_t index) -> bool {
          if (state[index] == 1) {
              report(
                  "FSIM-ELAB-SVFUNC-006",
                  "recursive function call graph involving '"
                      + function_frames_[index].source->name
                      + "' is not supported",
                  function_frames_[index].source->span);
              return true;
          }
          if (state[index] == 2) {
              return false;
          }
          state[index] = 1;
          for (const auto dependency :
               function_dependencies_[index]) {
              if (self(self, dependency)) {
                  state[index] = 2;
                  return true;
              }
          }
          state[index] = 2;
          return false;
        };
    for (std::size_t index = 0;
         index < function_frames_.size(); ++index) {
        if (state[index] == 0) {
            (void)visit(visit, index);
        }
    }
}

void Lowerer::lower_pending_functions() {
    while (!pending_functions_.empty()) {
        const auto function = pending_functions_.front();
        pending_functions_.pop_front();
        function_frames_[function].queued = false;
        lower_function_body(function);
    }
    diagnose_function_cycles();
}

} // namespace fsim::elaboration
