// SPDX-License-Identifier: Apache-2.0
//
// Design-wide sharing of lowered package-level VHDL function bodies: each
// process lowers its own copy of a called subprogram, and a body whose frame
// reads no process state is the same in every process up to register and
// instruction numbering, so later processes renumber the first recording.
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <tuple>
#include <typeinfo>
#include <utility>

namespace fsim::elaboration {

namespace {

// Applies `on_register` to each register operand and `on_target` to each
// instruction target of `operation` when it is one a recorded body may
// hold: register computation and control flow within the body. False for
// any other operation, or when a callback refuses an operand.
template <typename OnRegister, typename OnTarget>
bool for_each_replayable_operand(Operation& operation,
    OnRegister&& on_register, OnTarget&& on_target)
{
    if (auto* op = operation_get_if<LoadConstant>(&operation)) {
        return on_register(op->destination);
    }
    if (auto* op = operation_get_if<CopyRegister>(&operation)) {
        return on_register(op->destination) && on_register(op->source);
    }
    if (auto* op = operation_get_if<UnaryNot>(&operation)) {
        return on_register(op->destination) && on_register(op->source);
    }
    if (auto* op = operation_get_if<ConvertToTwoState>(&operation)) {
        return on_register(op->destination) && on_register(op->source);
    }
    if (auto* op = operation_get_if<LogicalNot>(&operation)) {
        return on_register(op->destination) && on_register(op->source);
    }
    if (auto* op = operation_get_if<LogicalBinary>(&operation)) {
        return on_register(op->destination) && on_register(op->lhs)
            && on_register(op->rhs);
    }
    if (auto* op = operation_get_if<Reduction>(&operation)) {
        return on_register(op->destination) && on_register(op->source);
    }
    if (auto* op = operation_get_if<CountOnes>(&operation)) {
        return on_register(op->destination) && on_register(op->source);
    }
    if (auto* op = operation_get_if<CountBits>(&operation)) {
        return on_register(op->destination) && on_register(op->source);
    }
    if (auto* op = operation_get_if<Shift>(&operation)) {
        return on_register(op->destination) && on_register(op->value)
            && on_register(op->amount);
    }
    if (auto* op = operation_get_if<DynamicPartSelect>(&operation)) {
        return on_register(op->destination) && on_register(op->source)
            && on_register(op->base);
    }
    if (auto* op = operation_get_if<DynamicPartInsert>(&operation)) {
        return on_register(op->destination) && on_register(op->target)
            && on_register(op->source) && on_register(op->selection.base);
    }
    if (auto* op = operation_get_if<IntegerUnary>(&operation)) {
        return on_register(op->destination) && on_register(op->source);
    }
    if (auto* op = operation_get_if<Extract>(&operation)) {
        return on_register(op->destination) && on_register(op->source);
    }
    if (auto* op = operation_get_if<DynamicExtract>(&operation)) {
        return on_register(op->destination) && on_register(op->source)
            && on_register(op->selection.index);
    }
    if (auto* op = operation_get_if<Insert>(&operation)) {
        return on_register(op->destination) && on_register(op->target)
            && on_register(op->source);
    }
    if (auto* op = operation_get_if<DynamicInsert>(&operation)) {
        return on_register(op->destination) && on_register(op->target)
            && on_register(op->source) && on_register(op->selection.index);
    }
    if (auto* op = operation_get_if<Concatenate>(&operation)) {
        return on_register(op->destination)
            && std::ranges::all_of(op->operands, on_register);
    }
    if (auto* op = operation_get_if<Binary>(&operation)) {
        return on_register(op->destination) && on_register(op->lhs)
            && on_register(op->rhs);
    }
    if (auto* op = operation_get_if<IntegerBinary>(&operation)) {
        return on_register(op->destination) && on_register(op->lhs)
            && on_register(op->rhs);
    }
    if (auto* op = operation_get_if<IntegerCheck>(&operation)) {
        return on_register(op->source);
    }
    if (auto* op = operation_get_if<ConditionalSelect>(&operation)) {
        return on_register(op->destination) && on_register(op->condition)
            && on_register(op->when_true) && on_register(op->when_false);
    }
    if (auto* op = operation_get_if<Branch>(&operation)) {
        return on_register(op->condition) && on_target(op->when_true)
            && on_target(op->when_false);
    }
    if (auto* op = operation_get_if<Jump>(&operation)) {
        return on_target(op->target);
    }
    if (const auto* op = operation_get_if<Return>(&operation)) {
        // The call stack is assigned after lowering.
        return op->stack.pointer == 0U && op->stack.entries == 0U
            && op->stack.capacity == 0U;
    }
    return operation_holds<DebugPoint>(operation);
}

// The fields of each replayable operation, for comparing two of a kind.
auto fields(const LoadConstant& op)
{
    return std::tie(op.destination, op.value);
}
auto fields(const CopyRegister& op)
{
    return std::tie(op.destination, op.source);
}
auto fields(const UnaryNot& op)
{
    return std::tie(op.destination, op.source);
}
auto fields(const ConvertToTwoState& op)
{
    return std::tie(op.destination, op.source);
}
auto fields(const LogicalNot& op)
{
    return std::tie(op.destination, op.source);
}
auto fields(const LogicalBinary& op)
{
    return std::tie(op.operation, op.destination, op.lhs, op.rhs);
}
auto fields(const Reduction& op)
{
    return std::tie(op.operation, op.destination, op.source);
}
auto fields(const CountOnes& op)
{
    return std::tie(op.destination, op.source);
}
auto fields(const CountBits& op)
{
    return std::tie(op.destination, op.source, op.state_mask);
}
auto fields(const Shift& op)
{
    return std::tie(op.operation, op.destination, op.value, op.amount,
        op.signed_amount);
}
auto fields(const DynamicPartSelect& op)
{
    return std::tie(op.destination, op.source, op.base, op.left, op.right,
        op.width, op.increasing, op.source_descending, op.two_state,
        op.base_offset);
}
auto fields(const DynamicPartInsert& op)
{
    return std::tie(op.destination, op.target, op.source, op.selection);
}
auto fields(const IntegerUnary& op)
{
    return std::tie(op.operation, op.destination, op.source);
}
auto fields(const Extract& op)
{
    return std::tie(op.destination, op.source, op.offset, op.width);
}
auto fields(const DynamicExtract& op)
{
    return std::tie(op.destination, op.source, op.selection);
}
auto fields(const Insert& op)
{
    return std::tie(op.destination, op.target, op.source, op.offset);
}
auto fields(const DynamicInsert& op)
{
    return std::tie(op.destination, op.target, op.source, op.selection);
}
auto fields(const Concatenate& op)
{
    return std::tie(op.destination, op.operands, op.width);
}
auto fields(const Binary& op)
{
    return std::tie(op.operation, op.destination, op.lhs, op.rhs);
}
auto fields(const IntegerBinary& op)
{
    return std::tie(op.operation, op.destination, op.lhs, op.rhs);
}
auto fields(const IntegerCheck& op)
{
    return std::tie(op.source, op.lower, op.upper);
}
auto fields(const ConditionalSelect& op)
{
    return std::tie(
        op.destination, op.condition, op.when_true, op.when_false);
}
auto fields(const Branch& op)
{
    return std::tie(
        op.condition, op.when_true, op.when_false, op.unknown_policy);
}
auto fields(const Jump& op)
{
    return std::tie(op.target);
}
auto fields(const Return& op)
{
    return std::tie(op.stack.pointer, op.stack.entries, op.stack.capacity);
}
auto fields(const DebugPoint& op)
{
    return std::tie(op.kind, op.source, op.scope);
}

template <typename... Alternatives>
struct ReplayableOperations {
    [[nodiscard]] static bool equal(
        const Operation& left, const Operation& right)
    {
        bool result { };
        const auto matches = [&]<typename Alternative>() {
            const auto* lhs = operation_get_if<Alternative>(&left);
            if (lhs == nullptr) {
                return false;
            }
            const auto* rhs = operation_get_if<Alternative>(&right);
            result = rhs != nullptr && fields(*lhs) == fields(*rhs);
            return true;
        };
        (matches.template operator()<Alternatives>() || ...);
        return result;
    }
};

using ReplayableOperationKinds = ReplayableOperations<LoadConstant,
    CopyRegister, UnaryNot, ConvertToTwoState, LogicalNot, LogicalBinary,
    Reduction, CountOnes, CountBits, Shift, DynamicPartSelect,
    DynamicPartInsert, IntegerUnary, Extract, DynamicExtract, Insert, DynamicInsert,
    Concatenate, Binary, IntegerBinary, IntegerCheck, ConditionalSelect,
    Branch, Jump, Return, DebugPoint>;

} // namespace

bool same_hir_callable_body(const HirCallableBodyRecording& left,
    const HirCallableBodyRecording& right)
{
    if (left.operations.size() != right.operations.size()
        || left.registers != right.registers
        || left.expression_profiles != right.expression_profiles
        || left.invocation_begin != right.invocation_begin
        || left.scalar_ranges.size() != right.scalar_ranges.size()) {
        return false;
    }
    for (std::size_t index { }; index < left.operations.size(); ++index) {
        if (!ReplayableOperationKinds::equal(
                left.operations[index], right.operations[index])) {
            return false;
        }
    }
    auto left_locals = left.body_registers;
    auto right_locals = right.body_registers;
    std::ranges::sort(left_locals);
    std::ranges::sort(right_locals);
    if (left_locals != right_locals) {
        return false;
    }
    // Names depend on the names each process already holds; the requests
    // and the locals' other fields do not.
    if (!std::ranges::equal(left.debug_names, right.debug_names,
            [](const auto& lhs, const auto& rhs) {
                return lhs.declaration == rhs.declaration
                    && lhs.name == rhs.name;
            })
        || !std::ranges::equal(left.debug_locals, right.debug_locals,
            [](const auto& lhs, const auto& rhs) {
                const auto& a = lhs.second;
                const auto& b = rhs.second;
                return lhs.first == rhs.first
                    && a.type_name == b.type_name
                    && a.register_id == b.register_id && a.width == b.width
                    && a.source == b.source
                    && a.integer_lower == b.integer_lower
                    && a.integer_upper == b.integer_upper
                    && a.value_kind == b.value_kind
                    && a.enumeration_literals == b.enumeration_literals
                    && a.systemverilog_scalar == b.systemverilog_scalar;
            })) {
        return false;
    }
    return std::ranges::equal(left.scalar_ranges, right.scalar_ranges,
        [](const auto& lhs, const auto& rhs) {
            return lhs.source == rhs.source && lhs.left == rhs.left
                && lhs.right == rhs.right
                && lhs.descending == rhs.descending;
        });
}

std::optional<std::string> Lowerer::hir_callable_body_memo_key(
    const HirCallableFrame& frame) const
{
    static const bool trace
        = std::getenv("FSIM_HIR_BODY_MEMO_TRACE") != nullptr;
    const auto refuse = [&](const std::string_view reason)
        -> std::optional<std::string> {
        if (trace && !frame.memo_key.empty()) {
            std::cerr << "[fsim-hir-lower] body-memo no key "
                      << frame.memo_key << ": " << reason << '\n';
        }
        return std::nullopt;
    };
    const auto holds = [](const std::vector<bool>& flags) {
        return std::ranges::find(flags, true) != flags.end();
    };
    if (frame.memo_key.empty() || !frame.function || !frame.type.automatic
        || frame.type.string || frame.type.container) {
        return refuse("callable kind");
    }
    if (frame.callee_unit != nullptr || frame.class_receiver
        || frame.class_specialization || frame.interface_receiver
        || !frame.vhdl_protected_members.empty()
        || !frame.vhdl_protected_formals.empty()
        || frame.vhdl_protected_receiver || frame.vhdl_protected_index) {
        return refuse("receiver");
    }
    if (frame.vhdl_shaped_result || !frame.vhdl_shaped_formals.empty()
        || !frame.signal_formals.empty()
        || holds(frame.argument_is_string)
        || holds(frame.argument_is_container)) {
        return refuse("formals");
    }
    // A static integer actual binds its formal like a generic; the frame key
    // holds its value.
    const auto static_actual = [&](const HirGenericBinding& binding) {
        return std::ranges::find(frame.formals, binding.formal)
            != frame.formals.end()
            && std::ranges::find(frame.static_integer_bindings,
                   binding.formal,
                   &std::pair<semantic::DeclarationId, std::int64_t>::first)
            != frame.static_integer_bindings.end();
    };
    if (!std::ranges::all_of(frame.generic_bindings, static_actual)
        || !frame.generic_environment.empty()
        || frame.enclosing_callable_invocation_identity
        || !frame.static_variables.empty()
        || !frame.static_string_variables.empty()
        || !frame.static_container_variables.empty()
        || specialized_hir_unit_ == nullptr) {
        return refuse("environment");
    }
    // Declared in a package, where its body names only the package's
    // declarations and those it uses, not a process's or an instance's
    // (IEEE 1076-2008 4.7, 12.1).
    const auto& semantics = specialized_hir_unit_->design().semantics;
    const auto& scopes = semantics.scopes();
    if (!frame.scope.valid() || frame.scope.value() >= scopes.size()) {
        return refuse("scope");
    }
    const auto unit = scopes[frame.scope.value()].unit;
    if (!unit.valid() || unit.value() >= semantics.units().size()) {
        return refuse("unit");
    }
    const auto& declaring_unit = semantics.units()[unit.value()];
    std::string key;
    if (declaring_unit.kind == semantic::UnitKind::vhdl_package
        && specialized_hir_unit_->unit() != unit) {
        key = frame.memo_key;
    } else if ((declaring_unit.language == semantic::Language::verilog
                   || declaring_unit.language
                       == semantic::Language::system_verilog)
        && specialized_hir_unit_->unit() == unit
        && !unit_callable_body_scope_.empty()
        && scopes[frame.scope.value()].parent
            == std::optional { declaring_unit.scope }) {
        // Declared at the top of the module being lowered, outside any
        // generate block, so its body names only the module's parameters
        // and objects, the same in every generate block of the instance
        // (IEEE 1800-2017 13.4, 27.3); a recorded body holds no object.
        key = "module-function|" + frame.memo_key + "|instance:"
            + unit_callable_body_scope_;
    } else {
        return refuse("unit");
    }
    key += "|language:";
    key += std::to_string(static_cast<int>(language_));
    if (coverage_hir_process_active_) {
        key += "|coverage";
    }
    return key;
}

std::vector<std::size_t> Lowerer::hir_callable_body_side_state() const
{
    return {
        hir_callable_frames_.size(),
        pending_hir_callables_.size(),
        static_cast<std::size_t>(next_string_register_),
        static_cast<std::size_t>(next_container_register_),
        static_cast<std::size_t>(next_callable_invocation_identity_),
        process_.debug_string_locals.size(),
        process_.debug_container_locals.size(),
        process_.container_register_types.size(),
        process_.static_sensitivity.size(),
        process_.static_trigger_regions.size(),
        process_.driver_regions.size(),
        generated_processes_.size(),
        implicit_signal_dependencies_.size(),
        hir_vhdl_access_heaps_.size(),
        readonly_signal_write_operations_.size(),
        diagnostics_.size(),
        hir_static_storage_signals_.size(),
    };
}

std::optional<HirCallableBodyRecording> Lowerer::record_hir_callable_body(
    const std::size_t callable, const HirCallableBodyStart& start,
    const RegisterId invocation_begin,
    std::vector<HirCallableBodyRecording::DebugName> debug_names) const
{
    // FSIM_HIR_BODY_MEMO_TRACE prints why a body is not recorded.
    static const bool trace
        = std::getenv("FSIM_HIR_BODY_MEMO_TRACE") != nullptr;
    const auto refuse = [&](const std::string_view reason,
                            const Operation* operation = nullptr)
        -> std::optional<HirCallableBodyRecording> {
        if (trace) {
            std::cerr << "[fsim-hir-lower] body-memo refused "
                      << hir_callable_frames_[callable].memo_key << ": "
                      << reason;
            if (operation != nullptr) {
                std::cerr << ' '
                          << visit_operation(
                                 [](const auto& op) {
                                     return typeid(op).name();
                                 },
                                 *operation);
            }
            std::cerr << '\n';
        }
        return std::nullopt;
    };
    const auto& frame = hir_callable_frames_[callable];
    if (const auto side_state = hir_callable_body_side_state();
        side_state != start.side_state) {
        for (std::size_t index { }; index < side_state.size(); ++index) {
            if (side_state[index] != start.side_state[index]) {
                return refuse("side state " + std::to_string(index));
            }
        }
    }
    if (frame.target != start.target
        || frame.body_vhdl_scalar_ranges.size() < start.scalar_ranges
        || hir_local_string_registers_.size()
            != hir_enclosing_local_string_registers_.size()) {
        return refuse("frame");
    }
    HirCallableBodyRecording recording;
    recording.target = start.target;
    recording.register_begin = start.register_begin;
    recording.invocation_begin = invocation_begin;
    const auto register_end = next_register_;
    for (auto id = start.register_begin; id < register_end; ++id) {
        recording.registers.push_back(
            { register_width(id), register_domain(id) });
    }
    recording.frame_registers.push_back(frame.result);
    recording.frame_registers.insert(recording.frame_registers.end(),
        frame.arguments.begin(), frame.arguments.end());
    for (const auto id : recording.frame_registers) {
        recording.frame_register_shapes.push_back(
            { register_width(id), register_domain(id) });
    }
    const auto known = [&](const RegisterId& id) {
        return (id >= start.register_begin && id < register_end)
            || std::ranges::find(recording.frame_registers, id)
            != recording.frame_registers.end();
    };
    const auto operations = std::as_const(process_).operations.size();
    const auto within = [&](const InstructionIndex& target) {
        return target >= start.target && target < operations;
    };
    // Debug scopes are recorded below the process name, which
    // debug_scope_name puts first.
    const auto& process_name = process_.name;
    recording.operations.reserve(operations - start.target);
    for (auto index = start.target; index < operations; ++index) {
        auto operation = std::as_const(process_).operations[index];
        if (auto* point = operation_get_if<DebugPoint>(&operation)) {
            const auto& scope = point->scope.str();
            if (process_name.empty()) {
            } else if (scope == process_name) {
                point->scope = InternedString { };
            } else if (scope.size() > process_name.size()
                && scope.starts_with(process_name)
                && scope[process_name.size()] == '.') {
                point->scope = InternedString { std::string_view { scope }
                        .substr(process_name.size() + 1U) };
            } else {
                return refuse("debug scope " + scope);
            }
        }
        if (!for_each_replayable_operand(operation, known, within)) {
            return refuse("operation", &operation);
        }
        recording.operations.push_back(std::move(operation));
    }
    for (const auto& [declaration, id] : hir_local_registers_) {
        const auto enclosing
            = hir_enclosing_local_registers_.find(declaration);
        if (enclosing != hir_enclosing_local_registers_.end()
            && enclosing->second == id) {
            continue;
        }
        if (!known(id)) {
            return refuse("local register");
        }
        recording.body_registers.emplace_back(declaration, id);
    }
    for (auto index = start.scalar_ranges;
         index < frame.body_vhdl_scalar_ranges.size(); ++index) {
        const auto& range = frame.body_vhdl_scalar_ranges[index];
        if (!known(range.left) || !known(range.right)) {
            return refuse("scalar range");
        }
        recording.scalar_ranges.push_back(
            { range.source, range.left, range.right, range.descending });
    }
    recording.debug_names = std::move(debug_names);
    for (auto index = start.debug_locals;
         index < process_.debug_locals.size(); ++index) {
        const auto& local = process_.debug_locals[index];
        const auto name = std::ranges::find(recording.debug_names,
            local.name, &HirCallableBodyRecording::DebugName::result);
        if (name == recording.debug_names.end()
            || !known(local.register_id)) {
            return refuse("debug local " + local.name);
        }
        recording.debug_locals.emplace_back(
            static_cast<std::size_t>(name - recording.debug_names.begin()),
            local);
    }
    const std::span<const ExpressionProfile> profiles
        = process_.expression_profiles;
    recording.expression_profiles.assign(
        profiles.begin() + static_cast<std::ptrdiff_t>(
            start.expression_profiles),
        profiles.end());
    return recording;
}

std::optional<HirCallableBodyRecording> Lowerer::renumber_hir_callable_body(
    const HirCallableBodyRecording& recording, const std::size_t callable,
    const InstructionIndex target, const RegisterId register_begin) const
{
    const auto& frame = hir_callable_frames_[callable];
    std::vector<RegisterId> frame_registers { frame.result };
    frame_registers.insert(frame_registers.end(), frame.arguments.begin(),
        frame.arguments.end());
    if (frame_registers.size() != recording.frame_registers.size()) {
        return std::nullopt;
    }
    for (std::size_t index { }; index < frame_registers.size(); ++index) {
        if (HirCallableBodyRecording::Register {
                register_width(frame_registers[index]),
                register_domain(frame_registers[index]) }
            != recording.frame_register_shapes[index]) {
            return std::nullopt;
        }
    }
    const auto recorded_end = recording.register_begin
        + static_cast<RegisterId>(recording.registers.size());
    const auto renumber_register = [&](RegisterId& id) {
        if (id >= recording.register_begin && id < recorded_end) {
            id = register_begin + (id - recording.register_begin);
            return true;
        }
        const auto found = std::ranges::find(recording.frame_registers, id);
        if (found == recording.frame_registers.end()) {
            return false;
        }
        id = frame_registers[static_cast<std::size_t>(
            found - recording.frame_registers.begin())];
        return true;
    };
    const auto renumber_target = [&](InstructionIndex& index) {
        index = target + (index - recording.target);
        return true;
    };
    auto body = recording;
    // The recording keeps each recorded scope alive, so its address names
    // the scope for the whole replay.
    std::unordered_map<const std::string*, InternedString> scope_prefixed;
    for (auto& operation : body.operations) {
        if (auto* point = operation_get_if<DebugPoint>(&operation)) {
            if (point->scope.empty() || !process_.name.empty()) {
                const auto* key = &point->scope.str();
                auto found = scope_prefixed.find(key);
                if (found == scope_prefixed.end()) {
                    found = scope_prefixed.emplace(key,
                        point->scope.empty()
                            ? InternedString { process_.name }
                            : InternedString {
                                process_.name + "." + point->scope.str()
                            }).first;
                }
                point->scope = found->second;
            }
        }
        (void)for_each_replayable_operand(
            operation, renumber_register, renumber_target);
    }
    for (auto& entry : body.body_registers) {
        (void)renumber_register(entry.second);
    }
    for (auto& range : body.scalar_ranges) {
        (void)renumber_register(range.left);
        (void)renumber_register(range.right);
    }
    for (auto& entry : body.debug_locals) {
        (void)renumber_register(entry.second.register_id);
    }
    body.target = target;
    body.register_begin = register_begin;
    body.invocation_begin
        = register_begin + (recording.invocation_begin - recording.register_begin);
    body.frame_registers = std::move(frame_registers);
    return body;
}

void Lowerer::replay_hir_callable_body(
    const std::size_t callable, HirCallableBodyRecording body)
{
    for (const auto& entry : body.registers) {
        (void)allocate_register(entry.width, entry.domain);
    }
    for (auto& operation : body.operations) {
        process_.operations.push_back(std::move(operation));
    }
    auto& frame = hir_callable_frames_[callable];
    for (auto id = body.invocation_begin; id < next_register_; ++id) {
        frame.invocation_registers.push_back(id);
    }
    for (const auto push_site : frame.invocation_push_sites) {
        process_.operations[push_site] = CallableFramePush {
            frame.invocation_identity,
            frame.invocation_registers,
            frame.invocation_strings,
            frame.invocation_containers,
            true,
        };
    }
    frame.invocation_push_sites.clear();
    frame.snapshot_complete = true;
    frame.body_registers = hir_enclosing_local_registers_;
    for (const auto& [declaration, id] : body.body_registers) {
        frame.body_registers.insert_or_assign(declaration, id);
    }
    frame.body_string_registers = hir_enclosing_local_string_registers_;
    for (const auto& range : body.scalar_ranges) {
        frame.body_vhdl_scalar_ranges.push_back(
            { range.source, range.left, range.right, range.descending });
    }
    for (auto& profile : body.expression_profiles) {
        process_.expression_profiles.push_back(std::move(profile));
    }
    // The body's locals take this process's debug names, in the order the
    // body asked for them.
    std::vector<std::string> names;
    names.reserve(body.debug_names.size());
    for (const auto& entry : body.debug_names) {
        names.push_back(entry.formal
                ? hir_callable_formal_debug_name(
                      callable, entry.declaration, entry.name)
                : hir_callable_debug_local_name(
                      callable, entry.declaration, entry.name));
    }
    for (auto& [name, local] : body.debug_locals) {
        local.name = names[name];
        if (std::ranges::none_of(process_.debug_locals,
                [&](const DebugLocal& existing) {
                    return existing.name == local.name;
                })) {
            process_.debug_locals.push_back(std::move(local));
        }
    }
}

} // namespace fsim::elaboration
