// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

// The role of a hidden DPI import the parser generates for a unit's export
// service (IEEE 1800-2023 35.5.3, 35.9). The runtime serves these imports
// itself; no C code is loaded for them.
enum class DpiHiddenImportRole : std::uint8_t {
    // An import the user declared.
    none,
    // Serves the pending export call when the call chain is in the scope
    // the step names.
    serve,
    // Waits for an export call of the running context import, or for its C
    // code to return.
    next,
    // The foreign arm of a service loop: lets the steps of the other scopes
    // serve the pending export call.
    foreign,
    // Delivers the actuals of an export call to the export's wrapper.
    arguments,
    // Hands the result and output actuals of an export back to the C code.
    results,
    last = results,
};

// The direction of one formal of an export, as its wrapper's hidden imports
// pass it: `input` is read by the export from the C value, `output` is read
// by the C code from the export's value, `inout` is both.
enum class DpiExportFlow : std::uint8_t {
    input,
    output,
    inout,
    last = inout,
};

// The C type of one DPI formal, element or result (IEEE 1800-2023 H.7.4).
// A vector is a packed bit or logic value that passes as an svBitVecVal or
// svLogicVecVal array; a structure is an unpacked structure (H.7.8).
// `void_result` is the result of a void function and `task_status` the int
// a task returns for its disable status.
enum class DpiCTypeCode : std::uint8_t {
    none,
    void_result,
    task_status,
    byte_signed,
    byte_unsigned,
    short_signed,
    short_unsigned,
    int_signed,
    int_unsigned,
    long_signed,
    long_unsigned,
    chandle,
    real,
    shortreal,
    string,
    bit,
    logic,
    bit_vector,
    logic_vector,
    structure,
    last = structure,
};

// The short letter the DPI call service and the kernel's call planner
// switch on for a C type code: c C s S i I l L (byte, shortint, int and
// longint, signed or unsigned), p chandle, d real, f shortreal, z string,
// b svBit, g svLogic, B svBitVecVal vector, V svLogicVecVal vector, R
// structure, v void, T task status; 0 for `none`.
[[nodiscard]] constexpr char dpi_c_type_letter(const DpiCTypeCode code) noexcept
{
    switch (code) {
    case DpiCTypeCode::none: return 0;
    case DpiCTypeCode::void_result: return 'v';
    case DpiCTypeCode::task_status: return 'T';
    case DpiCTypeCode::byte_signed: return 'c';
    case DpiCTypeCode::byte_unsigned: return 'C';
    case DpiCTypeCode::short_signed: return 's';
    case DpiCTypeCode::short_unsigned: return 'S';
    case DpiCTypeCode::int_signed: return 'i';
    case DpiCTypeCode::int_unsigned: return 'I';
    case DpiCTypeCode::long_signed: return 'l';
    case DpiCTypeCode::long_unsigned: return 'L';
    case DpiCTypeCode::chandle: return 'p';
    case DpiCTypeCode::real: return 'd';
    case DpiCTypeCode::shortreal: return 'f';
    case DpiCTypeCode::string: return 'z';
    case DpiCTypeCode::bit: return 'b';
    case DpiCTypeCode::logic: return 'g';
    case DpiCTypeCode::bit_vector: return 'B';
    case DpiCTypeCode::logic_vector: return 'V';
    case DpiCTypeCode::structure: return 'R';
    }
    return 0;
}

// The unsigned counterpart of a signed byte, shortint, int or longint code;
// any other code is its own counterpart.
[[nodiscard]] constexpr DpiCTypeCode dpi_c_type_unsigned(
    const DpiCTypeCode code) noexcept
{
    switch (code) {
    case DpiCTypeCode::byte_signed: return DpiCTypeCode::byte_unsigned;
    case DpiCTypeCode::short_signed: return DpiCTypeCode::short_unsigned;
    case DpiCTypeCode::int_signed: return DpiCTypeCode::int_unsigned;
    case DpiCTypeCode::long_signed: return DpiCTypeCode::long_unsigned;
    default: return code;
    }
}

// The role of a callable the parser generates for a unit's export service.
enum class DpiExportCallableRole : std::uint8_t {
    // A callable of the design.
    none,
    // The wrapper that calls one export with the pending C call's actuals.
    call,
    // The loop that serves the export calls of a running context import.
    service,
    // Serves the pending export call if the call chain is in this scope.
    step,
    // Runs the step of this scope from a foreign scope's service.
    step_hook,
    // The hook a context import's call site lowers: it runs the service.
    hook,
    // The hook of a scope without exports of its own: the foreign arm only.
    hook_foreign,
    // The step of a generate block that declares exports.
    block_step,
    // The hook of a generate block that declares exports.
    block_hook,
    last = block_hook,
};

} // namespace fsim::support
