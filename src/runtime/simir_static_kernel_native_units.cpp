// SPDX-License-Identifier: Apache-2.0
//
// Native units of the engine v4 static kernel: generated-code entry,
// runtime helpers, template binding and partitions.
#include "simir_static_kernel_compiled_internal.hpp"

#include <cxxabi.h>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <typeinfo>
#include <unordered_map>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;
using namespace static_kernel_compiled_detail;
namespace kw = kernel_word;

void Interpreter::Impl::StaticKernel::run_compiled(const std::uint32_t member_index)
{
    auto& member = members_[member_index];
    if (member.native.entry != nullptr) {
        run_native(member.native, *member.compiled, member_index);
        return;
    }
    execute(*member.compiled, member_index);
}

void Interpreter::Impl::StaticKernel::run_native(NativeUnit& unit,
    CompiledBody& body, const std::uint32_t member)
{
    profile_native_calls_[4] += profile_ ? 1U : 0U;
    StaticKernelNativeFrame frame;
    frame.arena = arena_.data();
    frame.bindings = unit.bindings.data();
    frame.kernel = this;
    frame.program = unit.program;
    frame.instance = &body;
    frame.writes = &writes_;
    frame.containers = container_info_.data();
    frame.member = member;
    frame.position = 0U;
    frame.status = 0U;
    profile_operations_ += profile_ ? body.code.size() : 0U;
    const auto status = unit.entry(&frame, body.registers.data());
    if (frame.status == 2U && pending_deopt_) {
        const auto deopt = *pending_deopt_;
        pending_deopt_.reset();
        handle_deopt(frame.member, body, deopt);
        return;
    }
    if (status != 0U || frame.status != 0U) {
        auto error = std::exchange(native_exception_, nullptr);
        if (error) {
            std::rethrow_exception(error);
        }
        fail(frame.member, members_[frame.member].body_begin,
            "static kernel native code failed");
    }
}

void Interpreter::Impl::StaticKernel::native_evaluate(
    StaticKernelNativeFrame* frame, const std::uint32_t at,
    const std::uint64_t xa, const std::uint64_t xb, const std::uint64_t ya,
    const std::uint64_t yb, const std::uint64_t za, const std::uint64_t zb,
    std::uint64_t* out)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    kernel.profile_native_calls_[0] += kernel.profile_ ? 1U : 0U;
    try {
        const auto& body = *static_cast<const CompiledBody*>(frame->program);
        const auto& inst = body.code[at];
        if (kernel.profile_) {
            ++kernel.profile_generic_ops_["evaluate op="
                + std::to_string(static_cast<int>(inst.op)) + " sub="
                + std::to_string(inst.sub) + " w=" + std::to_string(inst.width)];
        }
        std::uint32_t resolved = 0U;
        if (inst.op == KOp::load_host || inst.op == KOp::mem_read) {
            resolved = frame->bindings[2U * inst.x];
        } else if (((inst.op == KOp::dynamic_extract
                        || inst.op == KOp::dynamic_part_select)
                       && inst.sub == 1U)
            || inst.op == KOp::load_slot9 || inst.op == KOp::load_field9) {
            resolved = frame->bindings[2U * inst.x + 1U] & ~partition_tag;
        }
        const auto result = kernel.evaluate_slow(body, inst, at, frame->member,
            kernel.vhdl_ ? 0U : kernel.members_[frame->member].body_begin,
            { xa, xb }, { ya, yb },
            { za, zb }, resolved,
            &static_cast<CompiledBody*>(frame->instance)->wide_registers);
        out[0] = result.a;
        out[1] = result.b;
        out[2] = kernel.last_unknown_;
    } catch (const KernelDeopt& deopt) {
        kernel.pending_deopt_ = deopt;
        if (kernel.pending_deopt_->resume == 0xffffffffU) {
            kernel.pending_deopt_->resume = at;
        }
        frame->status = 2U;
        out[0] = 0U;
        out[1] = 0U;
    } catch (...) {
        kernel.native_exception_ = std::current_exception();
        frame->status = 1U;
        out[0] = 0U;
        out[1] = 0U;
    }
}

void Interpreter::Impl::StaticKernel::native_effect(
    StaticKernelNativeFrame* frame, const std::uint32_t at,
    const std::uint64_t xa, const std::uint64_t xb, const std::uint64_t ya,
    const std::uint64_t yb, const std::uint64_t za, const std::uint64_t zb,
    std::uint64_t* out)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    kernel.profile_native_calls_[1] += kernel.profile_ ? 1U : 0U;
    out[0] = 0U;
    out[1] = 0U;
    try {
        const auto& body = *static_cast<const CompiledBody*>(frame->program);
        const auto& inst = body.code[at];
        if (kernel.profile_) {
            ++kernel.profile_generic_ops_["effect op="
                + std::to_string(static_cast<int>(inst.op)) + " sub="
                + std::to_string(inst.sub) + " w=" + std::to_string(inst.width)];
        }
        const auto binding = inst.d;
        std::uint32_t resolved = 0U;
        auto source = Word { xa, xb };
        if (inst.op == KOp::store_host || inst.op == KOp::mem_write) {
            resolved = frame->bindings[2U * binding];
        } else if (inst.op == KOp::store_slot || inst.op == KOp::store_slot_nba
            || inst.op == KOp::store_slot_dynamic
            || inst.op == KOp::store_slot_part || inst.op == KOp::store_vhdl
            || inst.op == KOp::wide_move) {
            resolved = frame->bindings[2U * binding + 1U] & ~partition_tag;
        }
        if (inst.op == KOp::wide_move) {
            // x carries the source slot.
            source = { frame->bindings[2U * inst.x + 1U] & ~partition_tag, 0U };
        }
        kernel.running_position_ = frame->position;
        kernel.effect_slow(body, inst, at, frame->member,
            kernel.vhdl_ ? 0U : kernel.members_[frame->member].body_begin,
            source, { ya, yb }, { za, zb }, resolved);
    } catch (const KernelDeopt& deopt) {
        kernel.pending_deopt_ = deopt;
        if (kernel.pending_deopt_->resume == 0xffffffffU) {
            kernel.pending_deopt_->resume = at;
        }
        frame->status = 2U;
    } catch (...) {
        kernel.native_exception_ = std::current_exception();
        frame->status = 1U;
    }
}

void Interpreter::Impl::StaticKernel::native_notify(
    StaticKernelNativeFrame* frame, const std::uint32_t slot,
    const std::uint64_t changed)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    kernel.profile_native_calls_[2] += kernel.profile_ ? 1U : 0U;
    try {
        kernel.running_position_ = frame->position;
        if (auto& state = kernel.slots_[slot]; !state.single_writer) {
            state.last_writer = kernel.member_process_[frame->member];
        }
        kernel.slot_changed(slot, changed, nullptr);
    } catch (...) {
        kernel.native_exception_ = std::current_exception();
        frame->status = 1U;
    }
}

void Interpreter::Impl::StaticKernel::native_notify_field(
    StaticKernelNativeFrame* frame, const std::uint32_t slot,
    const std::uint64_t changed, const std::uint32_t field_offset)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    kernel.profile_native_calls_[2] += kernel.profile_ ? 1U : 0U;
    try {
        kernel.running_position_ = frame->position;
        if (auto& state = kernel.slots_[slot]; !state.single_writer) {
            state.last_writer = kernel.member_process_[frame->member];
        }
        kernel.slot_changed(slot, changed, nullptr, field_offset);
    } catch (...) {
        kernel.native_exception_ = std::current_exception();
        frame->status = 1U;
    }
}

void Interpreter::Impl::StaticKernel::native_fail(
    StaticKernelNativeFrame* frame, const std::uint32_t at,
    const std::uint32_t reason)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    if (reason == 2U) {
        // An operand holds 'U' where compiled code needs a known kind.
        kernel.pending_deopt_ = KernelDeopt { at, false };
        frame->status = 2U;
        return;
    }
    try {
        kernel.fail(frame->member,
            (kernel.vhdl_ ? 0U : kernel.members_[frame->member].body_begin) + at,
            "branch condition is unknown or high impedance");
    } catch (...) {
        kernel.native_exception_ = std::current_exception();
        frame->status = 1U;
    }
}

void Interpreter::Impl::StaticKernel::build_native(StaticKernelCodegen& codegen)
{
    std::unordered_map<std::uint32_t, std::uint32_t> slot_of_offset;
    for (std::uint32_t slot = 0U; slot < slots_.size(); ++slot) {
        slot_of_offset.emplace(slots_[slot].offset, slot);
    }
    struct Unit {
        NativeUnit* native;
        const CompiledBody* body;
    };
    std::vector<Unit> units;
    for (auto& partition_state : partitions_) {
        units.push_back({ &partition_state.native, &partition_state.program });
    }
    for (auto& member : members_) {
        if (member.compiled && member.partition == no_slot) {
            units.push_back({ &member.native, &*member.compiled });
        }
    }
    std::unordered_map<std::string, std::uint32_t> template_of_key;
    std::vector<std::uint32_t> template_of_unit(units.size(), no_slot);
    for (std::size_t unit = 0U; unit < units.size(); ++unit) {
        const auto& instance = *units[unit].body;
        auto canonical = std::make_unique<CompiledBody>(instance);
        std::vector<std::uint32_t> bindings;
        std::unordered_map<std::uint64_t, std::uint32_t> binding_of;
        const auto bind = [&](const std::uint64_t kind, const std::uint32_t value,
                              const std::uint32_t first, const std::uint32_t second) {
            const auto key = (kind << 40U) | value;
            const auto [it, inserted] = binding_of.emplace(
                key, static_cast<std::uint32_t>(bindings.size() / 2U));
            if (inserted) {
                bindings.push_back(first);
                bindings.push_back(second);
            }
            return it->second;
        };
        const auto bind_slot = [&](const std::uint32_t slot) {
            return bind(1U, slot, slots_[slot].offset,
                slot | (slots_[slot].silent ? partition_tag : 0U));
        };
        for (auto& inst : canonical->code) {
            switch (inst.op) {
            case KOp::load_slot:
                inst.x = bind_slot(slot_of_offset.at(inst.x));
                break;
            case KOp::load_field:
                inst.x = bind_slot(inst.x);
                break;
            case KOp::dynamic_extract:
            case KOp::dynamic_part_select:
                if (inst.sub == 1U) {
                    inst.x = bind_slot(inst.x);
                }
                break;
            case KOp::store_slot:
            case KOp::store_slot_nba:
            case KOp::store_slot_dynamic:
            case KOp::store_slot_part:
            case KOp::store_vhdl:
                inst.d = bind_slot(inst.d);
                break;
            case KOp::wide_move:
                inst.d = bind_slot(inst.d);
                inst.x = bind_slot(inst.x);
                break;
            case KOp::load_slot9:
            case KOp::load_field9:
                inst.x = bind_slot(inst.x);
                break;
            case KOp::load_host:
                inst.x = bind(2U, inst.x, inst.x, 0U);
                break;
            case KOp::store_host:
                inst.d = bind(2U, inst.d, inst.d, 0U);
                break;
            case KOp::mem_read:
                inst.x = bind(3U, inst.x, inst.x, 0U);
                break;
            case KOp::mem_write:
                inst.d = bind(3U, inst.d, inst.d, 0U);
                break;
            case KOp::mark:
                inst.x = bind(4U, inst.x, inst.x, 0U);
                break;
            default:
                break;
            }
        }
        std::string key;
        const auto append = [&](const auto& value) {
            key.append(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        for (const auto& inst : canonical->code) {
            append(inst.op);
            append(inst.sub);
            append(inst.flags);
            append(inst.width);
            append(inst.d);
            append(inst.x);
            append(inst.y);
            append(inst.z);
            append(inst.offset);
            append(inst.aux);
            append(inst.imm_a);
            append(inst.imm_b);
        }
        append(canonical->registers.size());
        append(canonical->entry);
        append(canonical->shadow_base);
        key.append(canonical->tracked.begin(), canonical->tracked.end());
        key.append(canonical->u_mode.begin(), canonical->u_mode.end());
        for (const auto reg : canonical->u_operands) {
            append(reg);
        }
        for (const auto target : canonical->return_targets) {
            append(target);
        }
        for (const auto& index : canonical->indices) {
            append(index.index);
            append(index.left);
            append(index.right);
            append(index.base_offset);
            append(index.strict);
        }
        for (const auto& part : canonical->parts) {
            append(part.left);
            append(part.right);
            append(part.base);
            append(part.base_offset);
            append(part.width);
            append(part.increasing);
            append(part.source_descending);
        }
        for (const auto& operand : canonical->concat) {
            append(operand.reg);
            append(operand.width);
        }
        const auto [found, inserted] = template_of_key.emplace(
            std::move(key), static_cast<std::uint32_t>(templates_.size()));
        if (inserted) {
            templates_.push_back(std::move(canonical));
        }
        template_of_unit[unit] = found->second;
        units[unit].native->bindings = std::move(bindings);
        units[unit].native->program = templates_[found->second].get();
    }
    std::vector<std::size_t> template_units(templates_.size(), 0U);
    for (const auto index : template_of_unit) {
        ++template_units[index];
    }
    std::vector<StaticKernelTemplate> templates;
    templates.reserve(templates_.size());
    const char* threshold_text = std::getenv("FSIM_STATIC_KERNEL_HOT_THRESHOLD");
    const std::size_t hot_threshold = threshold_text != nullptr
        ? static_cast<std::size_t>(std::strtoull(threshold_text, nullptr, 10))
        : 32768U;
    for (std::size_t index = 0U; index < templates_.size(); ++index) {
        // Instances times size estimates how much run time a template
        // carries; only the heaviest get the slower optimizing backend.
        const auto size = templates_[index]->code.size();
        templates.push_back({ templates_[index].get(),
            size >= 64U && template_units[index] * size >= hot_threshold });
    }
    StaticKernelNativeHelpers helpers;
    helpers.evaluate = &StaticKernel::native_evaluate;
    helpers.effect = &StaticKernel::native_effect;
    helpers.notify = &StaticKernel::native_notify;
    helpers.notify_field = &StaticKernel::native_notify_field;
    helpers.binary = [](const std::uint64_t operation_width, const std::uint64_t la,
                         const std::uint64_t lb, const std::uint64_t ra,
                         const std::uint64_t rb, std::uint64_t* out) {
        const auto result = kw::binary(
            static_cast<BinaryOperator>(operation_width & 0xffU), { la, lb },
            { ra, rb }, static_cast<std::uint32_t>(operation_width >> 8U));
        out[0] = result.a;
        out[1] = result.b;
    };
    helpers.fail = &StaticKernel::native_fail;
    helpers.generic = &StaticKernel::native_generic;
    if (std::getenv("FSIM_STATIC_KERNEL_PERF_MAP") != nullptr) {
        std::vector<std::size_t> units_of(templates_.size(), 0U);
        std::vector<std::string> example(templates_.size());
        for (std::size_t unit = 0U; unit < units.size(); ++unit) {
            const auto index = template_of_unit[unit];
            if (units_of[index]++ == 0U) {
                for (std::uint32_t member = 0U; member < members_.size(); ++member) {
                    if (&members_[member].native == units[unit].native) {
                        example[index]
                            = impl_.processes.program_view(members_[member].process).name();
                    }
                }
            }
        }
        for (std::size_t index = 0U; index < templates_.size(); ++index) {
            std::cerr << "fsim-kernel-template: " << index << " units="
                      << units_of[index] << " insts=" << templates_[index]->code.size()
                      << " example=" << example[index] << '\n';
        }
    }
    const auto entries = codegen.compile(templates, helpers);
    for (std::size_t unit = 0U; unit < units.size(); ++unit) {
        const auto index = template_of_unit[unit];
        if (index < entries.size() && entries[index] != nullptr) {
            units[unit].native->entry = entries[index];
            ++native_units_;
        }
    }
}

void Interpreter::Impl::StaticKernel::run_partition(const std::uint32_t partition)
{
    auto& state = partitions_[partition];
    const auto operations_before = profile_operations_;
    if (profile_) {
        ++profile_partition_runs_;
        if (profile_partition_counts_.empty()) {
            profile_partition_counts_.assign(partitions_.size(), 0U);
            profile_partition_operations_.assign(partitions_.size(), 0U);
        }
        ++profile_partition_counts_[partition];
    }
    running_partition_ = partition;
    running_position_ = 0U;
    try {
        if (state.native.entry != nullptr) {
            run_native(state.native, state.program, state.members.front());
        } else {
            execute(state.program, state.members.front());
        }
    } catch (...) {
        running_partition_ = no_slot;
        throw;
    }
    running_partition_ = no_slot;
    if (profile_) {
        profile_partition_operations_[partition]
            += profile_operations_ - operations_before;
    }
}

void Interpreter::Impl::StaticKernel::build_partitions(
    const std::vector<std::vector<std::uint32_t>>& successors)
{
    // Group compiled combinational members by key.
    std::map<std::uint32_t, std::vector<std::uint32_t>> groups;
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        const auto& member = members_[index];
        if (member.kind == StaticKernelMemberKind::combinational
            && member.compiled && member.partition_key != no_slot) {
            groups[member.partition_key].push_back(index);
        }
    }
    // Split each group into its weakly connected components: a change then
    // runs only the cone it reaches. FSIM_STATIC_KERNEL_WHOLE_PARTITIONS
    // keeps one partition per key.
    std::vector<std::vector<std::uint32_t>> components;
    const bool split = std::getenv("FSIM_STATIC_KERNEL_WHOLE_PARTITIONS") == nullptr;
    for (auto& [key, group] : groups) {
        (void)key;
        if (!split || group.size() < 2U) {
            components.push_back(std::move(group));
            continue;
        }
        std::unordered_map<std::uint32_t, std::uint32_t> local;
        for (std::uint32_t at = 0U; at < group.size(); ++at) {
            local.emplace(group[at], at);
        }
        std::vector<std::uint32_t> parent(group.size());
        for (std::uint32_t at = 0U; at < group.size(); ++at) {
            parent[at] = at;
        }
        const auto find = [&](std::uint32_t at) {
            while (parent[at] != at) {
                parent[at] = parent[parent[at]];
                at = parent[at];
            }
            return at;
        };
        for (std::uint32_t at = 0U; at < group.size(); ++at) {
            for (const auto successor : successors[group[at]]) {
                if (const auto found = local.find(successor); found != local.end()) {
                    const auto left = find(at);
                    const auto right = find(found->second);
                    if (left != right) {
                        parent[std::max(left, right)] = std::min(left, right);
                    }
                }
            }
        }
        std::map<std::uint32_t, std::vector<std::uint32_t>> by_root;
        for (std::uint32_t at = 0U; at < group.size(); ++at) {
            by_root[find(at)].push_back(group[at]);
        }
        for (auto& [root, component] : by_root) {
            (void)root;
            components.push_back(std::move(component));
        }
    }
    for (auto& members : components) {
        if (members.size() < 2U) {
            continue;
        }
        // Partition-local topological order, ties in declaration order, so
        // instances of one module share one canonical program.
        {
            std::unordered_map<std::uint32_t, std::uint32_t> local;
            for (std::uint32_t at = 0U; at < members.size(); ++at) {
                local.emplace(members[at], at);
            }
            std::vector<std::uint32_t> indegree(members.size(), 0U);
            for (const auto member : members) {
                for (const auto successor : successors[member]) {
                    if (const auto found = local.find(successor);
                        found != local.end() && successor != member) {
                        ++indegree[found->second];
                    }
                }
            }
            std::priority_queue<std::uint32_t, std::vector<std::uint32_t>,
                std::greater<>> ready;
            for (std::uint32_t at = 0U; at < members.size(); ++at) {
                if (indegree[at] == 0U) {
                    ready.push(members[at]);
                }
            }
            std::vector<std::uint32_t> ordered;
            std::vector<std::uint8_t> done(members.size(), 0U);
            while (ordered.size() < members.size()) {
                if (ready.empty()) {
                    // A combinational loop: continue with the first pending
                    // member in declaration order.
                    for (std::uint32_t at = 0U; at < members.size(); ++at) {
                        if (done[at] == 0U) {
                            indegree[at] = 0U;
                            ready.push(members[at]);
                            break;
                        }
                    }
                }
                const auto member = ready.top();
                ready.pop();
                const auto at = local[member];
                if (done[at] != 0U) {
                    continue;
                }
                done[at] = 1U;
                ordered.push_back(member);
                for (const auto successor : successors[member]) {
                    if (const auto found = local.find(successor);
                        found != local.end() && done[found->second] == 0U
                        && successor != member && indegree[found->second] != 0U
                        && --indegree[found->second] == 0U) {
                        ready.push(successor);
                    }
                }
            }
            members = std::move(ordered);
        }
        const auto partition = static_cast<std::uint32_t>(partitions_.size());
        Partition state;
        state.members = members;
        for (std::uint32_t position = 0U; position < members.size(); ++position) {
            members_[members[position]].partition = partition;
            members_[members[position]].position = position;
        }
        partitions_.push_back(std::move(state));
    }

    // Node levels over partitions and the remaining combinational members.
    const auto count = static_cast<std::uint32_t>(members_.size());
    const auto node_count = count + static_cast<std::uint32_t>(partitions_.size());
    const auto node_of = [&](const std::uint32_t member) {
        return members_[member].partition != no_slot
            ? count + members_[member].partition : member;
    };
    std::vector<std::vector<std::uint32_t>> node_successors(node_count);
    std::vector<std::uint32_t> indegree(node_count, 0U);
    for (std::uint32_t member = 0U; member < count; ++member) {
        for (const auto successor : successors[member]) {
            const auto from = node_of(member);
            const auto to = node_of(successor);
            if (from != to) {
                node_successors[from].push_back(to);
            }
        }
    }
    for (auto& list : node_successors) {
        std::ranges::sort(list);
        list.erase(std::unique(list.begin(), list.end()), list.end());
        for (const auto to : list) {
            ++indegree[to];
        }
    }
    std::vector<std::uint32_t> node_level(node_count, 0U);
    std::vector<std::uint8_t> placed(node_count, 0U);
    std::vector<std::uint32_t> ready;
    for (std::uint32_t node = 0U; node < node_count; ++node) {
        if (indegree[node] == 0U) {
            ready.push_back(node);
        }
    }
    std::uint32_t level = 0U;
    while (!ready.empty()) {
        std::vector<std::uint32_t> next;
        for (const auto node : ready) {
            node_level[node] = level;
            placed[node] = 1U;
            for (const auto successor : node_successors[node]) {
                if (--indegree[successor] == 0U) {
                    next.push_back(successor);
                }
            }
        }
        ready = std::move(next);
        ++level;
    }
    for (std::uint32_t node = 0U; node < node_count; ++node) {
        if (placed[node] == 0U) {
            node_level[node] = level++;
        }
    }
    for (std::uint32_t member = 0U; member < count; ++member) {
        if (members_[member].partition == no_slot) {
            members_[member].level = node_level[member];
        }
    }

    // Concatenate member programs.
    for (std::uint32_t partition = 0U; partition < partitions_.size();
         ++partition) {
        auto& state = partitions_[partition];
        state.level = node_level[count + partition];
        auto& program = state.program;
        for (std::uint32_t position = 0U; position < state.members.size();
             ++position) {
            const auto index = state.members[position];
            const auto& body = *members_[index].compiled;
            const auto register_base
                = static_cast<std::uint32_t>(program.registers.size());
            const auto code_base = static_cast<std::uint32_t>(program.code.size())
                + 1U;
            const auto exit = code_base
                + static_cast<std::uint32_t>(body.code.size());
            const auto index_base
                = static_cast<std::uint32_t>(program.indices.size());
            const auto part_base = static_cast<std::uint32_t>(program.parts.size());
            const auto concat_base
                = static_cast<std::uint32_t>(program.concat.size());
            program.registers.insert(program.registers.end(),
                body.registers.begin(), body.registers.end());
            if (!body.wide_registers.empty() || !program.wide_registers.empty()) {
                program.wide_registers.resize(register_base, PackedLogic4 { });
                if (body.wide_registers.empty()) {
                    program.wide_registers.resize(
                        register_base + body.registers.size(), PackedLogic4 { });
                } else {
                    program.wide_registers.insert(program.wide_registers.end(),
                        body.wide_registers.begin(), body.wide_registers.end());
                }
            }
            program.register_widths.insert(program.register_widths.end(),
                body.register_widths.begin(), body.register_widths.end());
            program.indices.insert(program.indices.end(), body.indices.begin(),
                body.indices.end());
            program.parts.insert(program.parts.end(), body.parts.begin(),
                body.parts.end());
            for (auto operand : body.concat) {
                operand.reg += register_base;
                program.concat.push_back(operand);
            }
            KInst mark;
            mark.op = KOp::mark;
            mark.d = position;
            mark.x = index;
            program.code.push_back(mark);
            for (auto inst : body.code) {
                const auto target = [&](std::uint32_t& value) {
                    value = value >= body.code.size() ? exit : code_base + value;
                };
                const auto reg = [&](std::uint32_t& value) {
                    value += register_base;
                };
                switch (inst.op) {
                case KOp::nop:
                case KOp::mem_bind:
                case KOp::mark:
                    break;
                case KOp::generic:
                    inst.y += register_base;
                    break;
                case KOp::constant:
                case KOp::load_slot:
                case KOp::load_host:
                    reg(inst.d);
                    break;
                case KOp::copy:
                case KOp::reduce:
                case KOp::unary_not:
                case KOp::logical_not:
                case KOp::two_state:
                case KOp::extract:
                    reg(inst.d);
                    reg(inst.x);
                    break;
                case KOp::binary:
                case KOp::logical_binary:
                case KOp::shift:
                case KOp::insert:
                    reg(inst.d);
                    reg(inst.x);
                    reg(inst.y);
                    break;
                case KOp::concat:
                    reg(inst.d);
                    inst.aux += concat_base;
                    break;
                case KOp::conditional:
                case KOp::dynamic_insert:
                    reg(inst.d);
                    reg(inst.x);
                    reg(inst.y);
                    reg(inst.z);
                    if (inst.op == KOp::dynamic_insert) {
                        inst.aux += index_base;
                    }
                    break;
                case KOp::dynamic_part_insert:
                    reg(inst.d);
                    reg(inst.x);
                    reg(inst.y);
                    reg(inst.z);
                    inst.aux += part_base;
                    break;
                case KOp::dynamic_extract:
                    reg(inst.d);
                    if (inst.sub != 1U) {
                        reg(inst.x);
                    }
                    reg(inst.y);
                    inst.aux += index_base;
                    break;
                case KOp::dynamic_part_select:
                    reg(inst.d);
                    if (inst.sub != 1U) {
                        reg(inst.x);
                    }
                    reg(inst.y);
                    inst.aux += part_base;
                    break;
                case KOp::load_field:
                    reg(inst.d);
                    break;
                case KOp::jump:
                    target(inst.d);
                    break;
                case KOp::branch:
                    reg(inst.x);
                    target(inst.y);
                    target(inst.z);
                    break;
                case KOp::store_slot:
                case KOp::store_slot_nba:
                case KOp::store_host:
                    reg(inst.x);
                    break;
                case KOp::store_slot_dynamic:
                    reg(inst.x);
                    reg(inst.y);
                    inst.aux += index_base;
                    break;
                case KOp::store_slot_part:
                    reg(inst.x);
                    reg(inst.y);
                    inst.aux += part_base;
                    break;
                case KOp::mem_read:
                    reg(inst.d);
                    reg(inst.y);
                    break;
                case KOp::mem_write:
                    reg(inst.x);
                    reg(inst.y);
                    if (inst.aux != 0U) {
                        reg(inst.z);
                        inst.aux += part_base;
                    }
                    break;
                case KOp::wide_move:
                    if (inst.sub == 1U) {
                        reg(inst.y);
                        inst.aux += part_base;
                    }
                    break;
                case KOp::load_slot9:
                case KOp::store_vhdl:
                case KOp::integer_binary:
                case KOp::integer_unary:
                case KOp::integer_check:
                case KOp::call:
                case KOp::ret:
                case KOp::assert_check:
                case KOp::deopt:
                case KOp::load_field9:
                    throw std::logic_error(
                        "VHDL kernel instructions are not partitioned");
                }
                program.code.push_back(inst);
            }
            // Member state now lives in the partition program.
            members_[index].compiled.reset();
        }
    }

    if (profile_) {
        // Diagnostic: distinct programs modulo instance bindings.
        const auto canonical = [&](const CompiledBody& body) {
            std::string key;
            std::unordered_map<std::uint64_t, std::uint32_t> bindings;
            const auto bind = [&](const std::uint64_t value) {
                const auto [it, inserted] = bindings.emplace(
                    value, static_cast<std::uint32_t>(bindings.size()));
                return it->second;
            };
            for (auto inst : body.code) {
                switch (inst.op) {
                case KOp::load_slot:
                    inst.x = bind(inst.x);
                    break;
                case KOp::load_host:
                    inst.x = bind((std::uint64_t { 1 } << 40U) | inst.x);
                    break;
                case KOp::store_slot:
                case KOp::store_slot_nba:
                case KOp::store_slot_dynamic:
                case KOp::store_slot_part:
                    inst.d = bind((std::uint64_t { 2 } << 40U) | inst.d);
                    break;
                case KOp::store_host:
                    inst.d = bind((std::uint64_t { 3 } << 40U) | inst.d);
                    break;
                case KOp::mem_read:
                    inst.x = bind((std::uint64_t { 4 } << 40U) | inst.x);
                    break;
                case KOp::mem_write:
                    inst.d = bind((std::uint64_t { 4 } << 40U) | inst.d);
                    break;
                case KOp::mark:
                    inst.x = 0U;
                    break;
                default:
                    break;
                }
                KInst clean;
                std::memset(static_cast<void*>(&clean), 0, sizeof(clean));
                clean.op = inst.op;
                clean.sub = inst.sub;
                clean.flags = inst.flags;
                clean.width = inst.width;
                clean.d = inst.d;
                clean.x = inst.x;
                clean.y = inst.y;
                clean.z = inst.z;
                clean.offset = inst.offset;
                clean.aux = inst.aux;
                clean.imm_a = inst.imm_a;
                clean.imm_b = inst.imm_b;
                key.append(reinterpret_cast<const char*>(&clean), sizeof(clean));
            }
            return key;
        };
        std::unordered_map<std::string, std::size_t> partition_templates;
        std::size_t partition_ops = 0U;
        for (const auto& partition_state : partitions_) {
            ++partition_templates[canonical(partition_state.program)];
            partition_ops += partition_state.program.code.size();
        }
        std::size_t template_ops = 0U;
        for (const auto& [key, uses] : partition_templates) {
            template_ops += key.size() / sizeof(KInst);
        }
        std::unordered_map<std::string, std::size_t> member_templates;
        std::size_t member_ops = 0U;
        std::size_t member_template_ops = 0U;
        for (const auto& member : members_) {
            if (member.compiled && member.partition == no_slot) {
                const auto key = canonical(*member.compiled);
                if (member_templates[key]++ == 0U) {
                    member_template_ops += member.compiled->code.size();
                }
                member_ops += member.compiled->code.size();
            }
        }
        if (const char* left_name = std::getenv("FSIM_KERNEL_DIFF_A")) {
            const char* right_name = std::getenv("FSIM_KERNEL_DIFF_B");
            const CompiledBody* left = nullptr;
            const CompiledBody* right = nullptr;
            for (const auto& partition_state : partitions_) {
                const auto& name = profile_names_[partition_state.members.front()];
                if (left == nullptr && name.find(left_name) != std::string::npos) {
                    left = &partition_state.program;
                }
                if (right == nullptr && right_name != nullptr
                    && name.find(right_name) != std::string::npos) {
                    right = &partition_state.program;
                }
            }
            if (left != nullptr && right != nullptr) {
                const auto a = canonical(*left);
                const auto b = canonical(*right);
                std::cerr << "fsim-kernel-diff: sizes " << left->code.size() << ' '
                          << right->code.size() << '\n';
                for (std::size_t at = 0U; at * sizeof(KInst) < a.size()
                     && at * sizeof(KInst) < b.size(); ++at) {
                    if (a.compare(at * sizeof(KInst), sizeof(KInst), b,
                            at * sizeof(KInst), sizeof(KInst)) != 0) {
                        KInst x;
                        KInst y;
                        std::memcpy(&x, a.data() + at * sizeof(KInst), sizeof(KInst));
                        std::memcpy(&y, b.data() + at * sizeof(KInst), sizeof(KInst));
                        std::cerr << "fsim-kernel-diff: at " << at << " op "
                                  << static_cast<int>(x.op) << '/' << static_cast<int>(y.op)
                                  << " sub " << int(x.sub) << '/' << int(y.sub)
                                  << " d " << x.d << '/' << y.d << " x " << x.x << '/' << y.x
                                  << " y " << x.y << '/' << y.y << " z " << x.z << '/' << y.z
                                  << " off " << x.offset << '/' << y.offset << " w " << x.width
                                  << '/' << y.width << " aux " << x.aux << '/' << y.aux
                                  << " imm " << x.imm_a << '/' << y.imm_a << " flags "
                                  << int(x.flags) << '/' << int(y.flags) << '\n';
                        break;
                    }
                }
            }
        }
        std::cerr << "fsim-kernel: partitions=" << partitions_.size()
                  << " partition_templates=" << partition_templates.size()
                  << " partition_ops=" << partition_ops
                  << " template_ops=" << template_ops
                  << " member_templates=" << member_templates.size()
                  << " member_ops=" << member_ops
                  << " member_template_ops=" << member_template_ops << '\n';
    }

    // Silent slots: no reader, or every writer and reader is in one
    // partition and every reader runs after every writer in its pass.
    std::unordered_map<ProcessId, std::uint32_t> member_of_process;
    for (std::uint32_t index = 0U; index < count; ++index) {
        member_of_process.emplace(members_[index].process, index);
    }
    for (auto& slot : slots_) {
        if (slot.output || !slot.edges.empty()) {
            continue;
        }
        if (slot.readers.empty()) {
            // Nothing observes changes: materialize reads the value itself.
            slot.silent = true;
            continue;
        }
        if (slot.writers.empty()) {
            continue;
        }
        std::uint32_t partition = no_slot;
        std::uint32_t last_writer = 0U;
        bool silent = true;
        for (const auto& writer : slot.writers) {
            const auto found = member_of_process.find(writer.process);
            if (found == member_of_process.end()
                || members_[found->second].partition == no_slot
                || (partition != no_slot
                    && members_[found->second].partition != partition)) {
                silent = false;
                break;
            }
            partition = members_[found->second].partition;
            last_writer = std::max(last_writer, members_[found->second].position);
        }
        for (const auto& reader : slot.readers) {
            if (!silent) {
                break;
            }
            silent = members_[reader.member].partition == partition
                && members_[reader.member].position > last_writer;
        }
        slot.silent = silent;
    }
}

} // namespace fsim::runtime::simir
