// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fsim::semantic {

class SpecializedHirUnit;

/// The resolved shape of a SystemVerilog data type in one specialization:
/// typedefs and type parameters are followed, and the use-site declarators
/// are kept. Two types match (IEEE 1800-2017 6.22.1) when their canonical
/// spellings are equal.
struct SystemVerilogTypeDescriptor {
    enum class Kind : std::uint8_t {
        integer_atom,
        integer_vector,
        real,
        shortreal,
        realtime,
        string,
        chandle,
        event,
        enumeration,
        structure,
        union_type,
        class_handle,
        interface,
        other,
    };

    struct Dimension {
        std::optional<std::int64_t> left;
        std::optional<std::int64_t> right;
        // A dynamic, queue or associative dimension has no bounds.
        std::string container;
    };

    Kind kind { Kind::other };
    // The built-in keyword for an integer atom or vector (`int`, `logic`).
    std::string keyword;
    bool is_signed { };
    bool four_state { };
    // Packed dimensions, outermost first.
    std::vector<Dimension> packed;
    // Unpacked dimensions, outermost first.
    std::vector<Dimension> unpacked;
    // The declaration of a nominal type: an enum, struct, union or class
    // matches only itself (6.22.1).
    std::optional<TypeId> nominal;
    std::string nominal_name;
    // The width of one element: the packed width without unpacked
    // dimensions.
    std::optional<std::uint64_t> bits;

    /// The bit-stream width (IEEE 1800-2017 20.6.2): the element width times
    /// every fixed unpacked dimension; nullopt for a variable-size dimension.
    [[nodiscard]] std::optional<std::uint64_t> total_bits() const;

    /// A deterministic spelling of the type's matching identity.
    [[nodiscard]] std::string canonical() const;
};

/// Describe a type reference used in `scope` of a specialization; nullopt
/// when it does not resolve.
[[nodiscard]] std::optional<SystemVerilogTypeDescriptor>
describe_systemverilog_type(const SpecializedHirUnit& unit,
    const sv::TypeReference& type, ScopeId scope);

/// Describe the self-determined type of an expression (IEEE 1800-2017 6.23,
/// 11.6.1): a name gives its declared type and an operator its result type.
/// A `type(...)` operand that names a type gives that type.
[[nodiscard]] std::optional<SystemVerilogTypeDescriptor>
describe_systemverilog_expression_type(const SpecializedHirUnit& unit,
    ExpressionId expression);

/// The type reference denoting a described type, so that `type(expression)`
/// used as a data type resolves like an ordinary declaration type.
[[nodiscard]] std::optional<sv::TypeReference>
systemverilog_type_reference(const SystemVerilogTypeDescriptor& type);

/// Whether two types match (IEEE 1800-2017 6.22.1).
[[nodiscard]] bool systemverilog_types_match(
    const SystemVerilogTypeDescriptor& left,
    const SystemVerilogTypeDescriptor& right);

} // namespace fsim::semantic
