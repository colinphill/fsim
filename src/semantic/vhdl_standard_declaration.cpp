// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/vhdl_standard_declaration.hpp"

namespace fsim::semantic {

std::optional<support::StandardDeclaration> standard_declaration_of(
    const vhdl::ProvidedType type) noexcept
{
    using Type = vhdl::ProvidedType;
    using Declaration = support::StandardDeclaration;
    switch (type) {
    case Type::boolean:
        return Declaration::boolean;
    case Type::bit:
        return Declaration::bit;
    case Type::character:
        return Declaration::character;
    case Type::severity_level:
        return Declaration::severity_level;
    case Type::integer:
        return Declaration::integer;
    case Type::natural:
        return Declaration::natural;
    case Type::positive:
        return Declaration::positive;
    case Type::real:
        return Declaration::real;
    case Type::time:
        return Declaration::time;
    case Type::delay_length:
        return Declaration::delay_length;
    case Type::file_open_kind:
        return Declaration::file_open_kind;
    case Type::file_open_status:
        return Declaration::file_open_status;
    case Type::range_direction:
        return Declaration::range_direction;
    case Type::string:
        return Declaration::string;
    case Type::bit_vector:
        return Declaration::bit_vector;
    case Type::boolean_vector:
        return Declaration::boolean_vector;
    case Type::integer_vector:
        return Declaration::integer_vector;
    case Type::real_vector:
        return Declaration::real_vector;
    case Type::time_vector:
        return Declaration::time_vector;
    default:
        return std::nullopt;
    }
}

std::optional<vhdl::ProvidedType> provided_type_of(
    const support::StandardDeclaration declaration) noexcept
{
    using Type = vhdl::ProvidedType;
    using Declaration = support::StandardDeclaration;
    switch (declaration) {
    case Declaration::boolean:
        return Type::boolean;
    case Declaration::bit:
        return Type::bit;
    case Declaration::character:
        return Type::character;
    case Declaration::severity_level:
        return Type::severity_level;
    case Declaration::integer:
        return Type::integer;
    case Declaration::natural:
        return Type::natural;
    case Declaration::positive:
        return Type::positive;
    case Declaration::real:
        return Type::real;
    case Declaration::time:
        return Type::time;
    case Declaration::delay_length:
        return Type::delay_length;
    case Declaration::file_open_kind:
        return Type::file_open_kind;
    case Declaration::file_open_status:
        return Type::file_open_status;
    case Declaration::range_direction:
        return Type::range_direction;
    case Declaration::string:
        return Type::string;
    case Declaration::bit_vector:
        return Type::bit_vector;
    case Declaration::boolean_vector:
        return Type::boolean_vector;
    case Declaration::integer_vector:
        return Type::integer_vector;
    case Declaration::real_vector:
        return Type::real_vector;
    case Declaration::time_vector:
        return Type::time_vector;
    default:
        return std::nullopt;
    }
}

} // namespace fsim::semantic
