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
    // `get_inst_coverage()` and `get_coverage()` of a covergroup or of one
    // of its coverpoints or crosses (IEEE 1800-2023 19.8, 19.9); operand:
    // the item (0 is the covergroup, n its coverage declaration n - 1).
    sv_coverage_query_instance,   // payload: runtime identity
    sv_coverage_query_type,       // payload: declaration identity
    // A covergroup type option read (19.7.1); payload: declaration
    // identity, operands: the item and the option (coverage_options).
    sv_coverage_type_option,
    // A covergroup instance option read (IEEE 1800-2023 19.7); payload:
    // runtime identity, operands: the item and the option (coverage_options).
    sv_coverage_option,
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
    // A covergroup instance option assignment (19.7); payload: runtime
    // identity, arguments: the item, the option and the value. A
    // nonblocking assignment's statement is nonblocking (10.4.2).
    sv_coverage_option_assign,
    // A covergroup type option assignment (19.7.1); payload: declaration
    // identity, arguments as for sv_coverage_option_assign.
    sv_coverage_type_option_assign,
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
    // Generated concurrent-assertion monitor calls (IEEE 1800-2023 16.14,
    // 20.11) and the placeholder actions of their generated processes.
    sva_tick,
    sva_outcome,
    sva_request,
    sva_local,
    sva_answer,
    sva_call,
    sva_current,
    sva_assertion_control,
    sva_action_success,
    sva_action_failure,
    sva_action_vacuous,
    sva_action_abort,
    sva_action_success_off,
    sva_action_failure_off,
    sva_action_vacuous_off,
    sva_action_callregion,
    sva_action_sampled_on,
    sva_action_sampled_off,
    // Placeholders of a generated property evaluation, replaced after it is
    // parsed: the disable condition, and a guard (payload: its ordinal).
    sva_disable,
    sva_guard,

    // Constraints, randomization and constraint expressions.
    constraint_forms_begin_,
    // Inside ranges: `[low : high]`, `[low +/- amount]`, `[low +%- percent]`
    // (an operand of inside, of case inside, or the choice of a dist item).
    inside_range,
    inside_absolute_tolerance,
    inside_relative_tolerance,
    // Constraint items. A dist item carries its choice and its weight as
    // operands; the marker says whether the weight is `:=` or `:/`.
    constraint_dist_equal,
    constraint_dist_divide,
    constraint_disable_soft,
    constraint_implies,
    constraint_block,
    constraint_unique,
    constraint_solve_list,
    constraint_solve_before,
    constraint_if,
    constraint_foreach,
    // Built-in methods of a class object (IEEE 1800-2023 18.6, 18.8, 18.9,
    // 18.13.4): the call's first operand is the object.
    builtin_randomize,
    builtin_rand_mode,            // payload: the random variable, or the whole object
    builtin_constraint_mode,      // payload: the constraint block, or the whole object
    builtin_get_randstate,

    // Class, callable, built-in method and container forms.
    class_forms_begin_,
    vhdl_array_choice,            // an aggregate choice that is not a simple name
    sv_default_choice,            // the `default:` key of an assignment pattern (10.9)
    vhdl_others_choice,           // the `others` choice of an aggregate (9.3.3)
    sv_new_array,                 // the `new` of a dynamic array new[size] (7.5.1)

    // VHDL, configuration and design-unit forms.
    vhdl_unit_forms_begin_,

    // One past the last marker.
    end_,
};

// The value of an unbased unsized literal (`'0`, `'1`, `'x`, `'z`; IEEE
// 1800-2023 5.7.1), which sets every bit of its context's width.
enum class UnbasedUnsizedLiteral : std::uint8_t {
    none,
    zero,
    one,
    unknown,
    high_impedance,
};

[[nodiscard]] constexpr bool expression_marker_valid(
    const ExpressionMarker marker) noexcept
{
    return static_cast<std::uint8_t>(marker)
        < static_cast<std::uint8_t>(ExpressionMarker::end_);
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

// The generated assertion monitor calls: system functions of the
// generated assertion processes.
[[nodiscard]] constexpr bool sva_monitor_call(
    const ExpressionMarker marker) noexcept
{
    return marker >= ExpressionMarker::sva_tick
        && marker <= ExpressionMarker::sva_assertion_control;
}

// The placeholder actions of a generated assertion process.
[[nodiscard]] constexpr bool sva_action(const ExpressionMarker marker) noexcept
{
    return marker >= ExpressionMarker::sva_action_success
        && marker <= ExpressionMarker::sva_action_sampled_off;
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
