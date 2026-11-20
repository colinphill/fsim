// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <string_view>

namespace fsim::support {

/// A declaration of the compiler-served package STD.REFLECTION (IEEE
/// 1076-2019 16.6): its designator (case-folded) and whether it is
/// overloadable (an enumeration literal of TYPE_CLASS). Taken from the
/// vendored third_party/ieee-1076-2019/std/reflection.vhdl: its types,
/// subtypes and protected types (their methods are not package members)
/// and TYPE_CLASS's literals.
struct VhdlReflectionDeclaration {
    std::string_view designator;
    bool overloadable { };
};

inline constexpr std::array vhdl_reflection_declarations {
    VhdlReflectionDeclaration { "access_subtype_mirror", false },
    VhdlReflectionDeclaration { "access_subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "access_value_mirror", false },
    VhdlReflectionDeclaration { "access_value_mirror_pt", false },
    VhdlReflectionDeclaration { "array_subtype_mirror", false },
    VhdlReflectionDeclaration { "array_subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "array_value_mirror", false },
    VhdlReflectionDeclaration { "array_value_mirror_pt", false },
    VhdlReflectionDeclaration { "dimension", false },
    VhdlReflectionDeclaration { "enumeration_subtype_mirror", false },
    VhdlReflectionDeclaration { "enumeration_subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "enumeration_value_mirror", false },
    VhdlReflectionDeclaration { "enumeration_value_mirror_pt", false },
    VhdlReflectionDeclaration { "file_subtype_mirror", false },
    VhdlReflectionDeclaration { "file_subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "file_value_mirror", false },
    VhdlReflectionDeclaration { "file_value_mirror_pt", false },
    VhdlReflectionDeclaration { "floating_subtype_mirror", false },
    VhdlReflectionDeclaration { "floating_subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "floating_value_mirror", false },
    VhdlReflectionDeclaration { "floating_value_mirror_pt", false },
    VhdlReflectionDeclaration { "index", false },
    VhdlReflectionDeclaration { "index_vector", false },
    VhdlReflectionDeclaration { "integer_subtype_mirror", false },
    VhdlReflectionDeclaration { "integer_subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "integer_value_mirror", false },
    VhdlReflectionDeclaration { "integer_value_mirror_pt", false },
    VhdlReflectionDeclaration { "natural_index", false },
    VhdlReflectionDeclaration { "physical_subtype_mirror", false },
    VhdlReflectionDeclaration { "physical_subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "physical_value_mirror", false },
    VhdlReflectionDeclaration { "physical_value_mirror_pt", false },
    VhdlReflectionDeclaration { "positive_index", false },
    VhdlReflectionDeclaration { "protected_subtype_mirror", false },
    VhdlReflectionDeclaration { "protected_subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "protected_value_mirror", false },
    VhdlReflectionDeclaration { "protected_value_mirror_pt", false },
    VhdlReflectionDeclaration { "record_subtype_mirror", false },
    VhdlReflectionDeclaration { "record_subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "record_value_mirror", false },
    VhdlReflectionDeclaration { "record_value_mirror_pt", false },
    VhdlReflectionDeclaration { "subtype_mirror", false },
    VhdlReflectionDeclaration { "subtype_mirror_pt", false },
    VhdlReflectionDeclaration { "type_class", false },
    VhdlReflectionDeclaration { "value_class", false },
    VhdlReflectionDeclaration { "value_mirror", false },
    VhdlReflectionDeclaration { "value_mirror_pt", false },
    VhdlReflectionDeclaration { "class_enumeration", true },
    VhdlReflectionDeclaration { "class_integer", true },
    VhdlReflectionDeclaration { "class_floating", true },
    VhdlReflectionDeclaration { "class_physical", true },
    VhdlReflectionDeclaration { "class_record", true },
    VhdlReflectionDeclaration { "class_array", true },
    VhdlReflectionDeclaration { "class_access", true },
    VhdlReflectionDeclaration { "class_file", true },
    VhdlReflectionDeclaration { "class_protected", true },
};

} // namespace fsim::support
