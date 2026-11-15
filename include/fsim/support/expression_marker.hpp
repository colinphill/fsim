// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

// The kind of a node the parser or a resolution pass synthesizes, where no
// source construct of the same shape exists: a class property read, a
// container method, a streaming slice, a VHDL dereference. The node's text
// never carries the kind; a payload (a member identity, a unit name, a
// width) travels in the node's marker argument.
enum class ExpressionMarker : std::uint8_t {
    none,
    // SystemVerilog expressions.
    sv_select,                    // payload: member identifier
    sv_cast,                      // payload: width, or a type name
    sv_size_cast,                 // operands: size expression, value
    sv_property,                  // payload: Owner::member
    sv_static_property,           // payload: Owner::member
    sv_method,                    // payload: Owner::member
    sv_static_method,             // payload: Owner::member
    sv_base_method,               // payload: Owner::member
    sv_new,                       // unresolved class new
    sv_new_constructor,           // payload: class identity
    sv_new_typed,                 // payload: class spelling
    sv_sync_new,                  // payload: built-in class spelling
    sv_shallow_copy,              // new C(obj)
    sv_dollar_cast,               // unresolved class $cast
    sv_dollar_cast_class,         // payload: class identity
    sv_null,
    sv_type,                      // operand is a data type
    sv_clocking_event,            // edge in call_result_width
    sv_foreach,                   // foreach loop condition
    sv_static_call_selection,
    sv_tagged,                    // payload: union member
    sv_dependent_property,        // payload: member name
    sv_container_property,        // payload: Owner::property
    sv_static_container_property, // payload: Owner::property
    sv_container_index,           // payload: Owner::property
    sv_container_pop_front,       // payload: Owner::property
    sv_container_pop_back,        // payload: Owner::property
    sv_container_size,            // payload: Owner::property
    sv_container_sum,             // payload: Owner::property
    sv_container_product,         // payload: Owner::property
    sv_container_and,             // payload: Owner::property
    sv_container_or,              // payload: Owner::property
    sv_container_xor,             // payload: Owner::property
    // Any other built-in array method (`exists`, `num`, `first`, `delete`,
    // ...; IEEE 1800-2023 7.5-7.12) of a container property: the node's
    // text is the method's name as written, the payload Owner::property.
    sv_container_method,
    sv_coverage_query_instance,   // payload: runtime identity
    sv_coverage_query_type,       // payload: declaration identity
    new_array,
    stream_left,
    stream_right,
    stream_with,
    stream_with_range,
    stream_with_plus,
    stream_with_minus,
    randomize_null,               // the null randomize() checker argument
    // SystemVerilog statement callees (Statement task marker).
    sv_task,                      // payload: Owner::method
    sv_static_task,               // payload: Owner::method
    sv_base_constructor,          // payload: Owner::new
    sv_object_srandom,
    sv_object_set_randstate,
    sv_container_push_back,       // payload: Owner::property
    sv_coverage_sample_procedural, // payload: covergroup identity
    sv_coverage_sample_explicit,  // payload: covergroup identity
    sv_coverage_sample_event,     // payload: covergroup identity
    sv_coverage_control_start,    // payload: covergroup identity
    sv_coverage_control_stop,     // payload: covergroup identity
    // Assignment-pattern and aggregate choices (choice marker).
    key,
    sv_inline_constraint,
    sv_inline_constraint_names,
    sv_process_self,              // std::process::self() (9.7)
    sv_process_state,             // payload: process::state value (9.7)
    sv_std_randomize,             // [std::]randomize() (18.12)
    // VHDL expressions.
    vhdl_dereference,
    vhdl_external,
    vhdl_formal_subelements,
    vhdl_inertial,
    vhdl_null,
    vhdl_new,
    vhdl_new_qualified,
    vhdl_case_range_to,
    vhdl_case_range_downto,
    vhdl_member,                  // payload: record element
    vhdl_physical,                // payload: unit name
    vhdl_qualified,               // payload: type mark
    vhdl_subtype,                 // payload: subtype spelling
    last = vhdl_subtype,
};

[[nodiscard]] constexpr bool expression_marker_valid(
    const ExpressionMarker marker) noexcept
{
    return static_cast<std::uint8_t>(marker)
        <= static_cast<std::uint8_t>(ExpressionMarker::last);
}

[[nodiscard]] constexpr bool sv_container_query(
    const ExpressionMarker marker) noexcept
{
    return marker >= ExpressionMarker::sv_container_pop_front
        && marker <= ExpressionMarker::sv_container_xor;
}

// Any built-in array method called on a class container property: the
// queries above, push_back, or another method whose name is the node's text.
[[nodiscard]] constexpr bool sv_container_method_family(
    const ExpressionMarker marker) noexcept
{
    return sv_container_query(marker)
        || marker == ExpressionMarker::sv_container_method
        || marker == ExpressionMarker::sv_container_push_back;
}

[[nodiscard]] constexpr bool vhdl_case_range(
    const ExpressionMarker marker) noexcept
{
    return marker == ExpressionMarker::vhdl_case_range_to
        || marker == ExpressionMarker::vhdl_case_range_downto;
}

// SystemVerilog markers of class operations bound to a class member.
[[nodiscard]] constexpr bool sv_class_member_operation(
    const ExpressionMarker marker) noexcept
{
    switch (marker) {
    case ExpressionMarker::sv_new_constructor:
    case ExpressionMarker::sv_dollar_cast_class:
    case ExpressionMarker::sv_property:
    case ExpressionMarker::sv_static_property:
    case ExpressionMarker::sv_base_method:
    case ExpressionMarker::sv_method:
    case ExpressionMarker::sv_static_method:
        return true;
    default:
        return false;
    }
}

} // namespace fsim::support
