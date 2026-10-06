// SPDX-License-Identifier: Apache-2.0
// Certified activation-kernel plan and direct member-body lowering.
#include "region_frontier_codegen_v2.hpp"
#include "region_frontier_kernel_plan.hpp"
#include "logic9_word_lowering.hpp"
#include "region_frontier_planner_storage_profile.hpp"

#include <fsim/compiler/object_cache.hpp>
#include <fsim/runtime/simir.hpp>
#include <fsim/runtime/simir_region_graph.hpp>

#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler {
namespace {

using namespace runtime::simir;
using namespace runtime::simir::scratch;
using Builder = llvm::IRBuilder<>;

enum class BodyOperationKind : std::uint8_t {
    load_constant,
    copy_register,
    extract,
    concatenate,
    conditional_select,
    unary_not,
    bit_and,
    bit_or,
    bit_xor,
    reduction_and,
    reduction_or,
    reduction_xor,
    shift,
    debug_noop,
};

struct BodyOperation {
    BodyOperationKind kind { };
    std::uint32_t instruction { };
    std::uint32_t destination { };
    std::uint32_t first_source { };
    std::uint32_t second_source { };
    std::uint32_t third_source { };
    std::vector<std::uint32_t> operands;
    std::uint32_t offset { };
    std::uint32_t width { };
    ShiftOperator shift_operator { ShiftOperator::logical_left };
    std::uint32_t output_site { UINT32_MAX };
    std::vector<std::uint64_t> constant_aval;
    std::vector<std::uint64_t> constant_bval;
};

struct MemberBody {
    std::uint32_t process_id { };
    std::uint32_t readiness_register { };
    std::uint32_t begin { };
    std::uint32_t end { };
    std::vector<BodyOperation> operations;
    std::vector<RegisterId> used_input_registers;
    bool has_unknown_constant { };
    bool has_logic9_value { };
};

struct RegisterShape {
    std::uint32_t width { };
    ValueKind value_kind { ValueKind::logic4 };
};

struct WordValue {
    std::uint32_t width { };
    ValueKind value_kind { ValueKind::logic4 };
    std::vector<llvm::Value*> aval;
    std::vector<llvm::Value*> bval;
    std::vector<llvm::Value*> plane2;
    std::vector<llvm::Value*> plane3;
};

[[nodiscard]] RegionFrontierValueKindV2 frontier_value_kind(
    const ValueKind kind)
{
    switch (kind) {
    case ValueKind::logic4:
        return RegionFrontierValueKindV2::logic4;
    case ValueKind::logic9:
        return RegionFrontierValueKindV2::logic9;
    }
    throw std::logic_error("unsupported region frontier value kind");
}

[[nodiscard]] ValueKind runtime_value_kind(
    const RegionFrontierValueKindV2 kind)
{
    switch (kind) {
    case RegionFrontierValueKindV2::logic4:
        return ValueKind::logic4;
    case RegionFrontierValueKindV2::logic9:
        return ValueKind::logic9;
    }
    throw std::logic_error("invalid region frontier value kind");
}

[[nodiscard]] llvm_detail::Logic9WordValue logic9_word_value(
    const WordValue& value)
{
    return { value.width,
        { value.aval, value.bval, value.plane2, value.plane3 } };
}

[[nodiscard]] WordValue word_value_from_logic9(
    llvm_detail::Logic9WordValue value)
{
    WordValue result;
    result.width = value.width;
    result.value_kind = ValueKind::logic9;
    result.aval = std::move(value.planes[0]);
    result.bval = std::move(value.planes[1]);
    result.plane2 = std::move(value.planes[2]);
    result.plane3 = std::move(value.planes[3]);
    return result;
}

[[nodiscard]] std::uint32_t word_count_for(const std::uint32_t width) noexcept
{
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
}

[[nodiscard]] std::uint64_t word_mask(const std::uint32_t width,
    const std::uint32_t word) noexcept
{
    const auto tail = width % 64U;
    if (tail == 0U || word + 1U < word_count_for(width)) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << tail) - 1U;
}

void append_number(std::string& identity, const std::uint64_t value)
{
    identity.append(std::to_string(value));
    identity.push_back(';');
}

struct MemberShapeCensusCounts {
    std::size_t members { };
    std::size_t known_variants { };
    std::size_t fallback_variants { };
    std::size_t operations { };
    std::set<std::string> strict_shapes;
    std::set<std::string> binding_tuples;
};

struct MemberShapeCensusKeys {
    std::string template_key;
    std::string strict_key;
    std::string binding_tuple;
    std::vector<RegisterId> ordered_inputs;
    std::vector<std::uint32_t> ordered_sites;
};

// These keys describe only the lowered operation body and are insufficient
// as full-region or cache identities. Helper reuse additionally requires the
// admitted plan and explicit immutable binding contract; scheduling, activation
// initialization, and final debug descriptors remain in the original caller.
[[nodiscard]] bool append_member_shape_keys(
    MemberShapeCensusKeys& keys, const MemberBody& body,
    const std::size_t member_index,
    const RegionFrontierMemberLayoutV2& member_layout,
    const std::vector<RegisterShape>& register_shapes,
    const std::vector<RegionFrontierSignalLayoutV2>& signals,
    const std::map<RegisterId, std::uint32_t>& input_slots,
    const std::vector<RegionFrontierWriteSiteV2>& sites,
    const bool diagnostic_details = true)
{
    if (body.process_id != member_layout.process_id) {
        return false;
    }

    std::map<RegisterId, std::uint32_t> register_ordinals;
    std::vector<RegisterId> registers_by_ordinal;
    const auto register_ordinal = [&](const RegisterId reg) {
        const auto found = register_ordinals.find(reg);
        if (found != register_ordinals.end()) {
            return found->second;
        }
        if (reg >= register_shapes.size()
            || registers_by_ordinal.size()
                >= std::numeric_limits<std::uint32_t>::max()) {
            return UINT32_MAX;
        }
        const auto ordinal = static_cast<std::uint32_t>(
            registers_by_ordinal.size());
        register_ordinals.emplace(reg, ordinal);
        registers_by_ordinal.push_back(reg);
        return ordinal;
    };

    std::map<std::uint32_t, std::uint32_t> signal_slot_ordinals;
    std::vector<std::uint32_t> signal_slots_by_ordinal;
    const auto signal_slot_ordinal = [&](const std::uint32_t slot) {
        const auto found = signal_slot_ordinals.find(slot);
        if (found != signal_slot_ordinals.end()) {
            return found->second;
        }
        if (slot >= signals.size()
            || signal_slots_by_ordinal.size()
                >= std::numeric_limits<std::uint32_t>::max()) {
            return UINT32_MAX;
        }
        const auto ordinal = static_cast<std::uint32_t>(
            signal_slots_by_ordinal.size());
        signal_slot_ordinals.emplace(slot, ordinal);
        signal_slots_by_ordinal.push_back(slot);
        return ordinal;
    };

    keys.ordered_inputs.clear();
    keys.ordered_sites.clear();
    std::map<std::uint32_t, std::uint32_t> site_ordinals;
    std::uint32_t next_site_ordinal { };
    const auto site_ordinal = [&](const std::uint32_t site) {
        const auto found = site_ordinals.find(site);
        if (found != site_ordinals.end()) {
            return found->second;
        }
        if (next_site_ordinal == UINT32_MAX) {
            return UINT32_MAX;
        }
        const auto ordinal = next_site_ordinal++;
        site_ordinals.emplace(site, ordinal);
        keys.ordered_sites.push_back(site);
        return ordinal;
    };

    auto& key = keys.template_key;
    key.append("frontier-operation-body-descriptor-candidate-v1;");
    append_number(key, body.has_unknown_constant ? 1U : 0U);
    append_number(key, body.has_logic9_value ? 1U : 0U);

    // Registers are alpha-renamed by first appearance in operation order.
    // The same bijection then records input aliases and signal bindings.
    for (const auto& operation : body.operations) {
        const auto register_is_valid = [&](const RegisterId reg) {
            return register_ordinal(reg) != UINT32_MAX;
        };
        switch (operation.kind) {
        case BodyOperationKind::load_constant:
            if (!register_is_valid(operation.destination)) {
                return false;
            }
            break;
        case BodyOperationKind::copy_register:
        case BodyOperationKind::extract:
        case BodyOperationKind::unary_not:
        case BodyOperationKind::reduction_and:
        case BodyOperationKind::reduction_or:
        case BodyOperationKind::reduction_xor:
            if (!register_is_valid(operation.destination)
                || !register_is_valid(operation.first_source)) {
                return false;
            }
            break;
        case BodyOperationKind::concatenate:
            if (!register_is_valid(operation.destination)) {
                return false;
            }
            for (const auto operand : operation.operands) {
                if (!register_is_valid(operand)) {
                    return false;
                }
            }
            break;
        case BodyOperationKind::conditional_select:
            if (!register_is_valid(operation.destination)
                || !register_is_valid(operation.first_source)
                || !register_is_valid(operation.second_source)
                || !register_is_valid(operation.third_source)) {
                return false;
            }
            break;
        case BodyOperationKind::bit_and:
        case BodyOperationKind::bit_or:
        case BodyOperationKind::bit_xor:
        case BodyOperationKind::shift:
            if (!register_is_valid(operation.destination)
                || !register_is_valid(operation.first_source)
                || !register_is_valid(operation.second_source)) {
                return false;
            }
            break;
        case BodyOperationKind::debug_noop:
            break;
        }
    }

    // Input bindings follow normalized register order rather than raw
    // RegisterId order. The all-inputs-known reduction is commutative, and
    // this diagnostic key models the corresponding helper parameter order.
    append_number(key, body.used_input_registers.size());
    std::vector<std::pair<std::uint32_t, RegisterId>> ordered_inputs;
    ordered_inputs.reserve(body.used_input_registers.size());
    for (const auto reg : body.used_input_registers) {
        const auto found = input_slots.find(reg);
        if (found == input_slots.end()) {
            return false;
        }
        const auto reg_ordinal = register_ordinal(reg);
        if (reg_ordinal == UINT32_MAX || found->second >= signals.size()) {
            return false;
        }
        ordered_inputs.emplace_back(reg_ordinal, reg);
    }
    std::ranges::sort(ordered_inputs, [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    for (const auto& [reg_ordinal, reg] : ordered_inputs) {
        keys.ordered_inputs.push_back(reg);
        const auto slot = input_slots.at(reg);
        const auto slot_ordinal = signal_slot_ordinal(slot);
        if (slot_ordinal == UINT32_MAX) {
            return false;
        }
        const auto& signal = signals[slot];
        append_number(key, reg_ordinal);
        append_number(key, slot_ordinal);
        append_number(key, static_cast<std::uint32_t>(signal.value_kind));
        append_number(key, signal.width);
        append_number(key, signal.word_count);
        append_number(key, signal.plane_count);
        append_number(key, signal.flags);
    }

    append_number(key, body.operations.size());
    for (const auto& operation : body.operations) {
        append_number(key, static_cast<std::uint8_t>(operation.kind));
        append_number(key, operation.width);
        append_number(key, operation.offset);
        append_number(key,
            static_cast<std::uint8_t>(operation.shift_operator));
        const auto append_reg = [&](const RegisterId reg) {
            const auto ordinal = register_ordinal(reg);
            append_number(key, ordinal);
            return ordinal != UINT32_MAX;
        };
        switch (operation.kind) {
        case BodyOperationKind::load_constant:
            if (!append_reg(operation.destination)) {
                return false;
            }
            break;
        case BodyOperationKind::copy_register:
        case BodyOperationKind::extract:
        case BodyOperationKind::unary_not:
        case BodyOperationKind::reduction_and:
        case BodyOperationKind::reduction_or:
        case BodyOperationKind::reduction_xor:
            if (!append_reg(operation.destination)
                || !append_reg(operation.first_source)) {
                return false;
            }
            break;
        case BodyOperationKind::concatenate:
            if (!append_reg(operation.destination)) {
                return false;
            }
            append_number(key, operation.operands.size());
            for (const auto operand : operation.operands) {
                if (!append_reg(operand)) {
                    return false;
                }
            }
            break;
        case BodyOperationKind::conditional_select:
            if (!append_reg(operation.destination)
                || !append_reg(operation.first_source)
                || !append_reg(operation.second_source)
                || !append_reg(operation.third_source)) {
                return false;
            }
            break;
        case BodyOperationKind::bit_and:
        case BodyOperationKind::bit_or:
        case BodyOperationKind::bit_xor:
        case BodyOperationKind::shift:
            if (!append_reg(operation.destination)
                || !append_reg(operation.first_source)
                || !append_reg(operation.second_source)) {
                return false;
            }
            break;
        case BodyOperationKind::debug_noop:
            break;
        }

        append_number(key, operation.constant_aval.size());
        for (const auto value : operation.constant_aval) {
            append_number(key, value);
        }
        append_number(key, operation.constant_bval.size());
        for (const auto value : operation.constant_bval) {
            append_number(key, value);
        }

        if (operation.output_site == UINT32_MAX) {
            append_number(key, 0U);
            continue;
        }
        if (operation.output_site >= sites.size()) {
            return false;
        }
        const auto& site = sites[operation.output_site];
        if (site.member_index != member_index) {
            return false;
        }
        const auto output_ordinal = site_ordinal(operation.output_site);
        const auto output_signal_ordinal
            = signal_slot_ordinal(site.signal_slot);
        if (output_ordinal == UINT32_MAX
            || output_signal_ordinal == UINT32_MAX) {
            return false;
        }
        const auto& signal = signals[site.signal_slot];
        append_number(key, 1U);
        append_number(key, output_ordinal);
        append_number(key, output_signal_ordinal);
        append_number(key, site.update_kind);
        append_number(key, site.event_kind);
        append_number(key, static_cast<std::uint32_t>(site.value_kind));
        append_number(key, site.width);
        append_number(key, site.word_count);
        append_number(key, site.plane_count);
        append_number(key, static_cast<std::uint32_t>(signal.value_kind));
        append_number(key, signal.width);
        append_number(key, signal.word_count);
        append_number(key, signal.plane_count);
        append_number(key, signal.flags);
    }

    append_number(key, registers_by_ordinal.size());
    for (const auto reg : registers_by_ordinal) {
        append_number(key, register_shapes[reg].width);
        append_number(key,
            static_cast<std::uint8_t>(register_shapes[reg].value_kind));
    }
    append_number(key, signal_slots_by_ordinal.size());

    if (!diagnostic_details) {
        return true;
    }

    // The descriptor tuple is deliberately separate from the candidate key.
    // It records every absolute/member-specific binding value normalized
    // above, including event source coordinates that a future shared helper
    // must receive at runtime rather than silently discard.
    auto& binding = keys.binding_tuple;
    binding.append("member-template-binding-tuple-v1;");
    append_number(binding, body.process_id);
    append_number(binding, member_layout.process_id);
    append_number(binding, member_layout.first_write_site);
    append_number(binding, member_layout.write_site_count);
    append_number(binding, member_layout.max_pending_writes);
    append_number(binding, member_layout.max_staged_events);
    for (const auto reserved : member_layout.reserved) {
        append_number(binding, reserved);
    }
    append_number(binding, body.readiness_register);
    append_number(binding, body.begin);
    append_number(binding, body.end);
    append_number(binding, member_index);
    append_number(binding, body.used_input_registers.size());
    for (const auto reg : body.used_input_registers) {
        const auto slot = input_slots.at(reg);
        append_number(binding, reg);
        append_number(binding, slot);
        append_number(binding, signals[slot].signal_id);
        append_number(binding, signals[slot].owner_process_id);
        append_number(binding, signals[slot].metadata_index);
    }
    append_number(binding, body.operations.size());
    for (const auto& operation : body.operations) {
        append_number(binding, operation.instruction);
        append_number(binding, operation.destination);
        append_number(binding, operation.first_source);
        append_number(binding, operation.second_source);
        append_number(binding, operation.third_source);
        append_number(binding, operation.operands.size());
        for (const auto operand : operation.operands) {
            append_number(binding, operand);
        }
        append_number(binding, operation.output_site);
        if (operation.output_site != UINT32_MAX) {
            const auto& site = sites[operation.output_site];
            const auto& signal = signals[site.signal_slot];
            append_number(binding, site.member_index);
            append_number(binding, site.signal_slot);
            append_number(binding, signal.signal_id);
            append_number(binding, signal.owner_process_id);
            append_number(binding, site.source_instruction);
            append_number(binding, site.pending_slot);
            append_number(binding, signal.metadata_index);
        }
    }

    keys.strict_key = key;
    keys.strict_key.append("strict-operation-body-coordinates-v1;");
    keys.strict_key.append(binding);
    return true;
}

void emit_member_shape_census(
    const std::string_view plan_identity,
    const std::vector<MemberBody>& bodies,
    const std::vector<RegionFrontierMemberLayoutV2>& members,
    const std::vector<RegisterShape>& register_shapes,
    const std::vector<RegionFrontierSignalLayoutV2>& signals,
    const std::map<RegisterId, std::uint32_t>& input_slots,
    const std::vector<RegionFrontierWriteSiteV2>& sites)
{
    if (std::getenv("FSIM_PROFILE_LLVM_MODULES") == nullptr) {
        return;
    }
    try {
        std::map<std::string, MemberShapeCensusCounts> shapes;
        std::size_t known_variants { };
        std::size_t fallback_variants { };
        std::size_t unavailable_keys { };
        for (std::size_t index = 0U; index < bodies.size(); ++index) {
            if (index >= members.size()) {
                ++unavailable_keys;
                continue;
            }
            MemberShapeCensusKeys keys;
            if (!append_member_shape_keys(keys, bodies[index], index,
                    members[index], register_shapes, signals, input_slots,
                    sites)) {
                ++unavailable_keys;
                continue;
            }
            const auto& body = bodies[index];
            // Match the branch structure in emit_member_body; these are
            // variant counts, not generated-code size estimates.
            const std::size_t known = body.has_unknown_constant
                    || body.has_logic9_value
                ? 0U : 1U;
            const std::size_t fallback
                = body.has_unknown_constant || body.has_logic9_value
                    || !body.used_input_registers.empty()
                ? 1U : 0U;
            auto& counts = shapes[keys.template_key];
            ++counts.members;
            counts.known_variants += known;
            counts.fallback_variants += fallback;
            counts.operations += body.operations.size();
            counts.strict_shapes.insert(std::move(keys.strict_key));
            counts.binding_tuples.insert(std::move(keys.binding_tuple));
            known_variants += known;
            fallback_variants += fallback;
        }

        std::size_t strict_shapes { };
        std::size_t repeated_templates { };
        std::size_t members_in_repeated_templates { };
        for (const auto& [key, counts] : shapes) {
            static_cast<void>(key);
            strict_shapes += counts.strict_shapes.size();
            if (counts.members > 1U) {
                ++repeated_templates;
                members_in_repeated_templates += counts.members;
            }
        }
        std::fprintf(stderr,
            "[fsim frontier-member-shapes] scope=operation-body-census-only "
            "plan=%.*s members=%zu "
            "candidate_shapes=%zu "
            "known_body_variants=%zu fallback_body_variants=%zu "
            "repeated_templates=%zu members_in_repeated_templates=%zu "
            "strict_body_coordinate_shapes=%zu unavailable_keys=%zu\n",
            static_cast<int>(plan_identity.size()), plan_identity.data(),
            bodies.size(), shapes.size(), known_variants,
            fallback_variants, repeated_templates,
            members_in_repeated_templates, strict_shapes,
            unavailable_keys);
        std::size_t shape_index { };
        for (const auto& [key, counts] : shapes) {
            CacheKeyBuilder shape_builder;
            shape_builder.add("domain",
                "frontier-operation-body-shape-census-candidate-v1");
            shape_builder.add("shape", key);
            const auto shape_id = shape_builder.finish();
            std::fprintf(stderr,
                "[fsim frontier-member-shape] plan=%.*s id=%zu digest=%.*s "
                "members=%zu "
                "known_variants=%zu fallback_variants=%zu "
                "operations_total=%zu strict_body_coordinate_shapes=%zu "
                "unique_binding_tuples=%zu key_bytes=%zu\n",
                static_cast<int>(plan_identity.size()), plan_identity.data(),
                shape_index++, static_cast<int>(shape_id.size()),
                shape_id.data(), counts.members, counts.known_variants,
                counts.fallback_variants, counts.operations,
                counts.strict_shapes.size(),
                counts.binding_tuples.size(), key.size());
        }
    } catch (...) {
        std::fprintf(stderr,
            "[fsim frontier-member-shapes] status=diagnostic-unavailable\n");
    }
}

[[nodiscard]] bool append_operation_identity(std::string& identity,
    const Operation& operation)
{
    if (const auto* constant = operation_get_if<LoadConstant>(&operation)) {
        identity.append("C:");
        append_number(identity, constant->destination);
        append_number(identity, constant->value.width());
        for (const auto word : constant->value.aval_words()) {
            append_number(identity, word);
        }
        identity.push_back('/');
        for (const auto word : constant->value.bval_words()) {
            append_number(identity, word);
        }
        return true;
    } else if (const auto* copy = operation_get_if<CopyRegister>(&operation)) {
        identity.append("P:");
        append_number(identity, copy->destination);
        append_number(identity, copy->source);
        return true;
    } else if (const auto* extract = operation_get_if<Extract>(&operation)) {
        identity.append("E:");
        append_number(identity, extract->destination);
        append_number(identity, extract->source);
        append_number(identity, extract->offset);
        append_number(identity, extract->width);
        return true;
    } else if (const auto* concatenate
        = operation_get_if<Concatenate>(&operation)) {
        identity.append("A:");
        append_number(identity, concatenate->destination);
        append_number(identity, concatenate->width);
        append_number(identity, concatenate->operands.size());
        for (const auto operand : concatenate->operands) {
            append_number(identity, operand);
        }
        return true;
    } else if (const auto* shift = operation_get_if<Shift>(&operation)) {
        identity.append("Q:");
        append_number(identity,
            static_cast<std::uint8_t>(shift->operation));
        append_number(identity, shift->destination);
        append_number(identity, shift->value);
        append_number(identity, shift->amount);
        append_number(identity, shift->signed_amount ? 1U : 0U);
        return true;
    } else if (const auto* select
        = operation_get_if<ConditionalSelect>(&operation)) {
        identity.append("S:");
        append_number(identity, select->destination);
        append_number(identity, select->condition);
        append_number(identity, select->when_true);
        append_number(identity, select->when_false);
        return true;
    } else if (const auto* unary = operation_get_if<UnaryNot>(&operation)) {
        identity.append("N:");
        append_number(identity, unary->destination);
        append_number(identity, unary->source);
        return true;
    } else if (const auto* binary = operation_get_if<Binary>(&operation)) {
        identity.append("B:");
        append_number(identity, static_cast<std::uint8_t>(binary->operation));
        append_number(identity, binary->destination);
        append_number(identity, binary->lhs);
        append_number(identity, binary->rhs);
        return true;
    } else if (const auto* reduction = operation_get_if<Reduction>(&operation)) {
        identity.append("R:");
        append_number(identity,
            static_cast<std::uint8_t>(reduction->operation));
        append_number(identity, reduction->destination);
        append_number(identity, reduction->source);
        return true;
    } else if (const auto* branch = operation_get_if<Branch>(&operation)) {
        identity.append("J:");
        append_number(identity, branch->condition);
        append_number(identity, branch->when_true);
        append_number(identity, branch->when_false);
        append_number(identity,
            static_cast<std::uint8_t>(branch->unknown_policy));
        return true;
    } else if (operation_holds<DebugPoint>(operation)) {
        identity.append("D;");
        return true;
    } else if (operation_holds<Halt>(operation)) {
        identity.append("H;");
        return true;
    } else {
        identity.append("unsupported;");
        return false;
    }
}

[[nodiscard]] std::uint64_t stable_hash(const std::string_view text,
    const std::uint64_t domain) noexcept
{
    auto hash = UINT64_C(14695981039346656037) ^ domain;
    for (const auto byte : text) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= UINT64_C(1099511628211);
    }
    return hash == 0U ? UINT64_C(1) : hash;
}

[[nodiscard]] bool add_register_shape(std::vector<RegisterShape>& shapes,
    const std::uint32_t reg, const std::uint32_t width,
    const ValueKind value_kind)
{
    if (reg >= shapes.size() || width == 0U
        || (value_kind != ValueKind::logic4
            && value_kind != ValueKind::logic9)) {
        return false;
    }
    auto& shape = shapes[reg];
    if (shape.width != 0U) {
        return shape.width == width && shape.value_kind == value_kind;
    }
    shape.width = width;
    shape.value_kind = value_kind;
    return true;
}

[[nodiscard]] bool supported_shift_operator(
    const ShiftOperator operation) noexcept
{
    switch (operation) {
    case ShiftOperator::logical_left:
    case ShiftOperator::logical_right:
    case ShiftOperator::arithmetic_right:
    case ShiftOperator::arithmetic_left:
    case ShiftOperator::rotate_left:
    case ShiftOperator::rotate_right:
        return true;
    }
    return false;
}

[[nodiscard]] llvm::Type* i8(Builder& builder)
{
    return llvm::Type::getInt8Ty(builder.getContext());
}

[[nodiscard]] llvm::Type* i32(Builder& builder)
{
    return llvm::Type::getInt32Ty(builder.getContext());
}

[[nodiscard]] llvm::Type* i64(Builder& builder)
{
    return llvm::Type::getInt64Ty(builder.getContext());
}

[[nodiscard]] llvm::Value* constant_i32(Builder& builder,
    const std::uint32_t value)
{
    return llvm::ConstantInt::get(i32(builder), value);
}

[[nodiscard]] llvm::Value* constant_i64(Builder& builder,
    const std::uint64_t value)
{
    return llvm::ConstantInt::get(i64(builder), value);
}

[[nodiscard]] llvm::Value* byte_offset_pointer(Builder& builder,
    llvm::Value* base, const std::size_t offset)
{
    auto* const bytes = builder.CreateBitCast(base,
        llvm::PointerType::getUnqual(builder.getContext()));
    auto* const address = builder.CreateInBoundsGEP(i8(builder), bytes,
        constant_i64(builder, offset));
    return address;
}

[[nodiscard]] llvm::Value* indexed_byte_pointer(Builder& builder,
    llvm::Value* base, llvm::Value* index, const std::size_t stride)
{
    auto* const bytes = builder.CreateBitCast(base,
        llvm::PointerType::getUnqual(builder.getContext()));
    auto* const offset = builder.CreateMul(
        builder.CreateZExt(index, i64(builder)), constant_i64(builder, stride));
    return builder.CreateInBoundsGEP(i8(builder), bytes, offset);
}

[[nodiscard]] llvm::Value* load_integer(Builder& builder,
    llvm::Value* base, const std::size_t offset, llvm::Type* type,
    const char* name)
{
    return builder.CreateLoad(type,
        byte_offset_pointer(builder, base, offset), name);
}

void store_integer(Builder& builder, llvm::Value* base,
    const std::size_t offset, llvm::Value* value)
{
    builder.CreateStore(value, byte_offset_pointer(builder, base, offset));
}

[[nodiscard]] llvm::Value* load_frame_pointer(Builder& builder,
    llvm::Value* frame, const std::size_t offset, const char* name)
{
    return load_integer(builder, frame, offset,
        llvm::PointerType::getUnqual(builder.getContext()), name);
}

[[nodiscard]] llvm::Value* load_plane_word(Builder& builder,
    llvm::Value* pointer, const std::uint32_t word, const char* name)
{
    auto* const words = builder.CreateBitCast(pointer,
        llvm::PointerType::getUnqual(builder.getContext()));
    auto* const address = builder.CreateInBoundsGEP(i64(builder), words,
        constant_i64(builder, word));
    return builder.CreateLoad(i64(builder), address, name);
}

void store_plane_word(Builder& builder, llvm::Value* pointer,
    const std::uint32_t word, llvm::Value* value)
{
    auto* const words = builder.CreateBitCast(pointer,
        llvm::PointerType::getUnqual(builder.getContext()));
    auto* const address = builder.CreateInBoundsGEP(i64(builder), words,
        constant_i64(builder, word));
    builder.CreateStore(value, address);
}

[[nodiscard]] llvm::Value* plane_word_pointer(Builder& builder,
    llvm::Value* plane, const std::size_t offset, const char* name)
{
    return load_integer(builder, plane, offset,
        llvm::PointerType::getUnqual(builder.getContext()), name);
}

void copy_key(Builder& builder, llvm::Value* destination,
    const std::size_t destination_offset, llvm::Value* source)
{
    constexpr std::size_t fields[] {
        offsetof(RegionFrontierKeyV2, time),
        offsetof(RegionFrontierKeyV2, delta),
        offsetof(RegionFrontierKeyV2, systemverilog_round),
        offsetof(RegionFrontierKeyV2, stable_order),
        offsetof(RegionFrontierKeyV2, sequence),
    };
    for (const auto offset : fields) {
        auto* const value = load_integer(builder, source, offset, i64(builder),
            "key.field");
        store_integer(builder, destination, destination_offset + offset, value);
    }
    constexpr std::size_t fields32[] {
        offsetof(RegionFrontierKeyV2, process_domain),
        offsetof(RegionFrontierKeyV2, phase),
    };
    for (const auto offset : fields32) {
        auto* const value = load_integer(builder, source, offset, i32(builder),
            "key.field32");
        store_integer(builder, destination, destination_offset + offset, value);
    }
}

// Member templates share code only. Their immutable descriptor carries every
// member-specific input and staged-write coordinate; the outer loop retains
// scheduling, debug descriptors, exact admission and member bookkeeping.
struct MemberTemplateRuntimeBinding {
    llvm::Value* descriptor { };
    std::map<RegisterId, std::uint32_t> input_ordinals;
    std::map<std::uint32_t, std::uint32_t> site_ordinals;
    std::uint32_t input_count { };
};

[[nodiscard]] llvm::Value* load_member_template_field(Builder& builder,
    const MemberTemplateRuntimeBinding& binding, const std::uint32_t field,
    const char* name)
{
    return load_integer(builder, binding.descriptor,
        static_cast<std::size_t>(field) * sizeof(std::uint32_t),
        i32(builder), name);
}

[[nodiscard]] llvm::Value* member_template_input_slot(Builder& builder,
    const MemberTemplateRuntimeBinding* binding, const RegisterId reg,
    const std::uint32_t static_slot)
{
    if (binding == nullptr) {
        return constant_i32(builder, static_slot);
    }
    const auto found = binding->input_ordinals.find(reg);
    if (found == binding->input_ordinals.end()) {
        throw std::logic_error("member template input is not certified");
    }
    return load_member_template_field(builder, *binding, 1U + found->second,
        "template.input.slot");
}

[[nodiscard]] WordValue load_input_value(Builder& builder,
    llvm::Value* frame, llvm::Value* signal_slot,
    const RegionFrontierSignalLayoutV2& signal)
{
    auto* const ports = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, port_planes), "port.planes");
    auto* const port_slot = indexed_byte_pointer(builder, ports,
        signal_slot, sizeof(void*));
    auto* const plane = builder.CreateLoad(
        llvm::PointerType::getUnqual(builder.getContext()), port_slot,
        "input.plane");
    const bool internal = (signal.flags
        & RegionFrontierPlaneFlagsV2::certified_internal_single_owner) != 0U;
    WordValue value;
    value.width = signal.width;
    value.value_kind = runtime_value_kind(signal.value_kind);
    const auto planes_offset = internal
        ? offsetof(RegionFrontierPlaneV2, current_planes)
        : offsetof(RegionFrontierPlaneV2, boundary_planes);
    std::vector<llvm::Value*>* const value_planes[] {
        &value.aval, &value.bval, &value.plane2, &value.plane3,
    };
    for (std::uint32_t value_plane = 0U;
         value_plane < signal.plane_count; ++value_plane) {
        auto* const plane_words = plane_word_pointer(builder, plane,
            planes_offset + value_plane * sizeof(std::uint64_t*),
            "input.value.plane");
        value_planes[value_plane]->reserve(signal.word_count);
        for (std::uint32_t word = 0U; word < signal.word_count; ++word) {
            const auto mask = constant_i64(builder,
                word_mask(signal.width, word));
            value_planes[value_plane]->push_back(builder.CreateAnd(
                load_plane_word(builder, plane_words, word,
                    "input.value.word"), mask));
        }
    }
    return value;
}

void emit_stage_write_body(Builder& builder, llvm::Value* frame,
    const RegionFrontierWriteSiteV2& site, const WordValue& value,
    llvm::Value* member_index, llvm::Value* signal_slot,
    llvm::Value* source_instruction, llvm::Value* update_kind,
    llvm::Value* pending_slot, llvm::Value* stable_order_process_id)
{
    auto* const writes = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_writes), "pending.writes");
    auto* const write = indexed_byte_pointer(builder, writes,
        pending_slot, sizeof(RegionFrontierPendingWriteV2));
    if (value.value_kind != runtime_value_kind(site.value_kind)
        || value.width != site.width
        || site.plane_count
            != region_frontier_required_plane_count_v2(site.value_kind)) {
        throw std::logic_error("frontier write value shape changed after certification");
    }
    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, value_kind),
        constant_i32(builder, static_cast<std::uint32_t>(site.value_kind)));
    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, plane_count),
        constant_i32(builder, site.plane_count));
    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, reserved), constant_i32(builder, 0U));
    std::vector<llvm::Value*> const* value_planes[] {
        &value.aval, &value.bval, &value.plane2, &value.plane3,
    };
    for (std::uint32_t value_plane = 0U;
         value_plane < site.plane_count; ++value_plane) {
        if (value_planes[value_plane]->size() != site.word_count) {
            throw std::logic_error("frontier write has malformed value planes");
        }
        auto* const plane_words = load_integer(builder, write,
            offsetof(RegionFrontierPendingWriteV2, value_planes)
                + value_plane * sizeof(std::uint64_t*),
            llvm::PointerType::getUnqual(builder.getContext()),
            "write.value.plane");
        for (std::uint32_t word = 0U; word < site.word_count; ++word) {
            const auto mask = constant_i64(builder,
                word_mask(site.width, word));
            store_plane_word(builder, plane_words, word,
                builder.CreateAnd((*value_planes[value_plane])[word], mask));
        }
    }
    for (std::uint32_t value_plane = site.plane_count;
         value_plane < 4U; ++value_plane) {
        if (!value_planes[value_plane]->empty()) {
            throw std::logic_error("frontier write contains an unused plane");
        }
    }

    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, member_index),
        member_index);
    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, signal_slot),
        signal_slot);
    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, source_instruction),
        source_instruction);
    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, update_kind),
        update_kind);
    std::uint32_t target_flag { };
    if (site.event_kind == static_cast<std::uint32_t>(
            RegionFrontierEventKindV2::internal_commit)) {
        target_flag = RegionFrontierPendingWriteFlagsV2::pending_internal_target;
    } else if (site.event_kind == static_cast<std::uint32_t>(
                   RegionFrontierEventKindV2::boundary_commit)) {
        target_flag = RegionFrontierPendingWriteFlagsV2::pending_boundary_target;
    } else if (site.event_kind == static_cast<std::uint32_t>(
                   RegionFrontierEventKindV2::generic_deferred_update)) {
        target_flag = RegionFrontierPendingWriteFlagsV2::pending_generic_target;
        auto* const commit_key = byte_offset_pointer(builder, write,
            offsetof(RegionFrontierPendingWriteV2, commit_key));
        constexpr std::size_t key_fields64[] {
            offsetof(RegionFrontierKeyV2, time),
            offsetof(RegionFrontierKeyV2, delta),
            offsetof(RegionFrontierKeyV2, systemverilog_round),
            offsetof(RegionFrontierKeyV2, stable_order),
            offsetof(RegionFrontierKeyV2, sequence),
        };
        for (const auto field : key_fields64) {
            store_integer(builder, commit_key, field,
                constant_i64(builder, 0U));
        }
        constexpr std::size_t key_fields32[] {
            offsetof(RegionFrontierKeyV2, process_domain),
            offsetof(RegionFrontierKeyV2, phase),
        };
        for (const auto field : key_fields32) {
            store_integer(builder, commit_key, field,
                constant_i32(builder, 0U));
        }
    } else {
        throw std::logic_error("region frontier write has unknown event kind");
    }
    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, flags),
        constant_i32(builder,
            RegionFrontierPendingWriteFlagsV2::pending_active
                | RegionFrontierPendingWriteFlagsV2::pending_value_ready
                | target_flag));
    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, width),
        constant_i32(builder, site.width));
    store_integer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, word_count),
        constant_i32(builder, site.word_count));

    auto* const members = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, members), "frontier.members");
    auto* const member_pointer = indexed_byte_pointer(builder, members,
        member_index, sizeof(RegionFrontierMemberV2));
    auto* const activation_origin = byte_offset_pointer(builder, member_pointer,
        offsetof(RegionFrontierMemberV2, activation_origin));
    auto* const write_origin = byte_offset_pointer(builder, write,
        offsetof(RegionFrontierPendingWriteV2, origin));
    copy_key(builder, write_origin, 0U, activation_origin);

    auto* const events = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, staged_events), "staged.events");
    auto* const event_count_address = byte_offset_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, staged_event_count));
    auto* const event_count = builder.CreateLoad(i32(builder), event_count_address,
        "staged.event.count");
    auto* const event = indexed_byte_pointer(builder, events, event_count,
        sizeof(RegionFrontierStagedEventV2));
    store_integer(builder, event,
        offsetof(RegionFrontierStagedEventV2, kind),
        constant_i32(builder, site.event_kind));
    store_integer(builder, event,
        offsetof(RegionFrontierStagedEventV2, descriptor_index),
        pending_slot);
    llvm::Value* event_stable_order { };
    if (site.event_kind == static_cast<std::uint32_t>(
            RegionFrontierEventKindV2::generic_deferred_update)) {
        event_stable_order = load_integer(builder, activation_origin,
            offsetof(RegionFrontierKeyV2, stable_order), i64(builder),
            "generic.origin.stable.order");
    } else {
        event_stable_order = builder.CreateZExt(stable_order_process_id,
            i64(builder), "bound.member.stable.order");
    }
    store_integer(builder, event,
        offsetof(RegionFrontierStagedEventV2, stable_order),
        event_stable_order);
    auto* const event_origin = byte_offset_pointer(builder, event,
        offsetof(RegionFrontierStagedEventV2, origin));
    copy_key(builder, event_origin, 0U, activation_origin);
    builder.CreateStore(builder.CreateAdd(event_count, constant_i32(builder, 1U)),
        event_count_address);

    auto* const pending_count_address = byte_offset_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_write_count));
    auto* const pending_count = builder.CreateLoad(i32(builder),
        pending_count_address, "pending.write.count");
    builder.CreateStore(builder.CreateAdd(pending_count,
        constant_i32(builder, 1U)), pending_count_address);
}

// Keep helper calls bounded to a modest scalar payload; larger packed values
// retain inline lowering instead of producing wide internal call signatures.
constexpr std::uint32_t kStageWriteHelperMaximumPayloadWords = 16U;

[[nodiscard]] std::string stage_write_helper_name(
    const RegionFrontierWriteSiteV2& site,
    const bool generic_execution)
{
    return "fsim.frontier.stage.write.v1."
        + std::string { generic_execution ? "generic" : "systemverilog" }
        + ".kind."
        + std::to_string(static_cast<std::uint32_t>(site.value_kind))
        + ".width." + std::to_string(site.width)
        + ".words." + std::to_string(site.word_count)
        + ".planes." + std::to_string(site.plane_count)
        + ".event." + std::to_string(site.event_kind);
}

[[nodiscard]] llvm::Function* get_or_create_stage_write_helper(
    llvm::Module& module, llvm::Type* const frame_type,
    const RegionFrontierWriteSiteV2& site,
    const bool generic_execution)
{
    if (generic_execution
            != (site.event_kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::generic_deferred_update))) {
        throw std::logic_error(
            "region frontier write event does not match execution domain");
    }
    std::vector<llvm::Type*> argument_types;
    argument_types.reserve(7U
        + static_cast<std::size_t>(site.plane_count) * site.word_count);
    argument_types.push_back(frame_type);
    for (std::uint32_t field = 0U; field < 6U; ++field) {
        argument_types.push_back(llvm::Type::getInt32Ty(module.getContext()));
    }
    for (std::uint32_t payload = 0U;
         payload < site.plane_count * site.word_count; ++payload) {
        argument_types.push_back(llvm::Type::getInt64Ty(module.getContext()));
    }
    auto* const function_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(module.getContext()), argument_types, false);
    const auto name = stage_write_helper_name(site, generic_execution);
    if (auto* const existing = module.getFunction(name)) {
        if (existing->getFunctionType() != function_type
            || existing->getLinkage() != llvm::GlobalValue::InternalLinkage
            || !existing->hasFnAttribute(llvm::Attribute::NoInline)
            || !existing->hasFnAttribute(llvm::Attribute::NoUnwind)
            || existing->empty()) {
            throw std::logic_error(
                "region frontier stage-write helper name collision");
        }
        return existing;
    }

    auto* const function = llvm::Function::Create(function_type,
        llvm::GlobalValue::InternalLinkage, name, module);
    function->addFnAttr(llvm::Attribute::NoInline);
    function->addFnAttr(llvm::Attribute::NoUnwind);
    auto* const entry = llvm::BasicBlock::Create(module.getContext(),
        "entry", function);
    Builder helper_builder(module.getContext());
    helper_builder.SetInsertPoint(entry);

    auto argument = function->arg_begin();
    llvm::Value* const frame = &*argument++;
    frame->setName("frame");
    llvm::Value* const member_index = &*argument++;
    member_index->setName("member.index");
    llvm::Value* const signal_slot = &*argument++;
    signal_slot->setName("signal.slot");
    llvm::Value* const source_instruction = &*argument++;
    source_instruction->setName("source.instruction");
    llvm::Value* const update_kind = &*argument++;
    update_kind->setName("update.kind");
    llvm::Value* const pending_slot = &*argument++;
    pending_slot->setName("pending.slot");
    llvm::Value* const stable_order_process_id = &*argument++;
    stable_order_process_id->setName("stable.order.process.id");

    WordValue value;
    value.width = site.width;
    value.value_kind = runtime_value_kind(site.value_kind);
    std::vector<llvm::Value*>* value_planes[] {
        &value.aval, &value.bval, &value.plane2, &value.plane3,
    };
    for (std::uint32_t plane = 0U; plane < site.plane_count; ++plane) {
        value_planes[plane]->reserve(site.word_count);
        for (std::uint32_t word = 0U; word < site.word_count; ++word) {
            value_planes[plane]->push_back(&*argument++);
        }
    }
    if (argument != function->arg_end()) {
        throw std::logic_error(
            "region frontier stage-write helper argument count changed");
    }

    emit_stage_write_body(helper_builder, frame, site, value, member_index,
        signal_slot, source_instruction, update_kind, pending_slot,
        stable_order_process_id);
    helper_builder.CreateRetVoid();
    return function;
}

void emit_stage_write(Builder& builder, llvm::Value* frame,
    const MemberBody& member, const RegionFrontierWriteSiteV2& site,
    const WordValue& value, llvm::Value* bound_process_id,
    const bool generic_execution,
    const MemberTemplateRuntimeBinding* binding = nullptr,
    const std::uint32_t site_index = UINT32_MAX)
{
    if (value.value_kind != runtime_value_kind(site.value_kind)
        || value.width != site.width
        || site.plane_count
            != region_frontier_required_plane_count_v2(site.value_kind)) {
        throw std::logic_error("frontier write value shape changed after certification");
    }
    std::vector<llvm::Value*> const* const value_planes[] {
        &value.aval, &value.bval, &value.plane2, &value.plane3,
    };
    for (std::uint32_t value_plane = 0U;
         value_plane < site.plane_count; ++value_plane) {
        if (value_planes[value_plane]->size() != site.word_count) {
            throw std::logic_error("frontier write has malformed value planes");
        }
    }
    for (std::uint32_t value_plane = site.plane_count;
         value_plane < 4U; ++value_plane) {
        if (!value_planes[value_plane]->empty()) {
            throw std::logic_error("frontier write contains an unused plane");
        }
    }

    if (generic_execution
            != (site.event_kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::generic_deferred_update))) {
        throw std::logic_error(
            "region frontier write event does not match execution domain");
    }

    llvm::Value* member_index = constant_i32(builder, site.member_index);
    llvm::Value* signal_slot = constant_i32(builder, site.signal_slot);
    llvm::Value* source_instruction = constant_i32(builder,
        site.source_instruction);
    llvm::Value* update_kind = constant_i32(builder, site.update_kind);
    llvm::Value* pending_slot = constant_i32(builder, site.pending_slot);
    if (binding != nullptr) {
        const auto found = binding->site_ordinals.find(site_index);
        if (found == binding->site_ordinals.end()) {
            throw std::logic_error("member template write is not certified");
        }
        const auto first_field = 1U + binding->input_count
            + 4U * found->second;
        member_index = load_member_template_field(builder, *binding, 0U,
            "template.member.index");
        signal_slot = load_member_template_field(builder, *binding,
            first_field, "template.output.signal.slot");
        source_instruction = load_member_template_field(builder, *binding,
            first_field + 1U, "template.output.source.instruction");
        update_kind = load_member_template_field(builder, *binding,
            first_field + 2U, "template.output.update.kind");
        pending_slot = load_member_template_field(builder, *binding,
            first_field + 3U, "template.output.pending.slot");
    }

    const bool helper_sized = site.word_count != 0U
        && site.plane_count != 0U
        && site.plane_count
            <= kStageWriteHelperMaximumPayloadWords / site.word_count;
    if (!helper_sized) {
        emit_stage_write_body(builder, frame, site, value,
            member_index, signal_slot, source_instruction, update_kind,
            pending_slot,
            bound_process_id != nullptr ? bound_process_id
                : constant_i32(builder, member.process_id));
        return;
    }

    auto* const insertion = builder.GetInsertBlock();
    if (insertion == nullptr || insertion->getParent() == nullptr) {
        throw std::logic_error("region frontier stage write has no caller function");
    }
    auto* const helper = get_or_create_stage_write_helper(
        *insertion->getModule(), frame->getType(), site, generic_execution);
    std::vector<llvm::Value*> arguments;
    arguments.reserve(7U
        + static_cast<std::size_t>(site.plane_count) * site.word_count);
    arguments.push_back(frame);
    arguments.push_back(member_index);
    arguments.push_back(signal_slot);
    arguments.push_back(source_instruction);
    arguments.push_back(update_kind);
    arguments.push_back(pending_slot);
    arguments.push_back(bound_process_id != nullptr ? bound_process_id
        : constant_i32(builder, member.process_id));
    for (std::uint32_t plane = 0U; plane < site.plane_count; ++plane) {
        arguments.insert(arguments.end(), value_planes[plane]->begin(),
            value_planes[plane]->end());
    }
    if (arguments.size() != helper->arg_size()) {
        throw std::logic_error(
            "region frontier stage-write call argument count changed");
    }
    builder.CreateCall(helper, arguments);
}

[[nodiscard]] constexpr std::uint64_t low_bit_mask(
    const std::uint32_t bit_count) noexcept
{
    if (bit_count == 0U) {
        return 0U;
    }
    if (bit_count >= 64U) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << bit_count) - 1U;
}

[[nodiscard]] std::uint64_t shift_fill_mask(
    const std::uint32_t width, const std::uint32_t word,
    const ShiftOperator operation, const std::uint32_t distance) noexcept
{
    const auto left = operation == ShiftOperator::arithmetic_left;
    const auto right = operation == ShiftOperator::arithmetic_right;
    if (!left && !right) {
        return 0U;
    }

    const auto word_begin = std::uint64_t { word } * 64U;
    const auto word_end = std::min<std::uint64_t>(
        width, word_begin + 64U);
    const auto fill_begin = right ? width - distance : 0U;
    const auto fill_end = left ? distance : width;
    const auto begin = std::max<std::uint64_t>(word_begin, fill_begin);
    const auto end = std::min<std::uint64_t>(word_end, fill_end);
    if (begin >= end) {
        return 0U;
    }
    const auto local_begin = static_cast<std::uint32_t>(begin - word_begin);
    const auto local_end = static_cast<std::uint32_t>(end - word_begin);
    return low_bit_mask(local_end) & ~low_bit_mask(local_begin);
}

[[nodiscard]] llvm::Value* emit_bit_is_set(Builder& builder,
    llvm::Value* word, const std::uint32_t bit)
{
    const auto shifted = builder.CreateLShr(word, constant_i64(builder, bit));
    return builder.CreateICmpNE(
        builder.CreateAnd(shifted, constant_i64(builder, 1U)),
        constant_i64(builder, 0U));
}

[[nodiscard]] WordValue emit_fixed_shift(Builder& builder,
    const WordValue& source, const ShiftOperator operation,
    const std::uint32_t distance, const bool known_logic4)
{
    const auto word_shift = distance / 64U;
    const auto bit_shift = distance % 64U;
    const auto word_count = word_count_for(source.width);
    const auto left = operation == ShiftOperator::logical_left
        || operation == ShiftOperator::arithmetic_left;
    const auto arithmetic_left
        = operation == ShiftOperator::arithmetic_left;

    WordValue result;
    result.width = source.width;
    result.aval.reserve(word_count);
    result.bval.reserve(word_count);
    for (std::uint32_t word = 0U; word < word_count; ++word) {
        llvm::Value* aval = constant_i64(builder, 0U);
        llvm::Value* bval = constant_i64(builder, 0U);
        if (left) {
            if (word >= word_shift) {
                const auto source_word = word - word_shift;
                auto* part_aval = source.aval[source_word];
                llvm::Value* part_bval = constant_i64(builder, 0U);
                if (!known_logic4) {
                    part_bval = source.bval[source_word];
                }
                if (bit_shift != 0U) {
                    const auto amount = constant_i64(builder, bit_shift);
                    part_aval = builder.CreateShl(part_aval, amount);
                    if (!known_logic4) {
                        part_bval = builder.CreateShl(part_bval, amount);
                    }
                }
                aval = builder.CreateOr(aval, part_aval);
                if (!known_logic4) {
                    bval = builder.CreateOr(bval, part_bval);
                }
            }
            if (bit_shift != 0U && word > word_shift) {
                const auto source_word = word - word_shift - 1U;
                const auto amount = constant_i64(builder, 64U - bit_shift);
                const auto part_aval = builder.CreateLShr(
                    source.aval[source_word], amount);
                aval = builder.CreateOr(aval, part_aval);
                if (!known_logic4) {
                    const auto part_bval = builder.CreateLShr(
                        source.bval[source_word], amount);
                    bval = builder.CreateOr(bval, part_bval);
                }
            }
        } else {
            const auto first_source_word
                = std::uint64_t { word } + word_shift;
            if (first_source_word < word_count) {
                auto* part_aval = source.aval[first_source_word];
                llvm::Value* part_bval = constant_i64(builder, 0U);
                if (!known_logic4) {
                    part_bval = source.bval[first_source_word];
                }
                if (bit_shift != 0U) {
                    const auto amount = constant_i64(builder, bit_shift);
                    part_aval = builder.CreateLShr(part_aval, amount);
                    if (!known_logic4) {
                        part_bval = builder.CreateLShr(part_bval, amount);
                    }
                }
                aval = builder.CreateOr(aval, part_aval);
                if (!known_logic4) {
                    bval = builder.CreateOr(bval, part_bval);
                }
            }
            const auto second_source_word = first_source_word + 1U;
            if (bit_shift != 0U && second_source_word < word_count) {
                const auto amount = constant_i64(builder, 64U - bit_shift);
                const auto part_aval = builder.CreateShl(
                    source.aval[second_source_word], amount);
                aval = builder.CreateOr(aval, part_aval);
                if (!known_logic4) {
                    const auto part_bval = builder.CreateShl(
                        source.bval[second_source_word], amount);
                    bval = builder.CreateOr(bval, part_bval);
                }
            }
        }

        const auto fill_mask = shift_fill_mask(
            source.width, word, operation, distance);
        if (fill_mask != 0U) {
            const auto fill_word = arithmetic_left
                ? 0U
                : (source.width - 1U) / 64U;
            const auto fill_bit = arithmetic_left
                ? 0U
                : (source.width - 1U) % 64U;
            const auto fill_aval = emit_bit_is_set(builder,
                source.aval[fill_word], fill_bit);
            const auto fill_aval_word = builder.CreateSelect(fill_aval,
                constant_i64(builder, fill_mask), constant_i64(builder, 0U));
            aval = builder.CreateOr(aval, fill_aval_word);
            if (!known_logic4) {
                const auto fill_bval = emit_bit_is_set(builder,
                    source.bval[fill_word], fill_bit);
                const auto fill_bval_word = builder.CreateSelect(fill_bval,
                    constant_i64(builder, fill_mask),
                    constant_i64(builder, 0U));
                bval = builder.CreateOr(bval, fill_bval_word);
            }
        }

        const auto mask = constant_i64(builder, word_mask(source.width, word));
        result.aval.push_back(builder.CreateAnd(aval, mask));
        result.bval.push_back(known_logic4
            ? constant_i64(builder, 0U)
            : builder.CreateAnd(bval, mask));
    }
    return result;
}

[[nodiscard]] WordValue emit_word_select(Builder& builder,
    llvm::Value* condition, const WordValue& when_true,
    const WordValue& when_false)
{
    WordValue result;
    result.width = when_true.width;
    result.aval.reserve(when_true.aval.size());
    result.bval.reserve(when_true.bval.size());
    for (std::size_t word = 0U; word < when_true.aval.size(); ++word) {
        result.aval.push_back(builder.CreateSelect(condition,
            when_true.aval[word], when_false.aval[word]));
        result.bval.push_back(builder.CreateSelect(condition,
            when_true.bval[word], when_false.bval[word]));
    }
    return result;
}

[[nodiscard]] WordValue emit_rotate_stage(Builder& builder,
    const WordValue& source, const std::uint32_t distance,
    const bool left, const bool known_logic4)
{
    const auto first_operation = left
        ? ShiftOperator::logical_left
        : ShiftOperator::logical_right;
    const auto second_operation = left
        ? ShiftOperator::logical_right
        : ShiftOperator::logical_left;
    const auto first = emit_fixed_shift(builder, source,
        first_operation, distance, known_logic4);
    const auto second = emit_fixed_shift(builder, source,
        second_operation, source.width - distance, known_logic4);

    WordValue result;
    result.width = source.width;
    result.aval.reserve(first.aval.size());
    result.bval.reserve(first.bval.size());
    for (std::size_t word = 0U; word < first.aval.size(); ++word) {
        const auto mask = constant_i64(builder,
            word_mask(source.width, static_cast<std::uint32_t>(word)));
        result.aval.push_back(builder.CreateAnd(builder.CreateOr(
            first.aval[word], second.aval[word]), mask));
        result.bval.push_back(known_logic4
            ? constant_i64(builder, 0U)
            : builder.CreateAnd(builder.CreateOr(first.bval[word],
                second.bval[word]), mask));
    }
    return result;
}

[[nodiscard]] WordValue emit_shift_fill(Builder& builder,
    const WordValue& source, const ShiftOperator operation,
    const bool known_logic4)
{
    WordValue result;
    result.width = source.width;
    const auto word_count = word_count_for(source.width);
    result.aval.reserve(word_count);
    result.bval.reserve(word_count);

    const auto arithmetic_left
        = operation == ShiftOperator::arithmetic_left;
    const auto arithmetic_right
        = operation == ShiftOperator::arithmetic_right;
    llvm::Value* fill_aval = nullptr;
    llvm::Value* fill_bval = nullptr;
    if (arithmetic_left || arithmetic_right) {
        const auto fill_word = arithmetic_left
            ? 0U
            : (source.width - 1U) / 64U;
        const auto fill_bit = arithmetic_left
            ? 0U
            : (source.width - 1U) % 64U;
        fill_aval = emit_bit_is_set(builder, source.aval[fill_word], fill_bit);
        if (!known_logic4) {
            fill_bval = emit_bit_is_set(builder, source.bval[fill_word], fill_bit);
        }
    }
    for (std::uint32_t word = 0U; word < word_count; ++word) {
        const auto mask = constant_i64(builder, word_mask(source.width, word));
        llvm::Value* aval = constant_i64(builder, 0U);
        llvm::Value* bval = constant_i64(builder, 0U);
        if (fill_aval != nullptr) {
            aval = builder.CreateSelect(fill_aval, mask,
                constant_i64(builder, 0U));
            if (!known_logic4) {
                bval = builder.CreateSelect(fill_bval, mask,
                    constant_i64(builder, 0U));
            }
        }
        result.aval.push_back(aval);
        result.bval.push_back(known_logic4 ? constant_i64(builder, 0U) : bval);
    }
    return result;
}

[[nodiscard]] WordValue emit_unknown_shift_result(Builder& builder,
    const std::uint32_t width)
{
    WordValue result;
    result.width = width;
    const auto word_count = word_count_for(width);
    result.aval.reserve(word_count);
    result.bval.reserve(word_count);
    for (std::uint32_t word = 0U; word < word_count; ++word) {
        const auto mask = constant_i64(builder, word_mask(width, word));
        result.aval.push_back(mask);
        result.bval.push_back(mask);
    }
    return result;
}

[[nodiscard]] WordValue emit_shift_value(Builder& builder,
    const WordValue& source, const WordValue& amount,
    const ShiftOperator operation, const bool known_logic4)
{
    llvm::Value* unknown_amount
        = llvm::ConstantInt::getFalse(builder.getContext());
    if (!known_logic4) {
        for (std::uint32_t word = 0U; word < amount.bval.size(); ++word) {
            const auto mask = constant_i64(builder,
                word_mask(amount.width, word));
            const auto bval = builder.CreateAnd(amount.bval[word], mask);
            unknown_amount = builder.CreateOr(unknown_amount,
                builder.CreateICmpNE(bval, constant_i64(builder, 0U)));
        }
    }

    auto shifted = source;
    const auto rotating = operation == ShiftOperator::rotate_left
        || operation == ShiftOperator::rotate_right;
    if (rotating) {
        llvm::Value* remainder = constant_i64(builder, 0U);
        for (std::size_t word = amount.aval.size(); word > 0U; --word) {
            const auto word_index = word - 1U;
            for (std::uint32_t bit_index = 64U; bit_index > 0U; --bit_index) {
                const auto bit = bit_index - 1U;
                const auto global_bit = std::uint64_t { word_index } * 64U
                    + bit;
                if (global_bit >= amount.width) {
                    continue;
                }
                const auto amount_bit = builder.CreateZExt(emit_bit_is_set(
                    builder, amount.aval[word_index], bit), i64(builder));
                const auto doubled = builder.CreateAdd(
                    builder.CreateShl(remainder, constant_i64(builder, 1U)),
                    amount_bit);
                remainder = builder.CreateURem(doubled,
                    constant_i64(builder, source.width));
            }
        }

        auto distance = 1U % source.width;
        auto rotation_bits = 0U;
        for (auto maximum = source.width - 1U;
             maximum != 0U; maximum >>= 1U) {
            ++rotation_bits;
        }
        for (std::uint32_t bit = 0U;
             bit < rotation_bits; ++bit) {
            if (distance != 0U) {
                const auto has_bit = builder.CreateICmpNE(
                    builder.CreateAnd(remainder,
                        constant_i64(builder, UINT64_C(1) << bit)),
                    constant_i64(builder, 0U));
                const auto candidate = emit_rotate_stage(builder, shifted,
                    distance, operation == ShiftOperator::rotate_left,
                    known_logic4);
                shifted = emit_word_select(builder, has_bit, candidate, shifted);
            }
            distance = distance >= source.width - distance
                ? distance - (source.width - distance)
                : distance + distance;
        }
    } else {
        llvm::Value* oversized
            = llvm::ConstantInt::getFalse(builder.getContext());
        std::uint64_t distance = 1U;
        for (std::size_t word = 0U; word < amount.aval.size(); ++word) {
            for (std::uint32_t bit = 0U; bit < 64U; ++bit) {
                const auto global_bit = std::uint64_t { word } * 64U + bit;
                if (global_bit >= amount.width) {
                    continue;
                }
                const auto selected = emit_bit_is_set(
                    builder, amount.aval[word], bit);
                if (distance < source.width) {
                    const auto candidate = emit_fixed_shift(builder, shifted,
                        operation, static_cast<std::uint32_t>(distance),
                        known_logic4);
                    shifted = emit_word_select(builder, selected,
                        candidate, shifted);
                    distance = distance > source.width / 2U
                        ? source.width
                        : distance * 2U;
                } else {
                    oversized = builder.CreateOr(oversized, selected);
                }
            }
        }
        const auto fill = emit_shift_fill(builder, source, operation,
            known_logic4);
        shifted = emit_word_select(builder, oversized, fill, shifted);
    }

    const auto unknown = emit_unknown_shift_result(builder, source.width);
    return emit_word_select(builder, unknown_amount, unknown, shifted);
}

void emit_member_operations(Builder& builder,
    llvm::Value* frame, const MemberBody& member,
    const std::vector<RegionFrontierSignalLayoutV2>& signals,
    const std::map<RegisterId, std::uint32_t>& input_slots,
    const std::vector<RegionFrontierWriteSiteV2>& sites,
    const bool known_logic4, std::map<RegisterId, WordValue> values,
    llvm::Value* bound_process_id, const bool generic_execution,
    const MemberTemplateRuntimeBinding* binding = nullptr)
{
    const auto lookup = [&](const RegisterId reg) -> const WordValue& {
        const auto found = values.find(reg);
        if (found != values.end()) {
            return found->second;
        }
        const auto input = input_slots.find(reg);
        if (input == input_slots.end()) {
            throw std::logic_error("unbound region frontier member register");
        }
        if (known_logic4) {
            throw std::logic_error(
                "known region frontier member input was not preloaded");
        }
        const auto inserted = values.emplace(reg,
            load_input_value(builder, frame,
                member_template_input_slot(builder, binding, reg,
                    input->second), signals[input->second]));
        return inserted.first->second;
    };

    for (const auto& operation : member.operations) {
        switch (operation.kind) {
        case BodyOperationKind::load_constant: {
            WordValue value;
            value.width = operation.width;
            value.aval.reserve(operation.constant_aval.size());
            value.bval.reserve(operation.constant_bval.size());
            for (std::size_t word = 0U;
                 word < operation.constant_aval.size(); ++word) {
                value.aval.push_back(constant_i64(builder,
                    operation.constant_aval[word]));
                value.bval.push_back(known_logic4
                    ? constant_i64(builder, 0U)
                    : constant_i64(builder, operation.constant_bval[word]));
            }
            values.insert_or_assign(operation.destination, std::move(value));
            break;
        }
        case BodyOperationKind::copy_register: {
            auto value = lookup(operation.first_source);
            if (value.value_kind == ValueKind::logic9) {
                auto copied = llvm_detail::emit_logic9_copy(builder,
                    logic9_word_value(value));
                if (!copied) {
                    throw std::logic_error(
                        "certified Logic9 copy has an invalid static shape");
                }
                value = word_value_from_logic9(std::move(*copied));
            }
            if (operation.output_site != UINT32_MAX) {
                emit_stage_write(builder, frame, member,
                    sites[operation.output_site], value, bound_process_id,
                    generic_execution, binding, operation.output_site);
            }
            values.insert_or_assign(operation.destination, std::move(value));
            break;
        }
        case BodyOperationKind::extract: {
            const auto& source = lookup(operation.first_source);
            WordValue value;
            value.width = operation.width;
            value.aval.reserve(word_count_for(value.width));
            value.bval.reserve(word_count_for(value.width));
            const auto source_word_offset
                = static_cast<std::size_t>(operation.offset / 64U);
            const auto bit_offset = operation.offset % 64U;
            for (std::size_t word = 0U;
                 word < word_count_for(value.width); ++word) {
                const auto source_word = source_word_offset + word;
                llvm::Value* aval = source.aval[source_word];
                llvm::Value* bval = known_logic4
                    ? constant_i64(builder, 0U)
                    : source.bval[source_word];
                if (bit_offset != 0U) {
                    const auto shift_right = constant_i64(builder, bit_offset);
                    const auto shift_left = constant_i64(builder,
                        64U - bit_offset);
                    const auto low_aval = builder.CreateLShr(aval, shift_right);
                    llvm::Value* low_bval = constant_i64(builder, 0U);
                    if (!known_logic4) {
                        low_bval = builder.CreateLShr(bval, shift_right);
                    }
                    llvm::Value* high_aval
                        = llvm::ConstantInt::get(i64(builder), 0U);
                    llvm::Value* high_bval
                        = llvm::ConstantInt::get(i64(builder), 0U);
                    if (source_word + 1U < source.aval.size()) {
                        high_aval = builder.CreateShl(
                            source.aval[source_word + 1U], shift_left);
                        if (!known_logic4) {
                            high_bval = builder.CreateShl(
                                source.bval[source_word + 1U], shift_left);
                        }
                    }
                    aval = builder.CreateOr(low_aval, high_aval);
                    if (!known_logic4) {
                        bval = builder.CreateOr(low_bval, high_bval);
                    }
                }
                const auto mask = constant_i64(builder,
                    word_mask(value.width, static_cast<std::uint32_t>(word)));
                value.aval.push_back(builder.CreateAnd(aval, mask));
                value.bval.push_back(known_logic4
                    ? constant_i64(builder, 0U)
                    : builder.CreateAnd(bval, mask));
            }
            values.insert_or_assign(operation.destination, std::move(value));
            break;
        }
        case BodyOperationKind::concatenate: {
            WordValue value;
            value.width = operation.width;
            const auto destination_word_count
                = word_count_for(value.width);
            value.aval.reserve(destination_word_count);
            value.bval.reserve(destination_word_count);
            for (std::uint32_t word = 0U;
                 word < destination_word_count; ++word) {
                value.aval.push_back(constant_i64(builder, 0U));
                value.bval.push_back(constant_i64(builder, 0U));
            }

            std::uint64_t bit_offset { };
            for (auto operand = operation.operands.rbegin();
                 operand != operation.operands.rend(); ++operand) {
                const auto& source = lookup(*operand);
                const auto destination_word_base
                    = static_cast<std::size_t>(bit_offset / 64U);
                const auto destination_bit_offset
                    = static_cast<std::uint32_t>(bit_offset % 64U);
                for (std::size_t source_word = 0U;
                     source_word < source.aval.size(); ++source_word) {
                    const auto source_mask = constant_i64(builder,
                        word_mask(source.width,
                            static_cast<std::uint32_t>(source_word)));
                    const auto source_aval = builder.CreateAnd(
                        source.aval[source_word], source_mask);
                    llvm::Value* source_bval
                        = constant_i64(builder, 0U);
                    if (!known_logic4) {
                        source_bval = builder.CreateAnd(
                            source.bval[source_word], source_mask);
                    }
                    const auto destination_word
                        = destination_word_base + source_word;
                    if (destination_word
                        < value.aval.size()) {
                        llvm::Value* low_aval = source_aval;
                        llvm::Value* low_bval = source_bval;
                        if (destination_bit_offset != 0U) {
                            const auto shift = constant_i64(builder,
                                destination_bit_offset);
                            low_aval = builder.CreateShl(source_aval, shift);
                            if (!known_logic4) {
                                low_bval = builder.CreateShl(source_bval, shift);
                            }
                        }
                        value.aval[destination_word] = builder.CreateOr(
                            value.aval[destination_word], low_aval);
                        if (!known_logic4) {
                            value.bval[destination_word] = builder.CreateOr(
                                value.bval[destination_word], low_bval);
                        }
                    }
                    if (destination_bit_offset != 0U
                        && destination_word + 1U < value.aval.size()) {
                        const auto shift = constant_i64(builder,
                            64U - destination_bit_offset);
                        const auto high_aval
                            = builder.CreateLShr(source_aval, shift);
                        llvm::Value* high_bval
                            = constant_i64(builder, 0U);
                        if (!known_logic4) {
                            high_bval = builder.CreateLShr(source_bval, shift);
                        }
                        value.aval[destination_word + 1U]
                            = builder.CreateOr(
                                value.aval[destination_word + 1U],
                                high_aval);
                        if (!known_logic4) {
                            value.bval[destination_word + 1U]
                                = builder.CreateOr(
                                    value.bval[destination_word + 1U],
                                    high_bval);
                        }
                    }
                }
                bit_offset += source.width;
            }

            for (std::uint32_t word = 0U;
                 word < destination_word_count; ++word) {
                const auto mask = constant_i64(builder,
                    word_mask(value.width, word));
                value.aval[word] = builder.CreateAnd(value.aval[word], mask);
                if (!known_logic4) {
                    value.bval[word] = builder.CreateAnd(value.bval[word], mask);
                }
            }
            values.insert_or_assign(operation.destination, std::move(value));
            break;
        }
        case BodyOperationKind::conditional_select: {
            const auto& condition = lookup(operation.first_source);
            const auto& when_true = lookup(operation.second_source);
            const auto& when_false = lookup(operation.third_source);
            llvm::Value* unknown
                = llvm::ConstantInt::getFalse(builder.getContext());
            if (!known_logic4) {
                unknown = builder.CreateICmpNE(
                    builder.CreateAnd(condition.bval[0],
                        constant_i64(builder, 1U)),
                    constant_i64(builder, 0U));
            }
            auto* const select_true = builder.CreateICmpNE(
                builder.CreateAnd(condition.aval[0],
                    constant_i64(builder, 1U)), constant_i64(builder, 0U));
            WordValue value;
            value.width = operation.width;
            value.aval.reserve(when_true.aval.size());
            value.bval.reserve(when_true.bval.size());
            for (std::size_t word = 0U; word < when_true.aval.size(); ++word) {
                const auto mask = constant_i64(builder,
                    word_mask(value.width, static_cast<std::uint32_t>(word)));
                const auto known_aval = builder.CreateSelect(select_true,
                    when_true.aval[word], when_false.aval[word]);
                if (known_logic4) {
                    value.aval.push_back(builder.CreateAnd(known_aval, mask));
                    value.bval.push_back(constant_i64(builder, 0U));
                } else {
                    const auto different = builder.CreateAnd(
                        builder.CreateOr(
                            builder.CreateXor(when_true.aval[word],
                                when_false.aval[word]),
                            builder.CreateXor(when_true.bval[word],
                                when_false.bval[word])), mask);
                    const auto same = builder.CreateXor(different, mask);
                    const auto merged_aval = builder.CreateOr(
                        builder.CreateAnd(when_true.aval[word], same),
                        different);
                    const auto merged_bval = builder.CreateOr(
                        builder.CreateAnd(when_true.bval[word], same),
                        different);
                    const auto known_bval = builder.CreateSelect(select_true,
                        when_true.bval[word], when_false.bval[word]);
                    value.aval.push_back(builder.CreateAnd(
                        builder.CreateSelect(unknown, merged_aval, known_aval),
                        mask));
                    value.bval.push_back(builder.CreateAnd(
                        builder.CreateSelect(unknown, merged_bval, known_bval),
                        mask));
                }
            }
            values.insert_or_assign(operation.destination, std::move(value));
            break;
        }
        case BodyOperationKind::unary_not: {
            const auto& source = lookup(operation.first_source);
            if (source.value_kind == ValueKind::logic9) {
                if (known_logic4) {
                    throw std::logic_error(
                        "Logic9 member entered the Logic4 knownness path");
                }
                auto lowered = llvm_detail::emit_logic9_not(builder,
                    logic9_word_value(source));
                if (!lowered) {
                    throw std::logic_error(
                        "certified Logic9 NOT has an invalid static shape");
                }
                values.insert_or_assign(operation.destination,
                    word_value_from_logic9(std::move(*lowered)));
                break;
            }
            WordValue value;
            value.width = operation.width;
            value.aval.reserve(source.aval.size());
            value.bval.reserve(source.bval.size());
            for (std::size_t word = 0U; word < source.aval.size(); ++word) {
                const auto mask = constant_i64(builder,
                    word_mask(value.width, static_cast<std::uint32_t>(word)));
                if (known_logic4) {
                    value.aval.push_back(builder.CreateAnd(
                        builder.CreateNot(source.aval[word]), mask));
                    value.bval.push_back(constant_i64(builder, 0U));
                } else {
                    value.aval.push_back(builder.CreateAnd(
                        builder.CreateOr(builder.CreateNot(source.aval[word]),
                            source.bval[word]), mask));
                    value.bval.push_back(builder.CreateAnd(
                        source.bval[word], mask));
                }
            }
            values.insert_or_assign(operation.destination, std::move(value));
            break;
        }
        case BodyOperationKind::bit_and:
        case BodyOperationKind::bit_or:
        case BodyOperationKind::bit_xor: {
            const auto& left = lookup(operation.first_source);
            const auto& right = lookup(operation.second_source);
            if (left.value_kind == ValueKind::logic9) {
                if (known_logic4 || right.value_kind != ValueKind::logic9) {
                    throw std::logic_error(
                        "certified Logic9 binary operands changed kind");
                }
                const auto operation_kind
                    = operation.kind == BodyOperationKind::bit_and
                    ? BinaryOperator::bit_and
                    : operation.kind == BodyOperationKind::bit_or
                    ? BinaryOperator::bit_or
                    : BinaryOperator::bit_xor;
                auto lowered = llvm_detail::emit_logic9_binary(builder,
                    logic9_word_value(left), logic9_word_value(right),
                    operation_kind);
                if (!lowered) {
                    throw std::logic_error(
                        "certified Logic9 binary operation has an invalid static shape");
                }
                values.insert_or_assign(operation.destination,
                    word_value_from_logic9(std::move(*lowered)));
                break;
            }
            WordValue value;
            value.width = operation.width;
            value.aval.reserve(left.aval.size());
            value.bval.reserve(left.bval.size());
            for (std::size_t word = 0U; word < left.aval.size(); ++word) {
                const auto mask = constant_i64(builder,
                    word_mask(value.width, static_cast<std::uint32_t>(word)));
                llvm::Value* aval { };
                llvm::Value* bval { };
                if (known_logic4) {
                    if (operation.kind == BodyOperationKind::bit_and) {
                        aval = builder.CreateAnd(left.aval[word],
                            right.aval[word]);
                    } else if (operation.kind == BodyOperationKind::bit_or) {
                        aval = builder.CreateOr(left.aval[word],
                            right.aval[word]);
                    } else {
                        aval = builder.CreateXor(left.aval[word],
                            right.aval[word]);
                    }
                    bval = constant_i64(builder, 0U);
                } else {
                    const auto left_zero = builder.CreateAnd(
                        builder.CreateNot(left.aval[word]),
                        builder.CreateNot(left.bval[word]));
                    const auto right_zero = builder.CreateAnd(
                        builder.CreateNot(right.aval[word]),
                        builder.CreateNot(right.bval[word]));
                    const auto left_one = builder.CreateAnd(left.aval[word],
                        builder.CreateNot(left.bval[word]));
                    const auto right_one = builder.CreateAnd(right.aval[word],
                        builder.CreateNot(right.bval[word]));
                    if (operation.kind == BodyOperationKind::bit_and) {
                        const auto known_zero = builder.CreateOr(left_zero,
                            right_zero);
                        const auto known_one = builder.CreateAnd(left_one,
                            right_one);
                        bval = builder.CreateNot(builder.CreateOr(known_zero,
                            known_one));
                        aval = builder.CreateOr(known_one, bval);
                    } else if (operation.kind == BodyOperationKind::bit_or) {
                        const auto known_zero = builder.CreateAnd(left_zero,
                            right_zero);
                        const auto known_one = builder.CreateOr(left_one,
                            right_one);
                        bval = builder.CreateNot(builder.CreateOr(known_zero,
                            known_one));
                        aval = builder.CreateOr(known_one, bval);
                    } else {
                        const auto known = builder.CreateAnd(
                            builder.CreateNot(left.bval[word]),
                            builder.CreateNot(right.bval[word]));
                        bval = builder.CreateNot(known);
                        aval = builder.CreateOr(bval, builder.CreateAnd(
                            builder.CreateXor(left.aval[word], right.aval[word]),
                            known));
                    }
                }
                value.aval.push_back(builder.CreateAnd(aval, mask));
                value.bval.push_back(known_logic4
                    ? constant_i64(builder, 0U)
                    : builder.CreateAnd(bval, mask));
            }
            values.insert_or_assign(operation.destination, std::move(value));
            break;
        }
        case BodyOperationKind::reduction_and:
        case BodyOperationKind::reduction_or:
        case BodyOperationKind::reduction_xor: {
            const auto& source = lookup(operation.first_source);
            llvm::Value* reduced_a { };
            llvm::Value* reduced_b = constant_i64(builder, 0U);
            if (known_logic4) {
                llvm::Value* any_one
                    = llvm::ConstantInt::getFalse(builder.getContext());
                llvm::Value* all_one
                    = llvm::ConstantInt::getTrue(builder.getContext());
                llvm::Value* parity
                    = llvm::ConstantInt::getFalse(builder.getContext());
                for (std::size_t word = 0U; word < source.aval.size(); ++word) {
                    const auto mask = constant_i64(builder,
                        word_mask(source.width,
                            static_cast<std::uint32_t>(word)));
                    const auto aval = builder.CreateAnd(
                        source.aval[word], mask);
                    any_one = builder.CreateOr(any_one,
                        builder.CreateICmpNE(aval,
                            constant_i64(builder, 0U)));
                    all_one = builder.CreateAnd(all_one,
                        builder.CreateICmpEQ(aval, mask));
                    auto* folded = builder.CreateXor(aval,
                        builder.CreateLShr(aval,
                            constant_i64(builder, 32U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 16U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 8U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 4U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 2U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 1U)));
                    parity = builder.CreateXor(parity,
                        builder.CreateICmpNE(builder.CreateAnd(folded,
                            constant_i64(builder, 1U)),
                            constant_i64(builder, 0U)));
                }
                if (operation.kind == BodyOperationKind::reduction_and) {
                    reduced_a = all_one;
                } else if (operation.kind == BodyOperationKind::reduction_or) {
                    reduced_a = any_one;
                } else {
                    reduced_a = parity;
                }
            } else {
                llvm::Value* any_zero
                    = llvm::ConstantInt::getFalse(builder.getContext());
                llvm::Value* any_one
                    = llvm::ConstantInt::getFalse(builder.getContext());
                llvm::Value* any_unknown
                    = llvm::ConstantInt::getFalse(builder.getContext());
                llvm::Value* all_one
                    = llvm::ConstantInt::getTrue(builder.getContext());
                llvm::Value* all_zero
                    = llvm::ConstantInt::getTrue(builder.getContext());
                llvm::Value* parity
                    = llvm::ConstantInt::getFalse(builder.getContext());
                for (std::size_t word = 0U;
                     word < source.aval.size(); ++word) {
                    const auto mask = constant_i64(builder,
                        word_mask(source.width,
                            static_cast<std::uint32_t>(word)));
                    const auto aval = builder.CreateAnd(source.aval[word], mask);
                    const auto bval = builder.CreateAnd(source.bval[word], mask);
                    const auto known_zero = builder.CreateAnd(mask,
                        builder.CreateAnd(builder.CreateNot(aval),
                            builder.CreateNot(bval)));
                    const auto known_one = builder.CreateAnd(mask,
                        builder.CreateAnd(aval, builder.CreateNot(bval)));
                    any_zero = builder.CreateOr(any_zero,
                        builder.CreateICmpNE(known_zero,
                            constant_i64(builder, 0U)));
                    any_one = builder.CreateOr(any_one,
                        builder.CreateICmpNE(known_one,
                            constant_i64(builder, 0U)));
                    any_unknown = builder.CreateOr(any_unknown,
                        builder.CreateICmpNE(bval,
                            constant_i64(builder, 0U)));
                    all_one = builder.CreateAnd(all_one,
                        builder.CreateICmpEQ(known_one, mask));
                    all_zero = builder.CreateAnd(all_zero,
                        builder.CreateICmpEQ(known_zero, mask));
                    auto* folded = builder.CreateXor(aval,
                        builder.CreateLShr(aval, constant_i64(builder, 32U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 16U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 8U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 4U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 2U)));
                    folded = builder.CreateXor(folded,
                        builder.CreateLShr(folded,
                            constant_i64(builder, 1U)));
                    parity = builder.CreateXor(parity,
                        builder.CreateICmpNE(builder.CreateAnd(folded,
                            constant_i64(builder, 1U)),
                            constant_i64(builder, 0U)));
                }
                if (operation.kind == BodyOperationKind::reduction_and) {
                    reduced_b = builder.CreateAnd(builder.CreateNot(any_zero),
                        builder.CreateNot(all_one));
                    reduced_a = builder.CreateOr(reduced_b,
                        builder.CreateAnd(builder.CreateNot(any_zero), all_one));
                } else if (operation.kind == BodyOperationKind::reduction_or) {
                    reduced_b = builder.CreateAnd(builder.CreateNot(any_one),
                        builder.CreateNot(all_zero));
                    reduced_a = builder.CreateOr(any_one, reduced_b);
                } else {
                    reduced_b = any_unknown;
                    reduced_a = builder.CreateOr(parity, reduced_b);
                }
            }
            WordValue value;
            value.width = 1U;
            value.aval.push_back(builder.CreateZExt(reduced_a, i64(builder)));
            value.bval.push_back(known_logic4
                ? constant_i64(builder, 0U)
                : builder.CreateZExt(reduced_b, i64(builder)));
            values.insert_or_assign(operation.destination, std::move(value));
            break;
        }
        case BodyOperationKind::shift: {
            const auto& source = lookup(operation.first_source);
            const auto& amount = lookup(operation.second_source);
            auto value = emit_shift_value(builder, source, amount,
                operation.shift_operator, known_logic4);
            values.insert_or_assign(operation.destination, std::move(value));
            break;
        }
        case BodyOperationKind::debug_noop:
            break;
        }
    }
}

void emit_member_body(Builder& builder,
    llvm::Value* frame, const std::size_t member_index,
    const MemberBody& member,
    const std::vector<RegionFrontierSignalLayoutV2>& signals,
    const std::map<RegisterId, std::uint32_t>& input_slots,
    const std::vector<RegionFrontierWriteSiteV2>& sites,
    llvm::Value* bound_process_id, const bool generic_execution,
    const MemberTemplateRuntimeBinding* binding = nullptr)
{
    std::map<RegisterId, WordValue> fallback_values;
    std::map<RegisterId, WordValue> known_values;
    llvm::Value* all_inputs_known
        = llvm::ConstantInt::getTrue(builder.getContext());

    for (const auto reg : member.used_input_registers) {
        const auto slot = input_slots.find(reg);
        if (slot == input_slots.end() || slot->second >= signals.size()) {
            throw std::logic_error(
                "region frontier knownness input is not bound");
        }
        auto value = load_input_value(builder, frame,
            member_template_input_slot(builder, binding, reg, slot->second),
            signals[slot->second]);
        auto known_value = value;
        for (std::size_t word = 0U; word < value.bval.size(); ++word) {
            all_inputs_known = builder.CreateAnd(all_inputs_known,
                builder.CreateICmpEQ(value.bval[word],
                    constant_i64(builder, 0U)));
            known_value.bval[word] = constant_i64(builder, 0U);
        }
        fallback_values.emplace(reg, std::move(value));
        known_values.emplace(reg, std::move(known_value));
    }

    if (member.has_unknown_constant || member.has_logic9_value) {
        emit_member_operations(builder, frame, member, signals, input_slots,
            sites, false, std::move(fallback_values), bound_process_id,
            generic_execution, binding);
        return;
    }
    if (member.used_input_registers.empty()) {
        emit_member_operations(builder, frame, member, signals, input_slots,
            sites, true, std::move(known_values), bound_process_id,
            generic_execution, binding);
        return;
    }

    auto* const insertion = builder.GetInsertBlock();
    if (insertion == nullptr || insertion->getParent() == nullptr) {
        throw std::logic_error("region frontier member has no entry block");
    }
    auto* const function = insertion->getParent();
    auto* const known_block = llvm::BasicBlock::Create(builder.getContext(),
        "member.known.logic4." + std::to_string(member_index), function);
    auto* const fallback_block = llvm::BasicBlock::Create(builder.getContext(),
        "member.four_state." + std::to_string(member_index), function);
    auto* const done_block = llvm::BasicBlock::Create(builder.getContext(),
        "member.body.done." + std::to_string(member_index), function);
    all_inputs_known->setName("member.input.known");
    builder.CreateCondBr(all_inputs_known, known_block, fallback_block);

    builder.SetInsertPoint(known_block);
    emit_member_operations(builder, frame, member, signals, input_slots,
        sites, true, std::move(known_values), bound_process_id,
        generic_execution, binding);
    builder.CreateBr(done_block);

    builder.SetInsertPoint(fallback_block);
    emit_member_operations(builder, frame, member, signals, input_slots,
        sites, false, std::move(fallback_values), bound_process_id,
        generic_execution, binding);
    builder.CreateBr(done_block);
    builder.SetInsertPoint(done_block);
}

} // namespace

struct RegionFrontierKernelPlan::Impl {
    RegionFrontierLayoutV2 layout;
    std::vector<RegionFrontierMemberLayoutV2> members;
    std::vector<RegionFrontierSignalLayoutV2> signals;
    std::vector<RegionFrontierWriteSiteV2> write_sites;
    std::vector<std::uint32_t> max_member_write_counts;
    std::vector<std::uint32_t> max_member_staged_event_counts;
    std::vector<RegionFrontierFanoutEdgeV2> fanout_edges;
    std::vector<RegionFrontierFanoutRangeSpan> fanout_range_spans;
    std::vector<RegionFrontierFanoutSensitivityRange>
        fanout_sensitivity_ranges;
    struct MemberTemplateDescription {
        std::uint32_t shape_index { };
        std::vector<RegisterId> ordered_inputs;
        std::vector<std::uint32_t> ordered_sites;
    };
    std::vector<MemberBody> bodies;
    std::vector<MemberTemplateDescription> member_templates;
    std::vector<std::string> member_template_shapes;
    std::vector<std::uint32_t> member_template_shape_counts;
    std::map<RegisterId, std::uint32_t> input_slots;
    std::string identity;
    std::string structural_census_identity;
    bool structural_census_identity_available { };
    std::string shared_body_identity;
    bool shared_body_identity_available { };

    void repoint_layout() noexcept
    {
        layout.members = members.data();
        layout.signals = signals.data();
        layout.write_sites = write_sites.data();
        layout.max_member_write_counts = max_member_write_counts.data();
        layout.max_member_staged_event_counts
            = max_member_staged_event_counts.data();
        layout.fanout_edges = fanout_edges.data();
    }

    void release_codegen_storage() noexcept
    {
        std::vector<MemberBody> { }.swap(bodies);
        std::vector<MemberTemplateDescription> { }.swap(member_templates);
        std::vector<std::string> { }.swap(member_template_shapes);
        std::vector<std::uint32_t> { }.swap(member_template_shape_counts);
        std::map<RegisterId, std::uint32_t> { }.swap(input_slots);
    }
};

RegionFrontierKernelPlan::RegionFrontierKernelPlan(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
    impl_->repoint_layout();
}

RegionFrontierKernelPlan::~RegionFrontierKernelPlan() = default;
RegionFrontierKernelPlan::RegionFrontierKernelPlan(
    RegionFrontierKernelPlan&&) noexcept = default;
RegionFrontierKernelPlan& RegionFrontierKernelPlan::operator=(
    RegionFrontierKernelPlan&&) noexcept = default;

const RegionFrontierLayoutV2& RegionFrontierKernelPlan::layout() const noexcept
{
    return impl_->layout;
}

std::span<const RegionFrontierFanoutRangeSpan>
RegionFrontierKernelPlan::fanout_range_spans() const noexcept
{
    return impl_->fanout_range_spans;
}

std::span<const RegionFrontierFanoutSensitivityRange>
RegionFrontierKernelPlan::fanout_sensitivity_ranges() const noexcept
{
    return impl_->fanout_sensitivity_ranges;
}

std::string_view RegionFrontierKernelPlan::cache_identity() const noexcept
{
    return impl_->identity;
}

std::optional<std::string_view>
RegionFrontierKernelPlan::structural_census_identity() const noexcept
{
    if (!impl_->structural_census_identity_available) {
        return std::nullopt;
    }
    return impl_->structural_census_identity;
}

std::optional<std::string_view>
RegionFrontierKernelPlan::shared_body_identity() const noexcept
{
    if (!impl_ || !impl_->shared_body_identity_available) {
        return std::nullopt;
    }
    return impl_->shared_body_identity;
}

void RegionFrontierKernelPlan::report_codegen_storage_profile(
    const std::string_view event) const noexcept
{
    if (impl_) {
        frontier_planner_detail::report_planner_storage(event, impl_.get(),
            impl_->layout.member_count, impl_->bodies);
    }
}

void RegionFrontierKernelPlan::release_codegen_storage() noexcept
{
    if (impl_) {
        impl_->release_codegen_storage();
    }
}

std::optional<RegionFrontierKernelPlan> RegionFrontierKernelPlan::try_create(
    const RegionConeActivationKernel& kernel,
    std::uint32_t* rejection_line,
    RegionFrontierRejectedSensitivity* rejected_sensitivity)
{
    if (rejection_line != nullptr) {
        *rejection_line = 0U;
    }
    if (rejected_sensitivity != nullptr) {
        *rejected_sensitivity = { };
    }
    const auto reject = [rejection_line](const std::uint32_t line)
        -> std::optional<RegionFrontierKernelPlan> {
        if (rejection_line != nullptr) {
            *rejection_line = line;
        }
        return std::nullopt;
    };
    const bool generic_deferred_update
        = kernel.program.scheduling_domain
            == ProcessSchedulingDomain::generic;
    const auto execution_mode = generic_deferred_update
        ? RegionFrontierExecutionModeV2::generic_deferred_update
        : RegionFrontierExecutionModeV2::systemverilog_active;
    if (kernel.members.empty() || kernel.outputs.empty()
        || (kernel.program.scheduling_domain
                != ProcessSchedulingDomain::systemverilog
            && !generic_deferred_update)
        || kernel.program.observed || kernel.program.reactive
        || kernel.program.postponed || kernel.program.register_count == 0U
        || kernel.program.operations.empty()
        || !operation_holds<Halt>(kernel.program.operations.expanded(
            kernel.program.operations.size() - 1U))) {
        return reject(__LINE__);
    }

    auto impl = std::make_unique<Impl>();
    const auto member_count = kernel.members.size();
    if (member_count > std::numeric_limits<std::uint32_t>::max()
        || kernel.inputs.size() > std::numeric_limits<std::uint32_t>::max()
        || kernel.outputs.size() > std::numeric_limits<std::uint32_t>::max()) {
        return reject(__LINE__);
    }

    std::set<SignalId> internal_signals(kernel.internal_signals.begin(),
        kernel.internal_signals.end());
    if (internal_signals.size() != kernel.internal_signals.size()) {
        return reject(__LINE__);
    }
    const auto full_output_signal_width = [](
        const RegionConeOutputBinding& output) noexcept {
        if (output.signal_width != 0U) {
            return output.signal_width;
        }
        // Older hand-built whole-output kernels did not store the target
        // width separately. Keep that representation only for an offset-zero
        // whole write; a nonzero slice must carry an authoritative width.
        return output.offset == 0U ? output.width : 0U;
    };

    std::map<SignalId, RegionFrontierSignalLayoutV2> signal_by_id;
    std::map<ProcessId, std::uint32_t> member_by_id;
    std::vector<RegisterShape> register_shapes(kernel.program.register_count);
    std::vector<std::uint8_t> instruction_covered(
        kernel.program.operations.size());
    std::set<RegisterId> input_registers;
    for (const auto& input : kernel.inputs) {
        if (input.width == 0U
            || (input.value_kind != ValueKind::logic4
                && input.value_kind != ValueKind::logic9)
            || input.value_register >= register_shapes.size()
            || !input_registers.insert(input.value_register).second
            || !add_register_shape(register_shapes, input.value_register,
                input.width, input.value_kind)) {
            return reject(__LINE__);
        }
        const bool is_internal = internal_signals.contains(input.signal);
        if (input.internal != is_internal) {
            return reject(__LINE__);
        }
        RegionFrontierSignalLayoutV2 descriptor;
        descriptor.signal_id = input.signal;
        descriptor.owner_process_id = UINT32_MAX;
        descriptor.value_kind = frontier_value_kind(input.value_kind);
        descriptor.width = input.width;
        descriptor.word_count = word_count_for(input.width);
        descriptor.plane_count
            = region_frontier_required_plane_count_v2(descriptor.value_kind);
        descriptor.flags = !generic_deferred_update && is_internal
            ? RegionFrontierPlaneFlagsV2::certified_internal_single_owner
            : RegionFrontierPlaneFlagsV2::read_only_boundary_port;
        descriptor.metadata_index = UINT32_MAX;
        const auto [it, inserted] = signal_by_id.emplace(input.signal,
            descriptor);
        if (!inserted && (it->second.width != input.width
                || it->second.value_kind != descriptor.value_kind
                || it->second.flags != descriptor.flags)) {
            return reject(__LINE__);
        }
        impl->input_slots.emplace(input.value_register, 0U);
    }

    std::set<RegisterId> readiness_registers;
    for (const auto& member : kernel.members) {
        if (input_registers.contains(member.readiness_register)
            || !readiness_registers.insert(member.readiness_register).second) {
            return reject(__LINE__);
        }
    }
    std::set<RegisterId> member_registers;

    for (std::size_t member_index = 0U;
         member_index < member_count; ++member_index) {
        const auto& member = kernel.members[member_index];
        if (!member_by_id.emplace(member.process,
                static_cast<std::uint32_t>(member_index)).second
            || !member.all_registers_definitely_defined
            || member.begin >= member.end
            || member.end > kernel.program.operations.size()
            || member.branch_instruction >= kernel.program.operations.size()
            || !add_register_shape(register_shapes,
                member.readiness_register, 1U, ValueKind::logic4)) {
            return reject(__LINE__);
        }
        const auto branch_operation
            = kernel.program.operations.expanded(member.branch_instruction);
        const auto* branch = operation_get_if<Branch>(&branch_operation);
        if (branch == nullptr || branch->condition != member.readiness_register
            || branch->when_true != member.begin
            || branch->when_false != member.end
            || branch->unknown_policy != UnknownBranchPolicy::error) {
            return reject(__LINE__);
        }
        if (instruction_covered[member.branch_instruction] != 0U) {
            return reject(__LINE__);
        }
        instruction_covered[member.branch_instruction] = 1U;
        for (std::uint32_t instruction = member.begin;
             instruction < member.end; ++instruction) {
            if (instruction_covered[instruction] != 0U) {
                return reject(__LINE__);
            }
            instruction_covered[instruction] = 1U;
        }
        for (std::size_t sensitivity_index = 0U;
             sensitivity_index < member.sensitivities.size();
             ++sensitivity_index) {
            const auto& sensitivity
                = member.sensitivities[sensitivity_index];
            const auto matching_input = std::ranges::find_if(
                kernel.inputs,
                [&](const RegionConeKernelInput& input) {
                    return input.signal == sensitivity.signal;
                });
            const bool whole_signal_sensitivity
                = sensitivity.offset == 0U && sensitivity.width == 0U;
            const bool bounded_internal_any_range
                = matching_input != kernel.inputs.end()
                    && matching_input->internal
                    && sensitivity.width != 0U
                    && sensitivity.offset <= matching_input->width
                    && sensitivity.width
                        <= matching_input->width - sensitivity.offset
                    && (!generic_deferred_update
                        || (sensitivity.offset == 0U
                            && sensitivity.width == matching_input->width));
            const bool exact_full_boundary_output_range
                = !generic_deferred_update
                    && sensitivity.offset == 0U
                    && sensitivity.width != 0U
                    && matching_input != kernel.inputs.end()
                    && !matching_input->internal
                    && sensitivity.width == matching_input->width
                    && std::ranges::any_of(kernel.outputs,
                        [&](const RegionConeOutputBinding& output) {
                            return output.signal == sensitivity.signal
                                && full_output_signal_width(output)
                                    == matching_input->width
                                && output.offset <= matching_input->width
                                && output.width
                                    <= matching_input->width - output.offset
                                && output.publication_kind
                                    == RegionOutputPublicationKind::update;
                        });
            const bool bounded_external_range
                = sensitivity.width != 0U
                    && matching_input != kernel.inputs.end()
                    && !matching_input->internal
                    && sensitivity.offset <= matching_input->width
                    && sensitivity.width
                        <= matching_input->width - sensitivity.offset
                    && std::ranges::none_of(kernel.outputs,
                        [&](const RegionConeOutputBinding& output) {
                            return output.signal == sensitivity.signal;
                        });
            if (sensitivity.edge != EdgeKind::any
                || matching_input == kernel.inputs.end()
                || (!whole_signal_sensitivity
                    && !bounded_internal_any_range
                    && !exact_full_boundary_output_range
                    && !bounded_external_range)) {
                if (rejected_sensitivity != nullptr) {
                    rejected_sensitivity->present = true;
                    rejected_sensitivity->member_process
                        = static_cast<std::uint32_t>(member.process);
                    rejected_sensitivity->member_index = member_index;
                    rejected_sensitivity->sensitivity_index
                        = sensitivity_index;
                    rejected_sensitivity->signal
                        = static_cast<std::uint32_t>(sensitivity.signal);
                    rejected_sensitivity->edge
                        = static_cast<std::uint32_t>(sensitivity.edge);
                    rejected_sensitivity->offset = sensitivity.offset;
                    rejected_sensitivity->width = sensitivity.width;
                    if (matching_input != kernel.inputs.end()) {
                        rejected_sensitivity->matching_input_present = true;
                        rejected_sensitivity->matching_input_width
                            = matching_input->width;
                        rejected_sensitivity->matching_input_kind
                            = static_cast<std::uint32_t>(
                                matching_input->value_kind);
                        rejected_sensitivity->matching_input_internal
                            = matching_input->internal;
                    }
                }
                return reject(__LINE__);
            }
        }
        for (const auto& binding : member.register_bindings) {
            if (!binding.defined
                || input_registers.contains(binding.activation_register)
                || readiness_registers.contains(binding.activation_register)
                || !member_registers.insert(binding.activation_register).second
                || !add_register_shape(register_shapes,
                    binding.activation_register, binding.width,
                    binding.value_kind)) {
                return reject(__LINE__);
            }
        }
    }
    const auto halt_index = kernel.program.operations.size() - 1U;
    if (instruction_covered[halt_index] != 0U) {
        return reject(__LINE__);
    }
    instruction_covered[halt_index] = 1U;
    if (std::ranges::any_of(instruction_covered,
            [](const std::uint8_t covered) { return covered == 0U; })) {
        return reject(__LINE__);
    }

    // Canonical signal order is independent of graph or process traversal.
    std::set<RegisterId> output_value_registers;
    for (const auto& output : kernel.outputs) {
        const auto member = member_by_id.find(output.owner);
        const auto signal_width = full_output_signal_width(output);
        const bool internal_output = internal_signals.contains(output.signal);
        const bool output_range_valid = signal_width != 0U
            && output.offset <= signal_width
            && output.width <= signal_width - output.offset;
        const bool output_target_shape_valid = generic_deferred_update
            ? output.offset == 0U && output.width <= signal_width
                && (!internal_output || output.width == signal_width)
            : output_range_valid
                && (!internal_output
                    || (output.offset == 0U
                        && output.width == signal_width));
        const auto signal_layout_width = generic_deferred_update
            ? output.width : signal_width;
        const bool update_contract_matches = generic_deferred_update
            ? output.domain == SignalUpdateDomain::generic
                && output.update_kind == RegionUpdateKind::generic
            : output.domain == SignalUpdateDomain::systemverilog_active
                && output.update_kind == RegionUpdateKind::systemverilog_active;
        if (member == member_by_id.end() || output.width == 0U
            || signal_width == 0U || !output_target_shape_valid
            || (output.value_kind != ValueKind::logic4
                && output.value_kind != ValueKind::logic9)
            || !update_contract_matches
            || output.projected_delay != 0U
            || output.projected_rejection != 0U
            || output.projected_mode != ProjectedDelayMode::inertial
            || output.value_register >= register_shapes.size()
            || input_registers.contains(output.value_register)
            || readiness_registers.contains(output.value_register)
            || member_registers.contains(output.value_register)
            || !output_value_registers.insert(output.value_register).second
            || !add_register_shape(register_shapes, output.value_register,
                output.width, output.value_kind)) {
            return reject(__LINE__);
        }
        const bool is_internal = internal_signals.contains(output.signal);
        auto [signal, inserted] = signal_by_id.try_emplace(output.signal);
        auto& descriptor = signal->second;
        if (inserted) {
            descriptor.signal_id = output.signal;
            descriptor.owner_process_id = UINT32_MAX;
            descriptor.value_kind = frontier_value_kind(output.value_kind);
            descriptor.width = signal_layout_width;
            descriptor.word_count = word_count_for(signal_layout_width);
            descriptor.plane_count = region_frontier_required_plane_count_v2(
                descriptor.value_kind);
            descriptor.flags = !generic_deferred_update && is_internal
                ? RegionFrontierPlaneFlagsV2::certified_internal_single_owner
                : RegionFrontierPlaneFlagsV2::read_only_boundary_port;
            descriptor.metadata_index = UINT32_MAX;
        } else if (descriptor.width != signal_layout_width
            || descriptor.value_kind != frontier_value_kind(output.value_kind)
            || (((descriptor.flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                    != 0U)
                != (!generic_deferred_update && is_internal))) {
            return reject(__LINE__);
        }
        if (is_internal) {
            if (descriptor.owner_process_id != UINT32_MAX) {
                return reject(__LINE__);
            }
            descriptor.owner_process_id = output.owner;
        }
    }

    std::map<SignalId, std::uint32_t> signal_slots;
    if (signal_by_id.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        return reject(__LINE__);
    }
    std::uint32_t next_metadata_index { };
    for (auto& [signal_id, descriptor] : signal_by_id) {
        if (descriptor.flags
            == RegionFrontierPlaneFlagsV2::certified_internal_single_owner) {
            if (descriptor.owner_process_id == UINT32_MAX) {
                return reject(__LINE__);
            }
            descriptor.metadata_index = next_metadata_index++;
        } else if (descriptor.flags
            != RegionFrontierPlaneFlagsV2::read_only_boundary_port) {
            return reject(__LINE__);
        }
        signal_slots.emplace(signal_id,
            static_cast<std::uint32_t>(impl->signals.size()));
        impl->signals.push_back(descriptor);
    }
    for (auto& [reg, slot] : impl->input_slots) {
        const auto input = std::ranges::find(kernel.inputs, reg,
            &RegionConeKernelInput::value_register);
        if (input == kernel.inputs.end()) {
            return reject(__LINE__);
        }
        const auto slot_iterator = signal_slots.find(input->signal);
        if (slot_iterator == signal_slots.end()) {
            return reject(__LINE__);
        }
        slot = slot_iterator->second;
    }

    // A generated frontier handles only acyclic event cones. The edges here
    // use certified whole-signal static sensitivities, which are the source
    // of future member activations after internal commits.
    std::vector<std::vector<std::uint32_t>> successors(member_count);
    std::vector<std::uint32_t> indegree(member_count);
    std::set<std::pair<std::uint32_t, std::uint32_t>> dependency_edges;
    for (std::size_t consumer_index = 0U;
         consumer_index < member_count; ++consumer_index) {
        for (const auto& sensitivity
            : kernel.members[consumer_index].sensitivities) {
            if (!internal_signals.contains(sensitivity.signal)) {
                continue;
            }
            const auto producer_signal = signal_by_id.find(sensitivity.signal);
            if (producer_signal == signal_by_id.end()) {
                return reject(__LINE__);
            }
            const auto producer = member_by_id.find(
                producer_signal->second.owner_process_id);
            if (producer == member_by_id.end()
                || producer->second == consumer_index) {
                return reject(__LINE__);
            }
            dependency_edges.emplace(producer->second,
                static_cast<std::uint32_t>(consumer_index));
        }
    }
    for (const auto& [producer, consumer] : dependency_edges) {
        successors[producer].push_back(consumer);
        ++indegree[consumer];
    }
    std::vector<std::uint32_t> topological_ready;
    topological_ready.reserve(member_count);
    for (std::size_t member_index = 0U;
         member_index < member_count; ++member_index) {
        if (indegree[member_index] == 0U) {
            topological_ready.push_back(
                static_cast<std::uint32_t>(member_index));
        }
    }
    for (std::size_t cursor = 0U; cursor < topological_ready.size(); ++cursor) {
        for (const auto successor : successors[topological_ready[cursor]]) {
            if (--indegree[successor] == 0U) {
                topological_ready.push_back(successor);
            }
        }
    }
    if (topological_ready.size() != member_count) {
        return reject(__LINE__);
    }

    std::set<std::pair<SignalId, ProcessId>> fanout_pairs;
    for (const auto& member : kernel.members) {
        for (const auto& sensitivity : member.sensitivities) {
            if (internal_signals.contains(sensitivity.signal)) {
                fanout_pairs.emplace(sensitivity.signal, member.process);
            }
        }
    }
    for (const auto& [signal, process] : fanout_pairs) {
        const auto member = member_by_id.find(process);
        const auto slot = signal_slots.find(signal);
        if (member == member_by_id.end() || slot == signal_slots.end()) {
            return reject(__LINE__);
        }
        impl->fanout_edges.push_back({ slot->second, member->second, 0U });

        const auto input = signal_by_id.find(signal);
        if (input == signal_by_id.end()
            || !internal_signals.contains(signal)) {
            return reject(__LINE__);
        }
        const auto first_range = impl->fanout_sensitivity_ranges.size();
        std::size_t range_count { };
        const auto maximum_range_count
            = static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max());
        for (const auto& sensitivity
            : kernel.members[member->second].sensitivities) {
            if (sensitivity.signal != signal) {
                continue;
            }
            if (sensitivity.edge != EdgeKind::any) {
                return reject(__LINE__);
            }
            const auto range_width = sensitivity.width == 0U
                ? input->second.width : sensitivity.width;
            if (range_width == 0U
                || sensitivity.offset > input->second.width
                || range_width > input->second.width - sensitivity.offset
                || first_range > maximum_range_count
                || range_count >= maximum_range_count - first_range) {
                return reject(__LINE__);
            }
            impl->fanout_sensitivity_ranges.push_back({
                sensitivity.offset, range_width });
            ++range_count;
        }
        if (range_count == 0U || first_range > maximum_range_count
            || range_count > maximum_range_count - first_range) {
            return reject(__LINE__);
        }
        impl->fanout_range_spans.push_back({
            static_cast<std::uint32_t>(first_range),
            static_cast<std::uint32_t>(range_count) });
    }

    std::vector<std::uint32_t> output_member_indices;
    output_member_indices.reserve(kernel.outputs.size());
    for (const auto& output : kernel.outputs) {
        output_member_indices.push_back(member_by_id.at(output.owner));
    }
    std::vector<std::size_t> output_order(kernel.outputs.size());
    for (std::size_t index = 0U; index < output_order.size(); ++index) {
        output_order[index] = index;
    }
    std::ranges::sort(output_order, [&](const std::size_t left,
                                      const std::size_t right) {
        const auto& lhs = kernel.outputs[left];
        const auto& rhs = kernel.outputs[right];
        if (output_member_indices[left] != output_member_indices[right]) {
            return output_member_indices[left] < output_member_indices[right];
        }
        if (lhs.source_instruction != rhs.source_instruction) {
            return lhs.source_instruction < rhs.source_instruction;
        }
        return lhs.signal < rhs.signal;
    });

    std::map<std::pair<std::uint32_t, InstructionIndex>, std::uint32_t>
        output_site_by_instruction;
    std::set<SignalId> internal_sites_seen;
    for (const auto output_index : output_order) {
        const auto& output = kernel.outputs[output_index];
        const auto member_index = output_member_indices[output_index];
        const auto slot = signal_slots.at(output.signal);
        const auto is_internal = internal_signals.contains(output.signal);
        if (output.publication_kind
            != RegionOutputPublicationKind::update) {
            return reject(__LINE__);
        }
        const auto site_index = static_cast<std::uint32_t>(
            impl->write_sites.size());
        const auto event_kind = generic_deferred_update
            ? RegionFrontierEventKindV2::generic_deferred_update
            : is_internal ? RegionFrontierEventKindV2::internal_commit
                          : RegionFrontierEventKindV2::boundary_commit;
        impl->write_sites.push_back({ member_index, slot,
            output.source_instruction,
            static_cast<std::uint32_t>(output.update_kind),
            static_cast<std::uint32_t>(event_kind),
            frontier_value_kind(output.value_kind), output.width,
            word_count_for(output.width),
            region_frontier_required_plane_count_v2(
                frontier_value_kind(output.value_kind)), site_index });
        if (!output_site_by_instruction.emplace(
                std::pair { member_index, output.kernel_instruction },
                site_index).second) {
            return reject(__LINE__);
        }
        if (is_internal && !internal_sites_seen.insert(output.signal).second) {
            return reject(__LINE__);
        }
    }
    if (internal_sites_seen != internal_signals) {
        return reject(__LINE__);
    }

    impl->bodies.resize(member_count);
    impl->members.resize(member_count);
    impl->max_member_write_counts.assign(member_count, 0U);
    impl->max_member_staged_event_counts.assign(member_count, 0U);
    std::vector<std::uint8_t> lowered_write_sites(impl->write_sites.size());
    std::size_t site_cursor { };
    for (std::size_t member_index = 0U;
         member_index < member_count; ++member_index) {
        auto& member_layout = impl->members[member_index];
        member_layout.first_write_site = static_cast<std::uint32_t>(site_cursor);
        while (site_cursor < impl->write_sites.size()
            && impl->write_sites[site_cursor].member_index == member_index) {
            ++impl->max_member_write_counts[member_index];
            ++impl->max_member_staged_event_counts[member_index];
            ++site_cursor;
        }
        member_layout.write_site_count
            = impl->max_member_write_counts[member_index];
        member_layout.max_pending_writes
            = impl->max_member_write_counts[member_index];
        member_layout.max_staged_events
            = impl->max_member_staged_event_counts[member_index];
    }
    if (site_cursor != impl->write_sites.size()) {
        return reject(__LINE__);
    }
    std::vector<std::uint32_t> fanout_count_by_signal(impl->signals.size());
    for (const auto& edge : impl->fanout_edges) {
        ++fanout_count_by_signal[edge.signal_slot];
    }
    const auto maximum_fanout = generic_deferred_update
        || fanout_count_by_signal.empty()
        ? 0U
        : *std::ranges::max_element(fanout_count_by_signal);

    for (std::size_t member_index = 0U;
         member_index < member_count; ++member_index) {
        const auto& source_member = kernel.members[member_index];
        auto& body = impl->bodies[member_index];
        body.process_id = source_member.process;
        body.readiness_register = source_member.readiness_register;
        body.begin = source_member.begin;
        body.end = source_member.end;
        auto& member_layout = impl->members[member_index];
        member_layout.process_id = source_member.process;
        std::set<RegisterId> defined_registers(input_registers);
        std::set<RegisterId> locally_defined_registers;
        std::set<RegisterId> used_input_registers;

        for (std::uint32_t instruction = source_member.begin;
             instruction < source_member.end; ++instruction) {
            const auto operation
                = kernel.program.operations.expanded(instruction);
            BodyOperation lowered;
            lowered.instruction = instruction;
            if (const auto* constant
                = operation_get_if<LoadConstant>(&operation)) {
                if (constant->destination >= register_shapes.size()
                    || register_shapes[constant->destination].width == 0U
                    || register_shapes[constant->destination].width
                        != constant->value.width()
                    || register_shapes[constant->destination].value_kind
                        != ValueKind::logic4
                    || constant->value.is_logic9()) {
                    return reject(__LINE__);
                }
                defined_registers.insert(constant->destination);
                lowered.kind = BodyOperationKind::load_constant;
                lowered.destination = constant->destination;
                lowered.width = static_cast<std::uint32_t>(
                    constant->value.width());
                lowered.constant_aval.assign(
                    constant->value.aval_words().begin(),
                    constant->value.aval_words().end());
                lowered.constant_bval.assign(
                    constant->value.bval_words().begin(),
                    constant->value.bval_words().end());
                if (lowered.constant_aval.size()
                        != word_count_for(lowered.width)
                    || lowered.constant_bval.size()
                        != word_count_for(lowered.width)) {
                    return reject(__LINE__);
                }
                body.has_unknown_constant = body.has_unknown_constant
                    || std::ranges::any_of(lowered.constant_bval,
                        [](const std::uint64_t word) { return word != 0U; });
            } else if (const auto* copy
                = operation_get_if<CopyRegister>(&operation)) {
                if (copy->destination >= register_shapes.size()
                    || copy->source >= register_shapes.size()
                    || register_shapes[copy->destination].width == 0U
                    || register_shapes[copy->destination].width
                        != register_shapes[copy->source].width
                    || register_shapes[copy->destination].value_kind
                        != register_shapes[copy->source].value_kind
                    || (!input_registers.contains(copy->source)
                        && !defined_registers.contains(copy->source))) {
                    return reject(__LINE__);
                }
                lowered.kind = BodyOperationKind::copy_register;
                lowered.destination = copy->destination;
                lowered.first_source = copy->source;
                lowered.width = register_shapes[copy->destination].width;
                body.has_logic9_value = body.has_logic9_value
                    || register_shapes[copy->destination].value_kind
                        == ValueKind::logic9;
                const auto site = output_site_by_instruction.find(
                    { static_cast<std::uint32_t>(member_index), instruction });
                if (site != output_site_by_instruction.end()) {
                    lowered.output_site = site->second;
                    if (lowered_write_sites[site->second] != 0U) {
                        return reject(__LINE__);
                    }
                    lowered_write_sites[site->second] = 1U;
                    const auto& descriptor = impl->write_sites[site->second];
                    if (descriptor.width != lowered.width
                        || descriptor.value_kind
                            != frontier_value_kind(register_shapes[
                                copy->destination].value_kind)) {
                        return reject(__LINE__);
                    }
                    const auto output
                        = std::ranges::find_if(kernel.outputs,
                            [&](const RegionConeOutputBinding& binding) {
                                return binding.kernel_instruction == instruction
                                    && binding.owner == source_member.process;
                            });
                    if (output == kernel.outputs.end()
                        || output->value_register != copy->destination) {
                        return reject(__LINE__);
                    }
                }
                defined_registers.insert(copy->destination);
            } else if (const auto* extract
                = operation_get_if<Extract>(&operation)) {
                if (extract->destination >= register_shapes.size()
                    || extract->source >= register_shapes.size()
                    || register_shapes[extract->source].width == 0U
                    || extract->width == 0U
                    || extract->offset
                        >= register_shapes[extract->source].width
                    || extract->width
                        > register_shapes[extract->source].width
                            - extract->offset
                    || register_shapes[extract->destination].width
                        != extract->width
                    || register_shapes[extract->source].value_kind
                        != ValueKind::logic4
                    || register_shapes[extract->destination].value_kind
                        != ValueKind::logic4
                    || !defined_registers.contains(extract->source)) {
                    return reject(__LINE__);
                }
                defined_registers.insert(extract->destination);
                lowered.kind = BodyOperationKind::extract;
                lowered.destination = extract->destination;
                lowered.first_source = extract->source;
                lowered.offset = extract->offset;
                lowered.width = extract->width;
            } else if (const auto* concatenate
                = operation_get_if<Concatenate>(&operation)) {
                if (concatenate->destination >= register_shapes.size()
                    || concatenate->operands.empty()
                    || concatenate->width == 0U
                    || register_shapes[concatenate->destination].width
                        != concatenate->width
                    || register_shapes[concatenate->destination].value_kind
                        != ValueKind::logic4) {
                    return reject(__LINE__);
                }
                std::uint64_t total_width { };
                lowered.operands.reserve(concatenate->operands.size());
                for (const auto operand : concatenate->operands) {
                    if (operand >= register_shapes.size()
                        || register_shapes[operand].width == 0U
                        || register_shapes[operand].value_kind
                            != ValueKind::logic4
                        || !defined_registers.contains(operand)
                        || register_shapes[operand].width
                            > std::numeric_limits<std::uint32_t>::max()
                                - total_width) {
                        return reject(__LINE__);
                    }
                    total_width += register_shapes[operand].width;
                    lowered.operands.push_back(operand);
                }
                if (total_width != concatenate->width) {
                    return reject(__LINE__);
                }
                defined_registers.insert(concatenate->destination);
                lowered.kind = BodyOperationKind::concatenate;
                lowered.destination = concatenate->destination;
                lowered.width = concatenate->width;
            } else if (const auto* shift
                = operation_get_if<Shift>(&operation)) {
                if (!supported_shift_operator(shift->operation)
                    || shift->signed_amount
                    || shift->destination >= register_shapes.size()
                    || shift->value >= register_shapes.size()
                    || shift->amount >= register_shapes.size()
                    || register_shapes[shift->destination].width == 0U
                    || register_shapes[shift->destination].width
                        != register_shapes[shift->value].width
                    || register_shapes[shift->amount].width == 0U
                    || register_shapes[shift->destination].value_kind
                        != ValueKind::logic4
                    || register_shapes[shift->value].value_kind
                        != ValueKind::logic4
                    || register_shapes[shift->amount].value_kind
                        != ValueKind::logic4
                    || !defined_registers.contains(shift->value)
                    || !defined_registers.contains(shift->amount)) {
                    return reject(__LINE__);
                }
                defined_registers.insert(shift->destination);
                lowered.kind = BodyOperationKind::shift;
                lowered.destination = shift->destination;
                lowered.first_source = shift->value;
                lowered.second_source = shift->amount;
                lowered.width = register_shapes[shift->destination].width;
                lowered.shift_operator = shift->operation;
            } else if (const auto* select
                = operation_get_if<ConditionalSelect>(&operation)) {
                if (select->destination >= register_shapes.size()
                    || select->condition >= register_shapes.size()
                    || select->when_true >= register_shapes.size()
                    || select->when_false >= register_shapes.size()
                    || register_shapes[select->condition].width != 1U
                    || register_shapes[select->destination].width == 0U
                    || register_shapes[select->when_true].width
                        != register_shapes[select->destination].width
                    || register_shapes[select->when_false].width
                        != register_shapes[select->destination].width
                    || register_shapes[select->condition].value_kind
                        != ValueKind::logic4
                    || register_shapes[select->when_true].value_kind
                        != ValueKind::logic4
                    || register_shapes[select->when_false].value_kind
                        != ValueKind::logic4
                    || register_shapes[select->destination].value_kind
                        != ValueKind::logic4
                    || !defined_registers.contains(select->condition)
                    || !defined_registers.contains(select->when_true)
                    || !defined_registers.contains(select->when_false)) {
                    return reject(__LINE__);
                }
                defined_registers.insert(select->destination);
                lowered.kind = BodyOperationKind::conditional_select;
                lowered.destination = select->destination;
                lowered.first_source = select->condition;
                lowered.second_source = select->when_true;
                lowered.third_source = select->when_false;
                lowered.width = register_shapes[select->destination].width;
            } else if (const auto* unary
                = operation_get_if<UnaryNot>(&operation)) {
                if (unary->destination >= register_shapes.size()
                    || unary->source >= register_shapes.size()
                    || register_shapes[unary->destination].width == 0U
                    || register_shapes[unary->destination].width
                        != register_shapes[unary->source].width
                    || register_shapes[unary->destination].value_kind
                        != register_shapes[unary->source].value_kind
                    || !defined_registers.contains(unary->source)) {
                    return reject(__LINE__);
                }
                defined_registers.insert(unary->destination);
                lowered.kind = BodyOperationKind::unary_not;
                lowered.destination = unary->destination;
                lowered.first_source = unary->source;
                lowered.width = register_shapes[unary->destination].width;
                body.has_logic9_value = body.has_logic9_value
                    || register_shapes[unary->destination].value_kind
                        == ValueKind::logic9;
            } else if (const auto* binary
                = operation_get_if<Binary>(&operation)) {
                if (binary->operation != BinaryOperator::bit_and
                    && binary->operation != BinaryOperator::bit_or
                    && binary->operation != BinaryOperator::bit_xor) {
                    return reject(__LINE__);
                }
                if (binary->destination >= register_shapes.size()
                    || binary->lhs >= register_shapes.size()
                    || binary->rhs >= register_shapes.size()
                    || register_shapes[binary->destination].width == 0U
                    || register_shapes[binary->destination].width
                        != register_shapes[binary->lhs].width
                    || register_shapes[binary->destination].width
                        != register_shapes[binary->rhs].width
                    || register_shapes[binary->destination].value_kind
                        != register_shapes[binary->lhs].value_kind
                    || register_shapes[binary->destination].value_kind
                        != register_shapes[binary->rhs].value_kind
                    || !defined_registers.contains(binary->lhs)
                    || !defined_registers.contains(binary->rhs)) {
                    return reject(__LINE__);
                }
                defined_registers.insert(binary->destination);
                lowered.kind = binary->operation == BinaryOperator::bit_and
                    ? BodyOperationKind::bit_and
                    : binary->operation == BinaryOperator::bit_or
                    ? BodyOperationKind::bit_or
                    : BodyOperationKind::bit_xor;
                lowered.destination = binary->destination;
                lowered.first_source = binary->lhs;
                lowered.second_source = binary->rhs;
                lowered.width = register_shapes[binary->destination].width;
                body.has_logic9_value = body.has_logic9_value
                    || register_shapes[binary->destination].value_kind
                        == ValueKind::logic9;
            } else if (const auto* reduction
                = operation_get_if<Reduction>(&operation)) {
                if (reduction->operation != ReductionOperator::bit_and
                    && reduction->operation != ReductionOperator::bit_or
                    && reduction->operation != ReductionOperator::bit_xor) {
                    return reject(__LINE__);
                }
                if (reduction->destination >= register_shapes.size()
                    || reduction->source >= register_shapes.size()
                    || register_shapes[reduction->destination].width != 1U
                    || register_shapes[reduction->source].width == 0U
                    || register_shapes[reduction->destination].value_kind
                        != ValueKind::logic4
                    || register_shapes[reduction->source].value_kind
                        != ValueKind::logic4
                    || !defined_registers.contains(reduction->source)) {
                    return reject(__LINE__);
                }
                defined_registers.insert(reduction->destination);
                lowered.kind = reduction->operation
                        == ReductionOperator::bit_and
                    ? BodyOperationKind::reduction_and
                    : reduction->operation == ReductionOperator::bit_or
                    ? BodyOperationKind::reduction_or
                    : BodyOperationKind::reduction_xor;
                lowered.destination = reduction->destination;
                lowered.first_source = reduction->source;
                lowered.width = 1U;
            } else if (operation_holds<DebugPoint>(operation)) {
                lowered.kind = BodyOperationKind::debug_noop;
            } else {
                return reject(__LINE__);
            }

            const auto include_external_input = [&](const std::uint32_t reg) {
                if (impl->input_slots.contains(reg)
                    && !locally_defined_registers.contains(reg)) {
                    used_input_registers.insert(reg);
                }
            };
            switch (lowered.kind) {
            case BodyOperationKind::load_constant:
            case BodyOperationKind::debug_noop:
                break;
            case BodyOperationKind::copy_register:
            case BodyOperationKind::extract:
            case BodyOperationKind::unary_not:
            case BodyOperationKind::reduction_and:
            case BodyOperationKind::reduction_or:
            case BodyOperationKind::reduction_xor:
                include_external_input(lowered.first_source);
                break;
            case BodyOperationKind::concatenate:
                for (const auto operand : lowered.operands) {
                    include_external_input(operand);
                }
                break;
            case BodyOperationKind::conditional_select:
                include_external_input(lowered.first_source);
                include_external_input(lowered.second_source);
                include_external_input(lowered.third_source);
                break;
            case BodyOperationKind::bit_and:
            case BodyOperationKind::bit_or:
            case BodyOperationKind::bit_xor:
                include_external_input(lowered.first_source);
                include_external_input(lowered.second_source);
                break;
            case BodyOperationKind::shift:
                include_external_input(lowered.first_source);
                include_external_input(lowered.second_source);
                break;
            }
            if (lowered.kind != BodyOperationKind::debug_noop) {
                locally_defined_registers.insert(lowered.destination);
            }
            body.operations.push_back(std::move(lowered));
        }
        body.used_input_registers.assign(used_input_registers.begin(),
            used_input_registers.end());
    }
    if (std::ranges::any_of(lowered_write_sites,
            [](const std::uint8_t was_lowered) {
                return was_lowered == 0U;
            })) {
        return reject(__LINE__);
    }

    const auto pending_capacity = static_cast<std::uint32_t>(
        impl->write_sites.size());
    if (pending_capacity == 0U
        || impl->write_sites.size()
            > std::numeric_limits<std::uint32_t>::max()
        || impl->fanout_edges.size()
            > std::numeric_limits<std::uint32_t>::max()
        || impl->write_sites.size() + impl->fanout_edges.size()
            > std::numeric_limits<std::uint32_t>::max()) {
        return reject(__LINE__);
    }
    const auto event_capacity = static_cast<std::uint32_t>(
        impl->write_sites.size()
        + (generic_deferred_update ? 0U : impl->fanout_edges.size()));
    impl->identity
        = "fsim-region-frontier-v2;generic-deferred-update-contract-v1;"
          "typed-logic4-logic9-planes-v2;"
          "known-logic4-guarded-member-body-v1;logic9-word-ops-copy-not-and-or-xor-v1;"
          "logic4-unsigned-shift-v1;";
    if (generic_deferred_update) {
        impl->identity.append("generic-topology-fanout-zero-mask-v1;");
    }
    const bool generic_logic9_value = generic_deferred_update
        && (std::ranges::any_of(kernel.inputs,
                [](const RegionConeKernelInput& input) {
                    return input.value_kind == ValueKind::logic9;
                })
            || std::ranges::any_of(kernel.outputs,
                [](const RegionConeOutputBinding& output) {
                    return output.value_kind == ValueKind::logic9;
                }));
    if (generic_logic9_value) {
        impl->identity += "generic-logic9-whole-write-v1;";
    }
    const auto append_abi_type = [&](const std::size_t size,
                                     const std::size_t alignment) {
        append_number(impl->identity, size);
        append_number(impl->identity, alignment);
    };
    const auto append_abi_offset = [&](const std::size_t offset) {
        append_number(impl->identity, offset);
    };
    append_number(impl->identity, kRegionFrontierAbiVersionV2);
    append_number(impl->identity, kRegionFrontierValuePlaneContractV2);
    append_number(impl->identity, static_cast<std::uint32_t>(execution_mode));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierValueKindV2::logic4));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierValueKindV2::logic9));
    append_number(impl->identity, kRegionFrontierLogic4PlaneCountV2);
    append_number(impl->identity, kRegionFrontierLogic9PlaneCountV2);
    append_number(impl->identity, kRegionFrontierPayloadKindShiftV2);
    append_number(impl->identity, kRegionFrontierPayloadIndexMaskV2);
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierStatusV2::quiescent));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierStatusV2::need_scheduler_keys));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierStatusV2::cut_before_key));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierStatusV2::boundary_publication));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierStatusV2::stopped));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierStatusV2::decline_before_mutation));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierStatusV2::stale_generation));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierStatusV2::yield_before_task));
    append_number(impl->identity,
        static_cast<std::uint32_t>(
            RegionFrontierStatusV2::generic_update_batch_ready));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierEventKindV2::member_activation));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierEventKindV2::internal_commit));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierEventKindV2::boundary_commit));
    append_number(impl->identity,
        static_cast<std::uint32_t>(
            RegionFrontierEventKindV2::generic_deferred_update));
    append_number(impl->identity,
        RegionFrontierPendingWriteFlagsV2::pending_generic_target);
    append_number(impl->identity,
        offsetof(RegionFrontierLayoutV2, execution_mode));
    append_number(impl->identity,
        offsetof(RegionFrontierFrameV2, generic_update_ack_count));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierCutKindV2::unknown));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierCutKindV2::same_slot_key));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierCutKindV2::closed_prefix));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierMemberFlagsV2::waiting_on_static));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierMemberFlagsV2::queued));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierMemberFlagsV2::executing));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierMemberFlagsV2::queued_key_valid));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierMemberFlagsV2::pending_activation));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierPlaneFlagsV2::certified_internal_single_owner));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierPlaneFlagsV2::read_only_boundary_port));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV2::pending_active));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV2::pending_value_ready));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV2::pending_key_assigned));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV2::pending_internal_target));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV2::pending_boundary_target));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV2::pending_committed));
    append_number(impl->identity,
        static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV2::pending_generic_target));
    append_number(impl->identity, sizeof(void*));

    append_abi_type(sizeof(RegionFrontierKeyV2), alignof(RegionFrontierKeyV2));
    append_abi_offset(offsetof(RegionFrontierKeyV2, time));
    append_abi_offset(offsetof(RegionFrontierKeyV2, delta));
    append_abi_offset(offsetof(RegionFrontierKeyV2, systemverilog_round));
    append_abi_offset(offsetof(RegionFrontierKeyV2, stable_order));
    append_abi_offset(offsetof(RegionFrontierKeyV2, sequence));
    append_abi_offset(offsetof(RegionFrontierKeyV2, process_domain));
    append_abi_offset(offsetof(RegionFrontierKeyV2, phase));

    append_abi_type(sizeof(RegionFrontierSlotV2), alignof(RegionFrontierSlotV2));
    append_abi_offset(offsetof(RegionFrontierSlotV2, time));
    append_abi_offset(offsetof(RegionFrontierSlotV2, delta));
    append_abi_offset(offsetof(RegionFrontierSlotV2, systemverilog_round));
    append_abi_offset(offsetof(RegionFrontierSlotV2, process_domain));
    append_abi_offset(offsetof(RegionFrontierSlotV2, phase));

    append_abi_type(sizeof(RegionFrontierMemberV2), alignof(RegionFrontierMemberV2));
    append_abi_offset(offsetof(RegionFrontierMemberV2, process_id));
    append_abi_offset(offsetof(RegionFrontierMemberV2, flags));
    append_abi_offset(offsetof(RegionFrontierMemberV2, static_trigger_mask));
    append_abi_offset(offsetof(RegionFrontierMemberV2, queued_key));
    append_abi_offset(offsetof(RegionFrontierMemberV2, activation_origin));
    append_abi_offset(offsetof(RegionFrontierMemberV2, pending_activation_origin));
    append_abi_type(sizeof(RegionFrontierSchedulerTaskV2),
        alignof(RegionFrontierSchedulerTaskV2));
    append_abi_offset(offsetof(RegionFrontierSchedulerTaskV2, payload));
    append_abi_offset(offsetof(RegionFrontierSchedulerTaskV2, stable_order));
    append_abi_offset(offsetof(RegionFrontierSchedulerTaskV2, sequence));

    append_abi_type(sizeof(RegionFrontierPlaneV2), alignof(RegionFrontierPlaneV2));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, signal_id));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, owner_process_id));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, value_kind));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, width));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, word_count));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, plane_count));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, flags));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, metadata_index));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, boundary_planes));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, current_planes));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, previous_planes));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, stored_planes));
    append_abi_offset(offsetof(RegionFrontierPlaneV2, owner_planes));

    append_abi_type(sizeof(RegionFrontierSignalMetadataV2),
        alignof(RegionFrontierSignalMetadataV2));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, event_time));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, event_delta));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, transaction_time));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, transaction_delta));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, value_revision));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, systemverilog_round));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, event_process_domain));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, event_phase));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, event_valid));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, transaction_valid));
    append_abi_offset(offsetof(RegionFrontierSignalMetadataV2, reserved));

    append_abi_type(sizeof(RegionFrontierPendingWriteV2),
        alignof(RegionFrontierPendingWriteV2));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, member_index));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, signal_slot));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, source_instruction));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, update_kind));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, flags));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, reserved));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, commit_key));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, origin));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, value_kind));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, width));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, word_count));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, plane_count));
    append_abi_offset(offsetof(RegionFrontierPendingWriteV2, value_planes));

    append_abi_type(sizeof(RegionFrontierStagedEventV2),
        alignof(RegionFrontierStagedEventV2));
    append_abi_offset(offsetof(RegionFrontierStagedEventV2, kind));
    append_abi_offset(offsetof(RegionFrontierStagedEventV2, descriptor_index));
    append_abi_offset(offsetof(RegionFrontierStagedEventV2, stable_order));
    append_abi_offset(offsetof(RegionFrontierStagedEventV2, origin));
    append_abi_type(sizeof(RegionFrontierCommittedSignalV2),
        alignof(RegionFrontierCommittedSignalV2));
    append_abi_offset(offsetof(RegionFrontierCommittedSignalV2, signal_slot));
    append_abi_offset(offsetof(RegionFrontierCommittedSignalV2, changed));
    append_abi_offset(offsetof(RegionFrontierCommittedSignalV2, state_changed));
    append_abi_type(sizeof(RegionFrontierFanoutEdgeV2),
        alignof(RegionFrontierFanoutEdgeV2));
    append_abi_offset(offsetof(RegionFrontierFanoutEdgeV2, signal_slot));
    append_abi_offset(offsetof(RegionFrontierFanoutEdgeV2, member_index));
    append_abi_offset(offsetof(RegionFrontierFanoutEdgeV2, trigger_mask));

    append_abi_type(sizeof(RegionFrontierMemberLayoutV2),
        alignof(RegionFrontierMemberLayoutV2));
    append_abi_offset(offsetof(RegionFrontierMemberLayoutV2, process_id));
    append_abi_offset(offsetof(RegionFrontierMemberLayoutV2, first_write_site));
    append_abi_offset(offsetof(RegionFrontierMemberLayoutV2, write_site_count));
    append_abi_offset(offsetof(RegionFrontierMemberLayoutV2, max_pending_writes));
    append_abi_offset(offsetof(RegionFrontierMemberLayoutV2, max_staged_events));
    append_abi_offset(offsetof(RegionFrontierMemberLayoutV2, reserved));
    append_abi_type(sizeof(RegionFrontierSignalLayoutV2),
        alignof(RegionFrontierSignalLayoutV2));
    append_abi_offset(offsetof(RegionFrontierSignalLayoutV2, signal_id));
    append_abi_offset(offsetof(RegionFrontierSignalLayoutV2, owner_process_id));
    append_abi_offset(offsetof(RegionFrontierSignalLayoutV2, value_kind));
    append_abi_offset(offsetof(RegionFrontierSignalLayoutV2, width));
    append_abi_offset(offsetof(RegionFrontierSignalLayoutV2, word_count));
    append_abi_offset(offsetof(RegionFrontierSignalLayoutV2, plane_count));
    append_abi_offset(offsetof(RegionFrontierSignalLayoutV2, flags));
    append_abi_offset(offsetof(RegionFrontierSignalLayoutV2, metadata_index));
    append_abi_type(sizeof(RegionFrontierWriteSiteV2),
        alignof(RegionFrontierWriteSiteV2));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, member_index));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, signal_slot));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, source_instruction));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, update_kind));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, event_kind));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, value_kind));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, width));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, word_count));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, plane_count));
    append_abi_offset(offsetof(RegionFrontierWriteSiteV2, pending_slot));

    append_abi_type(sizeof(RegionFrontierLayoutV2),
        alignof(RegionFrontierLayoutV2));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, abi_version));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, struct_size));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, value_plane_contract));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, reserved0));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, certificate_generation));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, component_generation));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, member_count));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, readiness_word_count));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, signal_slot_count));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, metadata_count));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, fanout_edge_count));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, write_site_count));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, pending_write_capacity));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, staged_event_capacity));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, max_commit_fanout_events));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, committed_signal_capacity));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, reserved_capacity));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, execution_mode));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, members));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, signals));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, write_sites));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, max_member_write_counts));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, max_member_staged_event_counts));
    append_abi_offset(offsetof(RegionFrontierLayoutV2, fanout_edges));

    append_abi_type(sizeof(RegionFrontierFrameV2),
        alignof(RegionFrontierFrameV2));
    append_abi_offset(offsetof(RegionFrontierFrameV2, abi_version));
    append_abi_offset(offsetof(RegionFrontierFrameV2, struct_size));
    append_abi_offset(offsetof(RegionFrontierFrameV2, value_plane_contract));
    append_abi_offset(offsetof(RegionFrontierFrameV2, generic_update_ack_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, runtime_generation));
    append_abi_offset(offsetof(RegionFrontierFrameV2, bound_runtime_generation));
    append_abi_offset(offsetof(RegionFrontierFrameV2, certificate_generation));
    append_abi_offset(offsetof(RegionFrontierFrameV2, component_generation));
    append_abi_offset(offsetof(RegionFrontierFrameV2, scheduler_frontier_generation));
    append_abi_offset(offsetof(RegionFrontierFrameV2, member_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, scheduler_task_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, scheduler_task_cursor));
    append_abi_offset(offsetof(RegionFrontierFrameV2, scheduler_task_capacity));
    append_abi_offset(offsetof(RegionFrontierFrameV2, readiness_word_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, signal_slot_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, metadata_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, fanout_edge_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, committed_signal_capacity));
    append_abi_offset(offsetof(RegionFrontierFrameV2, committed_signal_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, pending_write_capacity));
    append_abi_offset(offsetof(RegionFrontierFrameV2, pending_write_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, staged_event_capacity));
    append_abi_offset(offsetof(RegionFrontierFrameV2, staged_event_count));
    append_abi_offset(offsetof(RegionFrontierFrameV2, current_member));
    append_abi_offset(offsetof(RegionFrontierFrameV2, current_pending_write));
    append_abi_offset(offsetof(RegionFrontierFrameV2, current_commit_changed));
    append_abi_offset(offsetof(RegionFrontierFrameV2, saved_body_pc));
    append_abi_offset(offsetof(RegionFrontierFrameV2, ready_words));
    append_abi_offset(offsetof(RegionFrontierFrameV2, members));
    append_abi_offset(offsetof(RegionFrontierFrameV2, scheduler_tasks));
    append_abi_offset(offsetof(RegionFrontierFrameV2, planes));
    append_abi_offset(offsetof(RegionFrontierFrameV2, metadata));
    append_abi_offset(offsetof(RegionFrontierFrameV2, fanout_edges));
    append_abi_offset(offsetof(RegionFrontierFrameV2, port_planes));
    append_abi_offset(offsetof(RegionFrontierFrameV2, pending_writes));
    append_abi_offset(offsetof(RegionFrontierFrameV2, staged_events));
    append_abi_offset(offsetof(RegionFrontierFrameV2, committed_signals));
    append_abi_offset(offsetof(RegionFrontierFrameV2, native_frontier_member_dispatches));
    append_abi_offset(offsetof(RegionFrontierFrameV2, stop_requested));
    append_abi_offset(offsetof(RegionFrontierFrameV2, slot));
    append_abi_offset(offsetof(RegionFrontierFrameV2, cut));
    append_abi_type(sizeof(RegionFrontierCutV2), alignof(RegionFrontierCutV2));
    append_abi_offset(offsetof(RegionFrontierCutV2, scheduler_frontier_generation));
    append_abi_offset(offsetof(RegionFrontierCutV2, next_key));
    append_abi_offset(offsetof(RegionFrontierCutV2, kind));
    append_abi_offset(offsetof(RegionFrontierCutV2, reserved));

    append_number(impl->identity, member_count);
    append_number(impl->identity, (member_count + 63U) / 64U);
    append_number(impl->identity, impl->signals.size());
    append_number(impl->identity, next_metadata_index);
    append_number(impl->identity, impl->fanout_edges.size());
    append_number(impl->identity, impl->write_sites.size());
    append_number(impl->identity, pending_capacity);
    append_number(impl->identity, event_capacity);
    append_number(impl->identity, maximum_fanout);
    append_number(impl->identity, pending_capacity);
    append_number(impl->identity, sizeof(RegionFrontierMemberV2));
    append_number(impl->identity, alignof(RegionFrontierMemberV2));
    append_number(impl->identity, sizeof(RegionFrontierPlaneV2));
    append_number(impl->identity, alignof(RegionFrontierPlaneV2));
    append_number(impl->identity, sizeof(RegionFrontierPendingWriteV2));
    append_number(impl->identity, alignof(RegionFrontierPendingWriteV2));
    append_number(impl->identity, sizeof(RegionFrontierLayoutV2));
    append_number(impl->identity, alignof(RegionFrontierLayoutV2));
    append_number(impl->identity, sizeof(RegionFrontierCutV2));
    append_number(impl->identity, alignof(RegionFrontierCutV2));
    append_number(impl->identity,
        static_cast<std::uint8_t>(kernel.program.scheduling_domain));
    append_number(impl->identity, kernel.program.register_count);
    append_number(impl->identity, kernel.program.operations.size());
    append_number(impl->identity, kernel.members.size());
    append_number(impl->identity, kernel.inputs.size());
    append_number(impl->identity, kernel.outputs.size());
    append_number(impl->identity, kernel.internal_signals.size());
    append_number(impl->identity, impl->signals.size());
    impl->identity.append(
        "frontier-sensitivity-admission:explicit-full-width-boundary-output-v1;");
    impl->identity.append(
        "boundary-source-publication=exact-proxy-leaf-slice-v1;");
    impl->identity.append(
        "systemverilog-partial-boundary-output-v1;");
    impl->identity.append(
        "frontier-private-internal-range-readiness-v1;");
    struct StructuralIdPatch {
        std::size_t offset { };
        std::size_t length { };
        std::uint32_t ordinal { };
        bool is_process_id { };
    };
    std::vector<StructuralIdPatch> structural_id_patches;
    bool structural_census_available = true;
    std::string canonical_structural_identity;
    // Reuse the authenticated plan maps: member_by_id assigns ordinals in
    // kernel member order, and signal_slots assigns ordinals in immutable slot
    // order. No independent or re-sorted physical-ID table is introduced.
    // Only calls through these typed appenders mark numeric spans for
    // normalization; operation/register IDs, widths, ranges, source
    // coordinates, trigger masks, and all other serialized values stay exact.
    const auto append_mapped_id = [&](const std::uint32_t id,
                                      const auto& ordinals,
                                      const bool is_process_id) {
        const auto begin = impl->identity.size();
        append_number(impl->identity, id);
        const auto mapped = ordinals.find(id);
        if (mapped == ordinals.end()) {
            structural_census_available = false;
            return;
        }
        structural_id_patches.push_back({ begin,
            impl->identity.size() - begin - 1U,
            mapped->second, is_process_id });
    };
    const auto append_process_id = [&](const ProcessId id) {
        append_mapped_id(id, member_by_id, true);
    };
    const auto append_signal_id = [&](const SignalId id) {
        append_mapped_id(id, signal_slots, false);
    };
    const auto append_optional_process_id = [&](const ProcessId id) {
        if (id == UINT32_MAX) {
            // Preserve the V2 no-owner sentinel as a non-ID structural value.
            append_number(impl->identity, id);
            return;
        }
        append_process_id(id);
    };

    for (const auto& input : kernel.inputs) {
        append_signal_id(input.signal);
        append_number(impl->identity, input.value_register);
        append_number(impl->identity, input.width);
        append_number(impl->identity,
            static_cast<std::uint8_t>(input.value_kind));
        append_number(impl->identity, input.internal ? 1U : 0U);
    }
    for (const auto& signal : impl->signals) {
        append_signal_id(signal.signal_id);
        append_optional_process_id(signal.owner_process_id);
        append_number(impl->identity,
            static_cast<std::uint32_t>(signal.value_kind));
        append_number(impl->identity, signal.width);
        append_number(impl->identity, signal.word_count);
        append_number(impl->identity, signal.plane_count);
        append_number(impl->identity, signal.flags);
        append_number(impl->identity, signal.metadata_index);
    }
    for (const auto& member : kernel.members) {
        append_process_id(member.process);
        append_number(impl->identity, member.readiness_register);
        append_number(impl->identity, member.branch_instruction);
        append_number(impl->identity, member.begin);
        append_number(impl->identity, member.end);
        append_number(impl->identity, member.initialize ? 1U : 0U);
        append_number(impl->identity, member.register_bindings.size());
        for (const auto& binding : member.register_bindings) {
            append_number(impl->identity, binding.source_register);
            append_number(impl->identity, binding.activation_register);
            append_number(impl->identity, binding.defined ? 1U : 0U);
            append_number(impl->identity, binding.width);
            append_number(impl->identity,
                static_cast<std::uint8_t>(binding.value_kind));
        }
        append_number(impl->identity, member.sensitivities.size());
        for (const auto& sensitivity : member.sensitivities) {
            append_signal_id(sensitivity.signal);
            append_number(impl->identity,
                static_cast<std::uint8_t>(sensitivity.edge));
            append_number(impl->identity, sensitivity.offset);
            append_number(impl->identity, sensitivity.width);
        }
    }
    for (const auto& output : kernel.outputs) {
        append_process_id(output.owner);
        append_signal_id(output.signal);
        append_number(impl->identity, output.offset);
        append_number(impl->identity, output.width);
        append_number(impl->identity,
            full_output_signal_width(output));
        append_number(impl->identity,
            static_cast<std::uint8_t>(output.value_kind));
        append_number(impl->identity, output.value_register);
        append_number(impl->identity, output.source_instruction);
        append_number(impl->identity, output.kernel_instruction);
        append_number(impl->identity,
            static_cast<std::uint8_t>(output.domain));
        append_number(impl->identity,
            static_cast<std::uint8_t>(output.update_kind));
        append_number(impl->identity,
            static_cast<std::uint8_t>(output.publication_kind));
        append_number(impl->identity,
            static_cast<std::uint8_t>(output.projected_mode));
        append_number(impl->identity, output.projected_delay);
        append_number(impl->identity, output.projected_rejection);
    }
    for (std::size_t index = 0U;
         index < kernel.program.operations.size(); ++index) {
        const auto operation = kernel.program.operations.expanded(index);
        if (!append_operation_identity(impl->identity, operation)) {
            structural_census_available = false;
        }
    }
    append_number(impl->identity, impl->members.size());
    for (const auto& member_layout : impl->members) {
        append_process_id(member_layout.process_id);
        append_number(impl->identity, member_layout.first_write_site);
        append_number(impl->identity, member_layout.write_site_count);
        append_number(impl->identity, member_layout.max_pending_writes);
        append_number(impl->identity, member_layout.max_staged_events);
    }
    append_number(impl->identity, impl->write_sites.size());
    for (const auto& site : impl->write_sites) {
        append_number(impl->identity, site.member_index);
        append_number(impl->identity, site.signal_slot);
        append_number(impl->identity, site.source_instruction);
        append_number(impl->identity, site.update_kind);
        append_number(impl->identity, site.event_kind);
        append_number(impl->identity,
            static_cast<std::uint32_t>(site.value_kind));
        append_number(impl->identity, site.width);
        append_number(impl->identity, site.word_count);
        append_number(impl->identity, site.plane_count);
        append_number(impl->identity, site.pending_slot);
    }
    append_number(impl->identity, impl->fanout_edges.size());
    for (const auto& edge : impl->fanout_edges) {
        append_number(impl->identity, edge.signal_slot);
        append_number(impl->identity, edge.member_index);
        append_number(impl->identity, edge.trigger_mask);
    }
    append_number(impl->identity, impl->fanout_range_spans.size());
    for (const auto& span : impl->fanout_range_spans) {
        append_number(impl->identity, span.first_range);
        append_number(impl->identity, span.range_count);
    }
    append_number(impl->identity,
        impl->fanout_sensitivity_ranges.size());
    for (const auto& range : impl->fanout_sensitivity_ranges) {
        append_number(impl->identity, range.offset);
        append_number(impl->identity, range.width);
    }
    append_number(impl->identity, impl->bodies.size());
    for (const auto& body : impl->bodies) {
        append_number(impl->identity, body.has_unknown_constant ? 1U : 0U);
        append_number(impl->identity, body.has_logic9_value ? 1U : 0U);
        append_number(impl->identity, body.used_input_registers.size());
        for (const auto reg : body.used_input_registers) {
            append_number(impl->identity, reg);
        }
    }

    if (structural_census_available) {
        std::ranges::sort(structural_id_patches, std::ranges::less { },
            &StructuralIdPatch::offset);
        std::string canonical_identity;
        canonical_identity.reserve(impl->identity.size());
        std::size_t cursor { };
        for (const auto& patch : structural_id_patches) {
            if (patch.offset < cursor
                || patch.offset > impl->identity.size()
                || patch.length > impl->identity.size() - patch.offset) {
                structural_census_available = false;
                break;
            }
            canonical_identity.append(impl->identity, cursor,
                patch.offset - cursor);
            canonical_identity.push_back(
                patch.is_process_id ? 'p' : 's');
            append_number(canonical_identity, patch.ordinal);
            cursor = patch.offset + patch.length;
        }
        if (structural_census_available) {
            canonical_identity.append(impl->identity, cursor,
                impl->identity.size() - cursor);
            CacheKeyBuilder census_builder;
            census_builder.add("domain",
                "region-frontier-structural-census-v1");
            census_builder.add("serialized-plan", canonical_identity);
            impl->structural_census_identity = census_builder.finish();
            impl->structural_census_identity_available = true;
            canonical_structural_identity = std::move(canonical_identity);
        }
    }

    // Keep the complete semantic serialization only while constructing the
    // existing deterministic SHA-256 cache key; retain the compact digest in
    // the plan instead of duplicating the whole kernel description.
    CacheKeyBuilder identity_builder;
    identity_builder.add("region-frontier-kernel-plan-v7", impl->identity);
    auto compact_identity = identity_builder.finish();
    impl->identity.swap(compact_identity);

    impl->layout.abi_version = kRegionFrontierAbiVersionV2;
    impl->layout.struct_size = sizeof(RegionFrontierLayoutV2);
    impl->layout.value_plane_contract = kRegionFrontierValuePlaneContractV2;
    impl->layout.execution_mode = execution_mode;
    impl->layout.certificate_generation = stable_hash(impl->identity, 0x43455254U);
    impl->layout.component_generation = stable_hash(impl->identity, 0x434f4d50U);
    impl->layout.member_count = static_cast<std::uint32_t>(member_count);
    impl->layout.readiness_word_count
        = static_cast<std::uint32_t>((member_count + 63U) / 64U);
    impl->layout.signal_slot_count
        = static_cast<std::uint32_t>(impl->signals.size());
    impl->layout.metadata_count = next_metadata_index;
    impl->layout.fanout_edge_count
        = static_cast<std::uint32_t>(impl->fanout_edges.size());
    impl->layout.write_site_count
        = static_cast<std::uint32_t>(impl->write_sites.size());
    impl->layout.pending_write_capacity = pending_capacity;
    impl->layout.staged_event_capacity = event_capacity;
    impl->layout.max_commit_fanout_events = maximum_fanout;
    impl->layout.committed_signal_capacity = pending_capacity;
    impl->repoint_layout();

    // The normalized source-plan stream preserves physical-ID relationships
    // as ordinals, but it is only one input to this identity. Serialize the
    // final emitter state as well so changes in post-lowering member bodies,
    // frame shape, slots, or fanout tables cannot reuse an old shared body.
    if (structural_census_available
        && !canonical_structural_identity.empty()) {
        std::string emitted_body_shape;
        emitted_body_shape.reserve(canonical_structural_identity.size() / 4U
            + impl->bodies.size() * 64U);
        emitted_body_shape.append("lowered-layout-v1;");
        append_number(emitted_body_shape, impl->layout.abi_version);
        append_number(emitted_body_shape, impl->layout.struct_size);
        append_number(emitted_body_shape,
            impl->layout.value_plane_contract);
        append_number(emitted_body_shape, impl->layout.reserved0);
        append_number(emitted_body_shape, impl->layout.member_count);
        append_number(emitted_body_shape,
            impl->layout.readiness_word_count);
        append_number(emitted_body_shape, impl->layout.signal_slot_count);
        append_number(emitted_body_shape, impl->layout.metadata_count);
        append_number(emitted_body_shape, impl->layout.fanout_edge_count);
        append_number(emitted_body_shape, impl->layout.write_site_count);
        append_number(emitted_body_shape,
            impl->layout.pending_write_capacity);
        append_number(emitted_body_shape,
            impl->layout.staged_event_capacity);
        append_number(emitted_body_shape,
            impl->layout.max_commit_fanout_events);
        append_number(emitted_body_shape,
            impl->layout.committed_signal_capacity);
        append_number(emitted_body_shape, impl->layout.reserved_capacity);
        append_number(emitted_body_shape,
            static_cast<std::uint32_t>(impl->layout.execution_mode));

        // Member process IDs are deliberately supplied through the binding.
        // All remaining member layout fields are static shared-body inputs.
        append_number(emitted_body_shape, impl->members.size());
        for (const auto& member : impl->members) {
            append_number(emitted_body_shape, member.first_write_site);
            append_number(emitted_body_shape, member.write_site_count);
            append_number(emitted_body_shape, member.max_pending_writes);
            append_number(emitted_body_shape, member.max_staged_events);
            for (const auto reserved : member.reserved) {
                append_number(emitted_body_shape, reserved);
            }
        }

        // Signal IDs and owner process IDs are also binding fields. Keep every
        // other descriptor field in the shared identity.
        append_number(emitted_body_shape, impl->signals.size());
        for (const auto& signal : impl->signals) {
            append_number(emitted_body_shape,
                static_cast<std::uint32_t>(signal.value_kind));
            append_number(emitted_body_shape, signal.width);
            append_number(emitted_body_shape, signal.word_count);
            append_number(emitted_body_shape, signal.plane_count);
            append_number(emitted_body_shape, signal.flags);
            append_number(emitted_body_shape, signal.metadata_index);
        }

        append_number(emitted_body_shape, impl->write_sites.size());
        for (const auto& site : impl->write_sites) {
            append_number(emitted_body_shape, site.member_index);
            append_number(emitted_body_shape, site.signal_slot);
            append_number(emitted_body_shape, site.source_instruction);
            append_number(emitted_body_shape, site.update_kind);
            append_number(emitted_body_shape, site.event_kind);
            append_number(emitted_body_shape,
                static_cast<std::uint32_t>(site.value_kind));
            append_number(emitted_body_shape, site.width);
            append_number(emitted_body_shape, site.word_count);
            append_number(emitted_body_shape, site.plane_count);
            append_number(emitted_body_shape, site.pending_slot);
        }

        append_number(emitted_body_shape,
            impl->max_member_write_counts.size());
        for (const auto count : impl->max_member_write_counts) {
            append_number(emitted_body_shape, count);
        }
        append_number(emitted_body_shape,
            impl->max_member_staged_event_counts.size());
        for (const auto count : impl->max_member_staged_event_counts) {
            append_number(emitted_body_shape, count);
        }

        append_number(emitted_body_shape, impl->fanout_edges.size());
        for (const auto& edge : impl->fanout_edges) {
            append_number(emitted_body_shape, edge.signal_slot);
            append_number(emitted_body_shape, edge.member_index);
            append_number(emitted_body_shape, edge.trigger_mask);
        }
        append_number(emitted_body_shape, impl->fanout_range_spans.size());
        for (const auto& span : impl->fanout_range_spans) {
            append_number(emitted_body_shape, span.first_range);
            append_number(emitted_body_shape, span.range_count);
        }
        append_number(emitted_body_shape,
            impl->fanout_sensitivity_ranges.size());
        for (const auto& range : impl->fanout_sensitivity_ranges) {
            append_number(emitted_body_shape, range.offset);
            append_number(emitted_body_shape, range.width);
        }

        append_number(emitted_body_shape, impl->input_slots.size());
        for (const auto& [reg, slot] : impl->input_slots) {
            append_number(emitted_body_shape, reg);
            append_number(emitted_body_shape, slot);
        }

        append_number(emitted_body_shape, impl->bodies.size());
        bool body_shape_supported
            = impl->bodies.size() == impl->members.size();
        for (std::size_t member_index = 0U;
             member_index < impl->bodies.size(); ++member_index) {
            if (member_index >= impl->members.size()) {
                body_shape_supported = false;
                break;
            }
            const auto& body = impl->bodies[member_index];
            if (body.process_id != impl->members[member_index].process_id) {
                body_shape_supported = false;
                break;
            }
            // `body.process_id` is not read by a shared member emission:
            // active stable_order is loaded from the physical binding, while
            // Generic uses the captured activation key.
            append_number(emitted_body_shape, body.readiness_register);
            append_number(emitted_body_shape, body.begin);
            append_number(emitted_body_shape, body.end);
            append_number(emitted_body_shape,
                body.has_unknown_constant ? 1U : 0U);
            append_number(emitted_body_shape,
                body.has_logic9_value ? 1U : 0U);
            append_number(emitted_body_shape,
                body.used_input_registers.size());
            for (const auto reg : body.used_input_registers) {
                append_number(emitted_body_shape, reg);
            }
            append_number(emitted_body_shape, body.operations.size());
            for (const auto& operation : body.operations) {
                if (static_cast<std::uint8_t>(operation.kind)
                    > static_cast<std::uint8_t>(
                        BodyOperationKind::debug_noop)) {
                    body_shape_supported = false;
                    break;
                }
                append_number(emitted_body_shape,
                    static_cast<std::uint8_t>(operation.kind));
                append_number(emitted_body_shape, operation.instruction);
                append_number(emitted_body_shape, operation.destination);
                append_number(emitted_body_shape, operation.first_source);
                append_number(emitted_body_shape, operation.second_source);
                append_number(emitted_body_shape, operation.third_source);
                append_number(emitted_body_shape,
                    operation.operands.size());
                for (const auto operand : operation.operands) {
                    append_number(emitted_body_shape, operand);
                }
                append_number(emitted_body_shape, operation.offset);
                append_number(emitted_body_shape, operation.width);
                append_number(emitted_body_shape,
                    static_cast<std::uint8_t>(operation.shift_operator));
                append_number(emitted_body_shape, operation.output_site);
                append_number(emitted_body_shape,
                    operation.constant_aval.size());
                for (const auto word : operation.constant_aval) {
                    append_number(emitted_body_shape, word);
                }
                append_number(emitted_body_shape,
                    operation.constant_bval.size());
                for (const auto word : operation.constant_bval) {
                    append_number(emitted_body_shape, word);
                }
            }
        }

        if (body_shape_supported) {
            CacheKeyBuilder shared_builder;
            shared_builder.add("domain",
                "region-frontier-certified-shared-body-v2");
            shared_builder.add("codegen-contract",
                "loop-v2-bound-member-id-internal-commit-v2-v1;"
                "stage-write-helper-v1-max16-payload-words;"
                "bound-member-template-helper-v1;"
                "immutable-member-bound-and-selected-site-tables-v1;"
                "alias-prevalidated-geometry-entry-v1;"
                "canonical-values-prevalidated-entry-v1;"
                "initial-slot-validation-helper-v1");
            shared_builder.add("canonical-structural-plan",
                canonical_structural_identity);
            shared_builder.add("post-lowering-emitted-shape",
                emitted_body_shape);
            impl->shared_body_identity = shared_builder.finish();
            impl->shared_body_identity_available = true;
        }
    }

    // The operation-shape key alone is not full-region admission. Here it
    // certifies only helper emission after the complete plan has succeeded:
    // omitted scheduling/debug coordinates remain in the caller and every
    // staged-write coordinate is carried by the immutable binding descriptor.
    impl->member_templates.reserve(impl->bodies.size());
    std::map<std::string, std::uint32_t> template_shapes;
    for (std::size_t index = 0U; index < impl->bodies.size(); ++index) {
        MemberShapeCensusKeys keys;
        if (!append_member_shape_keys(keys, impl->bodies[index], index,
                impl->members[index], register_shapes, impl->signals,
                impl->input_slots, impl->write_sites, false)) {
            impl->member_templates.clear();
            break;
        }
        const auto input_count = keys.ordered_inputs.size();
        if (input_count > UINT32_MAX - 1U
            || keys.ordered_sites.size()
                > (UINT32_MAX - 1U - input_count) / 4U) {
            impl->member_templates.clear();
            break;
        }
        const auto next_shape = static_cast<std::uint32_t>(
            impl->member_template_shapes.size());
        const auto [shape, inserted] = template_shapes.emplace(
            keys.template_key, next_shape);
        if (inserted) {
            impl->member_template_shapes.push_back(std::move(keys.template_key));
            impl->member_template_shape_counts.push_back(0U);
        }
        ++impl->member_template_shape_counts[shape->second];
        impl->member_templates.push_back({ shape->second,
            std::move(keys.ordered_inputs), std::move(keys.ordered_sites) });
    }

    frontier_planner_detail::report_member_template_census(
        impl->bodies, impl->member_templates,
        impl->member_template_shape_counts);

    emit_member_shape_census(impl->identity, impl->bodies, impl->members,
        register_shapes, impl->signals, impl->input_slots,
        impl->write_sites);

    return RegionFrontierKernelPlan { std::move(impl) };
}

llvm::Function* RegionFrontierKernelPlan::emit_step(llvm::Module& module,
    const std::string_view symbol,
    const EmitCertifiedInternalCommitV2& emit_internal_commit) const
{
    if (!impl_ || symbol.empty() || !emit_internal_commit) {
        throw std::invalid_argument("invalid region frontier codegen request");
    }
    EmitCertifiedMemberBodyV2 emit_member = [this](Builder& builder,
                                                  const std::size_t index,
                                                  llvm::Value* frame) {
        if (index >= impl_->bodies.size()) {
            throw std::logic_error("region frontier member index is invalid");
        }
        emit_member_body(builder, frame, index, impl_->bodies[index],
            impl_->signals, impl_->input_slots, impl_->write_sites, nullptr,
            impl_->layout.execution_mode
                == RegionFrontierExecutionModeV2::generic_deferred_update);
    };
    return emit_region_frontier_loop_v2(module, std::string { symbol },
        impl_->layout, emit_member, emit_internal_commit);
}

llvm::Function* RegionFrontierKernelPlan::emit_shared_body(
    llvm::Module& module, const std::string_view symbol,
    const EmitCertifiedInternalCommitV2& emit_internal_commit) const
{
    if (!impl_ || symbol.empty() || !emit_internal_commit) {
        throw std::invalid_argument("invalid shared region frontier codegen request");
    }
    if (!impl_->shared_body_identity_available) {
        throw std::logic_error(
            "region frontier plan has no certified shared-body identity");
    }

    auto structural_members = impl_->members;
    auto structural_signals = impl_->signals;
    std::map<ProcessId, std::uint32_t> member_ordinals;
    for (std::size_t index = 0U;
         index < impl_->members.size(); ++index) {
        const auto ordinal = static_cast<std::uint32_t>(index);
        if (!member_ordinals.emplace(
                impl_->members[index].process_id, ordinal).second) {
            throw std::logic_error(
                "shared region frontier has duplicate physical members");
        }
        structural_members[index].process_id = ordinal;
    }
    for (std::size_t index = 0U;
         index < structural_signals.size(); ++index) {
        auto& signal = structural_signals[index];
        signal.signal_id = static_cast<std::uint32_t>(index);
        if (signal.owner_process_id == UINT32_MAX) {
            continue;
        }
        const auto owner = member_ordinals.find(signal.owner_process_id);
        if (owner == member_ordinals.end()) {
            throw std::logic_error(
                "shared region frontier owner is not a certified member");
        }
        signal.owner_process_id = owner->second;
    }

    auto structural_layout = impl_->layout;
    structural_layout.certificate_generation
        = UINT64_C(0x5348424459434552);
    structural_layout.component_generation
        = UINT64_C(0x5348424459434f4d);
    structural_layout.members = structural_members.data();
    structural_layout.signals = structural_signals.data();

    auto* const binding_type = region_frontier_physical_binding_type_v2(
        module.getContext(), structural_layout.member_count,
        structural_layout.signal_slot_count);
    EmitBoundCertifiedMemberBodyV2 emit_member = [this, binding_type](
        Builder& builder, const std::size_t index, llvm::Value* frame,
        llvm::Value* physical_binding) {
        if (index >= impl_->bodies.size()
            || index > std::numeric_limits<std::uint32_t>::max()) {
            throw std::logic_error("region frontier member index is invalid");
        }
        auto* const member_process_id
            = load_region_frontier_member_process_id_v2(builder,
                binding_type, physical_binding,
                constant_i32(builder, static_cast<std::uint32_t>(index)));
        const bool generic_execution = impl_->layout.execution_mode
            == RegionFrontierExecutionModeV2::generic_deferred_update;
        if (impl_->member_templates.size() != impl_->bodies.size()) {
            emit_member_body(builder, frame, index, impl_->bodies[index],
                impl_->signals, impl_->input_slots, impl_->write_sites,
                member_process_id, generic_execution);
            return;
        }
        const auto& description = impl_->member_templates[index];
        if (impl_->member_template_shape_counts[description.shape_index] < 2U) {
            emit_member_body(builder, frame, index, impl_->bodies[index],
                impl_->signals, impl_->input_slots, impl_->write_sites,
                member_process_id, generic_execution);
            return;
        }
        auto* const module = builder.GetInsertBlock()->getModule();
        CacheKeyBuilder helper_key;
        // Shape covers operation kind, alpha-register aliases, widths,
        // constants, extracts/shifts, input plane shape and output event shape.
        // Descriptors carry member/input/output IDs, source instructions,
        // update/pending slots; physical process ID is a separate argument.
        // Scheduling, readiness and final debug state remain in the caller.
        helper_key.add("domain", "certified-member-template-helper-v1");
        helper_key.add("operations",
            impl_->member_template_shapes[description.shape_index]);
        helper_key.add("execution", generic_execution ? "generic" : "sv");
        helper_key.add("binding-contract",
            "member-index;normalized-input-slots;output-signal-source-kind-slot;"
            "dynamic-process-id;preserved-known-fallback-stage-order-v1");
        const auto name = "fsim.frontier.member.template.v1."
            + helper_key.finish();
        auto* const descriptor_type = llvm::PointerType::getUnqual(
            builder.getContext());
        std::vector<llvm::Type*> argument_types {
            frame->getType(), descriptor_type, i32(builder),
        };
        auto* const function_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(builder.getContext()), argument_types, false);
        auto* helper = module->getFunction(name);
        if (helper == nullptr) {
            helper = llvm::Function::Create(function_type,
                llvm::GlobalValue::InternalLinkage, name, *module);
            helper->addFnAttr(llvm::Attribute::NoInline);
            helper->addFnAttr(llvm::Attribute::NoUnwind);
            auto* const entry = llvm::BasicBlock::Create(builder.getContext(),
                "entry", helper);
            Builder helper_builder(builder.getContext());
            helper_builder.SetInsertPoint(entry);
            MemberTemplateRuntimeBinding binding;
            binding.descriptor = helper->getArg(1U);
            binding.input_count = static_cast<std::uint32_t>(
                description.ordered_inputs.size());
            for (std::size_t input = 0U;
                 input < description.ordered_inputs.size(); ++input) {
                binding.input_ordinals.emplace(
                    description.ordered_inputs[input],
                    static_cast<std::uint32_t>(input));
            }
            for (std::size_t site = 0U;
                 site < description.ordered_sites.size(); ++site) {
                binding.site_ordinals.emplace(description.ordered_sites[site],
                    static_cast<std::uint32_t>(site));
            }
            emit_member_body(helper_builder, helper->getArg(0U), index,
                impl_->bodies[index], impl_->signals, impl_->input_slots,
                impl_->write_sites, helper->getArg(2U), generic_execution,
                &binding);
            helper_builder.CreateRetVoid();
        } else if (helper->getFunctionType() != function_type
            || helper->getLinkage() != llvm::GlobalValue::InternalLinkage
            || !helper->hasFnAttribute(llvm::Attribute::NoInline)
            || !helper->hasFnAttribute(llvm::Attribute::NoUnwind)
            || helper->empty()) {
            throw std::logic_error("member template helper name collision");
        }

        std::vector<llvm::Constant*> descriptor_fields;
        descriptor_fields.push_back(llvm::ConstantInt::get(i32(builder),
            static_cast<std::uint32_t>(index)));
        for (const auto reg : description.ordered_inputs) {
            descriptor_fields.push_back(llvm::ConstantInt::get(i32(builder),
                impl_->input_slots.at(reg)));
        }
        for (const auto site_index : description.ordered_sites) {
            const auto& site = impl_->write_sites[site_index];
            descriptor_fields.push_back(llvm::ConstantInt::get(i32(builder), site.signal_slot));
            descriptor_fields.push_back(llvm::ConstantInt::get(i32(builder),
                site.source_instruction));
            descriptor_fields.push_back(llvm::ConstantInt::get(i32(builder), site.update_kind));
            descriptor_fields.push_back(llvm::ConstantInt::get(i32(builder), site.pending_slot));
        }
        auto* const descriptor_array_type = llvm::ArrayType::get(i32(builder),
            descriptor_fields.size());
        auto* const descriptor = new llvm::GlobalVariable(*module,
            descriptor_array_type, true, llvm::GlobalValue::PrivateLinkage,
            llvm::ConstantArray::get(descriptor_array_type, descriptor_fields),
            "fsim.frontier.member.binding");
        builder.CreateCall(helper,
            { frame, descriptor, member_process_id });
    };
    return emit_region_frontier_shared_body_v2(module, std::string { symbol },
        structural_layout, emit_member, emit_internal_commit);
}

} // namespace fsim::compiler
