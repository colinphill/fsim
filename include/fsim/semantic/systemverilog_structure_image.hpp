// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace fsim::semantic::sv {

/// The value of a sized or unsized based literal without x or z digits
/// (`8'h81`, `'d5`), as an unsigned 64-bit pattern (IEEE 1800-2017 5.7.1).
[[nodiscard]] std::optional<std::uint64_t> systemverilog_based_literal_bits(
    std::string_view text);

/// An enumeration's literal values in declaration order; an implicit value
/// follows the previous one (IEEE 1800-2017 6.19). Empty when `definition`
/// is not an enumeration or a value is not a literal.
[[nodiscard]] std::vector<std::uint64_t> systemverilog_enumeration_values(
    const Hir& hir, const TypeDefinition& definition);

/// The type definition `type` names: its typedef, followed through aliases
/// to the structure, union or enumeration it defines. A type of another unit
/// (a `$unit` or package typedef) that keeps only its spelling resolves when
/// the spelling names exactly one typedef.
[[nodiscard]] const TypeDefinition* systemverilog_type_definition(
    const Hir& hir, const TypeReference& type);

/// `type`, or, when it names a typedef of an unpacked array, dynamic array,
/// queue or associative array (`arr_t m;`), that typedef's array type (IEEE
/// 1800-2017 6.18).
[[nodiscard]] const TypeReference& systemverilog_array_alias_type(
    const Hir& hir, const TypeReference& type);

/// The packed or unpacked structure `type` denotes, or null.
[[nodiscard]] const TypeDefinition* systemverilog_structure_definition(
    const Hir& hir, const TypeReference& type);

/// The bits a value of `type` takes in a class property's packed image of a
/// structure, first member most significant (IEEE 1800-2017 7.2), or nullopt
/// when some member has no packed image (an unpacked array, dynamic array,
/// queue or associative member, a string, a class handle or chandle).
[[nodiscard]] std::optional<std::size_t>
systemverilog_structure_image_width(const Hir& hir, const TypeReference& type);

/// An unpacked structure property held as an aggregate container value:
/// some member has no packed image.
[[nodiscard]] bool systemverilog_boxed_aggregate(
    const Hir& hir, const TypeReference& type);

} // namespace fsim::semantic::sv
