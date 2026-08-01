// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

bool writable_procedure_actual(const Expression& expression) {
    if (expression.kind == ExpressionKind::Identifier) {
        return true;
    }
    return (expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2)
        || (expression.kind == ExpressionKind::Slice
            && expression.operands.size() == 3);
}

} // namespace

void Lowerer::initialize_procedure_support() {
    procedure_frames_.clear();
    procedure_indices_.clear();
    pending_procedures_.clear();
    procedure_dependencies_.clear();
    active_procedure_.reset();
    procedure_return_jumps_.clear();
    procedure_call_stack_ = {};
    procedure_support_initialized_ = false;
    if (procedures_.empty()) {
        return;
    }
    if (procedures_.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-VHPROC-019",
            "too many visible VHDL procedures for the SimIR call stack",
            procedures_.front().span);
        return;
    }

    procedure_frames_.reserve(procedures_.size());
    for (const auto& procedure : procedures_) {
        auto& overloads = procedure_indices_[procedure.name];
        const bool duplicate_profile = std::ranges::any_of(
            overloads,
            [&](const std::size_t candidate_index) {
              const auto& candidate =
                  *procedure_frames_[candidate_index].source;
              const bool imported_distinct_declarations =
                  !candidate.visibility_owner.empty()
                  && !procedure.visibility_owner.empty()
                  && candidate.visibility_owner
                      != procedure.visibility_owner;
              if (candidate.arguments.size()
                      != procedure.arguments.size()
                  || imported_distinct_declarations) {
                return false;
              }
              for (std::size_t argument = 0;
                   argument < procedure.arguments.size();
                   ++argument) {
                const auto& left = candidate.arguments[argument];
                const auto& right = procedure.arguments[argument];
                if (left.direction != right.direction
                    || left.object_class != right.object_class
                    || !vhdl_callable_type_matches(
                        left.type, right.type)) {
                  return false;
                }
              }
              return true;
            });
        if (duplicate_profile) {
            report(
                "FSIM-ELAB-VHOVER-006",
                "duplicate VHDL procedure profile '"
                    + procedure.name + "'",
                procedure.span);
            continue;
        }
        const auto index = procedure_frames_.size();
        ProcedureFrame frame;
        frame.source = &procedure;
        procedure_frames_.push_back(std::move(frame));
        overloads.push_back(index);
    }
    procedure_dependencies_.resize(procedure_frames_.size());
    if (procedure_frames_.empty()) {
        return;
    }
    for (const auto& frame : procedure_frames_) {
        const auto& procedure = *frame.source;
        for (const auto& argument : procedure.arguments) {
            if (argument.default_value
                && !vhdl_expression_matches_type(
                    *argument.default_value, argument.type)) {
                report(
                    "FSIM-ELAB-VHLEGAL-008",
                    "default for VHDL procedure formal '"
                        + argument.name
                        + "' does not match its subtype",
                    argument.default_value->span);
            }
        }
    }

    procedure_call_stack_.pointer =
        allocate_register(32, frontend::ValueDomain::Bit2);
    procedure_call_stack_.entries = next_register_;
    procedure_call_stack_.capacity =
        static_cast<std::uint32_t>(procedure_frames_.size());
    process_.operations.emplace_back(LoadConstant{
        procedure_call_stack_.pointer, unsigned_value(0, 32)});
    for (std::uint32_t index = 0;
         index < procedure_call_stack_.capacity; ++index) {
        const auto entry =
            allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant{entry, unsigned_value(0, 32)});
    }
    procedure_support_initialized_ = true;
}

void Lowerer::lower_procedure_call(const Statement& statement) {
    if (!procedure_support_initialized_) {
        report(
            "FSIM-ELAB-VHPROC-014",
            "unknown VHDL procedure '" + statement.procedure_name + "'",
            statement.span);
        return;
    }
    const auto selected = select_procedure_overload(statement);
    if (!selected.named) {
        report(
            "FSIM-ELAB-VHPROC-014",
            "unknown VHDL procedure '" + statement.procedure_name + "'",
            statement.span);
        return;
    }
    if (!selected.index) {
        return;
    }
    const auto procedure_index = *selected.index;
    auto& frame = procedure_frames_[procedure_index];
    const auto& procedure = *frame.source;

    std::vector<const frontend::Expression*> actuals(
        procedure.arguments.size());
    std::size_t next_positional = 0;
    bool saw_named = false;
    for (const auto& association :
         statement.procedure_arguments) {
        std::optional<std::size_t> index;
        if (association.formal) {
            saw_named = true;
            const auto formal = std::ranges::find_if(
                procedure.arguments,
                [&](const auto& candidate) {
                    return candidate.name == *association.formal;
                });
            if (formal == procedure.arguments.end()) {
                report(
                    "FSIM-ELAB-VHPROC-015",
                    "procedure '" + procedure.name
                        + "' has no formal parameter '"
                        + *association.formal + "'",
                    association.span);
                continue;
            }
            index = static_cast<std::size_t>(
                std::distance(
                    procedure.arguments.begin(), formal));
        } else {
            if (saw_named) {
                report(
                    "FSIM-ELAB-VHPROC-016",
                    "a positional procedure actual cannot follow a "
                    "named actual",
                    association.span);
            }
            if (next_positional >= procedure.arguments.size()) {
                report(
                    "FSIM-ELAB-VHPROC-017",
                    "too many actual parameters for procedure '"
                        + procedure.name + "'",
                    association.span);
                continue;
            }
            index = next_positional++;
        }
        if (actuals[*index] != nullptr) {
            report(
                "FSIM-ELAB-VHPROC-015",
                "duplicate actual for procedure formal '"
                    + procedure.arguments[*index].name + "'",
                association.span);
            continue;
        }
        actuals[*index] = &association.value;
    }
    for (std::size_t index = 0;
         index < actuals.size(); ++index) {
      if (actuals[index] == nullptr) {
        if (procedure.arguments[index].default_value) {
          actuals[index] =
              &*procedure.arguments[index].default_value;
        } else {
          report(
              "FSIM-ELAB-VHPROC-017",
              "procedure '" + procedure.name
                  + "' requires an actual for formal '"
                  + procedure.arguments[index].name + "'",
              statement.span);
        }
      }
    }
    if (std::ranges::any_of(
            actuals,
            [](const auto* actual) {
                return actual == nullptr;
            })) {
        return;
    }

    if (!frame.allocated) {
        frame.arguments.reserve(procedure.arguments.size());
        for (const auto& argument : procedure.arguments) {
            const auto width = argument.type.width();
            if (!width || *width == 0 || *width > 64) {
                report(
                    "FSIM-ELAB-VHPROC-020",
                    "procedure argument '" + argument.name
                        + "' must have an executable width in [1, 64]",
                    argument.span);
                return;
            }
            frame.arguments.push_back(allocate_register(
                static_cast<std::size_t>(*width),
                argument.type.domain));
        }
        frame.allocated = true;
    }

    for (std::size_t index = 0;
         index < procedure.arguments.size(); ++index) {
        const auto& formal = procedure.arguments[index];
        const auto& actual = *actuals[index];
        const auto width =
            static_cast<std::size_t>(*formal.type.width());
        const bool writable =
            writable_procedure_actual(actual);
        if ((formal.object_class
                 == frontend::InterfaceObjectClass::Variable
             || formal.direction
                 != frontend::PortDirection::Input)
            && !writable) {
            report(
                "FSIM-ELAB-VHPROC-018",
                "variable-class, output, and inout procedure formal '"
                    + formal.name
                    + "' requires a writable signal or variable actual",
                actual.span);
            return;
        }
        if (formal.direction == frontend::PortDirection::Output) {
            process_.operations.emplace_back(LoadConstant{
                frame.arguments[index],
                default_packed_value(formal.type, width)});
            continue;
        }
        auto value = lower_expression(
            actual, width, &formal.type);
        if (!value) {
            return;
        }
        if (register_width(*value) != width) {
            *value = resize_register(
                *value, width, is_signed_expression(actual));
        }
        process_.operations.emplace_back(
            CopyRegister{frame.arguments[index], *value});
    }

    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call{
        frame.target.value_or(0),
        static_cast<InstructionIndex>(call_site + 1U),
        procedure_call_stack_});
    if (frame.target) {
        fsim::runtime::simir::operation_get<Call>(process_.operations[call_site]).target =
            *frame.target;
    } else {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_procedures_.push_back(procedure_index);
    }
    if (active_procedure_) {
        procedure_dependencies_[*active_procedure_].insert(
            procedure_index);
    }

    for (std::size_t index = 0;
         index < procedure.arguments.size(); ++index) {
        const auto& formal = procedure.arguments[index];
        if (formal.direction == frontend::PortDirection::Input) {
            continue;
        }
        const auto temporary =
            "@procedure_copyout_" + std::to_string(procedure_index)
            + "_" + std::to_string(index);
        locals_.insert_or_assign(
            temporary, frame.arguments[index]);
        local_signed_.insert_or_assign(
            temporary, formal.type.is_signed);
        local_ranges_.insert_or_assign(
            temporary, formal.type.packed_range);
        local_integer_ranges_.insert_or_assign(
            temporary, formal.type.integer_range);
        local_members_.insert_or_assign(
            temporary, formal.type.packed_members);
        local_types_.insert_or_assign(temporary, &formal.type);

        Statement copy_out;
        copy_out.kind = StatementKind::Assignment;
        copy_out.assignment_kind = AssignmentKind::Blocking;
        copy_out.target = *actuals[index];
        copy_out.value = Expression{
            ExpressionKind::Identifier,
            temporary,
            {},
            statement.span};
        copy_out.span = statement.span;
        lower_assignment(copy_out);
    }
}

void Lowerer::lower_procedure_return(
    const Statement& statement) {
    if (!active_procedure_) {
        report(
            "FSIM-ELAB-VHPROC-021",
            "procedure return statement is outside an executable "
            "procedure",
            statement.span);
        return;
    }
    if (statement.value.valid()) {
        report(
            "FSIM-ELAB-VHPROC-021",
            "VHDL procedure return cannot carry a value",
            statement.span);
    }
    procedure_return_jumps_.push_back(
        static_cast<InstructionIndex>(
            process_.operations.size()));
    process_.operations.emplace_back(Jump{});
}

void Lowerer::lower_procedure_body(
    const std::size_t procedure_index) {
    auto& frame = procedure_frames_[procedure_index];
    if (frame.lowered || !frame.allocated) {
        return;
    }
    frame.target = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto call_site : frame.call_sites) {
        fsim::runtime::simir::operation_get<Call>(process_.operations[call_site]).target =
            *frame.target;
    }
    frame.call_sites.clear();
    frame.lowered = true;

    auto saved_locals = std::move(locals_);
    auto saved_signed = std::move(local_signed_);
    auto saved_ranges = std::move(local_ranges_);
    auto saved_integer_ranges =
        std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    auto saved_loop_controls = std::move(loop_controls_);
    auto saved_return_jumps =
        std::move(procedure_return_jumps_);
    const auto saved_active = active_procedure_;
    locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    local_scope_ = {frame.source->name};
    loop_controls_.clear();
    procedure_return_jumps_.clear();
    active_procedure_ = procedure_index;

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
    for (std::size_t index = 0;
         index < frame.source->arguments.size(); ++index) {
        const auto& argument = frame.source->arguments[index];
        bind(
            argument.name,
            argument.type,
            frame.arguments[index]);
        auto debug_name = scoped_local_name(argument.name);
        if (!debug_local_names_.emplace(debug_name).second) {
            debug_name += "@"
                + std::to_string(argument.span.begin.line)
                + ":" + std::to_string(
                    argument.span.begin.column);
            debug_local_names_.emplace(debug_name);
        }
        process_.debug_locals.push_back(DebugLocal{
            std::move(debug_name),
            argument.type.spelling,
            frame.arguments[index],
            static_cast<std::size_t>(*argument.type.width()),
            SourceLocation{
                argument.span.source_name,
                static_cast<std::uint32_t>(
                    argument.span.begin.line),
                static_cast<std::uint32_t>(
                    argument.span.begin.column)},
            {},
            {},
            value_kind(argument.type.domain),
            argument.type.enumeration_literals});
        if (argument.type.integer_range) {
            const auto [lower, upper] =
                integer_bounds(argument.type.integer_range);
            process_.debug_locals.back().integer_lower = lower;
            process_.debug_locals.back().integer_upper = upper;
        }
    }
    initialize_variables(frame.source->variables);
    lower_statements(frame.source->statements);
    const auto epilogue = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto jump : procedure_return_jumps_) {
        process_.operations[jump] = Jump{epilogue};
    }
    process_.operations.emplace_back(
        Return{procedure_call_stack_});

    active_procedure_ = saved_active;
    procedure_return_jumps_ =
        std::move(saved_return_jumps);
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

void Lowerer::diagnose_procedure_cycles() {
    std::vector<std::uint8_t> state(
        procedure_frames_.size(), 0);
    const auto visit =
        [&](const auto& self, const std::size_t index) -> bool {
          if (state[index] == 1) {
              report(
                  "FSIM-ELAB-VHPROC-010",
                  "recursive procedure call graph involving '"
                      + procedure_frames_[index].source->name
                      + "' is not supported",
                  procedure_frames_[index].source->span);
              return true;
          }
          if (state[index] == 2) {
              return false;
          }
          state[index] = 1;
          for (const auto dependency :
               procedure_dependencies_[index]) {
              if (self(self, dependency)) {
                  state[index] = 2;
                  return true;
              }
          }
          state[index] = 2;
          return false;
        };
    for (std::size_t index = 0;
         index < procedure_frames_.size(); ++index) {
        if (state[index] == 0) {
            (void)visit(visit, index);
        }
    }
}

void Lowerer::lower_pending_procedures() {
    while (!pending_procedures_.empty()) {
        const auto procedure = pending_procedures_.front();
        pending_procedures_.pop_front();
        procedure_frames_[procedure].queued = false;
        lower_procedure_body(procedure);
    }
    diagnose_procedure_cycles();
}

} // namespace fsim::elaboration
