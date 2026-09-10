// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/object_cache.hpp"
#include "llvm_jit_internal.hpp"

namespace fsim::compiler::llvm_detail {

void add_key_u64(
    CacheKeyBuilder& builder,
    std::string_view label,
    std::uint64_t value);
void add_packed_value_key(
    CacheKeyBuilder& builder,
    const runtime::PackedLogic4& value);
void add_constraint_template_key(
    CacheKeyBuilder& builder,
    const runtime::SystemVerilogConstraintTemplate& expression,
    const std::string& prefix);
void add_dynamic_index_key(
    CacheKeyBuilder& builder,
    const runtime::simir::DynamicIndex& selection);
void add_dynamic_part_index_key(
    CacheKeyBuilder& builder,
    const runtime::simir::DynamicPartIndex& selection);
void add_container_type_key(
    CacheKeyBuilder& builder,
    const runtime::simir::ContainerType& type);
void add_primary_operation_cache_key(
    CacheKeyBuilder& builder,
    const runtime::simir::Operation& operation,
    std::span<const std::uint32_t> signal_widths);
void add_secondary_operation_cache_key(
    CacheKeyBuilder& builder,
    const runtime::simir::Process& process,
    const runtime::simir::Operation& operation,
    std::span<const std::uint32_t> signal_widths);
void add_operation_cache_keys(
    CacheKeyBuilder& builder,
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths);

} // namespace fsim::compiler::llvm_detail
