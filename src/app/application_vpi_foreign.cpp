// SPDX-License-Identifier: Apache-2.0
#include "application_vpi_foreign.hpp"

#include "application_dpi_foreign.hpp"
#include "application_foreign_signal.hpp"

#include "application_internal.hpp"
#include "application_vpi_class_model.hpp"
#include "application_vpi_frames.hpp"
#include "application_vpi_statements.hpp"
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/vpi_bridge.h"
#include "fsim/runtime/vpi_system.hpp"
#include "fsim/runtime/vpi_type_descriptor.hpp"
#include "fsim/support/index_distance.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/version.hpp"

#include "sv_vpi_user.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <set>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace fsim::app::application_detail {

namespace {

using runtime::Logic4;
using runtime::PackedLogic4;
using Routine = runtime::SystemVerilogVpiRoutineKind;
using runtime::SystemVerilogVpiObjectKind;

constexpr std::size_t maximum_startup_routines = 4096U;
constexpr std::size_t maximum_printed_bytes = 1U << 20U;

// Handles the service owns use a namespace the object registry never
// produces (both top bits set): 0xC0 | kind in bits 48-55 | serial.
enum class Tag : std::uint64_t {
    systf = 1,
    callback = 2,
    call = 3,
    argument = 4,
    iterator = 5,
    scheduled = 6,
    select = 7,
    constant = 8,
    range = 9,
    subarray = 10,
    modpath = 11,
    pathterm = 12,
    port = 13,
    reference = 14,
    typespec = 15,
    typespec_member = 16,
    enum_const = 17,
    // A process, statement, expression or continuous assignment of the
    // statement model (IEEE 1800-2023 37.43-37.79).
    node = 18,
    // A clocking block's default or a clockvar's skew as a delay control,
    // and a clocking block's event as an event control (IEEE 1800-2023
    // 37.48).
    clocking_skew = 19,
    clocking_event = 20,
    // A bit of a vector port (IEEE 1800-2023 37.14 vpiBit): the port's
    // index in the upper bits, the bit's position in the low 24.
    port_bit = 21,
    // A timing check and its reference or data terminal (IEEE 1800-2023
    // 37.40): the check's index, and for a terminal (index << 1) | data.
    tchk = 22,
    tchk_term = 23,
    // A class typespec, extends, method, parameter or typedef of the class
    // model (IEEE 1800-2023 37.31, 37.32).
    class_model = 24,
    // A live class object, and a property or constraint of one (IEEE
    // 1800-2023 37.33); the serial indexes the service's tables.
    class_object = 25,
    class_member = 26,
    // A frame and a thread of a run with the execution probes (IEEE
    // 1800-2023 37.43, 37.44); the serial names it in the frame model.
    frame = 27,
    thread = 28,
    // A gate, switch or UDP instance, an instance array of them, a prim term,
    // a UDP definition, one of its table entries and its initial statement
    // (IEEE 1800-2023 37.35, 37.36).
    primitive = 40,
    primitive_array = 41,
    prim_term = 42,
    udp_defn = 43,
    udp_entry = 44,
    udp_initial = 45,
    // An intermodule path, a delay device and a delay term that SDF
    // annotation created (IEEE 1800-2023 37.37, 37.45): the index of the
    // delay, and for a term (index << 1) | output.
    inter_mod_path = 50,
    delay_device = 51,
    delay_term = 52,
};
constexpr std::uint64_t serial_mask = (std::uint64_t { 1 } << 48U) - 1U;

[[nodiscard]] fsim_vpi_handle_v1 tagged(const Tag tag, const std::uint64_t serial)
{
    return (std::uint64_t { 0xC0 } << 56U)
        | (static_cast<std::uint64_t>(tag) << 48U) | (serial & serial_mask);
}

[[nodiscard]] bool owned(const fsim_vpi_handle_v1 handle)
{
    return (handle >> 56U) == 0xC0U;
}

[[nodiscard]] Tag tag_of(const fsim_vpi_handle_v1 handle)
{
    return static_cast<Tag>((handle >> 48U) & 0xFFU);
}

[[nodiscard]] std::uint64_t serial_of(const fsim_vpi_handle_v1 handle)
{
    return handle & serial_mask;
}

template <typename T>
[[nodiscard]] T* pointer(const std::uint64_t value)
{
    return reinterpret_cast<T*>(static_cast<std::uintptr_t>(value));
}

[[nodiscard]] std::uint64_t address(const void* value)
{
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
}

[[nodiscard]] fsim_vpi_status_v1 succeed(
    fsim_vpi_service_result_v1& result,
    const fsim_vpi_handle_v1 handle = 0U,
    const std::uint64_t value = 0U,
    const std::uint64_t user_data = 0U)
{
    result.status = FSIM_VPI_STATUS_OK;
    result.handle = handle;
    result.value = value;
    result.user_data = user_data;
    return FSIM_VPI_STATUS_OK;
}

// One thread-local call context per entry into foreign code; the standard
// routines a plug-in calls dispatch to the innermost one.
class Host {
public:
    virtual ~Host() = default;

    [[nodiscard]] virtual fsim_vpi_status_v1 invoke(Routine routine,
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result) = 0;

    static fsim_vpi_status_v1 FSIM_VPI_BRIDGE_CALL entry(void* const user_data,
        const std::uint32_t routine,
        const fsim_vpi_service_request_v1* const request,
        fsim_vpi_service_result_v1* const result)
    {
        if (user_data == nullptr || request == nullptr || result == nullptr) {
            return FSIM_VPI_STATUS_INVALID_ARGUMENT;
        }
        try {
            return static_cast<Host*>(user_data)->invoke(
                static_cast<Routine>(routine), *request, *result);
        } catch (...) {
            return FSIM_VPI_STATUS_INTERNAL_ERROR;
        }
    }

    // The VHPI service that answers calls too while a routine or callback
    // of a loaded library runs.
    ForeignCompanion companion;
};

class CallScope final {
public:
    explicit CallScope(Host& host, const bool with_companion = true)
        : host_ { host }
        , context_ {
            FSIM_VPI_CONTEXT_ABI_VERSION,
            static_cast<std::uint32_t>(sizeof(fsim_vpi_call_context_v1)),
            &host,
            &Host::entry,
        }
    {
        entered_ = fsim_vpi_call_context_enter_v1(&context_) == 0;
        if (entered_ && with_companion && host.companion.enter) {
            host.companion.enter();
            companion_ = true;
        }
    }
    ~CallScope()
    {
        if (companion_) {
            host_.companion.leave();
        }
        if (entered_) {
            (void)fsim_vpi_call_context_leave_v1(&context_);
        }
    }
    CallScope(const CallScope&) = delete;
    CallScope& operator=(const CallScope&) = delete;

    [[nodiscard]] bool entered() const noexcept { return entered_; }

private:
    Host& host_;
    fsim_vpi_call_context_v1 context_;
    bool entered_ { };
    bool companion_ { };
};

struct ModuleSpec {
    std::filesystem::path path;
    std::string bootstrap;
};

// `lib[:bootstrap]`; a library named without extension may be a `.vpi` or
// `.so` file. dlopen searches the loader path for a bare name, so an
// existing relative file is made absolute.
[[nodiscard]] ModuleSpec parse_module(std::string_view text)
{
    ModuleSpec spec;
    const auto colon = text.rfind(':');
    if (colon != std::string_view::npos && colon != 0U
        && text.substr(colon + 1U).find('/') == std::string_view::npos) {
        spec.bootstrap = std::string { text.substr(colon + 1U) };
        text = text.substr(0U, colon);
    }
    spec.path = std::filesystem::path { std::string { text } };
    std::error_code error;
    if (!support::native_fs::exists(spec.path, error)) {
        for (const auto* extension : { ".vpi", ".so" }) {
            auto candidate = spec.path;
            candidate += extension;
            if (support::native_fs::exists(candidate, error)) {
                spec.path = std::move(candidate);
                break;
            }
        }
    }
    if (support::native_fs::exists(spec.path, error)) {
        spec.path = std::filesystem::absolute(spec.path, error);
    }
    return spec;
}

// An endofcompile_routines[] entry runs once, after elaboration and before
// time 0 (IEEE 1364-2001 21.1.4, Annex F).
PLI_INT32 run_end_of_compile_routines(p_cb_data data)
{
    auto* const routines
        = reinterpret_cast<PLI_INT32 (**)()>(data->user_data);
    for (std::size_t index = 0U;
         index < maximum_startup_routines && routines[index] != nullptr;
         ++index) {
        (void)routines[index]();
    }
    return 0;
}

// Run a module's startup routines (vlog_startup_routines, IEEE 1800-2023
// 36.9.1) or its named bootstrap function inside the host's call context. A
// PLI 1.0 library may instead export veriusertfs[] and
// endofcompile_routines[] (IEEE 1364-2001 21.2 leaves the association to
// the vendor; Annex F declares endofcompile_routines): each table cell is
// registered as the user task or function it describes.
[[nodiscard]] std::unique_ptr<platform::DynamicLibrary> load_module(
    const ModuleSpec& spec, Host& host, std::string& error)
{
    // Global symbols let DPI imports resolve to functions a VPI module defines.
    auto library = platform::DynamicLibrary::open(spec.path, error, true);
    if (!library) {
        return nullptr;
    }
    CallScope scope { host };
    if (!scope.entered()) {
        error = "VPI call context depth exceeded";
        return nullptr;
    }
    if (!spec.bootstrap.empty()) {
        auto* const symbol = library->symbol(spec.bootstrap, error);
        if (symbol == nullptr) {
            return nullptr;
        }
        reinterpret_cast<void (*)()>(symbol)();
        return library;
    }
    std::string table_error;
    std::string end_error;
    auto* const startup = library->symbol("vlog_startup_routines", error);
    auto* const table = library->symbol("veriusertfs", table_error);
    auto* const end_of_compile
        = library->symbol("endofcompile_routines", end_error);
    if (startup == nullptr && table == nullptr && end_of_compile == nullptr) {
        return nullptr;
    }
    error.clear();
    if (startup != nullptr) {
        auto* const routines = static_cast<void (**)()>(startup);
        for (std::size_t index = 0U;
             index < maximum_startup_routines && routines[index] != nullptr;
             ++index) {
            routines[index]();
        }
    }
    if (table != nullptr) {
        auto* const registrar = platform::DynamicLibrary::default_symbol(
            "veriusertfs_register_table");
        if (registrar == nullptr) {
            error = "veriusertfs_register_table is not available";
            return nullptr;
        }
        reinterpret_cast<void (*)(void*)>(registrar)(table);
    }
    if (end_of_compile != nullptr) {
        s_cb_data callback {};
        callback.reason = cbEndOfCompile;
        callback.cb_rtn = &run_end_of_compile_routines;
        callback.user_data = static_cast<PLI_BYTE8*>(end_of_compile);
        (void)vpi_register_cb(&callback);
    }
    return library;
}

// The result width and signedness of a registered system function
// (IEEE 1800-2023 38.36.1 vpiSysFuncType).
void system_function_shape(const s_vpi_systf_data& data,
    std::uint32_t& width, bool& is_signed)
{
    width = 32U;
    is_signed = false;
    switch (data.sysfunctype) {
    case vpiIntFunc:
        is_signed = true;
        break;
    case vpiRealFunc:
    case vpiTimeFunc:
        width = 64U;
        break;
    case vpiSizedFunc:
    case vpiSizedSignedFunc:
        if (data.sizetf != nullptr) {
            const auto size = data.sizetf(data.user_data);
            width = size > 0 ? static_cast<std::uint32_t>(size) : 32U;
        }
        is_signed = data.sysfunctype == vpiSizedSignedFunc;
        break;
    default:
        break;
    }
}

// The elaboration pass: only registrations take effect; output is
// suppressed and every query fails as no design exists yet.
class RegistrationHost final : public Host {
public:
    std::vector<elaboration::UserSystemTask> tasks;
    // cbUnresolvedSystf callbacks; the others never run at elaboration.
    std::vector<s_cb_data> unresolved;

    fsim_vpi_status_v1 invoke(const Routine routine,
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result) override
    {
        if (request.flags != 0U) {
            return FSIM_VPI_STATUS_UNSUPPORTED;
        }
        switch (routine) {
        case Routine::RegisterSystemTaskFunction: {
            const auto* const data = pointer<s_vpi_systf_data>(request.user_data);
            if (data == nullptr || data->tfname == nullptr
                || data->tfname[0] != '$') {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            elaboration::UserSystemTask task;
            task.name = data->tfname;
            task.function = data->type == vpiSysFunc;
            if (task.function) {
                system_function_shape(*data, task.width, task.is_signed);
            }
            tasks.push_back(std::move(task));
            return succeed(result, tagged(Tag::systf, tasks.size()));
        }
        case Routine::RegisterCallback: {
            const auto* const data = pointer<s_cb_data>(request.user_data);
            if (data != nullptr && data->reason == cbUnresolvedSystf
                && data->cb_rtn != nullptr) {
                unresolved.push_back(*data);
            }
            return succeed(result, tagged(Tag::callback, ++callbacks_));
        }
        case Routine::Print:
        case Routine::VariablePrint:
        case Routine::McdPrint:
        case Routine::McdVariablePrint:
        case Routine::Flush:
        case Routine::McdFlush:
            return succeed(result);
        case Routine::GetVlogInfo: {
            auto* const info = pointer<s_vpi_vlog_info>(request.user_data);
            if (info == nullptr) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            info->argc = 1;
            info->argv = argv_.data();
            info->product = product_.data();
            info->version = version_.data();
            return succeed(result, 0U, 1U);
        }
        default:
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
    }

private:
    std::uint64_t callbacks_ { };
    std::string program_ { "fsim" };
    std::array<char*, 2> argv_ { program_.data(), nullptr };
    std::string product_ { "FSIM" };
    std::string version_ { fsim::version };
};

// --- Value conversion (IEEE 1800-2023 38.15 vpi_get_value) -----------------

struct Value {
    enum class Kind { bits, real, string } kind { Kind::bits };
    PackedLogic4 bits;
    double real { };
    std::string text;
    bool is_signed { };
};

[[nodiscard]] char logic_char(const Logic4 value)
{
    switch (value) {
    case Logic4::zero:
        return '0';
    case Logic4::one:
        return '1';
    case Logic4::x:
        return 'x';
    case Logic4::z:
        return 'z';
    }
    return 'x';
}

[[nodiscard]] PackedLogic4 resized(const PackedLogic4& value,
    const std::size_t width, const bool sign_extend)
{
    PackedLogic4 result { width, Logic4::zero };
    const auto fill = sign_extend && value.width() != 0U
        ? value.get(value.width() - 1U)
        : Logic4::zero;
    for (std::size_t bit = 0U; bit < width; ++bit) {
        result.set(bit, bit < value.width() ? value.get(bit) : fill);
    }
    return result;
}

[[nodiscard]] bool known(const PackedLogic4& value)
{
    for (std::size_t bit = 0U; bit < value.width(); ++bit) {
        const auto state = value.get(bit);
        if (state != Logic4::zero && state != Logic4::one) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] PackedLogic4 integer_bits(
    const std::uint64_t value, const std::size_t width)
{
    PackedLogic4 result { width, Logic4::zero };
    for (std::size_t bit = 0U; bit < width; ++bit) {
        result.set(bit,
            bit < 64U && ((value >> bit) & 1U) != 0U ? Logic4::one
                                                     : Logic4::zero);
    }
    return result;
}

[[nodiscard]] std::uint64_t low_bits(const PackedLogic4& value)
{
    std::uint64_t result { };
    for (std::size_t bit = 0U; bit < value.width() && bit < 64U; ++bit) {
        if (value.get(bit) == Logic4::one) {
            result |= std::uint64_t { 1 } << bit;
        }
    }
    return result;
}

// A real-typed value carried as IEEE 754 bits: binary64, or binary32 for a
// 32-bit shortreal.
[[nodiscard]] double real_bits(const PackedLogic4& value)
{
    const auto bits = low_bits(value);
    return value.width() == 32U
        ? static_cast<double>(std::bit_cast<float>(
              static_cast<std::uint32_t>(bits)))
        : std::bit_cast<double>(bits);
}

[[nodiscard]] std::int64_t signed_bits(const PackedLogic4& value,
    const bool is_signed)
{
    auto bits = low_bits(value);
    const auto width = value.width();
    if (is_signed && width != 0U && width < 64U
        && value.get(width - 1U) == Logic4::one) {
        bits |= ~std::uint64_t { 0 } << width;
    }
    return static_cast<std::int64_t>(bits);
}

[[nodiscard]] std::string binary_string(const PackedLogic4& value)
{
    std::string text;
    text.reserve(value.width());
    for (std::size_t bit = value.width(); bit-- > 0U;) {
        text.push_back(logic_char(value.get(bit)));
    }
    return text;
}

// One octal or hexadecimal digit stands for x or z when all its bits do, and
// for X or Z when only some do (IEEE 1800-2023 21.2.1.3).
[[nodiscard]] std::string radix_string(
    const PackedLogic4& value, const std::size_t digit_bits)
{
    const auto width = value.width();
    const auto digits = (width + digit_bits - 1U) / digit_bits;
    std::string text;
    for (std::size_t digit = digits; digit-- > 0U;) {
        unsigned number { };
        std::size_t x_bits { };
        std::size_t z_bits { };
        std::size_t present { };
        for (std::size_t offset = 0U; offset < digit_bits; ++offset) {
            const auto bit = digit * digit_bits + offset;
            if (bit >= width) {
                continue;
            }
            ++present;
            switch (value.get(bit)) {
            case Logic4::one:
                number |= 1U << offset;
                break;
            case Logic4::x:
                ++x_bits;
                break;
            case Logic4::z:
                ++z_bits;
                break;
            case Logic4::zero:
                break;
            }
        }
        if (x_bits == present) {
            text.push_back('x');
        } else if (z_bits == present) {
            text.push_back('z');
        } else if (x_bits != 0U) {
            text.push_back('X');
        } else if (z_bits != 0U) {
            text.push_back('Z');
        } else {
            text.push_back("0123456789abcdef"[number]);
        }
    }
    return text;
}

[[nodiscard]] std::string decimal_string(
    const PackedLogic4& value, const bool is_signed)
{
    const auto width = value.width();
    if (!known(value)) {
        std::size_t x_bits { };
        std::size_t z_bits { };
        for (std::size_t bit = 0U; bit < width; ++bit) {
            x_bits += value.get(bit) == Logic4::x ? 1U : 0U;
            z_bits += value.get(bit) == Logic4::z ? 1U : 0U;
        }
        return x_bits == width ? "x"
            : z_bits == width  ? "z"
            : x_bits != 0U     ? "X"
                               : "Z";
    }
    std::vector<std::uint32_t> limbs((width + 31U) / 32U + 1U, 0U);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        if (value.get(bit) == Logic4::one) {
            limbs[bit / 32U] |= 1U << (bit % 32U);
        }
    }
    const bool negative = is_signed && width != 0U
        && value.get(width - 1U) == Logic4::one;
    if (negative) {
        // Two's complement magnitude of the width-bit value.
        for (std::size_t bit = width; bit < limbs.size() * 32U; ++bit) {
            limbs[bit / 32U] |= 1U << (bit % 32U);
        }
        std::uint64_t carry = 1U;
        for (auto& limb : limbs) {
            const auto sum = static_cast<std::uint64_t>(~limb) + carry;
            limb = static_cast<std::uint32_t>(sum);
            carry = sum >> 32U;
        }
    }
    std::string digits;
    bool nonzero = true;
    while (nonzero) {
        std::uint64_t remainder { };
        nonzero = false;
        for (std::size_t index = limbs.size(); index-- > 0U;) {
            const auto current = (remainder << 32U) | limbs[index];
            limbs[index] = static_cast<std::uint32_t>(current / 10U);
            remainder = current % 10U;
            nonzero = nonzero || limbs[index] != 0U;
        }
        digits.push_back(static_cast<char>('0' + remainder));
    }
    if (negative) {
        digits.push_back('-');
    }
    return { digits.rbegin(), digits.rend() };
}

[[nodiscard]] std::string bytes_string(const PackedLogic4& value)
{
    std::string text;
    const auto bytes = (value.width() + 7U) / 8U;
    for (std::size_t byte = bytes; byte-- > 0U;) {
        unsigned character { };
        for (std::size_t bit = 0U; bit < 8U; ++bit) {
            const auto index = byte * 8U + bit;
            if (index < value.width() && value.get(index) == Logic4::one) {
                character |= 1U << bit;
            }
        }
        if (character != 0U) {
            text.push_back(static_cast<char>(character));
        }
    }
    return text;
}

[[nodiscard]] PackedLogic4 string_bits(
    const std::string_view text, const std::size_t width)
{
    PackedLogic4 result { width, Logic4::zero };
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const auto character
            = static_cast<unsigned char>(text[text.size() - 1U - index]);
        for (std::size_t bit = 0U; bit < 8U; ++bit) {
            const auto position = index * 8U + bit;
            if (position < width && ((character >> bit) & 1U) != 0U) {
                result.set(position, Logic4::one);
            }
        }
    }
    return result;
}

[[nodiscard]] std::optional<Logic4> digit_state(const char digit)
{
    switch (digit) {
    case 'x':
    case 'X':
        return Logic4::x;
    case 'z':
    case 'Z':
    case '?':
        return Logic4::z;
    default:
        return std::nullopt;
    }
}

// A binary, octal or hexadecimal digit string, least significant digit last;
// a shorter string is zero extended, or x/z extended from an x/z digit.
[[nodiscard]] PackedLogic4 parse_radix(const std::string_view text,
    const std::size_t digit_bits, const std::size_t width)
{
    PackedLogic4 result { width, Logic4::zero };
    std::size_t position { };
    Logic4 extension = Logic4::zero;
    for (std::size_t index = text.size(); index-- > 0U;) {
        const auto digit = text[index];
        if (digit == '_') {
            continue;
        }
        unsigned number { };
        auto state = digit_state(digit);
        if (!state) {
            const auto lowered = static_cast<char>(
                digit >= 'A' && digit <= 'F' ? digit - 'A' + 'a' : digit);
            number = lowered >= 'a' ? static_cast<unsigned>(lowered - 'a' + 10)
                                    : static_cast<unsigned>(lowered - '0');
        }
        for (std::size_t bit = 0U; bit < digit_bits; ++bit, ++position) {
            if (position < width) {
                result.set(position,
                    state ? *state
                          : ((number >> bit) & 1U) != 0U ? Logic4::one
                                                         : Logic4::zero);
            }
        }
        extension = state ? *state : Logic4::zero;
    }
    for (; position < width; ++position) {
        result.set(position, extension);
    }
    return result;
}

[[nodiscard]] PackedLogic4 parse_decimal(
    std::string_view text, const std::size_t width)
{
    if (text.size() == 1U) {
        if (const auto state = digit_state(text.front())) {
            return PackedLogic4 { width, *state };
        }
    }
    const bool negative = !text.empty() && text.front() == '-';
    if (negative || (!text.empty() && text.front() == '+')) {
        text.remove_prefix(1U);
    }
    std::vector<std::uint32_t> limbs((width + 31U) / 32U + 1U, 0U);
    for (const auto digit : text) {
        if (digit < '0' || digit > '9') {
            continue;
        }
        std::uint64_t carry = static_cast<std::uint64_t>(digit - '0');
        for (auto& limb : limbs) {
            const auto product = static_cast<std::uint64_t>(limb) * 10U + carry;
            limb = static_cast<std::uint32_t>(product);
            carry = product >> 32U;
        }
    }
    if (negative) {
        std::uint64_t carry = 1U;
        for (auto& limb : limbs) {
            const auto sum = static_cast<std::uint64_t>(~limb) + carry;
            limb = static_cast<std::uint32_t>(sum);
            carry = sum >> 32U;
        }
    }
    PackedLogic4 result { width, Logic4::zero };
    for (std::size_t bit = 0U; bit < width; ++bit) {
        if (((limbs[bit / 32U] >> (bit % 32U)) & 1U) != 0U) {
            result.set(bit, Logic4::one);
        }
    }
    return result;
}

[[nodiscard]] std::optional<Value> stored_to_value(
    const runtime::SystemVerilogVpiStoredValue& stored, const bool is_signed)
{
    Value value;
    value.is_signed = is_signed;
    if (const auto* bits = std::get_if<runtime::PackedBit2>(&stored.payload)) {
        value.bits = PackedLogic4 { bits->width(), Logic4::zero };
        for (std::size_t bit = 0U; bit < bits->width(); ++bit) {
            value.bits.set(bit, bits->get(bit) ? Logic4::one : Logic4::zero);
        }
    } else if (const auto* logic
        = std::get_if<PackedLogic4>(&stored.payload)) {
        value.bits = logic->is_logic9() ? runtime::collapse_to_logic4(*logic)
                                        : *logic;
    } else if (const auto* logic9
        = std::get_if<runtime::PackedLogic9>(&stored.payload)) {
        value.bits = runtime::collapse_to_logic4(*logic9);
    } else if (const auto* real = std::get_if<double>(&stored.payload)) {
        value.kind = Value::Kind::real;
        value.real = *real;
    } else if (const auto* short_real = std::get_if<float>(&stored.payload)) {
        value.kind = Value::Kind::real;
        value.real = *short_real;
    } else if (const auto* text = std::get_if<std::string>(&stored.payload)) {
        value.kind = Value::Kind::string;
        value.text = *text;
    } else if (const auto* time
        = std::get_if<std::uint64_t>(&stored.payload)) {
        value.bits = integer_bits(*time, 64U);
    } else {
        return std::nullopt;
    }
    return value;
}

// The bits of a value as an integral quantity: a real rounds to the nearest
// integer and a string packs its characters (IEEE 1800-2023 6.12.1, 6.16).
[[nodiscard]] PackedLogic4 value_bits(const Value& value, const std::size_t width)
{
    switch (value.kind) {
    case Value::Kind::bits:
        return resized(value.bits, width, value.is_signed);
    case Value::Kind::real:
        return integer_bits(static_cast<std::uint64_t>(
                                static_cast<std::int64_t>(std::llround(value.real))),
            width);
    case Value::Kind::string:
        return string_bits(value.text, width);
    }
    return PackedLogic4 { width, Logic4::x };
}

[[nodiscard]] std::size_t natural_width(const Value& value)
{
    switch (value.kind) {
    case Value::Kind::bits:
        return value.bits.width();
    case Value::Kind::real:
        return 64U;
    case Value::Kind::string:
        return value.text.size() * 8U;
    }
    return 0U;
}

class LiveService final : public Host, public VpiForeignService {
public:
    LiveService(Simulation& simulation, PrintSink print,
        std::span<const std::string> arguments)
        : simulation_ { simulation }
        , registry_ { simulation.systemverilog_vpi_objects() }
        , print_ { std::move(print) }
    {
        argv_storage_.emplace_back("fsim");
        for (const auto& argument : arguments) {
            argv_storage_.push_back(argument);
        }
        for (auto& argument : argv_storage_) {
            argv_.push_back(argument.data());
        }
        argv_.push_back(nullptr);
    }

    ~LiveService() override
    {
        if (path_delay_hook_ != 0U) {
            simulation_.remove_safe_point_hook(path_delay_hook_);
        }
        if (signal_hook_ != 0U) {
            simulation_.remove_safe_point_hook(signal_hook_);
        }
        if (!tchk_callbacks_.empty()) {
            simulation_.set_verilog_timing_violation_hook({ });
        }
        if (force_hook_ != 0U) {
            simulation_.remove_force_statement_hook(force_hook_);
            simulation_.remove_safe_point_hook(force_delivery_hook_);
        }
        if (statement_hook_ != 0U) {
            simulation_.remove_execution_point_hook(statement_hook_);
        }
        if (disable_hook_ != 0U) {
            simulation_.remove_safe_point_hook(disable_hook_);
        }
        if (size_hooked_) {
            simulation_.set_vpi_size_change_hook({ });
        }
        if (reclaim_hooked_) {
            simulation_.set_class_reclaim_hook({ });
        }
        if (frame_hook_ != 0U) {
            simulation_.remove_execution_point_hook(frame_hook_);
        }
        if (create_hooked_) {
            simulation_.set_class_created_hook({ });
        }
        // The simulation may outlive the service; no foreign routine may
        // run after it is gone.
        for (const auto& [serial, callback] : callbacks_) {
            if (callback->managed) {
                (void)simulation_.systemverilog_vpi_callbacks()
                    .remove_callback(*callback->managed);
            }
        }
        for (const auto& [serial, callback] : assertion_callbacks_) {
            if (callback.managed) {
                (void)simulation_.systemverilog_vpi_callbacks()
                    .remove_callback(*callback.managed);
            }
        }
        for (const auto& managed : assertion_system_hooks_) {
            (void)simulation_.systemverilog_vpi_callbacks().remove_callback(
                managed);
        }
    }
    LiveService(const LiveService&) = delete;
    LiveService& operator=(const LiveService&) = delete;

    [[nodiscard]] bool load(const std::span<const std::string> modules,
        diagnostic::Engine& diagnostics)
    {
        for (const auto& module : modules) {
            const auto spec = parse_module(module);
            std::string error;
            auto library = load_module(spec, *this, error);
            if (!library) {
                diagnostics.error("FSIM-VPI-LOAD-001",
                    "cannot load VPI module '" + module + "': " + error);
                return false;
            }
            libraries_.push_back(std::move(library));
        }
        return true;
    }

    // Each call site's compiletf runs once before simulation, then the
    // end-of-compile callbacks (IEEE 1800-2023 38.36.2, 38.36.3).
    void compile(const std::size_t process_count)
    {
        // A probe run tracks its frames and threads from the start, so a
        // routine may ask for them at any time (IEEE 1800-2023 37.43,
        // 37.44).
        if (simulation_.foreign_probes()) {
            ensure_frame_hook();
        }
        // Call sites exist for their compiletf routines here; a call creates
        // its site anyway.
        const bool compiletf = std::ranges::any_of(systfs_,
            [](const auto& systf) { return systf.data.compiletf != nullptr; });
        for (runtime::simir::ProcessId process = 0U;
             compiletf && process < process_count; ++process) {
            const auto& program = simulation_.process_program(process);
            for (std::size_t index = 0U; index < program.operations.size();
                 ++index) {
                const auto* const call = runtime::simir::operation_get_if<
                    runtime::simir::ClassStaticMethodCall>(
                    &program.operations[index]);
                if (call == nullptr) {
                    continue;
                }
                auto identity = std::string_view { call->method_identity };
                if (!identity.starts_with(
                        runtime::simir::systemverilog_dpi_function_prefix)) {
                    continue;
                }
                identity.remove_prefix(
                    runtime::simir::systemverilog_dpi_function_prefix.size());
                if (identity.starts_with(
                        elaboration::user_system_call_identity_prefix)) {
                    (void)call_site(identity, call->actual_names);
                }
            }
        }
        for (std::size_t site = 0U; site < calls_.size(); ++site) {
            const auto& systf = systfs_[calls_[site].systf];
            if (systf.data.compiletf == nullptr) {
                continue;
            }
            call_stack_.push_back(site);
            {
                CallScope scope { *this };
                systf.data.compiletf(systf.data.user_data);
            }
            call_stack_.pop_back();
        }
        const auto end_of_compile = std::move(end_of_compile_);
        end_of_compile_.clear();
        for (const auto serial : end_of_compile) {
            fire(serial, std::nullopt);
        }
    }

    [[nodiscard]] PackedLogic4 call(const std::string_view identity,
        const std::vector<PackedLogic4>& actuals,
        const std::span<const std::string> names)
    {
        const auto site_index = call_site(identity, names);
        auto& site = calls_[site_index];
        for (std::size_t index = 0U;
             index < site.arguments.size() && index < actuals.size();
             ++index) {
            auto& argument = site.arguments[index];
            if (argument.expression && argument.real) {
                argument.value.kind = Value::Kind::real;
                argument.value.real = real_bits(actuals[index]);
            } else if (argument.expression) {
                argument.value.kind = Value::Kind::bits;
                argument.value.bits = actuals[index];
            }
        }
        const auto& systf = systfs_[site.systf];
        const auto saved_finish = finish_requested_;
        finish_requested_ = false;
        call_stack_.push_back(site_index);
        if (systf.data.calltf != nullptr) {
            CallScope scope { *this };
            systf.data.calltf(systf.data.user_data);
        }
        call_stack_.pop_back();
        const auto finish = finish_requested_;
        finish_requested_ = saved_finish;
        auto& completed = calls_[site_index];
        if (completed.function) {
            return value_bits(completed.result, completed.width);
        }
        if (finish && !completed.function) {
            return PackedLogic4 { 1U, Logic4::one };
        }
        return PackedLogic4 { 1U, Logic4::zero };
    }

    // The phases the save/restart driver and a reset go through (IEEE
    // 1800-2023 38.36.3). Without a driver none of them occurs.
    void lifecycle(const Simulation::LifecycleEvent event,
        const std::string_view location)
    {
        switch (event) {
        case Simulation::LifecycleEvent::start_of_save:
            save_phase_ = SavePhase::save;
            save_location_ = std::string { location };
            save_data_.clear();
            deliver_feature(cbStartOfSave);
            break;
        case Simulation::LifecycleEvent::end_of_save:
            deliver_feature(cbEndOfSave);
            save_phase_ = SavePhase::none;
            save_location_.clear();
            break;
        // Only the restart callbacks exist after a restart, and the user
        // data of the call handles is gone (38.36.3, 38.14).
        case Simulation::LifecycleEvent::start_of_restart:
            drop_callbacks_for_restart();
            user_data_.clear();
            save_phase_ = SavePhase::restart;
            save_location_ = std::string { location };
            deliver_feature(cbStartOfRestart);
            break;
        case Simulation::LifecycleEvent::end_of_restart:
            deliver_feature(cbEndOfRestart);
            save_phase_ = SavePhase::none;
            save_location_.clear();
            save_data_.clear();
            break;
        case Simulation::LifecycleEvent::start_of_reset:
            deliver_feature(cbStartOfReset);
            break;
        // The time and the values are back at zero; the user data is gone
        // and may be set again from here on (38.14).
        case Simulation::LifecycleEvent::end_of_reset:
            user_data_.clear();
            deliver_feature(cbEndOfReset);
            break;
        }
    }

    [[nodiscard]] std::map<std::int32_t, std::string> saved_data()
        const override
    {
        std::map<std::int32_t, std::string> data;
        for (const auto& [id, saved] : save_data_) {
            data.emplace(id, saved.bytes);
        }
        return data;
    }

    // The data sets of a restart, read from the start; later ids continue
    // after the restored ones.
    void restore_data(std::map<std::int32_t, std::string> data) override
    {
        save_data_.clear();
        for (auto& [id, bytes] : data) {
            save_data_[id].bytes = std::move(bytes);
            last_save_restart_id_ = std::max(last_save_restart_id_, id);
        }
    }

    fsim_vpi_status_v1 invoke(const Routine routine,
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result) override
    {
        // vpi_chk_error reports the previous routine's error, and any other
        // routine clears it (IEEE 1800-2023 38.2).
        if (routine != Routine::CheckError) {
            error_ = false;
        }
        const auto status = route(routine, request, result);
        if (status != FSIM_VPI_STATUS_OK && routine != Routine::CheckError
            && !error_) {
            const auto* const descriptor
                = runtime::find_systemverilog_vpi_2023_routine(routine);
            error_message_ = std::string { descriptor != nullptr
                                     ? descriptor->name
                                     : std::string_view { "VPI routine" } }
                + (status == FSIM_VPI_STATUS_UNSUPPORTED
                        ? ": the request is not supported"
                        : ": the request failed");
            error_ = true;
        }
        if (status != FSIM_VPI_STATUS_OK && routine != Routine::CheckError) {
            pli_error();
        }
        return status;
    }

    // A run-time error ends the simulation: the cbError callbacks run
    // first, and vpi_chk_error reports it to them (IEEE 1800-2023
    // 38.36.3).
    [[nodiscard]] ForeignCompanion calls() override
    {
        return {
            [this] {
                companion_calls_.push_back(
                    std::make_unique<CallScope>(*this, false));
            },
            [this] {
                if (!companion_calls_.empty()) {
                    companion_calls_.pop_back();
                }
            },
        };
    }

    void set_companion(ForeignCompanion companion_service) override
    {
        companion = std::move(companion_service);
    }

    void run_error(const std::string_view message) noexcept override
    {
        try {
            error_message_ = message;
            error_ = true;
            const auto listeners = run_error_callbacks_;
            for (const auto serial : listeners) {
                error_message_ = message;
                error_ = true;
                fire(serial, std::nullopt, true);
            }
        } catch (...) {
        }
    }

private:
    // The cbUnresolvedSystf callbacks get the name in user_data
    // (IEEE 1800-2023 38.36.3); true when there were any.
    [[nodiscard]] bool resolve(const std::string& name)
    {
        std::string text { name };
        const auto listeners = unresolved_callbacks_;
        for (const auto serial : listeners) {
            const auto found = callbacks_.find(serial);
            if (found == callbacks_.end()) {
                continue;
            }
            auto data = found->second->data;
            data.obj = nullptr;
            data.time = nullptr;
            data.value = nullptr;
            data.user_data = text.data();
            CallScope scope { *this };
            (void)data.cb_rtn(&data);
        }
        return !listeners.empty();
    }

    void deliver_signals()
    {
        std::vector<std::uint64_t> arrived;
        for (const auto& [serial, trap] : signal_traps_) {
            if (trap->take()) {
                arrived.push_back(serial);
            }
        }
        for (const auto serial : arrived) {
            if (signal_traps_.contains(serial)) {
                fire(serial, std::nullopt, true);
            }
        }
    }

    void ensure_force_hook()
    {
        if (force_hook_ != 0U) {
            return;
        }
        force_hook_ = simulation_.add_force_statement_hook(
            [this](const runtime::simir::ProcessId process,
                const runtime::simir::SignalId signal, const std::size_t offset,
                const std::size_t width, const bool release) {
                pending_forces_.push_back(
                    ForceEvent { process, signal, offset, width, release });
            });
        force_delivery_hook_ = simulation_.add_safe_point_hook(
            [this](runtime::Scheduler&, runtime::SchedulerPhase) {
                deliver_forces();
            });
    }

    // Each force, release, assign or deassign a process executed since the
    // last safe point reaches the callbacks of its reason with the
    // statement as obj and the resulting value of its target (IEEE
    // 1800-2023 38.36.1).
    void deliver_forces()
    {
        if (pending_forces_.empty()) {
            return;
        }
        auto events = std::exchange(pending_forces_, { });
        for (const auto& event : events) {
            if (force_callbacks_.empty()) {
                return;
            }
            const auto path = simulation_.signal_path(event.signal);
            if (!path) {
                continue;
            }
            const auto target = registry_.find(*path);
            if (!target || !target.value) {
                continue;
            }
            const auto object = target.value->handle;
            // The statement is the executing process's own, or one of a
            // task or function it called. The processes that keep a forced
            // or assigned value applied have no source and no statement.
            const auto source = source_process(event.process);
            if (!source) {
                continue;
            }
            const auto& [scope, process] = *source;
            auto sites = statement_model().force_sites(scope, event.release,
                [&](const fsim_vpi_handle_v1 candidate) {
                    return candidate == object;
                });
            auto own = std::ranges::find(
                sites, process, &VpiStatementModel::ForceSite::process);
            if (own == sites.end()) {
                own = std::ranges::find(
                    sites, 0U, &VpiStatementModel::ForceSite::process);
            }
            if (own == sites.end()) {
                continue;
            }
            const auto site = *own;
            // The resulting value of the forced bits of the target.
            std::optional<Value> value = object_value(object);
            if (value && value->kind == Value::Kind::bits
                && event.width < value->bits.width()) {
                PackedLogic4 bits { event.width, Logic4::x };
                for (std::size_t bit = 0U; bit < event.width; ++bit) {
                    if (event.offset + bit < value->bits.width()) {
                        bits.set(bit, value->bits.get(event.offset + bit));
                    }
                }
                value->bits = std::move(bits);
            }
            const auto listeners = force_callbacks_;
            for (const auto serial : listeners) {
                const auto found = callbacks_.find(serial);
                if (found == callbacks_.end()
                    || found->second->data.reason != site.reason) {
                    continue;
                }
                const auto watched = resolve(internal(found->second->data.obj));
                if (watched != 0U && watched != object) {
                    continue;
                }
                fire_statement(serial, site.statement, value);
            }
        }
    }

    // The HIR scope of a running design process's instance, and the HIR
    // process it was elaborated from; none for a process without source.
    [[nodiscard]] std::optional<std::pair<VpiHirScope, std::uint32_t>>
    source_process(const runtime::simir::ProcessId running)
    {
        const auto& design = simulation_.design_ir();
        const auto occurrence = std::ranges::find(design.processes(), running,
            &semantic::design::ProcessOccurrence::runtime_index);
        if (occurrence == design.processes().end()
            || !occurrence->source_process) {
            return std::nullopt;
        }
        const auto& specialization = design.specializations().at(
            occurrence->specialization.value());
        const auto instance_path = std::string { design.path(
            design.instances().at(specialization.instance.value()).path) };
        const auto instance = registry_.find(instance_path);
        auto scope = instance && instance.value
            ? hir_scope(instance.value->handle)
            : std::nullopt;
        if (!scope) {
            return std::nullopt;
        }
        return std::pair { std::move(*scope),
            static_cast<std::uint32_t>(occurrence->source_process->value()) };
    }

    // The statement whose execution point runs a cbStmt callback on
    // `object`: the statement itself, or a block's first statement, before
    // which the block's callback runs (IEEE 1800-2023 Table 38-6). 0 for an
    // empty block.
    [[nodiscard]] fsim_vpi_handle_v1 statement_trigger(
        fsim_vpi_handle_v1 object)
    {
        for (std::size_t depth = 0U; depth < 1024U; ++depth) {
            const auto type = handle_type(object);
            std::vector<fsim_vpi_handle_v1> inner;
            if (const auto serial = node_of(object)) {
                if (type != vpiBegin && type != vpiFork) {
                    return object;
                }
                inner = statement_model_->iterate(*serial, vpiStmt)
                            .value_or(inner);
            } else if (const auto scope = hir_scope(object)) {
                inner = statement_model()
                            .iterate_scope(*scope, vpiStmt)
                            .value_or(inner);
            }
            if (inner.empty()) {
                return 0U;
            }
            object = inner.front();
        }
        return 0U;
    }

    // cbDisable callbacks on a disabled block, task or function run after
    // its disable statement has executed.
    void deliver_disables()
    {
        const auto targets = std::exchange(pending_disables_, { });
        for (const auto target : targets) {
            const auto listeners = disable_callbacks_;
            for (const auto serial : listeners) {
                const auto found = callbacks_.find(serial);
                if (found != callbacks_.end()
                    && resolve(internal(found->second->data.obj)) == target) {
                    fire_statement(serial, target, std::nullopt);
                }
            }
        }
    }

    void ensure_statement_hook()
    {
        if (disable_hook_ == 0U && !disable_callbacks_.empty()) {
            disable_hook_ = simulation_.add_safe_point_hook(
                [this](runtime::Scheduler&, runtime::SchedulerPhase) {
                    deliver_disables();
                });
        }
        if (statement_hook_ != 0U) {
            return;
        }
        statement_hook_ = simulation_.add_execution_point_hook(
            [this](auto&, const runtime::simir::ExecutionPoint& point) {
                using Kind = runtime::simir::ExecutionPointKind;
                if (point.kind == Kind::statement
                    || point.kind == Kind::assertion
                    || point.kind == Kind::process_entry) {
                    deliver_statement(point);
                }
            });
    }

    // Just before a statement executes, its cbStmt callbacks run with the
    // statement or block they were placed on (IEEE 1800-2023 38.36.1.1).
    void deliver_statement(const runtime::simir::ExecutionPoint& point)
    {
        if (statement_callbacks_.empty() && disable_callbacks_.empty()) {
            return;
        }
        // An interned path is one process-wide string.
        const auto key = std::tuple { point.design_process,
            &point.source.path.str(), point.source.line, point.source.column };
        auto found = statement_points_.find(key);
        if (found == statement_points_.end()) {
            VpiStatementModel::StatementPoint statement;
            if (const auto source = source_process(point.design_process)) {
                statement = statement_model().statement_at(source->first,
                    source->second, point.source.path.str(), point.source.line,
                    point.source.column);
            }
            found = statement_points_.emplace(key, statement).first;
        }
        if (found->second.statement == 0U) {
            return;
        }
        // A loop's later pass runs the loop's own callbacks, not those of a
        // block it begins.
        const auto [statement, pass] = found->second;
        // A disable statement's target reaches its cbDisable callbacks once
        // it has been disabled, at the next safe point (IEEE 1800-2023
        // 38.36.1).
        if (!disable_callbacks_.empty() && !pass
            && handle_type(statement) == vpiDisable) {
            if (const auto serial = node_of(statement)) {
                if (const auto target
                    = statement_model_->relation(*serial, vpiExpr)) {
                    if (*target != 0U) {
                        pending_disables_.push_back(*target);
                    }
                }
            }
        }
        const auto listeners = statement_callbacks_;
        for (const auto& listener : listeners) {
            if (listener.trigger == statement
                && (!pass || listener.object == statement)) {
                fire_statement(listener.serial, listener.object, std::nullopt);
            }
        }
    }

    // A statement callback: obj is the statement, and value the given
    // value in the registered format.
    void fire_statement(const std::uint64_t serial,
        const fsim_vpi_handle_v1 statement, const std::optional<Value>& result)
    {
        const auto found = callbacks_.find(serial);
        if (found == callbacks_.end()) {
            return;
        }
        const auto& stored = *found->second;
        auto data = stored.data;
        auto time = stored.time;
        auto value = stored.value;
        data.obj = reinterpret_cast<vpiHandle>(
            static_cast<std::uintptr_t>(statement));
        data.time = nullptr;
        data.value = nullptr;
        data.index = 0;
        if (stored.has_time && stored.time.type != vpiSuppressTime) {
            fill_time(time, statement);
            data.time = &time;
        }
        if (stored.has_value && result && write_value(*result, value)) {
            data.value = &value;
        }
        const auto routine = data.cb_rtn;
        CallScope scope { *this };
        routine(&data);
    }

    // The scheduler's timing-check violation hook: each cbTchkViolation
    // callback registered for all checks, or for this one, gets the check as
    // obj (IEEE 1800-2023 38.36.3). It runs before the notifier toggles.
    void deliver_tchk_violation(const std::uint32_t check)
    {
        const auto handle = tagged(Tag::tchk, check);
        const auto listeners = tchk_callbacks_;
        for (const auto serial : listeners) {
            const auto found = callbacks_.find(serial);
            if (found == callbacks_.end()) {
                continue;
            }
            const auto watched = internal(found->second->data.obj);
            if (watched == 0U || watched == handle) {
                fire_statement(serial, handle, std::nullopt);
            }
        }
    }

    // --- assertions (IEEE 1800-2023 39) -------------------------------------

    // vpi_register_assertion_cb's arguments, as libfsim_tf packs them.
    struct AssertionCallbackArguments {
        PLI_INT32 reason { };
        vpi_assertion_callback_func* callback { };
        PLI_BYTE8* user_data { };
    };
    struct AssertionCallback {
        fsim_vpi_handle_v1 assertion { };
        PLI_INT32 reason { };
        vpi_assertion_callback_func* routine { };
        PLI_BYTE8* user_data { };
        std::optional<runtime::SystemVerilogVpiCallbackHandle> managed;
    };

    // The concurrent assertion statement an assertion object's process was
    // lowered from, found by its label in the instance's unit.
    // The HIR unit of the module that holds an assertion object.
    [[nodiscard]] const semantic::sv::Unit* assertion_unit(
        const runtime::SystemVerilogVpiObjectInfo& object) const
    {
        if (object.kind != SystemVerilogVpiObjectKind::Assertion) {
            return nullptr;
        }
        const auto process = info(object.parent);
        if (!process) {
            return nullptr;
        }
        const auto module = info(enclosing_module(process->parent));
        if (!module || !module->type || !module->type->semantic_unit_id) {
            return nullptr;
        }
        const auto& units = simulation_.systemverilog_hir().units();
        const auto unit = std::ranges::find(units,
            semantic::UnitId::from_index(*module->type->semantic_unit_id),
            &semantic::sv::Unit::id);
        return unit == units.end() ? nullptr : &*unit;
    }

    [[nodiscard]] const semantic::sv::ConcurrentAssertion* concurrent_assertion(
        const runtime::SystemVerilogVpiObjectInfo& object) const
    {
        const auto* const unit = assertion_unit(object);
        if (unit == nullptr) {
            return nullptr;
        }
        const auto process = info(object.parent);
        const auto found = std::ranges::find(unit->concurrent_assertions,
            process->name, &semantic::sv::ConcurrentAssertion::name);
        return found == unit->concurrent_assertions.end() ? nullptr : &*found;
    }

    [[nodiscard]] const semantic::sv::ConcurrentAssertion* concurrent_assertion(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto object = info(handle);
        return object ? concurrent_assertion(*object) : nullptr;
    }

    // Where an assertion statement is: its file and line.
    [[nodiscard]] std::optional<std::pair<std::string, std::int64_t>>
    assertion_location(const semantic::sv::ConcurrentAssertion& assertion) const
    {
        const auto& semantics = simulation_.semantics();
        if (!assertion.source.valid()
            || assertion.source.value() >= semantics.source_spans().size()) {
            return std::nullopt;
        }
        const auto& location = semantics.source_spans().at(assertion.source.value());
        auto file = location.logical_name;
        if (file.empty() && location.file.valid()
            && location.file.value() < semantics.source_files().size()) {
            file = semantics.source_files().at(location.file.value()).physical_name;
        }
        return std::pair { std::move(file),
            static_cast<std::int64_t>(location.begin.line) };
    }

    [[nodiscard]] static PLI_INT32 assertion_type(
        const semantic::sv::ConcurrentAssertion& assertion)
    {
        switch (assertion.kind) {
        case semantic::sv::ConcurrentAssertionKind::assumption:
            return vpiAssume;
        case semantic::sv::ConcurrentAssertionKind::cover:
            return vpiCover;
        case semantic::sv::ConcurrentAssertionKind::restriction:
            return vpiRestrict;
        default:
            return vpiAssert;
        }
    }

    // The concurrent assertions of a scope and the scopes in it, short of
    // other instances; all of them for a NULL scope (IEEE 1800-2023 39.3.1).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> assertions_in(
        const fsim_vpi_handle_v1 scope)
    {
        std::vector<fsim_vpi_handle_v1> objects;
        for (const auto handle : drain(
                 registry_.iterate_objects(SystemVerilogVpiObjectKind::Assertion))) {
            const auto object = info(handle);
            if (!object || concurrent_assertion(*object) == nullptr) {
                continue;
            }
            const auto process = info(object->parent);
            bool inside = scope == 0U;
            for (auto current = process ? process->parent : 0U;
                 !inside && current != 0U;) {
                const auto owner = info(current);
                if (!owner) {
                    break;
                }
                if (current == scope) {
                    inside = true;
                } else if (instance_kind(owner->kind)) {
                    break;
                }
                current = owner->parent;
            }
            if (inside) {
                objects.push_back(handle);
            }
        }
        return objects;
    }

    fsim_vpi_status_v1 register_assertion_callback(
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto* const arguments
            = pointer<AssertionCallbackArguments>(request.user_data);
        const auto assertion = resolve(request.handle);
        if (arguments == nullptr || arguments->callback == nullptr
            || concurrent_assertion(assertion) == nullptr
            || assertion_system_ended_) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        std::optional<runtime::SystemVerilogVpiCallbackKind> kind;
        switch (arguments->reason) {
        case cbAssertionSuccess:
            kind = runtime::SystemVerilogVpiCallbackKind::AssertionSuccess;
            break;
        case cbAssertionFailure:
            kind = runtime::SystemVerilogVpiCallbackKind::AssertionFailure;
            break;
        case cbAssertionVacuousSuccess:
            kind = runtime::SystemVerilogVpiCallbackKind::AssertionVacuous;
            break;
        case cbAssertionDisabledEvaluation:
            kind = runtime::SystemVerilogVpiCallbackKind::AssertionDisabled;
            break;
        // Steps occur only on an attempt stepping is enabled for, and
        // vpiAssertionEnableStep is refused; the control reasons occur on
        // vpi_control (39.5.2).
        case cbAssertionStepSuccess:
        case cbAssertionStepFailure:
        case cbAssertionLock:
        case cbAssertionUnlock:
        case cbAssertionDisable:
        case cbAssertionEnable:
        case cbAssertionReset:
        case cbAssertionKill:
        case cbAssertionEnablePassAction:
        case cbAssertionDisablePassAction:
        case cbAssertionEnableFailAction:
        case cbAssertionDisableFailAction:
        case cbAssertionEnableNonvacuousAction:
        case cbAssertionDisableVacuousAction:
            break;
        // An attempt start comes from the monitor's attempt hook.
        case cbAssertionStart:
            ensure_assertion_start_hook();
            break;
        default:
            error_message_ = "vpi_register_assertion_cb: reason "
                + std::to_string(arguments->reason) + " is not supported";
            error_ = true;
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto serial = ++next_callback_;
        AssertionCallback stored;
        stored.assertion = assertion;
        stored.reason = arguments->reason;
        stored.routine = arguments->callback;
        stored.user_data = arguments->user_data;
        if (kind) {
            runtime::SystemVerilogVpiCallbackRegistration registration;
            registration.kind = *kind;
            registration.object = assertion;
            registration.callback
                = [this, serial](
                      const runtime::SystemVerilogVpiCallbackEvent& event) {
                      fire_assertion(serial, true,
                          event.assertion ? event.assertion->attempt_start
                                          : std::uint64_t { });
                  };
            const auto registered
                = simulation_.systemverilog_vpi_callbacks().register_callback(
                    std::move(registration));
            if (!registered) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            stored.managed = registered.value;
        }
        assertion_callbacks_.emplace(serial, stored);
        return succeed(result, tagged(Tag::callback, serial));
    }

    bool remove_assertion_callback(const std::uint64_t serial)
    {
        const auto found = assertion_callbacks_.find(serial);
        if (found == assertion_callbacks_.end()) {
            return false;
        }
        if (found->second.managed) {
            (void)simulation_.systemverilog_vpi_callbacks().remove_callback(
                *found->second.managed);
        }
        assertion_callbacks_.erase(found);
        return true;
    }

    // The callback gets the attempt information for an attempt's start and
    // outcome and NULL for a control action (IEEE 1800-2023 39.4.2);
    // attemptStartTime is the tick the attempt started at, and a failure has
    // no failing expression.
    void fire_assertion(const std::uint64_t serial, const bool attempt,
        const std::uint64_t attempt_start = 0U)
    {
        const auto found = assertion_callbacks_.find(serial);
        if (found == assertion_callbacks_.end()) {
            return;
        }
        const auto stored = found->second;
        s_vpi_time time { };
        time.type = vpiSimTime;
        fill_time(time, stored.assertion);
        s_vpi_attempt_info detail { };
        detail.detail.failExpr = nullptr;
        detail.attemptStartTime.type = vpiSimTime;
        detail.attemptStartTime.high
            = static_cast<PLI_UINT32>(attempt_start >> 32U);
        detail.attemptStartTime.low
            = static_cast<PLI_UINT32>(attempt_start & 0xFFFFFFFFU);
        CallScope scope { *this };
        stored.routine(stored.reason, &time,
            reinterpret_cast<vpiHandle>(
                static_cast<std::uintptr_t>(stored.assertion)),
            attempt ? &detail : nullptr, stored.user_data);
    }

    // The attempt-start callbacks of one assertion (cbAssertionStart).
    void fire_assertion_start(
        const fsim_vpi_handle_v1 assertion, const std::uint64_t start)
    {
        if (assertion_system_ended_) {
            return;
        }
        std::vector<std::uint64_t> listeners;
        for (const auto& [serial, stored] : assertion_callbacks_) {
            if (stored.assertion == assertion
                && stored.reason == cbAssertionStart) {
                listeners.push_back(serial);
            }
        }
        for (const auto serial : listeners) {
            fire_assertion(serial, true, start);
        }
    }

    void ensure_assertion_start_hook()
    {
        if (assertion_start_hooked_) {
            return;
        }
        assertion_start_hooked_ = true;
        simulation_.set_assertion_attempt_hook(
            [this](const std::uint64_t assertion,
                const runtime::SimulationTick start) {
                fire_assertion_start(assertion, start);
            });
    }

    // The control-action callbacks of one assertion.
    void fire_assertion_control(
        const fsim_vpi_handle_v1 assertion, const PLI_INT32 reason)
    {
        std::vector<std::uint64_t> listeners;
        for (const auto& [serial, stored] : assertion_callbacks_) {
            if (stored.assertion == assertion && stored.reason == reason) {
                listeners.push_back(serial);
            }
        }
        for (const auto serial : listeners) {
            fire_assertion(serial, false);
        }
    }

    // The assertion system callbacks of one reason (IEEE 1800-2023 39.4.1).
    void fire_assertion_system(const PLI_INT32 reason)
    {
        std::vector<std::uint64_t> listeners;
        for (const auto serial : assertion_system_callbacks_) {
            const auto found = callbacks_.find(serial);
            if (found != callbacks_.end()
                && found->second->data.reason == reason) {
                listeners.push_back(serial);
            }
        }
        for (const auto serial : listeners) {
            fire(serial, std::nullopt, true);
        }
    }

    // The assertion system initializes and starts with the simulation and
    // ends with it.
    void ensure_assertion_system_hooks()
    {
        if (assertion_system_hooked_) {
            return;
        }
        assertion_system_hooked_ = true;
        auto& callbacks = simulation_.systemverilog_vpi_callbacks();
        runtime::SystemVerilogVpiCallbackRegistration start;
        start.kind = runtime::SystemVerilogVpiCallbackKind::StartOfSimulation;
        start.callback = [this](const runtime::SystemVerilogVpiCallbackEvent&) {
            fire_assertion_system(cbAssertionSysInitialized);
            fire_assertion_system(cbAssertionSysOn);
        };
        if (const auto registered = callbacks.register_callback(std::move(start))) {
            assertion_system_hooks_.push_back(registered.value);
        }
        runtime::SystemVerilogVpiCallbackRegistration end;
        end.kind = runtime::SystemVerilogVpiCallbackKind::EndOfSimulation;
        end.callback = [this](const runtime::SystemVerilogVpiCallbackEvent&) {
            end_assertion_system();
        };
        if (const auto registered = callbacks.register_callback(std::move(end))) {
            assertion_system_hooks_.push_back(registered.value);
        }
    }

    // No assertion callback occurs after cbAssertionSysEnd, and every
    // assertion callback is removed (IEEE 1800-2023 39.4.1, 39.5.1).
    void end_assertion_system()
    {
        if (assertion_system_ended_) {
            return;
        }
        fire_assertion_system(cbAssertionSysEnd);
        assertion_system_ended_ = true;
        std::vector<std::uint64_t> serials;
        for (const auto& [serial, stored] : assertion_callbacks_) {
            serials.push_back(serial);
        }
        for (const auto serial : serials) {
            remove_assertion_callback(serial);
        }
        for (const auto serial : assertion_system_callbacks_) {
            callbacks_.erase(serial);
        }
        assertion_system_callbacks_.clear();
    }

    [[nodiscard]] static bool assertion_control_operation(
        const PLI_INT32 operation)
    {
        return (operation >= vpiAssertionDisable
                   && operation <= vpiAssertionSysKill)
            || (operation >= vpiAssertionDisablePassAction
                && operation <= vpiAssertionSysUnlock);
    }

    // vpi_control on assertions and the assertion system (IEEE 1800-2023
    // 39.5); a locked assertion or system keeps its status until unlocked.
    fsim_vpi_status_v1 assertion_control(const PLI_INT32 operation,
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        using Operation = runtime::SystemVerilogVpiAssertionControlOperation;
        auto* const arguments = pointer<va_list>(request.user_data);
        fsim_vpi_handle_v1 handle { };
        // The attempt start time of the per-attempt controls (39.5.2).
        const s_vpi_time* attempt_time = nullptr;
        if (arguments != nullptr) {
            va_list copy;
            va_copy(copy, *arguments);
            handle = resolve(static_cast<fsim_vpi_handle_v1>(
                reinterpret_cast<std::uintptr_t>(va_arg(copy, vpiHandle))));
            if (operation == vpiAssertionKill
                || operation == vpiAssertionDisableStep
                || operation == vpiAssertionEnableStep) {
                attempt_time = va_arg(copy, const s_vpi_time*);
            }
            va_end(copy);
        }
        if (assertion_system_ended_) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        auto& assertions = simulation_.systemverilog_vpi_assertions();
        const auto apply = [&](const Operation applied,
                               const std::optional<fsim_vpi_handle_v1> object) {
            return assertions.control(applied, object)
                == runtime::SystemVerilogVpiAssertionApiError::None;
        };
        const auto done = [&]() -> fsim_vpi_status_v1 {
            return succeed(result, 0U, 1U);
        };
        // The assertion system: every assertion, or those of a scope.
        const auto system = [&](const PLI_INT32 reason,
                                const std::initializer_list<Operation> steps)
            -> fsim_vpi_status_v1 {
            if (assertion_system_locked_) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            for (const auto step : steps) {
                if (handle == 0U) {
                    if (!apply(step, std::nullopt)) {
                        return FSIM_VPI_STATUS_NOT_FOUND;
                    }
                    continue;
                }
                for (const auto assertion : assertions_in(handle)) {
                    if (!locked_assertions_.contains(assertion)
                        && !apply(step, assertion)) {
                        return FSIM_VPI_STATUS_NOT_FOUND;
                    }
                }
            }
            fire_assertion_system(reason);
            return done();
        };
        switch (operation) {
        case vpiAssertionSysOn:
            return system(cbAssertionSysOn, { Operation::Enable });
        case vpiAssertionSysOff:
            return system(cbAssertionSysOff, { Operation::Disable });
        case vpiAssertionSysKill:
            return handle == 0U
                ? system(cbAssertionSysKill, { Operation::Kill })
                : system(cbAssertionSysKill,
                      { Operation::Kill, Operation::Disable });
        // The initial state, without the step callbacks.
        case vpiAssertionSysReset: {
            const auto status = system(cbAssertionSysReset,
                { Operation::Kill, Operation::Reset, Operation::Enable,
                    Operation::EnablePassAction, Operation::EnableFailAction });
            if (status == FSIM_VPI_STATUS_OK) {
                std::vector<std::uint64_t> steps;
                for (const auto& [serial, stored] : assertion_callbacks_) {
                    if (stored.reason == cbAssertionStepSuccess
                        || stored.reason == cbAssertionStepFailure) {
                        steps.push_back(serial);
                    }
                }
                for (const auto serial : steps) {
                    remove_assertion_callback(serial);
                }
            }
            return status;
        }
        case vpiAssertionSysEnd:
            if (!apply(Operation::Kill, std::nullopt)) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            end_assertion_system();
            return done();
        case vpiAssertionSysLock:
            assertion_system_locked_ = true;
            fire_assertion_system(cbAssertionSysLock);
            return done();
        case vpiAssertionSysUnlock:
            assertion_system_locked_ = false;
            fire_assertion_system(cbAssertionSysUnlock);
            return done();
        case vpiAssertionSysEnablePassAction:
            return system(cbAssertionSysEnablePassAction,
                { Operation::EnablePassAction });
        case vpiAssertionSysDisablePassAction:
            return system(cbAssertionSysDisablePassAction,
                { Operation::DisablePassAction });
        case vpiAssertionSysEnableFailAction:
            return system(cbAssertionSysEnableFailAction,
                { Operation::EnableFailAction });
        case vpiAssertionSysDisableFailAction:
            return system(cbAssertionSysDisableFailAction,
                { Operation::DisableFailAction });
        case vpiAssertionSysEnableNonvacuousAction:
            return system(cbAssertionSysEnableNonvacuousAction,
                { Operation::EnableNonvacuousAction });
        case vpiAssertionSysDisableVacuousAction:
            return system(cbAssertionSysDisableVacuousAction,
                { Operation::DisableVacuousAction });
        default:
            break;
        }
        // One assertion statement (39.5.2).
        if (concurrent_assertion(handle) == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (operation == vpiAssertionUnlock) {
            locked_assertions_.erase(handle);
            fire_assertion_control(handle, cbAssertionUnlock);
            return done();
        }
        if (assertion_system_locked_ || locked_assertions_.contains(handle)) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto single = [&](const PLI_INT32 reason,
                                const std::initializer_list<Operation> steps)
            -> fsim_vpi_status_v1 {
            for (const auto step : steps) {
                if (!apply(step, handle)) {
                    return FSIM_VPI_STATUS_NOT_FOUND;
                }
            }
            fire_assertion_control(handle, reason);
            return done();
        };
        switch (operation) {
        case vpiAssertionLock:
            locked_assertions_.insert(handle);
            fire_assertion_control(handle, cbAssertionLock);
            return done();
        case vpiAssertionReset:
            return single(cbAssertionReset,
                { Operation::Kill, Operation::Reset, Operation::Enable });
        case vpiAssertionDisable:
            return single(cbAssertionDisable, { Operation::Disable });
        case vpiAssertionEnable:
            return single(cbAssertionEnable, { Operation::Enable });
        // Kill discards the one attempt that started at the given time and
        // leaves the assertion enabled (39.5.2).
        case vpiAssertionKill: {
            if (attempt_time == nullptr || attempt_time->type != vpiSimTime) {
                error_message_ = "vpi_control: vpiAssertionKill takes an "
                                 "attempt start time of type vpiSimTime";
                error_ = true;
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            (void)simulation_.kill_assertion_attempt(handle,
                (static_cast<std::uint64_t>(attempt_time->high) << 32U)
                    | attempt_time->low);
            fire_assertion_control(handle, cbAssertionKill);
            return done();
        }
        case vpiAssertionEnablePassAction:
            return single(
                cbAssertionEnablePassAction, { Operation::EnablePassAction });
        case vpiAssertionDisablePassAction:
            return single(
                cbAssertionDisablePassAction, { Operation::DisablePassAction });
        case vpiAssertionEnableFailAction:
            return single(
                cbAssertionEnableFailAction, { Operation::EnableFailAction });
        case vpiAssertionDisableFailAction:
            return single(
                cbAssertionDisableFailAction, { Operation::DisableFailAction });
        case vpiAssertionEnableNonvacuousAction:
            return single(cbAssertionEnableNonvacuousAction,
                { Operation::EnableNonvacuousAction });
        case vpiAssertionDisableVacuousAction:
            return single(cbAssertionDisableVacuousAction,
                { Operation::DisableVacuousAction });
        // Stepping (vpiAssertionEnableStep, vpiAssertionDisableStep) is not
        // available.
        default:
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
    }

    // An error in a VPI routine runs the cbPLIError callbacks, which can
    // read it with vpi_chk_error; the caller still sees it afterwards
    // (IEEE 1800-2023 38.36.3).
    void pli_error()
    {
        if (in_pli_error_ || !error_ || pli_error_callbacks_.empty()) {
            return;
        }
        in_pli_error_ = true;
        const auto message = error_message_;
        const auto listeners = pli_error_callbacks_;
        try {
            for (const auto serial : listeners) {
                error_message_ = message;
                error_ = true;
                fire(serial, std::nullopt, true);
            }
        } catch (...) {
            in_pli_error_ = false;
            throw;
        }
        in_pli_error_ = false;
        error_message_ = message;
        error_ = true;
    }

    fsim_vpi_status_v1 route(const Routine routine,
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        // The array value routines reach the service as an extension
        // operation of vpi_get_value and vpi_put_value.
        constexpr std::uint32_t value_array_operation = 1U;
        if (request.flags == value_array_operation
            && (routine == Routine::GetValue || routine == Routine::PutValue)) {
            return routine == Routine::GetValue
                ? get_value_array(request, result)
                : put_value_array(request, result);
        }
        constexpr std::uint32_t assertion_callback_operation = 2U;
        if (request.flags == assertion_callback_operation
            && routine == Routine::RegisterCallback) {
            error_ = false;
            return register_assertion_callback(request, result);
        }
        if (request.flags == acc_link_callback_operation
            && routine == Routine::RegisterCallback) {
            error_ = false;
            return register_callback(request, result);
        }
        if (request.flags != 0U) {
            return FSIM_VPI_STATUS_UNSUPPORTED;
        }
        // vpi_chk_error reports the previous routine's error, and any
        // other routine clears it (IEEE 1800-2023 38.6).
        if (routine == Routine::CheckError) {
            return check_error(request, result);
        }
        error_ = false;
        if (routine == Routine::PutValue || routine == Routine::GetValue) {
            const auto status = routine == Routine::PutValue
                ? put_value(request, result)
                : get_value(request, result);
            // Keep the specific message a check gave.
            if (status != FSIM_VPI_STATUS_OK && !error_) {
                error_message_ = std::string { routine == Routine::PutValue
                        ? "vpi_put_value"
                        : "vpi_get_value" }
                    + ": the object has no value of that kind";
                error_ = true;
            }
            return status;
        }
        switch (routine) {
        case Routine::RegisterSystemTaskFunction:
            return register_systf(request, result);
        case Routine::GetSystemTaskFunctionInfo:
            return systf_info(request, result);
        case Routine::RegisterCallback:
            return register_callback(request, result);
        case Routine::RemoveCallback:
            return remove_callback(request, result);
        case Routine::GetCallbackInfo:
            return callback_info(request, result);
        case Routine::Handle:
            return handle(request, result);
        case Routine::HandleByName:
            return handle_by_name(request, result);
        case Routine::HandleByIndex:
            return handle_by_index(request, result);
        case Routine::HandleByMultiIndex:
            return handle_by_multi_index(request, result);
        case Routine::HandleMulti:
            return handle_multi(request, result);
        case Routine::Iterate:
            return iterate(request, result);
        case Routine::Scan:
            return scan(request, result);
        case Routine::GetProperty:
        case Routine::GetProperty64:
            return property(request, result);
        case Routine::GetStringProperty:
            return string_property(request, result);
        case Routine::GetDelays:
            return get_delays(request, result);
        case Routine::PutDelays:
            return put_delays(request, result);
        case Routine::GetValue:
            return get_value(request, result);
        case Routine::PutValue:
            return put_value(request, result);
        case Routine::GetTime:
            return get_time(request, result);
        case Routine::GetVlogInfo:
            return vlog_info(request, result);
        case Routine::CheckError:
            return check_error(request, result);
        case Routine::FreeObject:
        case Routine::ReleaseHandle:
            if (owned(request.handle) && tag_of(request.handle) == Tag::iterator) {
                iterators_.erase(serial_of(request.handle));
            }
            // Freeing a vpiSchedEvent handle does not affect the event
            // (IEEE 1800-2023 38.34).
            if (owned(request.handle)
                && tag_of(request.handle) == Tag::scheduled) {
                (void)simulation_.systemverilog_vpi_values().forget(
                    scheduled_write(request.handle));
            }
            return succeed(result, 0U, 1U);
        case Routine::CompareObjects:
            return succeed(result, 0U,
                resolve(request.handle) == resolve(request.user_data) ? 1U : 0U);
        case Routine::GetUserData: {
            const auto found = user_data_.find(request.handle);
            return succeed(result, 0U, 0U,
                found == user_data_.end() ? 0U : address(found->second));
        }
        case Routine::PutUserData:
            user_data_[request.handle] = pointer<void>(request.user_data);
            return succeed(result, 0U, 1U);
        case Routine::GetData:
            return get_data(request, result);
        case Routine::PutData:
            return put_data(request, result);
        case Routine::Control:
        case Routine::VariableControl:
            return control(request, result);
        case Routine::Print:
        case Routine::VariablePrint:
            return print(request, result);
        case Routine::McdPrint:
        case Routine::McdVariablePrint:
            return mcd_print(request, result);
        case Routine::Flush:
            // The simulator's output channel (IEEE 1800-2023 38.5).
            (void)std::fflush(stdout);
            std::cout.flush();
            return succeed(result);
        case Routine::McdFlush: {
            const auto descriptor = static_cast<std::uint32_t>(request.argument);
            if ((descriptor & 0x80000001U) == 1U) {
                (void)std::fflush(stdout);
                std::cout.flush();
            }
            // 0 if successful (IEEE 1800-2023 38.25).
            return succeed(result, 0U,
                simulation_.mcd_flush(descriptor) ? 0U : 1U);
        }
        case Routine::McdOpen:
            if (request.text == nullptr) {
                return succeed(result, 0U, 0U);
            }
            return succeed(result, 0U,
                simulation_.mcd_open(
                    std::string_view { request.text, request.text_size }));
        case Routine::McdClose:
            return succeed(result, 0U,
                simulation_.mcd_close(
                    static_cast<std::uint32_t>(request.argument)));
        case Routine::McdName: {
            // The name overwrites the previous one (IEEE 1800-2023 38.26).
            const auto name = simulation_.mcd_name(
                static_cast<std::uint32_t>(request.argument));
            if (!name) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            return succeed(result, 0U, 0U, address(store(*name)));
        }
        default:
            return FSIM_VPI_STATUS_UNSUPPORTED;
        }
    }

private:
    struct Systf {
        s_vpi_systf_data data { };
        std::string name;
    };
    struct Argument {
        std::string text;
        // A design object the argument names, else a constant whose value
        // the call supplies.
        fsim_vpi_handle_v1 object { };
        bool expression { };
        bool string_literal { };
        bool real { };
        Value value;
    };
    struct CallSite {
        std::string name;
        std::string scope;
        std::string file;
        std::uint32_t line { };
        std::size_t systf { };
        bool function { };
        std::uint32_t width { 1U };
        std::vector<Argument> arguments;
        Value result;
    };
    /// The extension operation of vpi_register_cb that registers an ACC
    /// value change link's callback.
    static constexpr std::uint32_t acc_link_callback_operation = 3U;

    struct Callback {
        s_cb_data data { };
        s_vpi_time time { };
        s_vpi_value value { };
        bool has_time { };
        bool has_value { };
        std::optional<runtime::SystemVerilogVpiCallbackHandle> managed;
        // A value change callback on a select watches its parent and
        // reports only a change of the selected bits (IEEE 1800-2023
        // 38.36.1): the select and the value it had when last reported.
        fsim_vpi_handle_v1 selected { };
        std::optional<Value> selected_last;
    };
    struct Iterator {
        std::vector<fsim_vpi_handle_v1> objects;
        std::size_t cursor { };
    };
    // A bit-select or part-select of a vector (IEEE 1800-2023 37.18):
    // `width` bits of the parent starting `offset` bits above its LSB.
    // An element of a multidimensional packed vector is itself a vector
    // of the parent's type, with the remaining packed dimensions; its
    // name keeps every index (IEEE 1800-2023 7.4.5, 37.17).
    struct Select {
        fsim_vpi_handle_v1 parent { };
        std::uint32_t offset { };
        std::uint32_t width { 1U };
        std::int64_t left { };
        std::int64_t right { };
        bool bit { };
        bool element { };
        std::string suffix;
        std::vector<std::pair<std::int64_t, std::int64_t>> dimensions;
        // A member of a structure or union, or an element of a packed
        // array of them (IEEE 1800-2023 37.18, 37.26): its own name, the
        // object it belongs to, its declared type, and whether it is part
        // of a net.
        std::string name;
        fsim_vpi_handle_v1 owner { };
        std::shared_ptr<const runtime::SystemVerilogVpiTypeDescriptor> declared;
        bool net { };
        bool member { };
        // An element of an unpacked array within a declared type, such as
        // a member `logic [7:0] b[0:2]` (IEEE 1800-2023 37.17).
        bool unpacked_element { };
    };
    // A subarray of a multidimensional unpacked array, after `depth`
    // indexes: an array of the remaining dimensions (IEEE 1800-2023 7.4.5,
    // 37.17).
    struct SubArray {
        fsim_vpi_handle_v1 memory { };
        std::size_t depth { };
        std::string name;
        std::string full_name;
    };
    // A constant the object model reaches through a relation, such as a
    // range bound or an index.
    struct Constant {
        Value value;
        PLI_INT32 const_type { vpiDecConst };
    };
    // The skew of a clocking block or clockvar (`owner`), input or output.
    struct ClockingSkewHandle {
        fsim_vpi_handle_v1 owner { };
        bool output { };
    };
    // One dimension of an array or vector (IEEE 1800-2023 37.22 vpiRange).
    struct Range {
        fsim_vpi_handle_v1 parent { };
        std::int64_t left { };
        std::int64_t right { };
        // The dimension of a dynamic array, queue or associative array
        // (IEEE 1800-2023 37.22): no bounds.
        bool empty { };
    };
    // A typespec: one typedef level and the leading dimensions that
    // vpiElemTypespec has removed (IEEE 1800-2023 37.25 detail 8).
    struct TypespecLevel {
        const runtime::SystemVerilogVpiTypespec* node { };
        std::size_t dropped { };
    };
    // A member of a structure or union typespec, or a constant of an
    // enumeration typespec (IEEE 1800-2023 37.25).
    struct TypespecPart {
        const runtime::SystemVerilogVpiTypespec* owner { };
        std::size_t index { };
        fsim_vpi_handle_v1 parent { };
    };

    // --- module paths (IEEE 1800-2023 37.39, 38.10, 38.32) ---------------

    // One terminal of a module path: its path, direction, and position.
    // A data term is one of the signals a data source reads
    // (vpiModDataPathIn).
    struct PathTerm {
        std::uint32_t path { };
        bool output { };
        std::uint32_t index { };
        bool data { };
    };

    [[nodiscard]] std::optional<std::uint32_t> modpath_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::modpath
            || serial_of(handle) >= simulation_.design().verilog_specify_paths().size()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(serial_of(handle));
    }

    // The vpiNetType constant of a net kind (IEEE 1800-2023 37.16).
    [[nodiscard]] static std::int64_t net_type(
        const runtime::SystemVerilogVpiNetKind kind)
    {
        switch (kind) {
        case runtime::SystemVerilogVpiNetKind::Wire:
            return vpiWire;
        case runtime::SystemVerilogVpiNetKind::Tri:
            return vpiTri;
        case runtime::SystemVerilogVpiNetKind::Wand:
            return vpiWand;
        case runtime::SystemVerilogVpiNetKind::Wor:
            return vpiWor;
        case runtime::SystemVerilogVpiNetKind::Tri0:
            return vpiTri0;
        case runtime::SystemVerilogVpiNetKind::Tri1:
            return vpiTri1;
        case runtime::SystemVerilogVpiNetKind::Supply0:
            return vpiSupply0;
        case runtime::SystemVerilogVpiNetKind::Supply1:
            return vpiSupply1;
        case runtime::SystemVerilogVpiNetKind::Uwire:
            return vpiUwire;
        case runtime::SystemVerilogVpiNetKind::TriReg:
            return vpiTriReg;
        case runtime::SystemVerilogVpiNetKind::TriAnd:
            return vpiTriAnd;
        case runtime::SystemVerilogVpiNetKind::TriOr:
            return vpiTriOr;
        case runtime::SystemVerilogVpiNetKind::None:
            break;
        }
        return vpiNone;
    }

    // A module, interface or program instance (IEEE 1800-2023 37.10).
    [[nodiscard]] static bool instance_kind(const SystemVerilogVpiObjectKind kind)
    {
        return kind == SystemVerilogVpiObjectKind::Root
            || kind == SystemVerilogVpiObjectKind::Module
            || kind == SystemVerilogVpiObjectKind::Interface
            || kind == SystemVerilogVpiObjectKind::Program;
    }

    // The vpiPrimType of a gate, switch or pull primitive named by its
    // keyword (IEEE 1800-2023 28.4, 37.35).
    [[nodiscard]] static PLI_INT32 primitive_type(const std::string_view name)
    {
        static constexpr std::pair<std::string_view, PLI_INT32> gates[] = {
            { "and", vpiAndPrim }, { "nand", vpiNandPrim },
            { "nor", vpiNorPrim }, { "or", vpiOrPrim },
            { "xor", vpiXorPrim }, { "xnor", vpiXnorPrim },
            { "buf", vpiBufPrim }, { "not", vpiNotPrim },
            { "bufif0", vpiBufif0Prim }, { "bufif1", vpiBufif1Prim },
            { "notif0", vpiNotif0Prim }, { "notif1", vpiNotif1Prim },
            { "nmos", vpiNmosPrim }, { "pmos", vpiPmosPrim },
            { "cmos", vpiCmosPrim }, { "rnmos", vpiRnmosPrim },
            { "rpmos", vpiRpmosPrim }, { "rcmos", vpiRcmosPrim },
            { "tran", vpiTranPrim }, { "tranif0", vpiTranif0Prim },
            { "tranif1", vpiTranif1Prim }, { "rtran", vpiRtranPrim },
            { "rtranif0", vpiRtranif0Prim }, { "rtranif1", vpiRtranif1Prim },
            { "pullup", vpiPullupPrim }, { "pulldown", vpiPulldownPrim },
        };
        for (const auto& [gate, type] : gates) {
            if (gate == name) {
                return type;
            }
        }
        return vpiUndefined;
    }

    [[nodiscard]] static bool array_kind(const SystemVerilogVpiObjectKind kind)
    {
        return kind == SystemVerilogVpiObjectKind::GenerateScopeArray
            || kind == SystemVerilogVpiObjectKind::ModuleArray
            || kind == SystemVerilogVpiObjectKind::InterfaceArray
            || kind == SystemVerilogVpiObjectKind::ProgramArray;
    }

    // The elements `name[i]...` of an array, which are published beside it
    // in its scope.
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> array_elements(
        const runtime::SystemVerilogVpiObjectInfo& array)
    {
        std::vector<fsim_vpi_handle_v1> elements;
        const auto prefix = array.name + "[";
        for (const auto handle :
            drain(registry_.iterate_children(array.parent))) {
            const auto object = info(handle);
            if (object && object->handle != array.handle
                && !array_kind(object->kind)
                && object->name.starts_with(prefix)) {
                elements.push_back(handle);
            }
        }
        return elements;
    }

    // An instance array's dimensions, leftmost first: its elements are
    // published in declaration order, so the first and the last element
    // hold each dimension's left and right bounds (IEEE 1800-2023 37.11).
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::int64_t>>
    instance_array_ranges(const runtime::SystemVerilogVpiObjectInfo& array)
    {
        std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
        if (array.kind == SystemVerilogVpiObjectKind::GenerateScopeArray) {
            return ranges;
        }
        const auto elements = array_elements(array);
        const auto indexes = [&](const fsim_vpi_handle_v1 handle) {
            std::vector<std::int64_t> result;
            const auto object = info(handle);
            if (!object) {
                return result;
            }
            auto rest = std::string_view { object->name }.substr(
                array.name.size());
            while (rest.starts_with('[')) {
                std::int64_t index { };
                const auto parsed = std::from_chars(
                    rest.data() + 1, rest.data() + rest.size(), index);
                if (parsed.ec != std::errc { } || parsed.ptr == rest.end()
                    || *parsed.ptr != ']') {
                    return std::vector<std::int64_t> { };
                }
                result.push_back(index);
                rest.remove_prefix(
                    static_cast<std::size_t>(parsed.ptr - rest.data()) + 1U);
            }
            return result;
        };
        if (elements.empty()) {
            return ranges;
        }
        const auto first = indexes(elements.front());
        const auto last = indexes(elements.back());
        if (first.empty() || first.size() != last.size()) {
            return ranges;
        }
        for (std::size_t dimension = 0U; dimension < first.size();
             ++dimension) {
            ranges.emplace_back(first[dimension], last[dimension]);
        }
        return ranges;
    }

    // The instance array an instance is an element of (IEEE 1800-2023
    // 37.10 vpiArrayMember).
    [[nodiscard]] bool instance_array_member(
        const runtime::SystemVerilogVpiObjectInfo& object)
    {
        const auto bracket = object.name.find('[');
        if (bracket == std::string::npos) {
            return false;
        }
        const auto name = std::string_view { object.name }.substr(0U, bracket);
        for (const auto handle :
            drain(registry_.iterate_children(object.parent))) {
            if (const auto sibling = info(handle); sibling
                && array_kind(sibling->kind) && sibling->name == name) {
                return true;
            }
        }
        return false;
    }

    // Whether a generate scope's block path from its instance is one the
    // implicit-name rule named (IEEE 1800-2023 27.6).
    [[nodiscard]] bool implicit_generate_scope(
        const runtime::SystemVerilogVpiObjectInfo& scope) const
    {
        std::vector<std::string> blocks;
        auto current = std::optional { scope };
        while (current
            && current->kind == SystemVerilogVpiObjectKind::GenerateScope) {
            blocks.push_back(current->name.substr(0U, current->name.find('[')));
            current = current->parent == 0U ? std::nullopt
                                            : info(current->parent);
        }
        if (!current || !instance_kind(current->kind) || !current->type) {
            return false;
        }
        std::string path;
        for (auto block = blocks.rbegin(); block != blocks.rend(); ++block) {
            if (!path.empty()) {
                path.push_back('.');
            }
            path.append(*block);
        }
        return std::ranges::find(current->type->implicit_generate_scopes, path)
            != current->type->implicit_generate_scopes.end();
    }

    // A port of an instance (IEEE 1800-2023 37.14), or an I/O declaration
    // of a modport (37.7).
    [[nodiscard]] const runtime::SystemVerilogVpiPort* port_of(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto& ports = simulation_.systemverilog_vpi_ports();
        if (!owned(handle) || tag_of(handle) != Tag::port
            || serial_of(handle) >= ports.size()) {
            return nullptr;
        }
        return &ports[serial_of(handle)];
    }

    // The reference object of an interface port (IEEE 1800-2023 37.15),
    // identified by its port.
    [[nodiscard]] const runtime::SystemVerilogVpiPort* reference_of(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto& ports = simulation_.systemverilog_vpi_ports();
        if (!owned(handle) || tag_of(handle) != Tag::reference
            || serial_of(handle) >= ports.size()
            || !ports[serial_of(handle)].interface_port) {
            return nullptr;
        }
        return &ports[serial_of(handle)];
    }

    [[nodiscard]] std::string reference_full_name(
        const runtime::SystemVerilogVpiPort& port)
    {
        const auto instance = info(port.instance);
        return instance ? instance->full_name + "." + port.name : port.name;
    }

    // An interface port's name, or a name through it, resolved to the
    // reference object or to the object of its actual (IEEE 1800-2023
    // 37.15, 23.3.3.4).
    [[nodiscard]] fsim_vpi_handle_v1 find_reference_path(
        const std::string& name, const std::size_t depth = 0U)
    {
        const auto& ports = simulation_.systemverilog_vpi_ports();
        for (std::size_t index = 0U; index < ports.size() && depth < 16U;
             ++index) {
            const auto& port = ports[index];
            if (!port.interface_port || port.io_declaration) {
                continue;
            }
            const auto full = reference_full_name(port);
            if (name == full) {
                return tagged(Tag::reference, index);
            }
            if (port.actual == 0U || !name.starts_with(full + ".")) {
                continue;
            }
            auto actual = info(port.actual);
            if (actual && actual->kind == SystemVerilogVpiObjectKind::Modport) {
                actual = info(actual->parent);
            }
            if (!actual) {
                return 0U;
            }
            const auto through
                = actual->full_name + name.substr(full.size());
            if (const auto found = registry_.find(through);
                found && found.value) {
                return found.value->handle;
            }
            if (const auto member = find_declared_path(through)) {
                return member;
            }
            return find_reference_path(through, depth + 1U);
        }
        return 0U;
    }

    // --- typespecs (IEEE 1800-2023 37.25) --------------------------------

    using Typespec = runtime::SystemVerilogVpiTypespec;
    using TypespecKind = runtime::SystemVerilogVpiTypespecKind;

    [[nodiscard]] fsim_vpi_handle_v1 make_typespec(const TypespecLevel level)
    {
        // A class named in the source is the class model's typespec; a
        // built-in class has none (IEEE 1800-2023 37.32 detail 13).
        if (level.node->kind == TypespecKind::Class && level.dropped == 0U) {
            if (const auto handle
                = class_model().typespec_for_name(level.node->name);
                handle != 0U) {
                return handle;
            }
        }
        const auto key = std::pair { static_cast<const void*>(level.node),
            level.dropped };
        if (const auto found = typespec_index_.find(key);
            found != typespec_index_.end()) {
            return tagged(Tag::typespec, found->second);
        }
        typespecs_.push_back(level);
        typespec_index_.emplace(key, typespecs_.size() - 1U);
        return tagged(Tag::typespec, typespecs_.size() - 1U);
    }

    [[nodiscard]] const TypespecLevel* typespec_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::typespec
            || serial_of(handle) >= typespecs_.size()) {
            return nullptr;
        }
        return &typespecs_[serial_of(handle)];
    }

    [[nodiscard]] const TypespecPart* typespec_part_of(
        const fsim_vpi_handle_v1 handle, const Tag tag) const
    {
        const auto& parts
            = tag == Tag::typespec_member ? typespec_members_ : enum_constants_;
        if (!owned(handle) || tag_of(handle) != tag
            || serial_of(handle) >= parts.size()) {
            return nullptr;
        }
        return &parts[serial_of(handle)];
    }

    // The typespec of a value stored as `descriptor`, for an object
    // elaboration gave none: a built-in type has no typedef to name
    // (37.25 detail 1).
    [[nodiscard]] static Typespec derived_typespec(
        const runtime::SystemVerilogVpiTypeDescriptor& descriptor)
    {
        using DescriptorKind = runtime::SystemVerilogVpiDescriptorKind;
        using Category = runtime::SystemVerilogVpiValueCategory;
        Typespec result;
        result.is_signed = descriptor.is_signed;
        switch (descriptor.kind) {
        case DescriptorKind::UnpackedArray:
        case DescriptorKind::DynamicArray:
        case DescriptorKind::Queue:
        case DescriptorKind::AssociativeArray:
            result.kind = TypespecKind::Array;
            result.is_signed = false;
            result.array_kind = descriptor.kind == DescriptorKind::DynamicArray
                ? runtime::SystemVerilogVpiArrayKind::Dynamic
                : descriptor.kind == DescriptorKind::Queue
                ? runtime::SystemVerilogVpiArrayKind::Queue
                : descriptor.kind == DescriptorKind::AssociativeArray
                ? runtime::SystemVerilogVpiArrayKind::Associative
                : runtime::SystemVerilogVpiArrayKind::Static;
            if (descriptor.kind == DescriptorKind::UnpackedArray) {
                result.ranges = descriptor.ranges;
            }
            if (!descriptor.children.empty()) {
                result.element.push_back(
                    derived_typespec(descriptor.children.front()));
            }
            return result;
        case DescriptorKind::Struct:
        case DescriptorKind::Union:
            result.kind = descriptor.kind == DescriptorKind::Struct
                ? TypespecKind::Struct
                : TypespecKind::Union;
            result.packed = descriptor.packed;
            for (std::size_t index = 0U; index < descriptor.children.size();
                 ++index) {
                result.members.push_back(
                    derived_typespec(descriptor.children[index]));
                result.member_names.push_back(
                    index < descriptor.member_names.size()
                        ? descriptor.member_names[index]
                        : std::string { });
            }
            return result;
        case DescriptorKind::Enum: {
            result.kind = TypespecKind::Enum;
            auto base = descriptor;
            base.kind = DescriptorKind::Scalar;
            base.enum_literals.clear();
            result.element.push_back(derived_typespec(base));
            for (const auto& literal : descriptor.enum_literals) {
                result.enum_constants.push_back(literal);
            }
            return result;
        }
        case DescriptorKind::PackedArray:
            // A vector of a built-in type stores its bits as a packed
            // array; its typespec is the type's own (37.25).
            if (descriptor.children.size() == 1U
                && descriptor.children.front().kind == DescriptorKind::Scalar) {
                auto scalar = descriptor.children.front();
                std::uint64_t width = scalar.width;
                for (const auto& range : descriptor.ranges) {
                    width *= support::index_distance(range.left, range.right)
                        + 1U;
                }
                if (descriptor.category
                    != runtime::SystemVerilogVpiValueCategory::None) {
                    scalar.category = descriptor.category;
                }
                scalar.width = static_cast<std::uint32_t>(width);
                scalar.is_signed = descriptor.is_signed || scalar.is_signed;
                auto vector = derived_typespec(scalar);
                if (vector.kind == TypespecKind::Logic
                    || vector.kind == TypespecKind::Bit) {
                    vector.ranges = descriptor.ranges;
                }
                return vector;
            }
            result.kind = TypespecKind::PackedArray;
            result.ranges = descriptor.ranges;
            if (!descriptor.children.empty()) {
                result.element.push_back(
                    derived_typespec(descriptor.children.front()));
            }
            return result;
        case DescriptorKind::String:
            result.kind = TypespecKind::String;
            result.is_signed = false;
            return result;
        case DescriptorKind::Class:
        case DescriptorKind::ClassHandle:
            result.kind = TypespecKind::Class;
            result.name = descriptor.nominal_name;
            return result;
        case DescriptorKind::Scalar:
            break;
        }
        switch (descriptor.category) {
        case Category::Real:
            result.kind = TypespecKind::Real;
            return result;
        case Category::ShortReal:
            result.kind = TypespecKind::ShortReal;
            return result;
        case Category::Time:
            result.kind = TypespecKind::Time;
            return result;
        case Category::String:
            result.kind = TypespecKind::String;
            return result;
        case Category::Event:
            result.kind = TypespecKind::Event;
            return result;
        case Category::Integer4:
            if (descriptor.width == 32U) {
                result.kind = TypespecKind::Integer;
                return result;
            }
            break;
        case Category::Integer2:
            result.kind = descriptor.width == 8U ? TypespecKind::Byte
                : descriptor.width == 16U        ? TypespecKind::ShortInt
                : descriptor.width == 64U        ? TypespecKind::LongInt
                                                 : TypespecKind::Int;
            return result;
        default:
            break;
        }
        result.kind = descriptor.category == Category::Bit2 ? TypespecKind::Bit
                                                            : TypespecKind::Logic;
        if (descriptor.width > 1U) {
            result.ranges.push_back(
                { std::int64_t { descriptor.width } - 1, 0 });
        }
        return result;
    }

    // The typespec of a net, variable, array, array word, member or
    // parameter (IEEE 1800-2023 37.16, 37.17, 37.25, 37.26).
    [[nodiscard]] std::optional<TypespecLevel> object_typespec(
        const fsim_vpi_handle_v1 handle, const std::size_t depth = 0U)
    {
        // A member's typespec is its structure's member's (37.26), an
        // element's its packed array's element's (37.18).
        if (const auto* select = select_of(handle);
            select != nullptr && select->declared && select->owner != 0U) {
            const auto copy = *select;
            const auto owner = depth > 16U
                ? std::nullopt
                : object_typespec(copy.owner, depth + 1U);
            if (!owner) {
                return std::nullopt;
            }
            if (!copy.member) {
                return element_typespec(*owner);
            }
            const auto* node = owner->node;
            for (std::size_t index = 0U; index < node->member_names.size();
                 ++index) {
                if (node->member_names[index] == copy.name) {
                    return TypespecLevel { &node->members[index], 0U };
                }
            }
            return std::nullopt;
        }
        const auto object = info(handle);
        if (!object || !object->type || depth > 16U
            || (object->kind != SystemVerilogVpiObjectKind::Net
                && object->kind != SystemVerilogVpiObjectKind::Variable
                && object->kind != SystemVerilogVpiObjectKind::Port
                && object->kind != SystemVerilogVpiObjectKind::Parameter
                && object->kind != SystemVerilogVpiObjectKind::Memory
                && object->kind != SystemVerilogVpiObjectKind::StructureMember
                && object->kind != SystemVerilogVpiObjectKind::NamedEvent
                && object->kind != SystemVerilogVpiObjectKind::ClassProperty)) {
            return std::nullopt;
        }
        if (object->type->typespec) {
            return TypespecLevel { object->type->typespec.get(), 0U };
        }
        // An array word's typespec is its array's element's (37.17).
        if (const auto parent = info(object->parent);
            parent && parent->kind == SystemVerilogVpiObjectKind::Memory) {
            // A word is indexed through every unpacked dimension.
            auto element = object_typespec(object->parent, depth + 1U);
            while (element && element->node->kind == TypespecKind::Array) {
                element = element_typespec(*element);
            }
            if (element) {
                return element;
            }
        }
        auto& derived = derived_typespecs_[handle];
        if (!derived) {
            // An array is described by its stored shape, any other object
            // by its value category, which tells an int from a bit vector.
            Typespec typespec;
            if (object->type->declared) {
                typespec = derived_typespec(*object->type->declared);
            } else if (object->kind == SystemVerilogVpiObjectKind::Memory
                && object->type->descriptor) {
                typespec = derived_typespec(*object->type->descriptor);
                typespec.kind = TypespecKind::Array;
            } else if (object->type->descriptor
                && object->type->descriptor->kind
                    == runtime::SystemVerilogVpiDescriptorKind::ClassHandle) {
                // A class property is a class variable (37.33).
                typespec = derived_typespec(*object->type->descriptor);
            } else {
                runtime::SystemVerilogVpiTypeDescriptor scalar;
                scalar.category = object->type->category;
                scalar.width = object->type->width;
                scalar.is_signed = object->type->is_signed;
                typespec = derived_typespec(scalar);
            }
            // A vector keeps its declared bounds (37.22).
            if (object->kind != SystemVerilogVpiObjectKind::Memory
                && (typespec.kind == TypespecKind::Logic
                           || typespec.kind == TypespecKind::Bit)
                && object->type->width > 1U) {
                typespec.ranges.clear();
                for (const auto& [left, right] : dimension_ranges(handle)) {
                    typespec.ranges.push_back({ left, right });
                }
            }
            derived = std::make_shared<const Typespec>(std::move(typespec));
        }
        return TypespecLevel { derived.get(), 0U };
    }

    // The ranges of a typespec, leftmost first, through the named levels
    // its dimensions continue into (37.25 details 8-10).
    [[nodiscard]] static std::vector<runtime::SystemVerilogVpiRange>
    typespec_ranges(const TypespecLevel level, bool& empty)
    {
        std::vector<runtime::SystemVerilogVpiRange> result;
        const auto* node = level.node;
        auto dropped = level.dropped;
        empty = node->kind == TypespecKind::Array
            && node->array_kind != runtime::SystemVerilogVpiArrayKind::Static;
        while (node != nullptr) {
            result.insert(result.end(),
                node->ranges.begin()
                    + static_cast<std::ptrdiff_t>(
                        std::min(dropped, node->ranges.size())),
                node->ranges.end());
            if (node->element.empty()
                || node->kind == TypespecKind::Enum) {
                break;
            }
            const auto& next = node->element.front();
            const bool continues = node->kind == TypespecKind::Array
                ? next.kind == TypespecKind::Array
                    && next.array_kind
                        == runtime::SystemVerilogVpiArrayKind::Static
                    && node->array_kind
                        == runtime::SystemVerilogVpiArrayKind::Static
                : node->kind == TypespecKind::PackedArray
                    && (next.kind == TypespecKind::PackedArray
                        || next.kind == TypespecKind::Logic
                        || next.kind == TypespecKind::Bit);
            if (!continues) {
                break;
            }
            node = &next;
            dropped = 0U;
        }
        return result;
    }

    // vpiElemTypespec: the typespec with its leftmost dimension removed
    // (37.25 detail 8).
    [[nodiscard]] static std::optional<TypespecLevel> element_typespec(
        const TypespecLevel level)
    {
        const auto* node = level.node;
        switch (node->kind) {
        case TypespecKind::Array:
        case TypespecKind::PackedArray:
            if (level.dropped + 1U < node->ranges.size()) {
                return TypespecLevel { node, level.dropped + 1U };
            }
            if (node->element.empty()) {
                return std::nullopt;
            }
            return TypespecLevel { &node->element.front(), 0U };
        case TypespecKind::Logic:
        case TypespecKind::Bit:
            if (level.dropped < node->ranges.size()) {
                return TypespecLevel { node, level.dropped + 1U };
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static PLI_INT32 typespec_type(const Typespec& node)
    {
        switch (node.kind) {
        case TypespecKind::Logic:
            return vpiLogicTypespec;
        case TypespecKind::Bit:
            return vpiBitTypespec;
        case TypespecKind::Byte:
            return vpiByteTypespec;
        case TypespecKind::ShortInt:
            return vpiShortIntTypespec;
        case TypespecKind::Int:
            return vpiIntTypespec;
        case TypespecKind::LongInt:
            return vpiLongIntTypespec;
        case TypespecKind::Integer:
            return vpiIntegerTypespec;
        case TypespecKind::Time:
            return vpiTimeTypespec;
        case TypespecKind::Real:
            return vpiRealTypespec;
        case TypespecKind::ShortReal:
            return vpiShortRealTypespec;
        case TypespecKind::String:
            return vpiStringTypespec;
        case TypespecKind::Event:
            return vpiEventTypespec;
        case TypespecKind::Chandle:
            return vpiChandleTypespec;
        case TypespecKind::Enum:
            return vpiEnumTypespec;
        case TypespecKind::Struct:
            return vpiStructTypespec;
        case TypespecKind::Union:
            return vpiUnionTypespec;
        case TypespecKind::PackedArray:
            return vpiPackedArrayTypespec;
        case TypespecKind::Array:
            return vpiArrayTypespec;
        case TypespecKind::Class:
            return vpiClassTypespec;
        }
        return vpiUndefined;
    }

    // The bits of an enumeration constant's value.
    [[nodiscard]] static std::int64_t enum_constant_width(const Typespec& node)
    {
        return node.enum_constants.empty()
            ? 0
            : static_cast<std::int64_t>(node.enum_constants.front().value.width());
    }

    // The typespec of a declared built-in type, and its range bounds
    // (IEEE 1800-2023 37.25, 37.41).
    [[nodiscard]] fsim_vpi_handle_v1 declared_type_relation(
        const VpiStatementModel::DeclaredType& declared, const PLI_INT32 type)
    {
        if (type != vpiTypespec) {
            return declared.ranges.empty() ? 0U
                : make_constant(type == vpiLeftRange
                          ? declared.ranges.front().first
                          : declared.ranges.front().second);
        }
        const auto key
            = std::tuple { declared.word, declared.is_signed, declared.ranges };
        if (const auto known = declared_typespecs_.find(key);
            known != declared_typespecs_.end()) {
            return known->second;
        }
        static const std::map<std::string, TypespecKind, std::less<>> kinds {
            { "logic", TypespecKind::Logic }, { "bit", TypespecKind::Bit },
            { "byte", TypespecKind::Byte },
            { "shortint", TypespecKind::ShortInt },
            { "int", TypespecKind::Int }, { "longint", TypespecKind::LongInt },
            { "integer", TypespecKind::Integer },
            { "time", TypespecKind::Time }, { "real", TypespecKind::Real },
            { "realtime", TypespecKind::Real },
            { "shortreal", TypespecKind::ShortReal },
            { "string", TypespecKind::String } };
        const auto kind = kinds.find(declared.word);
        if (kind == kinds.end()) {
            return 0U;
        }
        Typespec scalar;
        scalar.kind = kind->second;
        scalar.is_signed = declared.is_signed;
        // A built-in vector is one logic or bit typespec with its packed
        // dimensions, as the elaborator builds it.
        for (const auto& [left, right] : declared.ranges) {
            scalar.ranges.push_back({ left, right });
        }
        declared_typespec_nodes_.push_back(std::move(scalar));
        const auto handle
            = make_typespec({ &declared_typespec_nodes_.back(), 0U });
        declared_typespecs_.emplace(key, handle);
        return handle;
    }

    fsim_vpi_status_v1 typespec_relation(const TypespecLevel level,
        const fsim_vpi_handle_v1 self, const PLI_INT32 type,
        fsim_vpi_service_result_v1& result)
    {
        const auto& node = *level.node;
        switch (type) {
        case vpiElemTypespec:
            if (const auto element = element_typespec(level)) {
                return succeed(result, make_typespec(*element));
            }
            return succeed(result);
        case vpiIndexTypespec:
            return succeed(result,
                node.kind == TypespecKind::Array && level.dropped == 0U
                        && !node.index.empty()
                    ? make_typespec({ &node.index.front(), 0U })
                    : 0U);
        case vpiBaseTypespec:
            return succeed(result,
                node.kind == TypespecKind::Enum && !node.element.empty()
                    ? make_typespec({ &node.element.front(), 0U })
                    : 0U);
        case vpiTypedefAlias:
            return succeed(result,
                level.dropped == 0U && !node.alias.empty()
                    ? make_typespec({ &node.alias.front(), 0U })
                    : 0U);
        case vpiLeftRange:
        case vpiRightRange: {
            bool empty = false;
            const auto ranges = typespec_ranges(level, empty);
            if (ranges.empty() || empty) {
                return succeed(result);
            }
            return succeed(result,
                make_constant(type == vpiLeftRange ? ranges.front().left
                                                   : ranges.front().right));
        }
        default:
            static_cast<void>(self);
            return succeed(result);
        }
    }

    [[nodiscard]] std::vector<fsim_vpi_handle_v1> typespec_iteration(
        const TypespecLevel level, const fsim_vpi_handle_v1 self,
        const PLI_INT32 type)
    {
        std::vector<fsim_vpi_handle_v1> objects;
        const auto& node = *level.node;
        if (type == vpiRange) {
            bool empty = false;
            if (const auto ranges = typespec_ranges(level, empty);
                empty || !ranges.empty()) {
                if (empty) {
                    ranges_.push_back({ self, 0, 0, true });
                    objects.push_back(tagged(Tag::range, ranges_.size() - 1U));
                }
                for (const auto& range : ranges) {
                    objects.push_back(make_range(self, range.left, range.right));
                }
            }
        } else if (type == vpiTypespecMember) {
            for (std::size_t index = 0U; index < node.members.size(); ++index) {
                typespec_members_.push_back({ &node, index, self });
                objects.push_back(
                    tagged(Tag::typespec_member, typespec_members_.size() - 1U));
            }
        } else if (type == vpiEnumConst) {
            for (std::size_t index = 0U; index < node.enum_constants.size();
                 ++index) {
                enum_constants_.push_back({ &node, index, self });
                objects.push_back(
                    tagged(Tag::enum_const, enum_constants_.size() - 1U));
            }
        }
        return objects;
    }

    // The ports of an instance in port order, the ports whose low
    // connection is `object` (vpiPorts), or those whose high connection it
    // is (vpiPortInst) (IEEE 1800-2023 37.14, 37.16, 37.17).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> ports_of(
        const PLI_INT32 relation, const fsim_vpi_handle_v1 object) const
    {
        std::vector<fsim_vpi_handle_v1> found;
        const auto& ports = simulation_.systemverilog_vpi_ports();
        for (std::size_t index = 0U; index < ports.size(); ++index) {
            const auto& port = ports[index];
            if (port.io_declaration != (relation == vpiIODecl)) {
                continue;
            }
            const auto end = relation == vpiPort || relation == vpiIODecl
                ? port.instance
                : relation == vpiPorts ? port.low_conn
                                       : port.high_conn;
            if (end != 0U && end == object) {
                found.push_back(tagged(Tag::port, index));
            }
        }
        return found;
    }

    [[nodiscard]] static fsim_vpi_handle_v1 pathterm_handle(
        const PathTerm& term)
    {
        return tagged(Tag::pathterm,
            (static_cast<std::uint64_t>(term.path) << 24U)
                | (term.output ? std::uint64_t { 1 } << 23U : 0U)
                | (term.data ? std::uint64_t { 1 } << 22U : 0U) | term.index);
    }

    [[nodiscard]] std::optional<PathTerm> pathterm_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::pathterm) {
            return std::nullopt;
        }
        const auto serial = serial_of(handle);
        PathTerm term { static_cast<std::uint32_t>(serial >> 24U),
            ((serial >> 23U) & 1U) != 0U,
            static_cast<std::uint32_t>(serial & 0x3FFFFFU),
            ((serial >> 22U) & 1U) != 0U };
        const auto& paths = simulation_.design().verilog_specify_paths();
        if (term.path >= paths.size()
            || (term.data && term.output)) {
            return std::nullopt;
        }
        if (term.data) {
            return term.index < path_data_signals(term.path).size()
                ? std::optional<PathTerm> { term }
                : std::nullopt;
        }
        const auto& terminals = term.output ? paths[term.path].destinations
                                            : paths[term.path].sources;
        if (term.index >= terminals.size()) {
            return std::nullopt;
        }
        return term;
    }

    // The signals an edge-sensitive path's data source reads, in program
    // order without repeats (IEEE 1800-2023 37.39 vpiModDataPathIn).
    [[nodiscard]] std::vector<runtime::simir::SignalId> path_data_signals(
        const std::uint32_t path) const
    {
        std::vector<runtime::simir::SignalId> signals;
        for (const auto& node : simulation_.design()
                 .verilog_specify_paths()[path].data_source_program.nodes) {
            if (node.operation
                    == runtime::simir::ModulePathExpressionOperator::terminal
                && std::ranges::find(signals, node.terminal.signal)
                    == signals.end()) {
                signals.push_back(node.terminal.signal);
            }
        }
        return signals;
    }

    // The instance that declares a path; its full name is the path's
    // `instance`.
    [[nodiscard]] fsim_vpi_handle_v1 path_instance(
        const std::uint32_t path) const
    {
        const auto found = registry_.find(
            simulation_.design().verilog_specify_paths()[path].instance);
        return found && found.value ? found.value->handle : 0U;
    }

    // The net or variable a path terminal names, as published in the
    // declaring instance (IEEE 1800-2023 vpiPathTerm vpiExpr).
    [[nodiscard]] fsim_vpi_handle_v1 pathterm_object(const PathTerm& term) const
    {
        const auto& path = simulation_.design().verilog_specify_paths()[term.path];
        const auto signal = term.data
            ? path_data_signals(term.path)[term.index]
            : (term.output ? path.destinations[term.index]
                           : path.sources[term.index]).signal;
        return signal_object(signal, path_instance(term.path));
    }

    // `instance` when it is a module, else NULL: an interface is a vpiInstance
    // but not a vpiModule (IEEE 1800-2023 37.39).
    // The module instance that contains `scope`, through generate and other
    // named scopes; a top-level module is published as the Root kind.
    [[nodiscard]] fsim_vpi_handle_v1 enclosing_module_of(
        fsim_vpi_handle_v1 scope) const
    {
        for (std::size_t depth { }; scope != 0U && depth < 1024U; ++depth) {
            const auto object = info(scope);
            if (!object) {
                return 0U;
            }
            if (object->kind == SystemVerilogVpiObjectKind::Module
                || object->kind == SystemVerilogVpiObjectKind::Root) {
                return scope;
            }
            if (object->kind != SystemVerilogVpiObjectKind::GenerateScope
                && object->kind != SystemVerilogVpiObjectKind::Begin
                && object->kind != SystemVerilogVpiObjectKind::Fork) {
                return 0U;
            }
            scope = object->parent;
        }
        return 0U;
    }

    // The net or variable of `signal` published directly in `instance`; a
    // port stands in only when no net or variable does.
    [[nodiscard]] fsim_vpi_handle_v1 signal_object(
        const runtime::simir::SignalId signal,
        const fsim_vpi_handle_v1 instance) const
    {
        fsim_vpi_handle_v1 port { };
        for (const auto handle : simulation_.vpi_signal_object_handles(signal)) {
            const auto object = info(handle);
            if (!object || instance == 0U || object->parent != instance) {
                continue;
            }
            if (object->kind != SystemVerilogVpiObjectKind::Port) {
                return handle;
            }
            port = handle;
        }
        return port;
    }

    // The vpiPolarity/vpiDataPolarity value of a path polarity
    // (IEEE 1800-2023 37.39).
    [[nodiscard]] static std::int64_t path_polarity(
        const frontend::VerilogPathPolarity polarity)
    {
        return polarity == frontend::VerilogPathPolarity::Positive
            ? vpiPositive
            : polarity == frontend::VerilogPathPolarity::Negative
            ? vpiNegative
            : vpiUnknown;
    }

    // The vpiEdge mask of a path source edge (IEEE 1800-2023 37.39).
    [[nodiscard]] static std::int64_t path_edge_mask(
        const frontend::VerilogSpecifyEdge edge)
    {
        return edge == frontend::VerilogSpecifyEdge::Posedge ? vpiPosedge
            : edge == frontend::VerilogSpecifyEdge::Negedge ? vpiNegedge
            : edge == frontend::VerilogSpecifyEdge::Edge    ? vpiAnyEdge
                                                            : 0;
    }

    // vpi_get on a module path (IEEE 1800-2023 37.39). An edge-sensitive
    // path carries its `+`/`-` on the data source, so it reports the
    // polarity as vpiDataPolarity and vpiPolarity is unknown.
    fsim_vpi_status_v1 modpath_property(const std::uint32_t path,
        const PLI_INT32 property, fsim_vpi_service_result_v1& result) const
    {
        const auto& declared = simulation_.design().verilog_specify_paths()[path];
        const bool edge_sensitive = !declared.data_source_program.empty();
        std::int64_t value = vpiUndefined;
        switch (property) {
        case vpiPathType:
            value = declared.kind == frontend::VerilogModulePathKind::Full
                ? vpiPathFull
                : vpiPathParallel;
            break;
        case vpiPolarity:
            value = edge_sensitive ? vpiUnknown : path_polarity(declared.polarity);
            break;
        case vpiDataPolarity:
            value = edge_sensitive ? path_polarity(declared.polarity) : vpiUnknown;
            break;
        case vpiModPathHasIfNone:
            value = declared.ifnone ? 1 : 0;
            break;
        case vpiLineNo:
            value = static_cast<std::int64_t>(declared.source.begin.line);
            break;
        default:
            break;
        }
        return succeed(result, 0U, static_cast<std::uint64_t>(value));
    }

    // vpi_get on a path term: its direction and, for an input of an
    // edge-sensitive path, its edge (IEEE 1800-2023 37.39).
    fsim_vpi_status_v1 pathterm_property(const PathTerm& term,
        const PLI_INT32 property, fsim_vpi_service_result_v1& result) const
    {
        std::int64_t value = vpiUndefined;
        if (property == vpiDirection) {
            value = term.output ? vpiOutput : vpiInput;
        } else if (property == vpiEdge) {
            value = term.output || term.data
                ? 0
                : path_edge_mask(simulation_.design()
                          .verilog_specify_paths()[term.path].source_edge);
        }
        return succeed(result, 0U, static_cast<std::uint64_t>(value));
    }

    // --- SDF net delays: intermodule paths and delay terminals (IEEE
    // 1800-2023 37.37, 37.45) ----------------------------------------------
    //
    // Only a delay that SDF annotation created is published; a design
    // without one iterates none. The delay is the SDF effective value and
    // does not delay the net in simulation.

    [[nodiscard]] const std::vector<elaboration::SdfNetDelayInfo>& net_delays()
        const
    {
        return simulation_.design().sdf_net_delays();
    }

    [[nodiscard]] std::optional<std::uint32_t> net_delay_index(
        const fsim_vpi_handle_v1 handle, const Tag tag) const
    {
        if (!owned(handle) || tag_of(handle) != tag) {
            return std::nullopt;
        }
        const auto serial
            = tag == Tag::delay_term ? serial_of(handle) >> 1U : serial_of(handle);
        return serial < net_delays().size()
            ? std::optional<std::uint32_t> { static_cast<std::uint32_t>(serial) }
            : std::nullopt;
    }

    [[nodiscard]] static fsim_vpi_handle_v1 delay_term_handle(
        const std::uint32_t delay, const bool output)
    {
        return tagged(Tag::delay_term,
            (static_cast<std::uint64_t>(delay) << 1U) | (output ? 1U : 0U));
    }

    [[nodiscard]] bool net_delay_handle(const fsim_vpi_handle_v1 handle) const
    {
        return net_delay_index(handle, Tag::inter_mod_path)
            || net_delay_index(handle, Tag::delay_device)
            || net_delay_index(handle, Tag::delay_term);
    }

    [[nodiscard]] fsim_vpi_handle_v1 instance_by_path(
        const std::string& path) const
    {
        const auto found = registry_.find(path);
        return found && found.value ? found.value->handle : 0U;
    }

    // The instance that declares the port or net of a terminal: its path
    // without the last name.
    [[nodiscard]] fsim_vpi_handle_v1 net_terminal_instance(
        const elaboration::SdfNetDelayTerminal& terminal) const
    {
        const auto dot = terminal.object.rfind('.');
        return dot == std::string::npos
            ? instance_by_path(terminal.instance)
            : instance_by_path(terminal.object.substr(0U, dot));
    }

    // The net or port a delay terminal names, as published in its instance.
    [[nodiscard]] fsim_vpi_handle_v1 net_terminal_object(
        const elaboration::SdfNetDelayTerminal& terminal) const
    {
        if (const auto handle = signal_object(
                terminal.signal, net_terminal_instance(terminal))) {
            return handle;
        }
        const auto handles = simulation_.vpi_signal_object_handles(terminal.signal);
        return handles.empty() ? 0U : handles.front();
    }

    // The published port a terminal names; none for a net or a select, since
    // an intermodule path joins whole ports (IEEE 1800-2023 38.22).
    [[nodiscard]] std::optional<std::uint32_t> net_terminal_port(
        const elaboration::SdfNetDelayTerminal& terminal) const
    {
        // The published port table decides: the endpoint's port flag is
        // not set for the signals of SystemVerilog module ports.
        if (terminal.select_width != 0U) {
            return std::nullopt;
        }
        const auto instance = net_terminal_instance(terminal);
        const auto dot = terminal.object.rfind('.');
        const auto name = dot == std::string::npos
            ? terminal.object
            : terminal.object.substr(dot + 1U);
        const auto& ports = simulation_.systemverilog_vpi_ports();
        for (std::uint32_t index = 0U; index < ports.size(); ++index) {
            if (!ports[index].io_declaration && ports[index].instance == instance
                && ports[index].name == name) {
                return index;
            }
        }
        return std::nullopt;
    }

    // The output port and the input port of an intermodule path; none when
    // the delay is not between two ports.
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
    net_delay_ports(const std::uint32_t delay) const
    {
        const auto& declared = net_delays()[delay];
        if (declared.kind != elaboration::SdfNetDelayKind::interconnect) {
            return std::nullopt;
        }
        const auto output = net_terminal_port(declared.source);
        const auto input = net_terminal_port(declared.destination);
        return output && input
            ? std::optional { std::pair { *output, *input } }
            : std::nullopt;
    }

    // vpi_handle_multi(vpiInterModPath, output port, input port) (IEEE
    // 1800-2023 37.37 detail 1, 38.22).
    fsim_vpi_status_v1 handle_multi(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto fail = [&](const char* const message) -> fsim_vpi_status_v1 {
            error_message_ = message;
            error_ = true;
            return FSIM_VPI_STATUS_NOT_FOUND;
        };
        if (static_cast<PLI_INT32>(request.argument) != vpiInterModPath) {
            return fail("vpi_handle_multi: only vpiInterModPath is defined "
                        "(IEEE 1800-2023 38.22)");
        }
        const auto output = resolve(request.handle);
        const auto input = resolve(request.user_data);
        if (port_of(output) == nullptr || port_of(input) == nullptr
            || port_of(output)->io_declaration || port_of(input)->io_declaration) {
            return fail("vpi_handle_multi: the references must be ports");
        }
        const auto sized = [&](const fsim_vpi_handle_v1 handle) {
            return port_width(*port_of(handle));
        };
        if (sized(output) != sized(input)) {
            return fail("vpi_handle_multi: the ports must be of the same size "
                        "(IEEE 1800-2023 38.22)");
        }
        for (std::uint32_t delay = 0U; delay < net_delays().size(); ++delay) {
            const auto ports = net_delay_ports(delay);
            if (ports && ports->first == serial_of(output)
                && ports->second == serial_of(input)) {
                return succeed(result, tagged(Tag::inter_mod_path, delay));
            }
        }
        return succeed(result);
    }

    // The transitions an intermodule path reports: rise, fall and turn-off.
    // The turn-off delay is the smaller of the 0->z and 1->z delays (IEEE
    // 1800-2023 30.5.2).
    [[nodiscard]] std::array<runtime::SimulationTick, 3U> net_delay_triple(
        const std::vector<runtime::SimulationTick>& table) const
    {
        return { table[0], table[1], std::min(table[2], table[4]) };
    }

    // The delays a caller sees, a vpi_put_delays value first.
    [[nodiscard]] const std::vector<runtime::SimulationTick>& net_delay_table(
        const std::uint32_t delay) const
    {
        const auto changed = net_delay_overrides_.find(delay);
        return changed != net_delay_overrides_.end()
            ? changed->second
            : net_delays()[delay].delays;
    }

    // A delay in the requested format, scaled to the time unit of the module
    // the delay device belongs to (IEEE 1800-2023 38.10).
    void write_scaled_delay(s_vpi_time& out, const runtime::SimulationTick ticks,
        const PLI_INT32 type, const fsim_vpi_handle_v1 instance) const
    {
        const auto profile = simulation_.systemverilog_vpi_time().profile();
        const auto quanta = ticks * profile.tick_multiplier;
        out.type = type;
        if (type == vpiScaledRealTime) {
            const auto scale = module_time_scale(instance);
            const int unit = scale ? scale->first
                                   : static_cast<int>(profile.unit_exponent);
            out.real = static_cast<double>(quanta)
                * std::pow(10.0, static_cast<int>(profile.precision_exponent) - unit);
            return;
        }
        out.high = static_cast<PLI_UINT32>(quanta >> 32U);
        out.low = static_cast<PLI_UINT32>(quanta & 0xFFFFFFFFU);
    }

    [[nodiscard]] static bool valid_net_delay_request(const s_vpi_delay* const delays)
    {
        return delays != nullptr && delays->da != nullptr
            && (delays->no_of_delays == 2 || delays->no_of_delays == 3)
            && (delays->time_type == vpiScaledRealTime
                || delays->time_type == vpiSimTime);
    }

    // vpi_get_delays on an intermodule path: two (rise, fall) or three (and
    // turn-off) delays, each repeated for min, typ and max with mtm_flag and
    // followed by its reject and error limits with pulsere_flag (IEEE
    // 1800-2023 38.10 Table 38-2). SDF gives one value per delay, so min, typ
    // and max are equal, and a pulse is rejected and errors at the delay.
    fsim_vpi_status_v1 net_get_delays(const std::uint32_t delay,
        const s_vpi_delay* const delays, fsim_vpi_service_result_v1& result)
    {
        if (!valid_net_delay_request(delays)) {
            error_message_ = "vpi_get_delays: an intermodule path takes two or "
                             "three delays (IEEE 1800-2023 38.10)";
            error_ = true;
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto triple = net_delay_triple(net_delay_table(delay));
        const auto instance = instance_by_path(net_delays()[delay].instance);
        const std::size_t values = delays->mtm_flag != 0 ? 3U : 1U;
        const std::size_t kinds = delays->pulsere_flag != 0 ? 3U : 1U;
        for (std::size_t index = 0U;
             index < static_cast<std::size_t>(delays->no_of_delays); ++index) {
            for (std::size_t kind = 0U; kind < kinds; ++kind) {
                for (std::size_t value = 0U; value < values; ++value) {
                    write_scaled_delay(
                        delays->da[(index * kinds + kind) * values + value],
                        triple[index], delays->time_type, instance);
                }
            }
        }
        return succeed(result);
    }

    // vpi_put_delays on an intermodule path replaces, or with append_flag adds
    // to, the rise and fall delays and, given three, the turn-off delay; the
    // typical value of each triple is kept (IEEE 1800-2023 38.32).
    fsim_vpi_status_v1 net_put_delays(const std::uint32_t delay,
        const s_vpi_delay* const delays, fsim_vpi_service_result_v1& result)
    {
        if (!valid_net_delay_request(delays)) {
            error_message_ = "vpi_put_delays: an intermodule path takes two or "
                             "three delays (IEEE 1800-2023 38.32)";
            error_ = true;
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto instance = instance_by_path(net_delays()[delay].instance);
        const std::size_t count = static_cast<std::size_t>(delays->no_of_delays);
        const std::size_t values = delays->mtm_flag != 0 ? 3U : 1U;
        const std::size_t kinds = delays->pulsere_flag != 0 ? 3U : 1U;
        const auto previous = net_delay_triple(net_delay_table(delay));
        std::vector<runtime::SimulationTick> table;
        table.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            auto element = delays->da[index * kinds * values + values / 2U];
            element.type = delays->time_type;
            const auto converted = simulation_.systemverilog_vpi_time().convert_delay(
                delay_of(element, instance));
            if (!converted) {
                error_message_ = "vpi_put_delays: the delay is not a valid time";
                error_ = true;
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            table.push_back(delays->append_flag != 0
                    ? previous[index] + converted.ticks
                    : converted.ticks);
        }
        if (count == 2U) {
            // Without a turn-off delay it follows from the other two.
            table.push_back(std::min(table[0], table[1]));
        }
        const auto expanded = expanded_path_delays(table);
        net_delay_overrides_[delay].assign(expanded.begin(), expanded.end());
        return succeed(result);
    }

    // The net or port a delay term is on; vpi_get_value reads it (IEEE
    // 1800-2023 37.45).
    [[nodiscard]] fsim_vpi_handle_v1 delay_term_object(
        const fsim_vpi_handle_v1 term) const
    {
        const auto delay = net_delay_index(term, Tag::delay_term);
        if (!delay) {
            return 0U;
        }
        const auto& declared = net_delays()[*delay];
        return net_terminal_object(
            (serial_of(term) & 1U) != 0U ? declared.destination : declared.source);
    }

    // vpi_get on an intermodule path, a delay device or a delay term;
    // nullopt for any other handle (IEEE 1800-2023 37.37, 37.45).
    [[nodiscard]] std::optional<std::int64_t> net_delay_property(
        const fsim_vpi_handle_v1 handle, const PLI_INT32 property) const
    {
        if (!net_delay_handle(handle)) {
            return std::nullopt;
        }
        if (property != vpiDelayType || net_delay_index(handle, Tag::inter_mod_path)) {
            return vpiUndefined;
        }
        const auto delay = owned(handle) && tag_of(handle) == Tag::delay_term
            ? net_delay_index(handle, Tag::delay_term)
            : net_delay_index(handle, Tag::delay_device);
        return net_delays()[*delay].kind == elaboration::SdfNetDelayKind::interconnect
            ? vpiInterModPathDelay
            : vpiMIPDelay;
    }

    // vpi_handle on a delay device or a delay term; the device belongs to its
    // module and a term to its device (IEEE 1800-2023 37.45).
    [[nodiscard]] fsim_vpi_handle_v1 net_delay_relation(
        const fsim_vpi_handle_v1 handle, const PLI_INT32 type) const
    {
        if (const auto delay = net_delay_index(handle, Tag::delay_term)) {
            return type == vpiParent ? tagged(Tag::delay_device, *delay) : 0U;
        }
        if (const auto delay = net_delay_index(handle, Tag::delay_device)) {
            const auto instance = instance_by_path(net_delays()[*delay].instance);
            return type == vpiModule ? enclosing_module_of(instance)
                : type == vpiParent  ? instance
                                     : 0U;
        }
        return 0U;
    }

    // vpi_iterate on an intermodule path (its ports), a delay device (its
    // terms) and a delay term (what drives its net or what it loads); nullopt
    // for any other reference (IEEE 1800-2023 37.37, 37.45).
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>>
    net_delay_iteration(const PLI_INT32 type, const fsim_vpi_handle_v1 reference)
    {
        std::vector<fsim_vpi_handle_v1> found;
        if (const auto delay = net_delay_index(reference, Tag::inter_mod_path)) {
            if (const auto ports = net_delay_ports(*delay); ports && type == vpiPort) {
                found = { tagged(Tag::port, ports->first),
                    tagged(Tag::port, ports->second) };
            }
        } else if (const auto device = net_delay_index(reference, Tag::delay_device)) {
            if (type == vpiInTerm || type == vpiOutTerm) {
                found.push_back(delay_term_handle(*device, type == vpiOutTerm));
            }
        } else if (net_delay_index(reference, Tag::delay_term)) {
            const bool output = (serial_of(reference) & 1U) != 0U;
            if (type == (output ? vpiLoad : vpiDriver)) {
                if (const auto net = delay_term_object(reference)) {
                    found = connections_of(type, net);
                }
            }
        } else {
            return std::nullopt;
        }
        return found;
    }

    // The delay terms on the net or variable `handle`: an output term drives
    // it and an input term loads it (IEEE 1800-2023 37.45, 37.46).
    void append_net_delay_connections(const PLI_INT32 type,
        const fsim_vpi_handle_v1 handle,
        std::vector<fsim_vpi_handle_v1>& found) const
    {
        const auto& all = net_delays();
        for (std::uint32_t delay = 0U; delay < all.size(); ++delay) {
            for (const bool output : { false, true }) {
                const auto& terminal = output ? all[delay].destination
                                              : all[delay].source;
                const bool wanted = type == vpiUse
                    || (output ? type == vpiDriver || type == vpiLocalDriver
                               : type == vpiLoad || type == vpiLocalLoad);
                if (!wanted) {
                    continue;
                }
                const auto handles
                    = simulation_.vpi_signal_object_handles(terminal.signal);
                if (std::ranges::find(handles, handle) != handles.end()) {
                    found.push_back(delay_term_handle(delay, output));
                }
            }
        }
    }

    // --- timing checks (IEEE 1800-2023 37.40) ------------------------------

    [[nodiscard]] std::optional<std::uint32_t> tchk_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::tchk
            || serial_of(handle)
                >= simulation_.design().verilog_timing_checks().size()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(serial_of(handle));
    }

    // A terminal is the check's index and whether it is the data event.
    [[nodiscard]] static fsim_vpi_handle_v1 tchkterm_handle(
        const std::uint32_t check, const bool data)
    {
        return tagged(Tag::tchk_term,
            (static_cast<std::uint64_t>(check) << 1U) | (data ? 1U : 0U));
    }

    [[nodiscard]] std::optional<std::pair<std::uint32_t, bool>> tchkterm_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::tchk_term) {
            return std::nullopt;
        }
        const auto serial = serial_of(handle);
        const auto check = static_cast<std::uint32_t>(serial >> 1U);
        const bool data = (serial & 1U) != 0U;
        const auto& checks = simulation_.design().verilog_timing_checks();
        if (check >= checks.size() || (data && !checks[check].data)) {
            return std::nullopt;
        }
        return std::pair { check, data };
    }

    [[nodiscard]] const runtime::simir::ModuleTimingEvent& tchkterm_event(
        const std::pair<std::uint32_t, bool>& term) const
    {
        const auto& check = simulation_.design().verilog_timing_checks()[term.first];
        return term.second ? *check.data : check.reference;
    }

    // The instance that declares a timing check.
    [[nodiscard]] fsim_vpi_handle_v1 tchk_instance(const std::uint32_t check) const
    {
        const auto found = registry_.find(
            simulation_.design().verilog_timing_checks()[check].instance);
        return found && found.value ? found.value->handle : 0U;
    }

    // vpiTchkType of a check kind (IEEE 1800-2023 37.40).
    [[nodiscard]] static std::int64_t tchk_type(
        const runtime::simir::ModuleTimingCheckKind kind)
    {
        using Kind = runtime::simir::ModuleTimingCheckKind;
        switch (kind) {
        case Kind::setup:
            return vpiSetup;
        case Kind::hold:
            return vpiHold;
        case Kind::recovery:
            return vpiRecovery;
        case Kind::removal:
            return vpiRemoval;
        case Kind::skew:
            return vpiSkew;
        case Kind::period:
            return vpiPeriod;
        case Kind::width:
            return vpiWidth;
        case Kind::setuphold:
            return vpiSetupHold;
        case Kind::recrem:
            return vpiRecrem;
        case Kind::timeskew:
            return vpiTimeskew;
        case Kind::fullskew:
            return vpiFullskew;
        case Kind::nochange:
            return vpiNoChange;
        }
        return vpiUndefined;
    }

    // vpiEdge of a timing-check terminal: the transitions it selects, from
    // `posedge`/`negedge` or an `edge` descriptor list (IEEE 1800-2023
    // 31.5, vpi_user.h). z transitions have no mask bit.
    [[nodiscard]] static std::int64_t tchk_edge_mask(
        const runtime::simir::ModuleTimingEvent& event)
    {
        using Edge = runtime::simir::ModulePathEdge;
        if (event.edge == Edge::posedge) {
            return vpiPosedge;
        }
        if (event.edge == Edge::negedge) {
            return vpiNegedge;
        }
        std::int64_t mask = 0;
        for (const auto& descriptor : event.edge_descriptors) {
            if (descriptor == "01") {
                mask |= vpiEdge01;
            } else if (descriptor == "10") {
                mask |= vpiEdge10;
            } else if (descriptor == "0x" || descriptor == "0X") {
                mask |= vpiEdge0x;
            } else if (descriptor == "x1" || descriptor == "X1") {
                mask |= vpiEdgex1;
            } else if (descriptor == "1x" || descriptor == "1X") {
                mask |= vpiEdge1x;
            } else if (descriptor == "x0" || descriptor == "X0") {
                mask |= vpiEdgex0;
            }
        }
        return mask;
    }

    fsim_vpi_status_v1 tchk_property(const std::uint32_t check,
        const PLI_INT32 property, fsim_vpi_service_result_v1& result) const
    {
        const auto& declared = simulation_.design().verilog_timing_checks()[check];
        std::int64_t value = vpiUndefined;
        if (property == vpiTchkType) {
            value = tchk_type(declared.kind);
        } else if (property == vpiLineNo) {
            value = declared.source.line;
        }
        return succeed(result, 0U, static_cast<std::uint64_t>(value));
    }

    // --- primitives and UDPs (IEEE 1800-2023 37.35, 37.36) ---------------

    // The elaborated primitive records indexed by scope and full name; the
    // `head` of a primitive is the first element of its instance array.
    struct PrimitiveIndex {
        std::unordered_map<std::string, std::uint32_t> by_name;
        std::unordered_map<std::string, std::vector<std::uint32_t>> by_scope;
        std::vector<std::uint32_t> head;
    };

    [[nodiscard]] const std::vector<elaboration::VerilogPrimitiveInfo>&
    primitives() const
    {
        return simulation_.design().verilog_primitives();
    }

    [[nodiscard]] const PrimitiveIndex& primitive_index() const
    {
        if (primitive_index_) {
            return *primitive_index_;
        }
        PrimitiveIndex index;
        std::unordered_map<std::string, std::uint32_t> arrays;
        const auto& all = primitives();
        index.head.resize(all.size());
        for (std::uint32_t position = 0U; position < all.size(); ++position) {
            const auto& primitive = all[position];
            index.by_scope[primitive.instance].push_back(position);
            if (!primitive.full_name.empty()) {
                index.by_name.emplace(primitive.full_name, position);
            }
            index.head[position] = position;
            if (primitive.array_member && !primitive.array_name.empty()) {
                const auto first = arrays.emplace(
                    primitive.instance + '\n' + primitive.array_name, position);
                index.head[position] = first.first->second;
            }
        }
        primitive_index_ = std::move(index);
        return *primitive_index_;
    }

    [[nodiscard]] const elaboration::VerilogPrimitiveInfo* primitive_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::primitive
            || serial_of(handle) >= primitives().size()) {
            return nullptr;
        }
        return &primitives()[serial_of(handle)];
    }

    // An instance array is named by the index of its first element.
    [[nodiscard]] std::optional<std::uint32_t> primitive_array_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::primitive_array
            || serial_of(handle) >= primitives().size()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(serial_of(handle));
    }

    [[nodiscard]] static fsim_vpi_handle_v1 prim_term_handle(
        const std::uint32_t primitive, const std::uint32_t term)
    {
        return tagged(Tag::prim_term,
            (static_cast<std::uint64_t>(primitive) << 16U) | term);
    }

    // The primitive and the zero-based terminal of a prim term.
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
    prim_term_of(const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::prim_term) {
            return std::nullopt;
        }
        const auto primitive = static_cast<std::uint32_t>(serial_of(handle) >> 16U);
        const auto term = static_cast<std::uint32_t>(serial_of(handle) & 0xFFFFU);
        if (primitive >= primitives().size()
            || term >= primitives()[primitive].terminals.size()) {
            return std::nullopt;
        }
        return std::pair { primitive, term };
    }

    [[nodiscard]] const elaboration::UdpTableInfo* udp_table_of(
        const fsim_vpi_handle_v1 handle, const Tag tag) const
    {
        if (!owned(handle) || tag_of(handle) != tag
            || serial_of(handle) >= simulation_.design().udp_tables().size()) {
            return nullptr;
        }
        return &simulation_.design().udp_tables()[serial_of(handle)];
    }

    [[nodiscard]] std::uint64_t udp_table_serial(
        const elaboration::UdpTableInfo& table) const
    {
        return static_cast<std::uint64_t>(
            &table - simulation_.design().udp_tables().data());
    }

    // The table and the row of a table entry.
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
    udp_entry_of(const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::udp_entry) {
            return std::nullopt;
        }
        const auto table = static_cast<std::uint32_t>(serial_of(handle) >> 24U);
        const auto row = static_cast<std::uint32_t>(serial_of(handle) & 0xFFFFFFU);
        const auto& tables = simulation_.design().udp_tables();
        if (table >= tables.size() || row >= tables[table].row_text.size()) {
            return std::nullopt;
        }
        return std::pair { table, row };
    }

    // The UDP table a UDP instance runs.
    [[nodiscard]] const elaboration::UdpTableInfo* primitive_table(
        const elaboration::VerilogPrimitiveInfo& primitive) const
    {
        const auto& tables = simulation_.design().udp_tables();
        return primitive.udp_table && *primitive.udp_table < tables.size()
            ? &tables[*primitive.udp_table]
            : nullptr;
    }

    // vpiGate, vpiSwitch or vpiUdp of a primitive, or the array type of
    // one (IEEE 1800-2023 37.35); a pullup or pulldown is a gate.
    [[nodiscard]] static PLI_INT32 primitive_class(
        const elaboration::VerilogPrimitiveKind kind, const bool array)
    {
        switch (kind) {
        case elaboration::VerilogPrimitiveKind::switch_device:
            return array ? vpiSwitchArray : vpiSwitch;
        case elaboration::VerilogPrimitiveKind::udp:
            return array ? vpiUdpArray : vpiUdp;
        case elaboration::VerilogPrimitiveKind::gate:
        case elaboration::VerilogPrimitiveKind::pull:
            break;
        }
        return array ? vpiGateArray : vpiGate;
    }

    // Whether a primitive of `kind` is of an iteration type.
    [[nodiscard]] static bool primitive_matches(const PLI_INT32 type,
        const elaboration::VerilogPrimitiveKind kind)
    {
        return type == vpiPrimitive || type == vpiPrimitiveArray
            || type == primitive_class(kind, false)
            || type == primitive_class(kind, true);
    }

    [[nodiscard]] PLI_INT32 primitive_prim_type(
        const elaboration::VerilogPrimitiveInfo& primitive) const
    {
        if (primitive.kind == elaboration::VerilogPrimitiveKind::udp) {
            const auto* table = primitive_table(primitive);
            return table == nullptr ? vpiUndefined
                : table->sequential ? vpiSeqPrim
                                    : vpiCombPrim;
        }
        return primitive_type(primitive.keyword);
    }

    // vpiSize: the number of inputs, which is every terminal but the
    // outputs (IEEE 1800-2023 37.35 detail 1).
    [[nodiscard]] static std::int64_t primitive_size(
        const elaboration::VerilogPrimitiveInfo& primitive)
    {
        return std::ranges::count_if(primitive.terminals, [](const auto& term) {
            return term.direction
                != frontend::VerilogPrimitiveTerminalDirection::Output;
        });
    }

    // The vpiStrength0/1 constant of a drive-strength rank (IEEE 1800-2023
    // 38.15 strength coding).
    [[nodiscard]] static std::int64_t strength_code(const std::size_t rank)
    {
        static constexpr std::array<std::int64_t, 8U> codes {
            vpiHiZ, vpiSmallCharge, vpiMediumCharge, vpiWeakDrive,
            vpiLargeCharge, vpiPullDrive, vpiStrongDrive, vpiSupplyDrive,
        };
        return rank < codes.size() ? codes[rank] : vpiUndefined;
    }

    // vpiStrength0/1 of a primitive. A gate or UDP without a drive strength
    // is strong; a pullup has only a 1-strength and a pulldown only a
    // 0-strength, both pull by default (IEEE 1800-2023 28.8). A switch has
    // no drive strength.
    [[nodiscard]] static std::int64_t primitive_strength(
        const elaboration::VerilogPrimitiveInfo& primitive, const bool one)
    {
        constexpr auto strong
            = static_cast<std::size_t>(frontend::VerilogStrength::Strong);
        constexpr auto pull
            = static_cast<std::size_t>(frontend::VerilogStrength::Pull);
        const auto declared = one ? primitive.drive_one : primitive.drive_zero;
        switch (primitive.kind) {
        case elaboration::VerilogPrimitiveKind::switch_device:
            return vpiUndefined;
        case elaboration::VerilogPrimitiveKind::pull: {
            const bool pullup = primitive.keyword == "pullup";
            return pullup == one ? strength_code(declared ? *declared : pull)
                                 : vpiUndefined;
        }
        case elaboration::VerilogPrimitiveKind::gate:
        case elaboration::VerilogPrimitiveKind::udp:
            break;
        }
        return strength_code(declared ? *declared : strong);
    }

    // The value a terminal shows: the bits of its signal it selects. A
    // terminal that is an expression has no signal here.
    [[nodiscard]] std::optional<Value> prim_term_value(
        const elaboration::VerilogPrimitiveTerminalInfo& term) const
    {
        if (term.width == 0U) {
            return std::nullopt;
        }
        const auto bits = simulation_.read_signal_snapshot(term.signal);
        Value value;
        value.bits = PackedLogic4 { term.width, Logic4::x };
        for (std::uint32_t bit = 0U; bit < term.width; ++bit) {
            if (term.offset + bit < bits.width()) {
                value.bits.set(bit, bits.get(term.offset + bit));
            }
        }
        return value;
    }

    // The value of a primitive is its output's (IEEE 1800-2023 37.35); a
    // bidirectional switch has no output.
    [[nodiscard]] static const elaboration::VerilogPrimitiveTerminalInfo*
    primitive_output(const elaboration::VerilogPrimitiveInfo& primitive)
    {
        for (const auto& term : primitive.terminals) {
            if (term.direction
                == frontend::VerilogPrimitiveTerminalDirection::Output) {
                return &term;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::optional<Value> primitive_model_value(
        const fsim_vpi_handle_v1 handle) const
    {
        if (const auto* primitive = primitive_of(handle)) {
            const auto* output = primitive_output(*primitive);
            return output != nullptr ? prim_term_value(*output) : std::nullopt;
        }
        if (const auto term = prim_term_of(handle)) {
            return prim_term_value(
                primitives()[term->first].terminals[term->second]);
        }
        return std::nullopt;
    }

    // vpi_get_value with vpiStrengthVal on a scalar (IEEE 1800-2023 38.15):
    // the logic value and the strength of each side from the signal's
    // drivers. A net or terminal that is not a whole one-bit signal, and a
    // variable, is strong on the side its value takes ("a reg or variable
    // is always strong").
    [[nodiscard]] bool write_strength_value(
        const fsim_vpi_handle_v1 handle, s_vpi_value& out)
    {
        const elaboration::VerilogPrimitiveTerminalInfo* terminal { };
        if (const auto* primitive = primitive_of(handle)) {
            terminal = primitive_output(*primitive);
        } else if (const auto term = prim_term_of(handle)) {
            terminal = &primitives()[term->first].terminals[term->second];
        }
        std::optional<runtime::simir::SignalId> signal;
        std::optional<Value> value;
        if (terminal != nullptr) {
            value = prim_term_value(*terminal);
            if (terminal->width == 1U
                && simulation_.read_signal_snapshot(terminal->signal).width() == 1U) {
                signal = terminal->signal;
            }
        } else if (!owned(handle)) {
            const auto object = info(handle);
            if (!object || !object->type || object->type->width != 1U
                || (object->kind != SystemVerilogVpiObjectKind::Net
                    && object->kind != SystemVerilogVpiObjectKind::Variable)) {
                return false;
            }
            value = object_value(handle);
            if (object->kind == SystemVerilogVpiObjectKind::Net) {
                signal = simulation_.find_signal(object->full_name);
                if (signal
                    && simulation_.read_signal_snapshot(*signal).width() != 1U) {
                    signal.reset();
                }
            }
        }
        if (!value || value->kind != Value::Kind::bits
            || value->bits.width() == 0U) {
            return false;
        }
        const auto bit = value->bits.get(0U);
        constexpr auto strong
            = static_cast<std::size_t>(runtime::simir::StrengthRank::strong);
        std::size_t zero = bit == Logic4::zero || bit == Logic4::x ? strong : 0U;
        std::size_t one = bit == Logic4::one || bit == Logic4::x ? strong : 0U;
        if (signal) {
            const auto drive = simulation_.read_signal_strength(*signal);
            zero = static_cast<std::size_t>(drive.zero);
            one = static_cast<std::size_t>(drive.one);
        }
        auto& slot = strengths_[strength_cursor_++ % strengths_.size()];
        slot.logic = bit == Logic4::zero ? vpi0
            : bit == Logic4::one         ? vpi1
            : bit == Logic4::z           ? vpiZ
                                         : vpiX;
        slot.s0 = static_cast<PLI_INT32>(strength_code(zero));
        slot.s1 = static_cast<PLI_INT32>(strength_code(one));
        out.value.strength = &slot;
        return true;
    }

    [[nodiscard]] fsim_vpi_handle_v1 primitive_scope(
        const elaboration::VerilogPrimitiveInfo& primitive) const
    {
        const auto scope = registry_.find(primitive.instance);
        return scope && scope.value ? scope.value->handle : 0U;
    }

    // The net a terminal connects, or the select of it the terminal names
    // (IEEE 1800-2023 37.35 vpiExpr). A terminal that is an expression has
    // no object.
    [[nodiscard]] fsim_vpi_handle_v1 prim_term_expression(
        const elaboration::VerilogPrimitiveInfo& primitive,
        const elaboration::VerilogPrimitiveTerminalInfo& term)
    {
        if (term.width == 0U) {
            return 0U;
        }
        const auto net = signal_object(term.signal, primitive_scope(primitive));
        const auto object = info(net);
        if (!object || !object->type) {
            return 0U;
        }
        if (term.offset == 0U && term.width == object->type->width) {
            return net;
        }
        const auto range = declared_range(net);
        if (!range) {
            return 0U;
        }
        const auto [msb, lsb] = *range;
        const auto index_of = [&](const std::uint32_t position) {
            return msb >= lsb ? lsb + static_cast<std::int64_t>(position)
                              : lsb - static_cast<std::int64_t>(position);
        };
        return make_select(net, index_of(term.offset + term.width - 1U),
            index_of(term.offset), term.width == 1U);
    }

    [[nodiscard]] static std::string udp_defn_name(
        const elaboration::UdpTableInfo& table)
    {
        const auto separator = table.identity.rfind('.');
        return separator == std::string::npos
            ? table.identity
            : table.identity.substr(separator + 1U);
    }

    // The number of symbols of a table entry: the inputs, the current
    // state of a sequential UDP, and the output (IEEE 1800-2023 37.36).
    [[nodiscard]] static std::int64_t udp_entry_size(
        const elaboration::UdpTableInfo& table, const std::size_t row)
    {
        return static_cast<std::int64_t>(table.rows[row].inputs.size()
            + (table.rows[row].current_state ? 1U : 0U) + 1U);
    }

    // The entry's symbols as characters: an edge `(01)` is the two
    // characters 01 (IEEE 1800-2023 29.3.6).
    [[nodiscard]] static std::vector<std::string> udp_entry_symbols(
        const std::string_view text)
    {
        std::vector<std::string> symbols;
        std::size_t position = 0U;
        while (position < text.size()) {
            const auto end = std::min(text.find(' ', position), text.size());
            auto token = std::string { text.substr(position, end - position) };
            position = end + 1U;
            if (token.empty() || token == ":") {
                continue;
            }
            if (token.size() > 2U && token.front() == '(' && token.back() == ')') {
                token = token.substr(1U, token.size() - 2U);
            }
            symbols.push_back(std::move(token));
        }
        return symbols;
    }

    // vpi_get_value on a table entry: only the string (decompilation) and
    // vector (ASCII values) formats exist (IEEE 1800-2023 37.36 detail 1,
    // 38.15). A symbol takes two bytes, its first character in the high
    // one; they pack two to a 32-bit field, the a-field of each vecval
    // before its b-field.
    [[nodiscard]] bool write_entry_value(
        const std::uint32_t table, const std::uint32_t row, s_vpi_value& out)
    {
        const auto& text = simulation_.design().udp_tables()[table].row_text[row];
        if (out.format == vpiStringVal) {
            std::string compact;
            std::ranges::copy_if(text, std::back_inserter(compact),
                [](const char character) { return character != ' '; });
            out.value.str = const_cast<PLI_BYTE8*>(store(std::move(compact)));
            return true;
        }
        if (out.format != vpiVectorVal) {
            return false;
        }
        const auto symbols = udp_entry_symbols(text);
        auto& words = vectors_[vector_cursor_++ % vectors_.size()];
        words.assign((symbols.size() + 3U) / 4U, s_vpi_vecval { 0, 0 });
        for (std::size_t index = 0U; index < symbols.size(); ++index) {
            const auto first = static_cast<PLI_UINT32>(
                static_cast<unsigned char>(symbols[index][0]));
            const auto second = symbols[index].size() > 1U
                ? static_cast<PLI_UINT32>(
                      static_cast<unsigned char>(symbols[index][1]))
                : 0U;
            const auto field = index / 2U;
            auto& word = words[field / 2U];
            auto& part = field % 2U == 0U ? word.aval : word.bval;
            part |= ((first << 8U) | second) << (16U * (index % 2U));
        }
        out.value.vector = words.data();
        return true;
    }

    // vpi_get on a primitive-model handle; nullopt when it is none.
    [[nodiscard]] std::optional<std::int64_t> primitive_model_property(
        const fsim_vpi_handle_v1 handle, const PLI_INT32 property) const
    {
        std::int64_t value = vpiUndefined;
        if (const auto* primitive = primitive_of(handle)) {
            switch (property) {
            case vpiPrimType:
                value = primitive_prim_type(*primitive);
                break;
            case vpiSize:
                value = primitive_size(*primitive);
                break;
            case vpiStrength0:
                value = primitive_strength(*primitive, false);
                break;
            case vpiStrength1:
                value = primitive_strength(*primitive, true);
                break;
            case vpiArray:
            case vpiArrayMember:
                value = primitive->array_member ? 1 : 0;
                break;
            case vpiLineNo:
                value = static_cast<std::int64_t>(primitive->source.begin.line);
                break;
            default:
                break;
            }
        } else if (primitive_array_of(handle)) {
            return value;
        } else if (const auto term = prim_term_of(handle)) {
            const auto& terminal = primitives()[term->first].terminals[term->second];
            using Direction = frontend::VerilogPrimitiveTerminalDirection;
            if (property == vpiDirection) {
                value = terminal.direction == Direction::Output ? vpiOutput
                    : terminal.direction == Direction::Inout    ? vpiInout
                                                                : vpiInput;
            } else if (property == vpiTermIndex) {
                value = term->second;
            }
        } else if (const auto* table = udp_table_of(handle, Tag::udp_defn)) {
            switch (property) {
            case vpiSize:
                value = static_cast<std::int64_t>(table->terminals.size()) - 1;
                break;
            case vpiPrimType:
                value = table->sequential ? vpiSeqPrim : vpiCombPrim;
                break;
            // The source of a UDP is never protected here (IEEE 1800-2023
            // 37.36).
            case vpiProtected:
                value = 0;
                break;
            default:
                break;
            }
        } else if (const auto entry = udp_entry_of(handle)) {
            if (property == vpiSize) {
                value = udp_entry_size(
                    simulation_.design().udp_tables()[entry->first], entry->second);
            }
        } else if (udp_table_of(handle, Tag::udp_initial) == nullptr) {
            return std::nullopt;
        }
        return value;
    }

    // vpi_get_str on a primitive-model handle; nullopt when it has none.
    [[nodiscard]] std::optional<std::string> primitive_model_string(
        const fsim_vpi_handle_v1 handle, const PLI_INT32 property) const
    {
        if (const auto* primitive = primitive_of(handle)) {
            switch (property) {
            case vpiName:
                return primitive->name.empty() ? std::nullopt
                    : std::optional { primitive->name };
            case vpiFullName:
                return primitive->full_name.empty() ? std::nullopt
                    : std::optional { primitive->full_name };
            case vpiDefName:
                return primitive->keyword;
            case vpiFile:
                return primitive->source.source_name.str();
            default:
                return std::nullopt;
            }
        }
        if (const auto head = primitive_array_of(handle)) {
            const auto& first = primitives()[*head];
            if (property == vpiName) {
                return first.array_name;
            }
            if (property == vpiFullName) {
                return first.instance.empty()
                    ? first.array_name
                    : first.instance + "." + first.array_name;
            }
            return std::nullopt;
        }
        if (const auto* table = udp_table_of(handle, Tag::udp_defn)) {
            return property == vpiDefName
                ? std::optional { udp_defn_name(*table) }
                : std::nullopt;
        }
        return std::nullopt;
    }

    // The relation `type` of a primitive-model handle (IEEE 1800-2023
    // 37.35, 37.36).
    [[nodiscard]] fsim_vpi_handle_v1 primitive_model_relation(
        const fsim_vpi_handle_v1 handle, const PLI_INT32 type)
    {
        const auto scope_relation
            = [&](const elaboration::VerilogPrimitiveInfo& primitive)
            -> fsim_vpi_handle_v1 {
            const auto scope = primitive_scope(primitive);
            return type == vpiModule ? enclosing_module_of(scope)
                : type == vpiParent || type == vpiInstance || type == vpiScope
                ? scope
                : 0U;
        };
        if (const auto* primitive = primitive_of(handle)) {
            if (type == vpiUdpDefn && primitive->udp_table) {
                return tagged(Tag::udp_defn, *primitive->udp_table);
            }
            // The index within an instance array, NULL outside one (IEEE
            // 1800-2023 37.35 detail 4).
            if (type == vpiIndex) {
                return primitive->array_member
                    ? make_constant(primitive->array_index)
                    : 0U;
            }
            if (type == vpiPrimitiveArray) {
                return primitive->array_member
                    ? tagged(Tag::primitive_array,
                          primitive_index().head[serial_of(handle)])
                    : 0U;
            }
            return scope_relation(*primitive);
        }
        if (const auto head = primitive_array_of(handle)) {
            const auto& first = primitives()[*head];
            if (type == vpiLeftRange) {
                return make_constant(first.array_left);
            }
            if (type == vpiRightRange) {
                return make_constant(first.array_right);
            }
            return scope_relation(first);
        }
        if (const auto term = prim_term_of(handle)) {
            const auto& primitive = primitives()[term->first];
            if (type == vpiExpr) {
                return prim_term_expression(
                    primitive, primitive.terminals[term->second]);
            }
            return type == vpiPrimitive || type == vpiParent
                ? tagged(Tag::primitive, term->first)
                : 0U;
        }
        if (const auto* table = udp_table_of(handle, Tag::udp_defn)) {
            return type == vpiInitial && table->initial_output
                ? tagged(Tag::udp_initial, udp_table_serial(*table))
                : 0U;
        }
        if (const auto entry = udp_entry_of(handle)) {
            return type == vpiUdpDefn || type == vpiParent
                ? tagged(Tag::udp_defn, entry->first)
                : 0U;
        }
        if (const auto* table = udp_table_of(handle, Tag::udp_initial)) {
            return type == vpiUdpDefn || type == vpiParent
                ? tagged(Tag::udp_defn, udp_table_serial(*table))
                : 0U;
        }
        return 0U;
    }

    // The objects of an iteration over primitives, prim terms, UDP
    // definitions, table entries or initial statements; nullopt when the
    // reference has none (IEEE 1800-2023 37.35, 37.36).
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>>
    primitive_iteration(const PLI_INT32 type, const fsim_vpi_handle_v1 reference)
    {
        std::vector<fsim_vpi_handle_v1> found;
        const bool element_type = type == vpiPrimitive || type == vpiGate
            || type == vpiSwitch || type == vpiUdp;
        const bool array_type = type == vpiPrimitiveArray || type == vpiGateArray
            || type == vpiSwitchArray || type == vpiUdpArray;
        if (owned(reference)
            ? tag_of(reference) != Tag::primitive
                && tag_of(reference) != Tag::primitive_array
                && tag_of(reference) != Tag::udp_defn
            : reference == 0U ? type != vpiUdpDefn
                              : !element_type && !array_type) {
            return std::nullopt;
        }
        const auto& all = primitives();
        const auto& index = primitive_index();
        if (reference == 0U) {
            std::vector<bool> used(simulation_.design().udp_tables().size());
            for (const auto& primitive : all) {
                if (primitive.udp_table && *primitive.udp_table < used.size()
                    && !used[*primitive.udp_table]) {
                    used[*primitive.udp_table] = true;
                    found.push_back(tagged(Tag::udp_defn, *primitive.udp_table));
                }
            }
            return found;
        }
        if (owned(reference)) {
            if (const auto head = primitive_array_of(reference)) {
                const auto& first = all[*head];
                if (type == vpiRange) {
                    found.push_back(make_range(
                        reference, first.array_left, first.array_right));
                } else if (const auto scope = index.by_scope.find(first.instance);
                           element_type && scope != index.by_scope.end()) {
                    for (const auto position : scope->second) {
                        if (all[position].array_member
                            && index.head[position] == *head
                            && primitive_matches(type, all[position].kind)) {
                            found.push_back(tagged(Tag::primitive, position));
                        }
                    }
                }
                return found;
            }
            if (const auto* primitive = primitive_of(reference)) {
                if (type == vpiPrimTerm) {
                    for (std::uint32_t term = 0U;
                         term < primitive->terminals.size(); ++term) {
                        found.push_back(prim_term_handle(
                            static_cast<std::uint32_t>(serial_of(reference)), term));
                    }
                }
                return found;
            }
            if (const auto* table = udp_table_of(reference, Tag::udp_defn)) {
                const auto serial = udp_table_serial(*table);
                if (type == vpiTableEntry) {
                    for (std::size_t row = 0U; row < table->row_text.size(); ++row) {
                        found.push_back(
                            tagged(Tag::udp_entry, (serial << 24U) | row));
                    }
                } else if (type == vpiInitial && table->initial_output) {
                    found.push_back(tagged(Tag::udp_initial, serial));
                }
                return found;
            }
            return std::nullopt;
        }
        const auto scope = info(reference);
        if (!scope) {
            return std::nullopt;
        }
        const auto members = index.by_scope.find(scope->full_name);
        if (members == index.by_scope.end()) {
            return found;
        }
        for (const auto position : members->second) {
            const auto& primitive = all[position];
            if (!primitive_matches(type, primitive.kind)) {
                continue;
            }
            if (element_type) {
                found.push_back(tagged(Tag::primitive, position));
            } else if (primitive.array_member && index.head[position] == position) {
                // Each instance array once, at its first element.
                found.push_back(tagged(Tag::primitive_array, position));
            }
        }
        return found;
    }

    // The primitive terminals that drive or load the net or variable
    // `handle` (IEEE 1800-2023 37.35, 37.16): an output terminal drives
    // its signal, an input loads it, an inout does both.
    void append_primitive_connections(const PLI_INT32 type,
        const fsim_vpi_handle_v1 handle,
        std::vector<fsim_vpi_handle_v1>& found) const
    {
        using Direction = frontend::VerilogPrimitiveTerminalDirection;
        const auto& all = primitives();
        for (std::uint32_t position = 0U; position < all.size(); ++position) {
            const auto& terminals = all[position].terminals;
            for (std::uint32_t term = 0U; term < terminals.size(); ++term) {
                const auto& terminal = terminals[term];
                const bool wanted = type == vpiDriver || type == vpiLocalDriver
                    ? terminal.direction != Direction::Input
                    : type == vpiLoad || type == vpiLocalLoad
                    ? terminal.direction != Direction::Output
                    : true;
                if (!wanted || terminal.width == 0U) {
                    continue;
                }
                const auto handles
                    = simulation_.vpi_signal_object_handles(terminal.signal);
                if (std::ranges::find(handles, handle) != handles.end()) {
                    found.push_back(prim_term_handle(position, term));
                }
            }
        }
    }

    // vpi_put_value on a primitive: only a sequential UDP takes a value,
    // set at once whatever the instance's delay (IEEE 1800-2023 37.35
    // detail 2, 38.34).
    fsim_vpi_status_v1 put_primitive_value(
        const elaboration::VerilogPrimitiveInfo& primitive, const PLI_INT32 flags,
        const s_vpi_value* const input, fsim_vpi_service_result_v1& result)
    {
        const auto fail = [&](const char* const message) -> fsim_vpi_status_v1 {
            error_message_ = message;
            error_ = true;
            return FSIM_VPI_STATUS_INVALID_ARGUMENT;
        };
        const auto* table = primitive_table(primitive);
        if (table == nullptr || !table->sequential) {
            return fail("vpi_put_value: only a sequential UDP takes a value "
                        "(IEEE 1800-2023 37.35 detail 2)");
        }
        if ((flags & 0xFFF) != vpiNoDelay) {
            return fail("vpi_put_value: a UDP instance takes only vpiNoDelay "
                        "(IEEE 1800-2023 38.34)");
        }
        if (read_only_) {
            return fail("vpi_put_value: no write in a cbReadOnlySynch callback");
        }
        const auto value = input != nullptr ? input_value(*input, 1U)
                                            : std::nullopt;
        if (!value || value->kind != Value::Kind::bits
            || primitive.terminals.empty() || primitive.terminals.front().width == 0U) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        // The state is 0, 1 or x; z is not a UDP value (IEEE 1800-2023 29.3).
        auto bit = value_bits(*value, 1U).get(0U);
        if (bit == Logic4::z) {
            bit = Logic4::x;
        }
        simulation_.deposit_signal(
            primitive.terminals.front().signal, PackedLogic4 { 1U, bit });
        return succeed(result);
    }

    // --- vpi_get_delays and vpi_put_delays (IEEE 1800-2023 38.10, 38.32) ---

    // One delay of an object in signed scheduler ticks ($setuphold limits may
    // be negative): its minimum, typical and maximum values, the one the
    // simulation uses, and for a path the reject and error limits.
    struct DelayRow {
        std::array<std::int64_t, 3U> delay { };
        std::int64_t applied { };
        std::array<std::int64_t, 3U> reject { };
        std::array<std::int64_t, 3U> error { };
        // The reject and error limits were put; otherwise a path's follow
        // its delay.
        bool limits_set { };
    };
    using DelayRows = std::vector<DelayRow>;

    fsim_vpi_status_v1 fail_delays(const char* const message)
    {
        error_message_ = message;
        error_ = true;
        return FSIM_VPI_STATUS_NOT_FOUND;
    }

    [[nodiscard]] static bool valid_delay_time_type(const PLI_INT32 type)
    {
        return type == vpiScaledRealTime || type == vpiSimTime
            || type == vpiSuppressTime;
    }

    // One time in the requested format; vpiScaledRealTime is in the time unit
    // of `scale_object`'s module, and vpiSuppressTime returns no value
    // (IEEE 1800-2023 38.10).
    void write_delay_time(s_vpi_time& out, const std::int64_t ticks,
        const PLI_INT32 type, const fsim_vpi_handle_v1 scale_object) const
    {
        out.type = type;
        if (type == vpiSuppressTime) {
            return;
        }
        const auto profile = simulation_.systemverilog_vpi_time().profile();
        const auto quanta
            = ticks * static_cast<std::int64_t>(profile.tick_multiplier);
        if (type == vpiScaledRealTime) {
            const auto scale = module_time_scale(scale_object);
            const int unit = scale ? scale->first
                                   : static_cast<int>(profile.unit_exponent);
            out.real = static_cast<double>(quanta)
                * std::pow(10.0, static_cast<int>(profile.precision_exponent) - unit);
            return;
        }
        const auto bits = static_cast<std::uint64_t>(quanta);
        out.high = static_cast<PLI_UINT32>(bits >> 32U);
        out.low = static_cast<PLI_UINT32>(bits & 0xFFFFFFFFU);
    }

    // One time of a vpi_put_delays array in scheduler ticks, in the format
    // delay_p->time_type names; none for vpiSuppressTime (no value to write)
    // and for a time outside the 64-bit range.
    [[nodiscard]] std::optional<std::int64_t> delay_ticks(const s_vpi_time& time,
        const PLI_INT32 type, const fsim_vpi_handle_v1 scale_object) const
    {
        const auto profile = simulation_.systemverilog_vpi_time().profile();
        const auto multiplier = static_cast<std::int64_t>(
            std::max<std::uint64_t>(profile.tick_multiplier, 1U));
        if (type == vpiSimTime) {
            const auto whole = (static_cast<std::uint64_t>(time.high) << 32U)
                | static_cast<std::uint64_t>(time.low);
            if (whole > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max() / 2)) {
                return std::nullopt;
            }
            return (static_cast<std::int64_t>(whole) + multiplier / 2)
                / multiplier;
        }
        if (type != vpiScaledRealTime || !std::isfinite(time.real)) {
            return std::nullopt;
        }
        const auto scale = module_time_scale(scale_object);
        const int unit = scale ? scale->first
                               : static_cast<int>(profile.unit_exponent);
        const auto ticks = std::llround(time.real
            * std::pow(10.0, unit - static_cast<int>(profile.precision_exponent))
            / static_cast<double>(multiplier));
        if (ticks > std::numeric_limits<std::int64_t>::max() / 2
            || ticks < std::numeric_limits<std::int64_t>::min() / 2) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(ticks);
    }

    // Fill da in the order of Table 38-2: each delay as its minimum, typical
    // and maximum values when mtm_flag is set (else the one the simulation
    // uses), each followed by its reject and error limits in the same form
    // when pulsere_flag is set.
    void fill_delays(const DelayRows& rows, const s_vpi_delay& delays,
        const fsim_vpi_handle_v1 scale_object) const
    {
        const std::size_t values = delays.mtm_flag != 0 ? 3U : 1U;
        const std::size_t kinds = delays.pulsere_flag != 0 ? 3U : 1U;
        for (std::size_t index = 0U;
             index < static_cast<std::size_t>(delays.no_of_delays); ++index) {
            const auto& row = rows[index];
            const std::array<const std::array<std::int64_t, 3U>*, 3U> sets {
                &row.delay, &row.reject, &row.error
            };
            for (std::size_t kind = 0U; kind < kinds; ++kind) {
                for (std::size_t value = 0U; value < values; ++value) {
                    const auto ticks = values == 3U
                        ? (*sets[kind])[value]
                        : kind == 0U ? row.applied : (*sets[kind])[1U];
                    write_delay_time(
                        delays.da[(index * kinds + kind) * values + value],
                        ticks, delays.time_type, scale_object);
                }
            }
        }
    }

    // The rows vpi_put_delays writes: the da array read in the same order. A
    // delay without minimum and maximum repeats its value in all three. With
    // append_flag the values add to `current`; without pulsere_flag the
    // reject and error limits keep their values (IEEE 1800-2023 38.32).
    [[nodiscard]] std::optional<DelayRows> incoming_delay_rows(
        const s_vpi_delay& delays, const DelayRows& current,
        const fsim_vpi_handle_v1 scale_object) const
    {
        const bool mtm = delays.mtm_flag != 0;
        const bool pulses = delays.pulsere_flag != 0;
        const std::size_t values = mtm ? 3U : 1U;
        const std::size_t kinds = pulses ? 3U : 1U;
        const auto count = static_cast<std::size_t>(delays.no_of_delays);
        DelayRows rows(count);
        for (std::size_t index = 0U; index < count; ++index) {
            auto& row = rows[index];
            row.reject = current[index].reject;
            row.error = current[index].error;
            row.limits_set = pulses || current[index].limits_set;
            const std::array<std::array<std::int64_t, 3U>*, 3U> sets {
                &row.delay, &row.reject, &row.error
            };
            for (std::size_t kind = 0U; kind < kinds; ++kind) {
                for (std::size_t value = 0U; value < values; ++value) {
                    const auto ticks = delay_ticks(
                        delays.da[(index * kinds + kind) * values + value],
                        delays.time_type, scale_object);
                    if (!ticks) {
                        return std::nullopt;
                    }
                    (*sets[kind])[value] = *ticks;
                }
                if (!mtm) {
                    (*sets[kind])[1U] = (*sets[kind])[0U];
                    (*sets[kind])[2U] = (*sets[kind])[0U];
                }
            }
            if (delays.append_flag != 0) {
                for (std::size_t value = 0U; value < 3U; ++value) {
                    row.delay[value] += current[index].delay[value];
                    if (pulses) {
                        row.reject[value] += current[index].reject[value];
                        row.error[value] += current[index].error[value];
                    }
                }
            }
            row.applied = row.delay[1U];
        }
        return rows;
    }

    // --- primitives ---------------------------------------------------------

    [[nodiscard]] fsim_vpi_handle_v1 primitive_instance(
        const elaboration::VerilogPrimitiveInfo& primitive) const
    {
        const auto found = registry_.find(primitive.instance);
        return found && found.value ? found.value->handle : 0U;
    }

    // The rise, fall and turn-off delays of a primitive. A gate declared with
    // one delay falls and turns off at it, and one declared with two turns
    // off at the smaller (IEEE 1800-2023 28.3.3, 28.16). The value in effect
    // is the driver's own; vpi_put_delays with mtm_flag keeps all three
    // values it was given.
    [[nodiscard]] DelayRows primitive_rows(const std::uint32_t index) const
    {
        const auto& primitive = primitives()[index];
        DelayRows rows(3U);
        const auto written = [&](const std::size_t position) {
            const auto& value = primitive.delays[position];
            return std::array<std::int64_t, 3U> {
                static_cast<std::int64_t>(value.minimum),
                static_cast<std::int64_t>(value.typical),
                static_cast<std::int64_t>(value.maximum)
            };
        };
        if (!primitive.delays.empty()) {
            rows[0].delay = written(0U);
            rows[1].delay = primitive.delays.size() > 1U ? written(1U)
                                                         : rows[0].delay;
            if (primitive.delays.size() > 2U) {
                rows[2].delay = written(2U);
            } else {
                for (std::size_t value = 0U; value < 3U; ++value) {
                    rows[2].delay[value] = std::min(
                        rows[0].delay[value], rows[1].delay[value]);
                }
            }
        }
        for (auto& row : rows) {
            row.applied = row.delay[1U];
        }
        if (!primitive.drivers.empty()) {
            if (const auto live
                = simulation_.verilog_driver_delays(primitive.drivers.front())) {
                rows[0].applied = static_cast<std::int64_t>(live->rise);
                rows[1].applied = static_cast<std::int64_t>(live->fall);
                rows[2].applied = static_cast<std::int64_t>(live->turnoff);
            }
        }
        // A put's rows hold until the value in effect changes under them.
        if (const auto stored = primitive_delay_rows_.find(index);
            stored != primitive_delay_rows_.end()) {
            bool fresh = true;
            for (std::size_t position = 0U; position < 3U; ++position) {
                fresh = fresh
                    && stored->second[position].applied == rows[position].applied;
            }
            if (fresh) {
                return stored->second;
            }
        }
        return rows;
    }

    // vpi_get_delays on a gate, switch or UDP instance: 2 or 3 delays, rise
    // then fall then turn-off (IEEE 1800-2023 38.10). A primitive has no
    // pulse limits, so pulsere_flag is an error.
    fsim_vpi_status_v1 primitive_get_delays(const std::uint32_t index,
        const s_vpi_delay& delays, fsim_vpi_service_result_v1& result)
    {
        if (delays.da == nullptr || delays.pulsere_flag != 0
            || (delays.no_of_delays != 2 && delays.no_of_delays != 3)
            || !valid_delay_time_type(delays.time_type)) {
            return fail_delays("vpi_get_delays: a primitive takes 2 or 3 delays "
                               "without pulse limits");
        }
        fill_delays(primitive_rows(index), delays,
            primitive_instance(primitives()[index]));
        return succeed(result);
    }

    // vpi_put_delays on a primitive: the delay of its driver changes for the
    // transitions after the call (IEEE 1800-2023 38.32). The simulation uses
    // the typical value of each minimum:typical:maximum triple. Two delays
    // leave the turn-off delay at the smaller of rise and fall (28.16).
    fsim_vpi_status_v1 primitive_put_delays(const std::uint32_t index,
        const s_vpi_delay& delays, fsim_vpi_service_result_v1& result)
    {
        const auto& primitive = primitives()[index];
        if (delays.da == nullptr || delays.pulsere_flag != 0
            || (delays.no_of_delays != 2 && delays.no_of_delays != 3)
            || !valid_delay_time_type(delays.time_type)) {
            return fail_delays("vpi_put_delays: a primitive takes 2 or 3 delays "
                               "without pulse limits");
        }
        if (primitive.drivers.empty()) {
            return fail_delays("vpi_put_delays: the primitive has no delayed "
                               "driver to retime");
        }
        auto rows = primitive_rows(index);
        const auto incoming = incoming_delay_rows(
            delays, rows, primitive_instance(primitive));
        if (!incoming) {
            return fail_delays("vpi_put_delays: the delay is not a valid time");
        }
        const auto count = static_cast<std::size_t>(delays.no_of_delays);
        for (std::size_t position = 0U; position < count; ++position) {
            rows[position] = (*incoming)[position];
        }
        if (count == 2U) {
            for (std::size_t value = 0U; value < 3U; ++value) {
                rows[2].delay[value]
                    = std::min(rows[0].delay[value], rows[1].delay[value]);
            }
            rows[2].applied = std::min(rows[0].applied, rows[1].applied);
        }
        if (std::ranges::any_of(rows, [](const DelayRow& row) {
                return row.delay[0] < 0 || row.delay[1] < 0
                    || row.delay[2] < 0 || row.applied < 0;
            })) {
            return fail_delays("vpi_put_delays: a primitive delay cannot be negative");
        }
        const runtime::simir::TransitionDelays applied {
            static_cast<runtime::SimulationTick>(rows[0].applied),
            static_cast<runtime::SimulationTick>(rows[1].applied),
            static_cast<runtime::SimulationTick>(rows[2].applied)
        };
        for (const auto driver : primitive.drivers) {
            if (!simulation_.set_verilog_driver_delays(driver, applied)) {
                return fail_delays("vpi_put_delays: the primitive's driver has "
                                   "no delay slot in this run");
            }
        }
        primitive_delay_rows_[index] = std::move(rows);
        return succeed(result);
    }

    // --- continuous assignments ---------------------------------------------

    // vpi_get_delays on a continuous assignment (IEEE 1800-2023 37.47): the
    // delays as written, rise, fall and turn-off, with the fall of a single
    // delay equal to its rise and the turn-off of fewer than three the
    // smaller of rise and fall (10.3.3). The model gives it no
    // vpi_put_delays, and the standard does not state its count.
    fsim_vpi_status_v1 continuous_get_delays(
        const VpiStatementModel::ContinuousDelay& assignment,
        const s_vpi_delay& delays, fsim_vpi_service_result_v1& result)
    {
        if (delays.da == nullptr || delays.pulsere_flag != 0
            || delays.no_of_delays < 1 || delays.no_of_delays > 3
            || !valid_delay_time_type(delays.time_type)) {
            return fail_delays("vpi_get_delays: a continuous assignment takes 1 "
                               "to 3 delays without pulse limits");
        }
        const auto& written = assignment.values;
        const std::int64_t rise = written.empty()
            ? 0 : static_cast<std::int64_t>(written[0]);
        const std::int64_t fall = written.size() > 1U
            ? static_cast<std::int64_t>(written[1]) : rise;
        const std::int64_t turnoff = written.size() > 2U
            ? static_cast<std::int64_t>(written[2]) : std::min(rise, fall);
        DelayRows rows(3U);
        const std::array<std::int64_t, 3U> ticks { rise, fall, turnoff };
        for (std::size_t position = 0U; position < 3U; ++position) {
            rows[position].delay = { ticks[position], ticks[position],
                ticks[position] };
            rows[position].applied = ticks[position];
        }
        fill_delays(rows, delays, assignment.scope);
        return succeed(result);
    }

    // --- timing checks ------------------------------------------------------

    // The limits of a check in effect: a put waiting for a safe point counts.
    [[nodiscard]] std::vector<std::int64_t> check_limits_now(
        const std::uint32_t check) const
    {
        const auto queued = queued_check_limits_.find(check);
        return queued != queued_check_limits_.end()
            ? queued->second
            : simulation_.verilog_timing_check_limits(check);
    }

    // One delay per limit, in the order written ($setuphold: setup then
    // hold). The check keeps one value per limit, so a get repeats it for
    // min, typ and max unless a put gave three.
    [[nodiscard]] DelayRows check_rows(const std::uint32_t check) const
    {
        const auto limits = check_limits_now(check);
        DelayRows rows(limits.size());
        for (std::size_t index = 0U; index < limits.size(); ++index) {
            rows[index].delay = { limits[index], limits[index], limits[index] };
            rows[index].applied = limits[index];
        }
        if (const auto stored = check_delay_rows_.find(check);
            stored != check_delay_rows_.end()
            && stored->second.size() == rows.size()
            && std::ranges::equal(stored->second, rows,
                [](const DelayRow& left, const DelayRow& right) {
                    return left.applied == right.applied;
                })) {
            return stored->second;
        }
        return rows;
    }

    // vpi_get_delays on a timing check: no_of_delays matches the check's
    // limits (IEEE 1800-2023 38.10); there are no pulse limits.
    fsim_vpi_status_v1 tchk_get_delays(const std::uint32_t check,
        const s_vpi_delay& delays, fsim_vpi_service_result_v1& result)
    {
        const auto rows = check_rows(check);
        if (delays.da == nullptr || delays.pulsere_flag != 0
            || static_cast<std::size_t>(std::max(delays.no_of_delays, 0))
                != rows.size()
            || !valid_delay_time_type(delays.time_type)) {
            return fail_delays("vpi_get_delays: the timing-check delay request "
                               "is unsupported");
        }
        fill_delays(rows, delays, tchk_instance(check));
        return succeed(result);
    }

    // vpi_put_delays on a timing check: one value per limit; the typical
    // value of each triple is the limit the check applies (IEEE 1800-2023
    // 38.32). The check takes it at the next safe point when the scheduler
    // is running.
    fsim_vpi_status_v1 tchk_put_delays(const std::uint32_t check,
        const s_vpi_delay& delays, fsim_vpi_service_result_v1& result)
    {
        auto rows = check_rows(check);
        if (delays.da == nullptr || delays.pulsere_flag != 0
            || static_cast<std::size_t>(std::max(delays.no_of_delays, 0))
                != rows.size()
            || !valid_delay_time_type(delays.time_type)) {
            return fail_delays("vpi_put_delays: the timing-check delay request "
                               "is unsupported");
        }
        const auto incoming = incoming_delay_rows(
            delays, rows, tchk_instance(check));
        if (!incoming) {
            return fail_delays("vpi_put_delays: the delay is not a valid time");
        }
        std::vector<std::int64_t> limits;
        limits.reserve(incoming->size());
        for (const auto& row : *incoming) {
            limits.push_back(row.applied);
        }
        try {
            if (simulation_.set_verilog_timing_check_limits(check, limits)) {
                queued_check_limits_.erase(check);
            } else {
                queued_check_limits_[check] = limits;
                arm_timing_hook();
            }
        } catch (const std::invalid_argument&) {
            return fail_delays("vpi_put_delays: the limits do not suit the "
                               "timing check");
        }
        check_delay_rows_[check] = *incoming;
        return succeed(result);
    }

    // --- module paths -------------------------------------------------------

    // The path's delays expanded to the twelve-transition form, in the
    // order 0->1, 1->0, 0->z, z->1, 1->z, z->0, 0->x, x->1, 1->x, x->0,
    // x->z, z->x (IEEE 1800-2023 30.5.1 Table 30-2, 30.5.2).
    [[nodiscard]] std::array<runtime::SimulationTick, 12U> expanded_path_delays(
        const std::vector<runtime::SimulationTick>& table) const
    {
        constexpr std::array<std::pair<Logic4, Logic4>, 12U> transitions { {
            { Logic4::zero, Logic4::one }, { Logic4::one, Logic4::zero },
            { Logic4::zero, Logic4::z }, { Logic4::z, Logic4::one },
            { Logic4::one, Logic4::z }, { Logic4::z, Logic4::zero },
            { Logic4::zero, Logic4::x }, { Logic4::x, Logic4::one },
            { Logic4::one, Logic4::x }, { Logic4::x, Logic4::zero },
            { Logic4::x, Logic4::z }, { Logic4::z, Logic4::x },
        } };
        std::array<runtime::SimulationTick, 12U> expanded { };
        for (std::size_t index = 0U; index < expanded.size(); ++index) {
            expanded[index] = runtime::simir::module_path_transition_delay(
                transitions[index].first, transitions[index].second, table)
                                  .value_or(0U);
        }
        return expanded;
    }

    // The delay table a caller sees: a delay queued by vpi_put_delays until
    // the scheduler reaches a safe point, else the live table.
    [[nodiscard]] std::vector<runtime::SimulationTick> path_delay_table(
        const std::uint32_t path) const
    {
        const auto queued = queued_path_delays_.find(path);
        return queued != queued_path_delays_.end()
            ? queued->second.delays
            : simulation_.verilog_module_path_delays(path);
    }

    // The twelve transitions of a path with their reject and error limits.
    // Absent explicit limits, a path rejects and errors at its delay (IEEE
    // 1800-2023 30.7.1). The path keeps one value per delay, so min, typ and
    // max repeat it unless a put gave three.
    [[nodiscard]] DelayRows path_rows(const std::uint32_t path) const
    {
        const auto& declared = simulation_.design().verilog_specify_paths()[path];
        const auto expanded = expanded_path_delays(path_delay_table(path));
        const auto limits = [&](const std::vector<runtime::SimulationTick>& limit_table,
                                const std::optional<runtime::SimulationTick>& limit) {
            if (!limit_table.empty()) {
                return expanded_path_delays(limit_table);
            }
            auto fallback = expanded;
            if (limit) {
                fallback.fill(*limit);
            }
            return fallback;
        };
        const auto reject = limits(
            declared.pulse_reject_delays, declared.pulse_reject_limit);
        const auto error = limits(
            declared.pulse_error_delays, declared.pulse_error_limit);
        DelayRows rows(expanded.size());
        for (std::size_t index = 0U; index < rows.size(); ++index) {
            const auto delay = static_cast<std::int64_t>(expanded[index]);
            const auto low = static_cast<std::int64_t>(reject[index]);
            const auto high = static_cast<std::int64_t>(error[index]);
            rows[index].delay = { delay, delay, delay };
            rows[index].applied = delay;
            rows[index].reject = { low, low, low };
            rows[index].error = { high, high, high };
        }
        // A put's rows hold until the delays change under them.
        if (const auto stored = path_delay_rows_.find(path);
            stored != path_delay_rows_.end()) {
            bool fresh = true;
            for (std::size_t index = 0U; index < stored->second.size(); ++index) {
                fresh = fresh && stored->second[index].applied == rows[index].applied;
            }
            for (std::size_t index = 0U; fresh && index < stored->second.size();
                 ++index) {
                const auto& kept = stored->second[index];
                rows[index].delay = kept.delay;
                if (kept.limits_set) {
                    rows[index].reject = kept.reject;
                    rows[index].error = kept.error;
                }
            }
        }
        return rows;
    }

    // vpi_get_delays on a module path: 1, 2, 3, 6 or 12 delays in the
    // transition order above, each followed by its reject and error limits
    // when pulsere_flag is set, and with min, typ and max values when
    // mtm_flag is set (IEEE 1800-2023 38.10 Table 38-2).
    fsim_vpi_status_v1 path_get_delays(const std::uint32_t path,
        const s_vpi_delay& delays, fsim_vpi_service_result_v1& result)
    {
        if (delays.da == nullptr || !valid_path_delay_count(delays.no_of_delays)
            || !valid_delay_time_type(delays.time_type)) {
            return fail_delays("vpi_get_delays: the object or delay request is unsupported");
        }
        fill_delays(path_rows(path), delays, path_instance(path));
        return succeed(result);
    }

    [[nodiscard]] static bool valid_path_delay_count(const PLI_INT32 count)
    {
        return count == 1 || count == 2 || count == 3 || count == 6
            || count == 12;
    }

    // vpi_put_delays on a module path replaces, or with append_flag adds to,
    // the first no_of_delays transitions, and with pulsere_flag their reject
    // and error limits; the typical value of each min:typ:max triple is the
    // one the path applies, and without pulsere_flag the pulse limits retain
    // their values (IEEE 1800-2023 38.32). The change reaches the scheduler
    // at its next safe point, so a call made from a system task still leaves
    // the in-flight path events on the old timing.
    fsim_vpi_status_v1 path_put_delays(const std::uint32_t path,
        const s_vpi_delay& delays, fsim_vpi_service_result_v1& result)
    {
        if (delays.da == nullptr || !valid_path_delay_count(delays.no_of_delays)
            || !valid_delay_time_type(delays.time_type)) {
            return fail_delays("vpi_put_delays: the object or delay request is unsupported");
        }
        const auto incoming = incoming_delay_rows(
            delays, path_rows(path), path_instance(path));
        if (!incoming) {
            return fail_delays("vpi_put_delays: the delay is not a valid time");
        }
        PathTimingUpdate update;
        for (const auto& row : *incoming) {
            if (row.applied < 0
                || (delays.pulsere_flag != 0
                    && (row.reject[1U] < 0 || row.error[1U] < row.reject[1U]))) {
                return fail_delays("vpi_put_delays: a path delay or pulse limit "
                                   "is negative or its reject limit exceeds its error limit");
            }
            update.delays.push_back(static_cast<runtime::SimulationTick>(row.applied));
            if (delays.pulsere_flag != 0) {
                update.reject.push_back(static_cast<runtime::SimulationTick>(row.reject[1U]));
                update.error.push_back(static_cast<runtime::SimulationTick>(row.error[1U]));
            }
        }
        try {
            if (simulation_.set_verilog_module_path_timing(
                    path, update.delays, update.reject, update.error)) {
                queued_path_delays_.erase(path);
            } else {
                queued_path_delays_[path] = std::move(update);
                arm_timing_hook();
            }
        } catch (const std::invalid_argument&) {
            return fail_delays("vpi_put_delays: the delays do not suit the path");
        }
        path_delay_rows_[path] = *incoming;
        return succeed(result);
    }

    // --- dispatch -----------------------------------------------------------

    fsim_vpi_status_v1 get_delays(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        auto* const delays = pointer<s_vpi_delay>(request.user_data);
        if (delays != nullptr) {
            if (primitive_of(request.handle) != nullptr) {
                return primitive_get_delays(
                    static_cast<std::uint32_t>(serial_of(request.handle)),
                    *delays, result);
            }
            if (const auto node = node_of(request.handle)) {
                if (const auto assignment
                    = statement_model().continuous_delay(*node)) {
                    return continuous_get_delays(*assignment, *delays, result);
                }
            }
            if (const auto delay = net_delay_index(
                    resolve(request.handle), Tag::inter_mod_path)) {
                return net_get_delays(*delay, delays, result);
            }
            if (const auto check = tchk_of(resolve(request.handle))) {
                return tchk_get_delays(*check, *delays, result);
            }
            if (const auto path = modpath_of(resolve(request.handle))) {
                return path_get_delays(*path, *delays, result);
            }
        }
        return fail_delays("vpi_get_delays: the object or delay request is unsupported");
    }

    fsim_vpi_status_v1 put_delays(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto* const delays = pointer<const s_vpi_delay>(request.user_data);
        if (delays != nullptr) {
            if (primitive_of(request.handle) != nullptr) {
                return primitive_put_delays(
                    static_cast<std::uint32_t>(serial_of(request.handle)),
                    *delays, result);
            }
            if (const auto delay = net_delay_index(
                    resolve(request.handle), Tag::inter_mod_path)) {
                return net_put_delays(*delay, delays, result);
            }
            if (const auto check = tchk_of(resolve(request.handle))) {
                return tchk_put_delays(*check, *delays, result);
            }
            if (const auto path = modpath_of(resolve(request.handle))) {
                return path_put_delays(*path, *delays, result);
            }
        }
        return fail_delays("vpi_put_delays: the object or delay request is unsupported");
    }

    // The safe-point hook that applies the path delays and check limits a
    // put could not apply while the scheduler was running.
    void arm_timing_hook()
    {
        if (path_delay_hook_ == 0U) {
            path_delay_hook_ = simulation_.add_safe_point_hook(
                [this](runtime::Scheduler&, runtime::SchedulerPhase) {
                    commit_queued_timing();
                });
        }
    }

    void commit_queued_timing()
    {
        auto paths = std::move(queued_path_delays_);
        queued_path_delays_.clear();
        for (const auto& [path, update] : paths) {
            (void)simulation_.set_verilog_module_path_timing(
                path, update.delays, update.reject, update.error);
        }
        auto checks = std::move(queued_check_limits_);
        queued_check_limits_.clear();
        for (const auto& [check, limits] : checks) {
            (void)simulation_.set_verilog_timing_check_limits(check, limits);
        }
        // The hook is one-shot: it removes itself once the timing is applied.
        simulation_.remove_safe_point_hook(
            std::exchange(path_delay_hook_, std::uint64_t { }));
    }

    // --- selects and constants ----------------------------------------------

    [[nodiscard]] static bool dynamic_array_kind(
        const runtime::SystemVerilogVpiDescriptorKind kind)
    {
        return kind == runtime::SystemVerilogVpiDescriptorKind::DynamicArray
            || kind == runtime::SystemVerilogVpiDescriptorKind::Queue;
    }

    // The declared range of a vector, or the unpacked range of an array,
    // left bound first.
    [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>> declared_range(
        const fsim_vpi_handle_v1 handle) const
    {
        if (const auto unpacked = declared_unpacked(handle)) {
            return std::pair { unpacked->ranges.front().left,
                unpacked->ranges.front().right };
        }
        const auto object = info(handle);
        if (!object || !object->type) {
            return std::nullopt;
        }
        const auto& descriptor = object->type->descriptor;
        // A dynamic array or a queue is indexed from zero over its live
        // words (IEEE 1800-2023 7.5, 7.10).
        if (object->kind == SystemVerilogVpiObjectKind::Memory
            && descriptor && dynamic_array_kind(descriptor->kind)) {
            return std::pair { std::int64_t { 0 },
                std::int64_t { object->live_children } - 1 };
        }
        const auto wanted = object->kind == SystemVerilogVpiObjectKind::Memory
            ? runtime::SystemVerilogVpiDescriptorKind::UnpackedArray
            : runtime::SystemVerilogVpiDescriptorKind::PackedArray;
        if (descriptor && descriptor->kind == wanted && !descriptor->ranges.empty()) {
            return std::pair { descriptor->ranges.front().left,
                descriptor->ranges.front().right };
        }
        if (object->kind == SystemVerilogVpiObjectKind::Memory
            || object->type->width == 0U) {
            return std::nullopt;
        }
        return std::pair { std::int64_t { object->type->width } - 1, std::int64_t { 0 } };
    }

    // Whether a net or variable is a vector, a scalar, or neither (IEEE
    // 1800-2023 37.16, 37.17): a declared packed range makes a vector even
    // of one bit, so `logic [0:0]` is a vector; without one the width
    // decides; a real object is neither.
    enum class VectorShape { scalar, vector, neither };

    [[nodiscard]] static VectorShape vector_shape(
        const runtime::SystemVerilogVpiObjectInfo& object)
    {
        if (!object.type) {
            return VectorShape::neither;
        }
        using Category = runtime::SystemVerilogVpiValueCategory;
        if (object.type->category == Category::Real
            || object.type->category == Category::ShortReal) {
            return VectorShape::neither;
        }
        const auto& descriptor = object.type->descriptor;
        if (object.kind != SystemVerilogVpiObjectKind::Memory && descriptor
            && descriptor->kind
                == runtime::SystemVerilogVpiDescriptorKind::PackedArray
            && !descriptor->ranges.empty()) {
            return VectorShape::vector;
        }
        return object.type->width > 1U ? VectorShape::vector
            : object.type->width == 1U ? VectorShape::scalar
                                       : VectorShape::neither;
    }

    // Whether the object reports vpiRange, vpiLeftRange and vpiRightRange: a
    // structure or union variable or net has none, only its members and
    // vector bits (IEEE 1800-2023 37.26).
    [[nodiscard]] bool reports_range(const fsim_vpi_handle_v1 handle) const
    {
        using Kind = runtime::SystemVerilogVpiDescriptorKind;
        const auto object = info(handle);
        return !object || !object->type || !object->type->declared
            || (object->type->declared->kind != Kind::Struct
                && object->type->declared->kind != Kind::Union);
    }

    // Every dimension of an object, leftmost first: an array's unpacked
    // dimensions, or a vector's packed ones (IEEE 1800-2023 37.17).
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::int64_t>>
    dimension_ranges(const fsim_vpi_handle_v1 handle) const
    {
        std::vector<std::pair<std::int64_t, std::int64_t>> result;
        for (const auto* level = declared_unpacked(handle).get();
             level != nullptr
             && level->kind == runtime::SystemVerilogVpiDescriptorKind::UnpackedArray;
             level = level->children.empty() ? nullptr : &level->children.front()) {
            for (const auto& range : level->ranges) {
                result.emplace_back(range.left, range.right);
            }
        }
        if (!result.empty()) {
            return result;
        }
        const auto object = info(handle);
        if (!object || !object->type) {
            return result;
        }
        if (object->kind == SystemVerilogVpiObjectKind::Memory) {
            const auto* level = object->type->descriptor.get();
            if (level != nullptr && dynamic_array_kind(level->kind)) {
                if (const auto range = declared_range(handle)) {
                    result.push_back(*range);
                }
                return result;
            }
            while (level != nullptr
                && level->kind
                    == runtime::SystemVerilogVpiDescriptorKind::UnpackedArray) {
                for (const auto& range : level->ranges) {
                    result.emplace_back(range.left, range.right);
                }
                level = level->children.empty() ? nullptr
                                                 : &level->children.front();
            }
            return result;
        }
        // Packed dimensions that tile the vector, else its flat range.
        const auto& descriptor = object->type->descriptor;
        if (descriptor
            && descriptor->kind
                == runtime::SystemVerilogVpiDescriptorKind::PackedArray
            && descriptor->ranges.size() > 1U) {
            std::uint64_t product = 1U;
            for (const auto& range : descriptor->ranges) {
                product *= static_cast<std::uint64_t>(
                               range.left >= range.right
                                   ? range.left - range.right
                                   : range.right - range.left)
                    + 1U;
                result.emplace_back(range.left, range.right);
            }
            if (product == object->type->width) {
                return result;
            }
            result.clear();
        }
        if (const auto range = declared_range(handle)) {
            result.push_back(*range);
        }
        return result;
    }

    [[nodiscard]] fsim_vpi_handle_v1 make_range(const fsim_vpi_handle_v1 parent,
        const std::int64_t left, const std::int64_t right)
    {
        ranges_.push_back({ parent, left, right });
        return tagged(Tag::range, ranges_.size() - 1U);
    }

    [[nodiscard]] const Range* range_of(const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::range
            || serial_of(handle) >= ranges_.size()) {
            return nullptr;
        }
        return &ranges_[serial_of(handle)];
    }

    [[nodiscard]] fsim_vpi_handle_v1 make_select(const fsim_vpi_handle_v1 parent,
        const std::int64_t left, const std::int64_t right, const bool bit)
    {
        const auto object = info(parent);
        const auto range = declared_range(parent);
        if (!object || !range || object->kind == SystemVerilogVpiObjectKind::Memory
            || (object->type && object->type->descriptor
                && object->type->descriptor->ranges.size() > 1U)) {
            return 0U;
        }
        const auto [msb, lsb] = *range;
        const auto offset_of = [&](const std::int64_t index) -> std::optional<std::int64_t> {
            if (msb >= lsb) {
                return index < lsb || index > msb ? std::nullopt
                                                  : std::optional { index - lsb };
            }
            return index < msb || index > lsb ? std::nullopt
                                              : std::optional { lsb - index };
        };
        const auto low = offset_of(right);
        const auto high = offset_of(left);
        if (!low || !high || *high < *low) {
            return 0U;
        }
        Select select { parent, static_cast<std::uint32_t>(*low),
            static_cast<std::uint32_t>(*high - *low + 1), left, right, bit, false, { }, { },
            { }, { }, { }, false, false };
        for (std::size_t index = 0U; index < selects_.size(); ++index) {
            const auto& known = selects_[index];
            if (known.parent == parent && known.offset == select.offset
                && known.width == select.width && known.bit == bit
                && !known.element && known.suffix.empty()) {
                return tagged(Tag::select, index);
            }
        }
        selects_.push_back(select);
        return tagged(Tag::select, selects_.size() - 1U);
    }

    [[nodiscard]] const Select* select_of(const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::select
            || serial_of(handle) >= selects_.size()) {
            return nullptr;
        }
        return &selects_[serial_of(handle)];
    }

    [[nodiscard]] static std::uint64_t extent(
        const std::pair<std::int64_t, std::int64_t>& range)
    {
        return static_cast<std::uint64_t>(range.first >= range.second
                   ? range.first - range.second
                   : range.second - range.first)
            + 1U;
    }

    // The position of `index` in `range`, counted from its right bound.
    [[nodiscard]] static std::optional<std::uint64_t> position(
        const std::pair<std::int64_t, std::int64_t>& range,
        const std::int64_t index)
    {
        const auto [left, right] = range;
        if (left >= right) {
            return index < right || index > left
                ? std::nullopt
                : std::optional { static_cast<std::uint64_t>(index - right) };
        }
        return index < left || index > right
            ? std::nullopt
            : std::optional { static_cast<std::uint64_t>(right - index) };
    }

    // The element at `index` of the leftmost of `dimensions`, packed
    // `offset` bits above the LSB of `parent`: a bit when one dimension
    // remains, else a vector of the others (IEEE 1800-2023 7.4.5).
    [[nodiscard]] fsim_vpi_handle_v1 make_element(
        const fsim_vpi_handle_v1 parent, const std::uint64_t offset,
        const std::vector<std::pair<std::int64_t, std::int64_t>>& dimensions,
        const std::string& suffix, const std::int64_t index)
    {
        if (dimensions.empty()) {
            return 0U;
        }
        const auto at = position(dimensions.front(), index);
        if (!at) {
            return 0U;
        }
        Select select;
        select.parent = parent;
        select.suffix = suffix + "[" + std::to_string(index) + "]";
        select.dimensions.assign(dimensions.begin() + 1, dimensions.end());
        std::uint64_t width = 1U;
        for (const auto& range : select.dimensions) {
            width *= extent(range);
        }
        const auto element_offset = offset + *at * width;
        if (element_offset + width > std::numeric_limits<std::uint32_t>::max()) {
            return 0U;
        }
        select.offset = static_cast<std::uint32_t>(element_offset);
        select.width = static_cast<std::uint32_t>(width);
        select.bit = select.dimensions.empty();
        select.element = !select.bit;
        select.left = select.bit ? index : select.dimensions.front().first;
        select.right = select.bit ? index : select.dimensions.front().second;
        for (std::size_t known = 0U; known < selects_.size(); ++known) {
            if (selects_[known].parent == parent
                && selects_[known].suffix == select.suffix) {
                return tagged(Tag::select, known);
            }
        }
        selects_.push_back(std::move(select));
        return tagged(Tag::select, selects_.size() - 1U);
    }

    // --- declared types -----------------------------------------------

    using Declared = std::shared_ptr<const runtime::SystemVerilogVpiTypeDescriptor>;

    // The declared type of an object, or of a member or element select.
    [[nodiscard]] Declared declared_of(const fsim_vpi_handle_v1 handle) const
    {
        if (const auto* select = select_of(handle)) {
            return select->declared;
        }
        const auto object = info(handle);
        return object && object->type ? object->type->declared : nullptr;
    }

    // The declared type of an object or select when it is an unpacked
    // array, such as an array of unpacked structures (IEEE 1800-2023 7.4).
    [[nodiscard]] Declared declared_unpacked(const fsim_vpi_handle_v1 handle) const
    {
        auto declared = declared_of(handle);
        return declared
                && declared->kind == runtime::SystemVerilogVpiDescriptorKind::UnpackedArray
                && !declared->ranges.empty() && declared->children.size() == 1U
            ? declared
            : nullptr;
    }

    [[nodiscard]] static bool net_object(
        const runtime::SystemVerilogVpiObjectInfo& object)
    {
        return object.kind == SystemVerilogVpiObjectKind::Net
            || (object.kind == SystemVerilogVpiObjectKind::Port && object.type
                && object.type->net_kind != runtime::SystemVerilogVpiNetKind::None);
    }

    // The vpiType of a net or variable of a declared type (IEEE 1800-2023
    // 37.16, 37.17, 37.18).
    [[nodiscard]] static PLI_INT32 declared_type(
        const runtime::SystemVerilogVpiTypeDescriptor& type, const bool net)
    {
        using Kind = runtime::SystemVerilogVpiDescriptorKind;
        using Category = runtime::SystemVerilogVpiValueCategory;
        switch (type.kind) {
        case Kind::Struct:
            return net ? vpiStructNet : vpiStructVar;
        case Kind::Union:
            return net ? vpiUnionNet : vpiUnionVar;
        case Kind::Enum:
            return net ? vpiEnumNet : vpiEnumVar;
        case Kind::PackedArray:
            return net ? vpiPackedArrayNet : vpiPackedArrayVar;
        // An unpacked array of any element type (37.17 detail 4).
        case Kind::UnpackedArray:
            return net ? vpiNetArray : vpiRegArray;
        case Kind::String:
            return vpiStringVar;
        default:
            break;
        }
        switch (type.category) {
        case Category::Integer4:
            return net ? vpiIntegerNet : vpiIntegerVar;
        case Category::Integer2:
            switch (type.width) {
            case 8U:
                return net ? vpiByteNet : vpiByteVar;
            case 16U:
                return net ? vpiShortIntNet : vpiShortIntVar;
            case 64U:
                return net ? vpiLongIntNet : vpiLongIntVar;
            default:
                return net ? vpiIntNet : vpiIntVar;
            }
        case Category::Bit2:
            return net ? vpiBitNet : vpiBitVar;
        case Category::Real:
            return net ? vpiRealNet : vpiRealVar;
        case Category::ShortReal:
            return net ? vpiShortRealNet : vpiShortRealVar;
        default:
            return net ? vpiNet : vpiReg;
        }
    }

    // The properties a declared type decides (IEEE 1800-2023 37.17
    // details 9, 17 and 20; 37.18 details 1, 2, 4 and 5).
    [[nodiscard]] static std::optional<std::int64_t> declared_property(
        const runtime::SystemVerilogVpiTypeDescriptor& type,
        const PLI_INT32 property, const bool member, const bool element)
    {
        using Kind = runtime::SystemVerilogVpiDescriptorKind;
        // An unpacked array has its element count as its size and is no
        // vector (37.17 details 4, 9).
        if (type.kind == Kind::UnpackedArray) {
            std::int64_t count = 1;
            for (const auto& range : type.ranges) {
                count *= static_cast<std::int64_t>(
                    support::index_distance(range.left, range.right) + 1U);
            }
            switch (property) {
            case vpiSize:
                return count;
            case vpiVector:
            case vpiScalar:
            case vpiSigned:
                return 0;
            case vpiStructUnionMember:
                return member ? 1 : 0;
            default:
                return std::nullopt;
            }
        }
        const bool aggregate = type.kind == Kind::Struct || type.kind == Kind::Union;
        const bool array = type.kind == Kind::PackedArray;
        const auto bits = runtime::systemverilog_vpi_declared_bits(type);
        const bool integer_atom = type.category
                == runtime::SystemVerilogVpiValueCategory::Integer2
            || type.category == runtime::SystemVerilogVpiValueCategory::Integer4;
        const bool vector = aggregate ? type.packed
            : array || integer_atom || bits > 1U;
        switch (property) {
        case vpiPacked:
            if (aggregate) {
                return type.packed ? 1 : 0;
            }
            // A packed array and its elements are packed (37.18 detail
            // 1); vpiPacked is no property of another enum var (37.17).
            if (array || (type.kind == Kind::Enum && element)) {
                return 1;
            }
            return std::nullopt;
        case vpiVector:
            return vector ? 1 : 0;
        case vpiScalar:
            return !vector && !aggregate && bits == 1U ? 1 : 0;
        case vpiSize:
            return aggregate && !type.packed
                ? static_cast<std::int64_t>(type.children.size())
                : static_cast<std::int64_t>(bits);
        case vpiSigned:
            return aggregate || array ? 0 : (type.is_signed ? 1 : 0);
        case vpiStructUnionMember:
            return member ? 1 : 0;
        case vpiPackedArrayMember:
            return element ? 1 : 0;
        default:
            return std::nullopt;
        }
    }

    // The stored object, offset, name suffix and net-ness an object or
    // select of a declared type lies in.
    struct DeclaredPlace {
        fsim_vpi_handle_v1 root { };
        std::uint64_t offset { };
        std::string suffix;
        std::string name;
        bool net { };
    };

    [[nodiscard]] std::optional<DeclaredPlace> declared_place(
        const fsim_vpi_handle_v1 handle) const
    {
        if (const auto* select = select_of(handle)) {
            return DeclaredPlace { select->parent, select->offset,
                select->suffix, select->name, select->net };
        }
        const auto object = info(handle);
        if (!object) {
            return std::nullopt;
        }
        return DeclaredPlace { handle, 0U, { },
            std::string { unescaped(object->name) }, net_object(*object) };
    }

    [[nodiscard]] fsim_vpi_handle_v1 make_declared_select(
        const DeclaredPlace& place, const std::uint64_t offset,
        const std::uint64_t bits, const std::string& step,
        const std::string& name, const fsim_vpi_handle_v1 owner,
        Declared declared, const bool member)
    {
        if (bits == 0U
            || offset + bits > std::numeric_limits<std::uint32_t>::max()) {
            return 0U;
        }
        const auto suffix = place.suffix + step;
        for (std::size_t known = 0U; known < selects_.size(); ++known) {
            if (selects_[known].parent == place.root
                && selects_[known].suffix == suffix) {
                return tagged(Tag::select, known);
            }
        }
        Select select;
        select.parent = place.root;
        select.offset = static_cast<std::uint32_t>(offset);
        select.width = static_cast<std::uint32_t>(bits);
        select.left = static_cast<std::int64_t>(bits) - 1;
        select.right = 0;
        select.bit = declared == nullptr && bits == 1U;
        select.suffix = suffix;
        select.name = name;
        select.owner = owner;
        select.declared = std::move(declared);
        select.net = place.net;
        select.member = member;
        selects_.push_back(std::move(select));
        return tagged(Tag::select, selects_.size() - 1U);
    }

    // The members of a structure or union, its first member the most
    // significant (IEEE 1800-2023 7.2.1, 7.3.1, 37.26).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> members_of(
        const fsim_vpi_handle_v1 owner)
    {
        using Kind = runtime::SystemVerilogVpiDescriptorKind;
        std::vector<fsim_vpi_handle_v1> members;
        const auto type = declared_of(owner);
        const auto place = declared_place(owner);
        if (!type || !place
            || (type->kind != Kind::Struct && type->kind != Kind::Union)
            || type->children.size() != type->member_names.size()) {
            return members;
        }
        auto above = runtime::systemverilog_vpi_declared_bits(*type);
        for (std::size_t index = 0U; index < type->children.size(); ++index) {
            const auto& child = type->children[index];
            const auto bits = runtime::systemverilog_vpi_declared_bits(child);
            if (type->kind == Kind::Struct) {
                above -= std::min(above, bits);
            }
            const auto at = type->kind == Kind::Struct ? above : 0U;
            const auto& name = type->member_names[index];
            members.push_back(make_declared_select(*place, place->offset + at,
                bits, "." + name, name, owner, Declared { type, &child }, true));
        }
        return members;
    }

    // The element at `index` of a packed array of a declared type, one
    // dimension at a time; its parent is the largest containing packed
    // array (IEEE 1800-2023 37.17 detail 26, 37.18 detail 3).
    [[nodiscard]] fsim_vpi_handle_v1 packed_element(
        const fsim_vpi_handle_v1 owner, const std::int64_t index)
    {
        using Kind = runtime::SystemVerilogVpiDescriptorKind;
        const auto type = declared_of(owner);
        const auto place = declared_place(owner);
        const bool unpacked = type && type->kind == Kind::UnpackedArray;
        if (!type || !place || (type->kind != Kind::PackedArray && !unpacked)
            || type->ranges.empty() || type->children.size() != 1U) {
            return 0U;
        }
        const auto at = position(
            { type->ranges.front().left, type->ranges.front().right }, index);
        if (!at) {
            return 0U;
        }
        Declared element;
        if (type->ranges.size() == 1U) {
            element = Declared { type, &type->children.front() };
        } else {
            auto rest = *type;
            rest.ranges.erase(rest.ranges.begin());
            element = std::make_shared<const runtime::SystemVerilogVpiTypeDescriptor>(
                std::move(rest));
        }
        const auto bits = runtime::systemverilog_vpi_declared_bits(*element);
        auto parent = owner;
        if (const auto* select = select_of(owner); select != nullptr
            && !select->member && select->declared
            && select->unpacked_element == unpacked) {
            parent = select->owner;
        }
        const auto step = "[" + std::to_string(index) + "]";
        const auto handle = make_declared_select(*place,
            place->offset + *at * bits, bits, step, place->name + step,
            parent, std::move(element), false);
        if (handle != 0U) {
            selects_[serial_of(handle)].unpacked_element = unpacked;
        }
        return handle;
    }

    // The element of a declared-type object at `index`: a packed array's
    // element, or a bit of a member vector.
    [[nodiscard]] fsim_vpi_handle_v1 declared_index(
        const fsim_vpi_handle_v1 owner, const std::int64_t index)
    {
        using Kind = runtime::SystemVerilogVpiDescriptorKind;
        const auto type = declared_of(owner);
        if (!type) {
            return 0U;
        }
        if (type->kind == Kind::PackedArray || type->kind == Kind::UnpackedArray) {
            return packed_element(owner, index);
        }
        const auto place = declared_place(owner);
        const auto bits = runtime::systemverilog_vpi_declared_bits(*type);
        if (!place || type->kind == Kind::Struct || type->kind == Kind::Union
            || index < 0 || static_cast<std::uint64_t>(index) >= bits) {
            return 0U;
        }
        const auto step = "[" + std::to_string(index) + "]";
        return make_declared_select(*place,
            place->offset + static_cast<std::uint64_t>(index), 1U, step,
            place->name + step, owner, nullptr, false);
    }

    // A member or element of a declared-type object named after it, such
    // as `top.pkt.dst.tile` or `top.arr[1].hi` (IEEE 1800-2023 37.17
    // detail 28).
    [[nodiscard]] fsim_vpi_handle_v1 find_declared_path(const std::string& name)
    {
        for (auto split = name.size(); split > 1U;) {
            split = name.find_last_of(".[", split - 1U);
            if (split == std::string::npos || split == 0U) {
                return 0U;
            }
            const auto found = registry_.find(name.substr(0U, split));
            if (!found || !found.value) {
                continue;
            }
            if (!found.value->type || !found.value->type->declared) {
                return 0U;
            }
            auto handle = found.value->handle;
            auto rest = std::string_view { name }.substr(split);
            while (!rest.empty() && handle != 0U) {
                if (rest.front() == '.') {
                    const auto end = rest.find_first_of(".[", 1U);
                    const auto wanted = rest.substr(1U,
                        end == std::string_view::npos ? std::string_view::npos
                                                      : end - 1U);
                    fsim_vpi_handle_v1 next = 0U;
                    for (const auto candidate : members_of(handle)) {
                        if (const auto* member = select_of(candidate);
                            member != nullptr && member->name == wanted) {
                            next = candidate;
                            break;
                        }
                    }
                    handle = next;
                    rest = end == std::string_view::npos ? std::string_view { }
                                                         : rest.substr(end);
                    continue;
                }
                const auto close = rest.find(']');
                std::int64_t index { };
                const auto digits = rest.substr(1U,
                    close == std::string_view::npos ? 0U : close - 1U);
                const auto parsed = std::from_chars(
                    digits.data(), digits.data() + digits.size(), index);
                if (close == std::string_view::npos || digits.empty()
                    || parsed.ec != std::errc { }
                    || parsed.ptr != digits.data() + digits.size()) {
                    return 0U;
                }
                handle = declared_index(handle, index);
                rest = rest.substr(close + 1U);
            }
            return handle;
        }
        return 0U;
    }

    // An unpacked structure or union has no value of its own (IEEE
    // 1800-2023 37.17 detail 11).
    [[nodiscard]] bool valueless(const fsim_vpi_handle_v1 handle) const
    {
        using Kind = runtime::SystemVerilogVpiDescriptorKind;
        const auto type = declared_of(handle);
        return type && (type->kind == Kind::Struct || type->kind == Kind::Union)
            && !type->packed;
    }

    [[nodiscard]] const SubArray* subarray_of(const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::subarray
            || serial_of(handle) >= subarrays_.size()) {
            return nullptr;
        }
        return &subarrays_[serial_of(handle)];
    }

    // The unpacked dimensions a subarray has left.
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::int64_t>>
    subarray_dimensions(const SubArray& subarray) const
    {
        auto dimensions = dimension_ranges(subarray.memory);
        dimensions.erase(dimensions.begin(),
            dimensions.begin()
                + static_cast<std::ptrdiff_t>(
                    std::min(subarray.depth, dimensions.size())));
        return dimensions;
    }

    // Indexes the array `memory` after `depth` indexes named `name` and
    // `full_name`: a word once every unpacked dimension has an index,
    // else a subarray.
    [[nodiscard]] fsim_vpi_handle_v1 index_array(
        const fsim_vpi_handle_v1 memory, const std::size_t depth,
        const std::string& name, const std::string& full_name,
        const std::int64_t index)
    {
        const auto dimensions = dimension_ranges(memory);
        if (depth >= dimensions.size()
            || !position(dimensions[depth], index)) {
            return 0U;
        }
        const auto selected = "[" + std::to_string(index) + "]";
        if (depth + 1U == dimensions.size()) {
            const auto found = registry_.find(full_name + selected);
            return found && found.value ? found.value->handle : 0U;
        }
        for (std::size_t known = 0U; known < subarrays_.size(); ++known) {
            if (subarrays_[known].full_name == full_name + selected) {
                return tagged(Tag::subarray, known);
            }
        }
        subarrays_.push_back(
            { memory, depth + 1U, name + selected, full_name + selected });
        return tagged(Tag::subarray, subarrays_.size() - 1U);
    }

    [[nodiscard]] fsim_vpi_handle_v1 make_constant(const std::int64_t value)
    {
        Constant constant;
        constant.value.bits = integer_bits(static_cast<std::uint64_t>(value), 32U);
        constant.value.is_signed = true;
        constant.const_type = vpiIntConst;
        constants_.push_back(std::move(constant));
        return tagged(Tag::constant, constants_.size() - 1U);
    }

    [[nodiscard]] const Constant* constant_of(const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::constant
            || serial_of(handle) >= constants_.size()) {
            return nullptr;
        }
        return &constants_[serial_of(handle)];
    }

    // --- clocking blocks (IEEE 1800-2023 37.48) -----------------------------

    // A clocking block or clockvar object and its declaration in the
    // instance's unit.
    struct ClockingView {
        const semantic::sv::ClockingBlock* block { };
        const semantic::sv::ClockingSignal* signal { };
        fsim_vpi_handle_v1 block_handle { };
        // The module, interface or program holding the block.
        fsim_vpi_handle_v1 scope { };
    };

    [[nodiscard]] const semantic::sv::Unit* unit_of(
        const runtime::SystemVerilogVpiObjectInfo& instance) const
    {
        if (!instance.type || !instance.type->semantic_unit_id) {
            return nullptr;
        }
        const auto& units = simulation_.systemverilog_hir().units();
        const auto unit = std::ranges::find(units,
            semantic::UnitId::from_index(*instance.type->semantic_unit_id),
            &semantic::sv::Unit::id);
        return unit == units.end() ? nullptr : &*unit;
    }

    [[nodiscard]] std::optional<ClockingView> clocking_view(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto object = info(handle);
        if (!object
            || (object->kind != SystemVerilogVpiObjectKind::ClockingBlock
                && object->kind != SystemVerilogVpiObjectKind::ClockingIo)) {
            return std::nullopt;
        }
        ClockingView view;
        view.block_handle
            = object->kind == SystemVerilogVpiObjectKind::ClockingBlock
            ? handle
            : object->parent;
        const auto block = info(view.block_handle);
        const auto scope = block ? info(block->parent) : std::nullopt;
        const auto* unit = scope ? unit_of(*scope) : nullptr;
        if (unit == nullptr) {
            return std::nullopt;
        }
        view.scope = block->parent;
        const auto found = std::ranges::find(unit->clocking_blocks,
            block->name, &semantic::sv::ClockingBlock::name);
        if (found == unit->clocking_blocks.end()) {
            return std::nullopt;
        }
        view.block = &*found;
        if (object->kind == SystemVerilogVpiObjectKind::ClockingIo) {
            const auto signal = std::ranges::find(found->signals,
                object->name,
                [](const semantic::sv::ClockingSignal& entry)
                    -> const std::string& { return entry.name.spelling; });
            if (signal == found->signals.end()) {
                return std::nullopt;
            }
            view.signal = &*signal;
        }
        return view;
    }

    // The skew that applies to the block's defaults or to the clockvar
    // itself (37.48 detail 1); the output skew of an input is none.
    [[nodiscard]] static const semantic::sv::ClockingSkew* clocking_skew_of(
        const ClockingView& view, const bool output)
    {
        using Direction = semantic::sv::Direction;
        if (view.signal == nullptr) {
            const auto& skew = output ? view.block->default_output_skew
                                      : view.block->default_input_skew;
            return skew ? &*skew : nullptr;
        }
        const auto direction = view.signal->direction;
        const auto& skew = output && direction == Direction::inout
            ? view.signal->output_skew
            : view.signal->skew;
        if (output ? direction == Direction::input
                   : direction == Direction::output) {
            return nullptr;
        }
        return skew ? &*skew : nullptr;
    }

    [[nodiscard]] static PLI_INT32 clocking_edge(
        const semantic::sv::ClockingSkew* skew)
    {
        if (skew == nullptr) {
            return vpiNoEdge;
        }
        return skew->edge == semantic::sv::EdgeKind::positive ? vpiPosedge
            : skew->edge == semantic::sv::EdgeKind::negative  ? vpiNegedge
                                                              : vpiNoEdge;
    }

    [[nodiscard]] fsim_vpi_handle_v1 make_clocking_skew(
        const fsim_vpi_handle_v1 owner, const bool output)
    {
        for (std::size_t index = 0U; index < clocking_skews_.size(); ++index) {
            if (clocking_skews_[index].owner == owner
                && clocking_skews_[index].output == output) {
                return tagged(Tag::clocking_skew, index);
            }
        }
        clocking_skews_.push_back({ owner, output });
        return tagged(Tag::clocking_skew, clocking_skews_.size() - 1U);
    }

    [[nodiscard]] const ClockingSkewHandle* clocking_skew_handle(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::clocking_skew
            || serial_of(handle) >= clocking_skews_.size()) {
            return nullptr;
        }
        return &clocking_skews_[serial_of(handle)];
    }

    [[nodiscard]] fsim_vpi_handle_v1 make_clocking_event(
        const fsim_vpi_handle_v1 block)
    {
        const auto found = std::ranges::find(clocking_events_, block);
        if (found != clocking_events_.end()) {
            return tagged(Tag::clocking_event,
                static_cast<std::uint64_t>(found - clocking_events_.begin()));
        }
        clocking_events_.push_back(block);
        return tagged(Tag::clocking_event, clocking_events_.size() - 1U);
    }

    [[nodiscard]] fsim_vpi_handle_v1 clocking_event_block(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::clocking_event
            || serial_of(handle) >= clocking_events_.size()) {
            return 0U;
        }
        return clocking_events_[serial_of(handle)];
    }

    // The default clocking of an instance (14.12), or the global clocking in
    // effect there, which is the nearest enclosing declaration (14.14).
    [[nodiscard]] fsim_vpi_handle_v1 clocking_declared(
        const runtime::SystemVerilogVpiObjectInfo& instance,
        const bool global) const
    {
        auto current = std::optional { instance };
        for (std::size_t depth = 0U; current && depth < 256U; ++depth) {
            if (const auto* unit = unit_of(*current);
                unit != nullptr && instance_kind(current->kind)) {
                std::string name;
                if (!global) {
                    if (unit->default_clocking) {
                        name = unit->default_clocking->block.spelling;
                    }
                } else if (const auto block
                    = std::ranges::find_if(unit->clocking_blocks,
                        &semantic::sv::ClockingBlock::global);
                    block != unit->clocking_blocks.end()) {
                    name = block->name;
                }
                if (!name.empty()) {
                    const auto found
                        = registry_.find_child(current->handle, name);
                    if (found && found.value) {
                        return found.value->handle;
                    }
                }
            }
            if (!global) {
                break;
            }
            current = info(current->parent);
        }
        return 0U;
    }

    // vpi_handle(type, clocking object) for the relations of 37.48; 0 when
    // the object has none. The expression a clockvar names is the object of
    // its name, or of the simple or hierarchical name it is assigned
    // (detail 4); any other expression is not published.
    [[nodiscard]] fsim_vpi_handle_v1 clocking_relation(
        const fsim_vpi_handle_v1 handle, const PLI_INT32 type)
    {
        const auto view = clocking_view(handle);
        if (!view) {
            return 0U;
        }
        switch (type) {
        case vpiInputSkew:
        case vpiOutputSkew: {
            const auto* skew = clocking_skew_of(*view, type == vpiOutputSkew);
            return skew != nullptr && (skew->delay || skew->one_step)
                ? make_clocking_skew(handle, type == vpiOutputSkew)
                : 0U;
        }
        case vpiClockingEvent:
            return view->signal == nullptr
                    && !view->block->event.empty()
                ? make_clocking_event(handle)
                : 0U;
        case vpiExpr: {
            if (view->signal == nullptr) {
                return 0U;
            }
            std::string name = view->signal->name.spelling;
            if (view->signal->expression) {
                const auto* expression = simulation_.systemverilog_hir()
                                             .find_expression(*view->signal->expression);
                if (expression == nullptr
                    || expression->kind != semantic::sv::ExpressionKind::name) {
                    return 0U;
                }
                name = expression->text;
            }
            return name_from_scope(view->scope, name);
        }
        default:
            return 0U;
        }
    }

    // --- nettype declarations (IEEE 1800-2023 37.23) -----------------------

    // A nettype declaration object and its declaration in the instance's
    // unit.
    struct NettypeView {
        const semantic::sv::TypeDefinition* type { };
        fsim_vpi_handle_v1 instance { };
    };

    [[nodiscard]] std::optional<NettypeView> nettype_view(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto object = info(handle);
        if (!object
            || object->kind != SystemVerilogVpiObjectKind::TypeSpecification) {
            return std::nullopt;
        }
        const auto instance = info(object->parent);
        if (!instance || !instance->type || !instance->type->semantic_unit_id) {
            return std::nullopt;
        }
        const auto& hir = simulation_.systemverilog_hir();
        const auto& scopes = simulation_.semantics().scopes();
        const auto unit
            = semantic::UnitId::from_index(*instance->type->semantic_unit_id);
        for (const auto& declaration : hir.declarations()) {
            if (declaration.form
                    != semantic::sv::DeclarationForm::nettype_declaration
                || declaration.name != object->name
                || !declaration.declared_type || !declaration.scope.valid()
                || declaration.scope.value() >= scopes.size()
                || scopes[declaration.scope.value()].unit != unit) {
                continue;
            }
            if (const auto* type = hir.find_type(*declaration.declared_type)) {
                return NettypeView { type, object->parent };
            }
        }
        return std::nullopt;
    }

    // vpiWith is the resolution function and vpiNetTypedefAlias the nettype
    // this one is declared as a synonym of; each is NULL when absent (37.23
    // details 1, 2).
    [[nodiscard]] fsim_vpi_handle_v1 nettype_relation(
        const fsim_vpi_handle_v1 handle, const PLI_INT32 type) const
    {
        const auto view = nettype_view(handle);
        if (!view) {
            return 0U;
        }
        if (type == vpiWith) {
            return view->type->resolution_function.empty()
                ? 0U
                : name_from_scope(
                      view->instance, view->type->resolution_function);
        }
        const auto sibling = registry_.find_child(
            view->instance, view->type->base.target.spelling);
        return type == vpiNetTypedefAlias && sibling && sibling.value
                && sibling.value->kind
                    == SystemVerilogVpiObjectKind::TypeSpecification
            ? sibling.value->handle
            : 0U;
    }

    // The delay of a skew as a constant, in the unit it is written in;
    // `#1step` and a delay given by an expression have none.
    [[nodiscard]] fsim_vpi_handle_v1 clocking_skew_delay(
        const ClockingSkewHandle& handle)
    {
        const auto view = clocking_view(handle.owner);
        const auto* skew = view ? clocking_skew_of(*view, handle.output)
                                : nullptr;
        if (skew == nullptr || !skew->delay || skew->one_step) {
            return 0U;
        }
        const auto& primary = skew->delay->primary;
        if (primary.expression && primary.magnitude == 0U) {
            return 0U;
        }
        return make_constant(static_cast<std::int64_t>(
            primary.magnitude / std::max<std::uint64_t>(primary.divisor, 1U)));
    }

    // The index of an array word, from the name the registry gives it.
    [[nodiscard]] std::optional<std::int64_t> word_index(
        const runtime::SystemVerilogVpiObjectInfo& word) const
    {
        const auto open = word.full_name.rfind('[');
        if (open == std::string::npos || word.full_name.back() != ']'
            || word.parent == 0U || owned(word.parent)
            || registry_.kind_of(word.parent)
                != SystemVerilogVpiObjectKind::Memory) {
            return std::nullopt;
        }
        std::int64_t index { };
        const auto digits = std::string_view { word.full_name }.substr(
            open + 1U, word.full_name.size() - open - 2U);
        const auto parsed = std::from_chars(
            digits.data(), digits.data() + digits.size(), index);
        return parsed.ec == std::errc { } && parsed.ptr == digits.data() + digits.size()
            ? std::optional { index }
            : std::nullopt;
    }

    // --- call sites -------------------------------------------------------

    [[nodiscard]] std::size_t call_site(const std::string_view identity,
        const std::span<const std::string> names)
    {
        const auto found = call_by_identity_.find(std::string { identity });
        if (found != call_by_identity_.end()) {
            return found->second;
        }
        // `$vpi:<name>|<scope>|<file>|<line>|<column>`
        auto rest = identity.substr(
            elaboration::user_system_call_identity_prefix.size());
        CallSite site;
        const auto take = [&rest] {
            const auto separator = rest.find('|');
            const auto field = rest.substr(0U, separator);
            rest = separator == std::string_view::npos
                ? std::string_view { }
                : rest.substr(separator + 1U);
            return std::string { field };
        };
        site.name = take();
        site.scope = take();
        const auto column_separator = rest.rfind('|');
        const auto line_separator = column_separator == std::string_view::npos
            ? std::string_view::npos
            : rest.rfind('|', column_separator - 1U);
        if (line_separator != std::string_view::npos) {
            site.file = std::string { rest.substr(0U, line_separator) };
            const auto line = rest.substr(line_separator + 1U,
                column_separator - line_separator - 1U);
            for (const auto digit : line) {
                site.line = site.line * 10U
                    + static_cast<std::uint32_t>(digit - '0');
            }
        }
        auto systf = systf_by_name_.find(site.name);
        if (systf == systf_by_name_.end() && resolve(site.name)) {
            systf = systf_by_name_.find(site.name);
        }
        if (systf == systf_by_name_.end()) {
            throw std::runtime_error { "VPI system task '" + site.name
                + "' is not registered by a loaded VPI module" };
        }
        site.systf = systf->second;
        const auto& data = systfs_[site.systf].data;
        site.function = data.type == vpiSysFunc;
        if (site.function) {
            bool is_signed { };
            CallScope scope { *this };
            system_function_shape(data, site.width, is_signed);
            site.result.bits = PackedLogic4 { site.width, Logic4::x };
            site.result.is_signed = is_signed;
        }
        for (const auto& name : names) {
            Argument argument;
            if (name.starts_with("n:")) {
                argument.text = name.substr(2U);
                argument.object = find_in_scope(site.scope, argument.text);
                if (argument.object == 0U) {
                    argument.value.bits = PackedLogic4 { 1U, Logic4::x };
                }
            } else if (name.starts_with("x:")) {
                // `x:<name>|<left>|<right>|<b or p>`: a constant bit- or
                // part-select of a named vector.
                const auto fields = std::string_view { name }.substr(2U);
                const auto first = fields.find('|');
                const auto second = fields.find('|', first + 1U);
                const auto third = fields.find('|', second + 1U);
                std::int64_t left { };
                std::int64_t right { };
                if (third != std::string_view::npos) {
                    (void)std::from_chars(fields.data() + first + 1U,
                        fields.data() + second, left);
                    (void)std::from_chars(fields.data() + second + 1U,
                        fields.data() + third, right);
                    const auto base = std::string { fields.substr(0U, first) };
                    argument.text = base
                        + (fields.substr(third + 1U) == "b"
                                ? "[" + std::to_string(left) + "]"
                                : "[" + std::to_string(left) + ":"
                                    + std::to_string(right) + "]");
                    if (const auto parent = find_in_scope(site.scope, base)) {
                        argument.object = make_select(parent, left, right,
                            fields.substr(third + 1U) == "b");
                    }
                }
                if (argument.object == 0U) {
                    argument.value.bits = PackedLogic4 { 1U, Logic4::x };
                }
            } else if (name.starts_with("s:")) {
                argument.text = name.substr(2U);
                argument.string_literal = true;
                argument.value.kind = Value::Kind::string;
                argument.value.text = argument.text;
            } else if (name.starts_with("e:")) {
                argument.expression = true;
                argument.real = name.starts_with("e:r:");
                argument.value.is_signed = name.starts_with("e:s:");
                argument.text = name.size() > 4U ? name.substr(4U) : "";
            } else {
                argument.value.bits = PackedLogic4 { 1U, Logic4::x };
            }
            site.arguments.push_back(std::move(argument));
        }
        calls_.push_back(std::move(site));
        call_by_identity_.emplace(std::string { identity }, calls_.size() - 1U);
        return calls_.size() - 1U;
    }

    // A simple name resolves in the call's scope and then each enclosing
    // scope; a dotted name may also be a complete hierarchical name.
    [[nodiscard]] fsim_vpi_handle_v1 find_in_scope(
        const std::string& scope, const std::string& name) const
    {
        auto prefix = scope;
        while (true) {
            const auto candidate = prefix.empty() ? name
                : prefix.ends_with("::") ? prefix + name
                                         : prefix + "." + name;
            if (const auto found = registry_.find(candidate);
                found && found.value) {
                return found.value->handle;
            }
            if (prefix.empty()) {
                return 0U;
            }
            const auto separator = prefix.rfind('.');
            prefix.resize(separator == std::string::npos ? 0U : separator);
        }
    }

    [[nodiscard]] const Argument* argument(const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::argument) {
            return nullptr;
        }
        const auto serial = serial_of(handle);
        const auto site = static_cast<std::size_t>(serial >> 16U);
        const auto index = static_cast<std::size_t>(serial & 0xFFFFU);
        if (site >= calls_.size() || index >= calls_[site].arguments.size()) {
            return nullptr;
        }
        return &calls_[site].arguments[index];
    }

    [[nodiscard]] const CallSite* call_of(const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::call
            || serial_of(handle) >= calls_.size()) {
            return nullptr;
        }
        return &calls_[serial_of(handle)];
    }

    // The design object behind a handle, through an argument naming one.
    [[nodiscard]] fsim_vpi_handle_v1 resolve(const fsim_vpi_handle_v1 handle) const
    {
        if (const auto* named = argument(handle);
            named != nullptr && named->object != 0U) {
            return named->object;
        }
        return handle;
    }

    // --- statement model ---------------------------------------------------

    // The design's processes and statements, built on first use from the
    // SystemVerilog HIR.
    [[nodiscard]] VpiStatementModel& statement_model()
    {
        if (!statement_model_) {
            VpiStatementModel::Hooks hooks;
            hooks.handle = [](const std::uint64_t serial) {
                return tagged(Tag::node, serial);
            };
            hooks.resolve = [this](const fsim_vpi_handle_v1 scope,
                                const std::string& name) {
                return name_from_scope(scope, name);
            };
            hooks.child_scope = [this](const fsim_vpi_handle_v1 scope,
                                    const std::string& name) {
                const auto owner = info(scope);
                if (!owner) {
                    return fsim_vpi_handle_v1 { };
                }
                const auto found = registry_.find(owner->full_name + "." + name);
                return found && found.value && scope_kind(found.value->kind)
                    ? found.value->handle
                    : fsim_vpi_handle_v1 { };
            };
            hooks.full_name = [this](const fsim_vpi_handle_v1 scope) {
                const auto owner = info(scope);
                return owner ? owner->full_name : std::string { };
            };
            hooks.unpacked_dimensions = [this](const fsim_vpi_handle_v1 handle) {
                const auto owner = info(handle);
                return owner && owner->kind == SystemVerilogVpiObjectKind::Memory
                    ? dimension_ranges(handle).size()
                    : std::size_t { 0U };
            };
            hooks.bit_width = [this](const fsim_vpi_handle_v1 handle) {
                auto object = handle;
                if (const auto selected = select_expression_object(handle);
                    selected != 0U) {
                    object = selected;
                }
                if (const auto* select = select_of(object)) {
                    return static_cast<std::size_t>(select->width);
                }
                const auto described = info(object);
                if (!described || !described->type
                    || described->kind == SystemVerilogVpiObjectKind::Memory) {
                    return std::size_t { 0U };
                }
                return static_cast<std::size_t>(described->type->width);
            };
            statement_model_ = std::make_unique<VpiStatementModel>(
                simulation_.systemverilog_hir(), simulation_.semantics(),
                std::move(hooks));
        }
        return *statement_model_;
    }

    // --- classes (IEEE 1800-2023 37.31 to 37.33) ---------------------------

    // The class defns and class typespecs of the design, built on first
    // use from the SystemVerilog HIR and the class specializations.
    [[nodiscard]] VpiClassModel& class_model()
    {
        if (!class_model_) {
            VpiClassModel::Hooks hooks;
            hooks.handle = [](const std::uint64_t serial) {
                return tagged(Tag::class_model, serial);
            };
            hooks.defn = [this](const std::string& identity) {
                return class_defn_handle(identity);
            };
            hooks.members = [this](const std::string& identity,
                                const PLI_INT32 type) {
                return class_members(identity, type);
            };
            hooks.full_name = [this](const fsim_vpi_handle_v1 handle) {
                const auto object = info(handle);
                return object ? object->full_name : std::string { };
            };
            class_model_ = std::make_unique<VpiClassModel>(
                simulation_.systemverilog_hir(), simulation_.semantics(),
                simulation_.class_specializations(), std::move(hooks));
        }
        return *class_model_;
    }

    // The declaration identity a class defn object was published for.
    [[nodiscard]] static std::optional<std::string> class_identity(
        const runtime::SystemVerilogVpiObjectInfo& object)
    {
        if (object.kind != SystemVerilogVpiObjectKind::Class || !object.type
            || !object.type->descriptor) {
            return std::nullopt;
        }
        return object.type->descriptor->nominal_name;
    }

    // A variable of a class type: a class property, or a variable whose
    // typespec is a class (IEEE 1800-2023 37.33).
    [[nodiscard]] static bool class_variable(
        const runtime::SystemVerilogVpiObjectInfo& object)
    {
        if (!object.type) {
            return false;
        }
        if (object.type->descriptor
            && object.type->descriptor->kind
                == runtime::SystemVerilogVpiDescriptorKind::ClassHandle) {
            return true;
        }
        return object.kind == SystemVerilogVpiObjectKind::Variable
            && object.type->typespec
            && object.type->typespec->kind == TypespecKind::Class;
    }

    [[nodiscard]] bool class_variable_handle(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto object = handle != 0U && !owned(handle)
            ? info(handle)
            : std::nullopt;
        return object && class_variable(*object);
    }

    // A handle of the class model, a class defn or one of its properties:
    // none holds a value (IEEE 1800-2023 37.31 detail 2).
    [[nodiscard]] bool class_scope_object(const fsim_vpi_handle_v1 handle) const
    {
        if (owned(handle)) {
            return tag_of(handle) == Tag::class_model;
        }
        const auto object = info(handle);
        return object
            && (object->kind == SystemVerilogVpiObjectKind::Class
                || object->kind == SystemVerilogVpiObjectKind::ClassProperty);
    }

    [[nodiscard]] fsim_vpi_handle_v1 class_defn_handle(
        const std::string& identity)
    {
        if (!class_defns_built_) {
            class_defns_built_ = true;
            for (const auto handle : drain(registry_.iterate_objects(
                     SystemVerilogVpiObjectKind::Class))) {
                if (const auto object = info(handle)) {
                    if (auto name = class_identity(*object)) {
                        class_defns_.emplace(std::move(*name), handle);
                    }
                }
            }
        }
        const auto found = class_defns_.find(identity);
        return found == class_defns_.end() ? fsim_vpi_handle_v1 { }
                                           : found->second;
    }

    // The objects under a class defn that iterate as `type`.
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> class_members(
        const std::string& identity, const PLI_INT32 type)
    {
        std::vector<fsim_vpi_handle_v1> objects;
        const auto defn = class_defn_handle(identity);
        if (defn == 0U) {
            return objects;
        }
        for (const auto handle : drain(registry_.iterate_children(defn))) {
            if (const auto object = info(handle);
                object && matches(type, *object)) {
                objects.push_back(handle);
            }
        }
        return objects;
    }

    // --- class objects (IEEE 1800-2023 37.33) --------------------------------

    // A property or a constraint of a class object as a handle.
    struct ClassMember {
        bool constraint { };
        runtime::SystemVerilogClassHandle object { };
        // The property's index in the object.
        std::size_t property { };
        // The class model's constraint.
        fsim_vpi_handle_v1 model { };
    };

    // vpiObjId: the object's heap slot, unique among the live objects and
    // reused once the object is gone (37.33 detail 1).
    [[nodiscard]] static std::int64_t class_object_id(
        const runtime::SystemVerilogClassHandle object) noexcept
    {
        return static_cast<std::int64_t>(object & 0xFFFFFFFFU);
    }

    [[nodiscard]] fsim_vpi_handle_v1 class_object_handle(
        const runtime::SystemVerilogClassHandle object)
    {
        if (!simulation_.class_heap().contains(object)) {
            return 0U;
        }
        auto found = class_object_serials_.find(object);
        if (found == class_object_serials_.end()) {
            class_objects_.push_back(object);
            found = class_object_serials_
                        .emplace(object, class_objects_.size() - 1U)
                        .first;
        }
        return tagged(Tag::class_object, found->second);
    }

    // The heap's handle of a class obj while the object is alive, else 0.
    [[nodiscard]] runtime::SystemVerilogClassHandle class_object_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::class_object
            || serial_of(handle) >= class_objects_.size()) {
            return 0U;
        }
        const auto object = class_objects_[serial_of(handle)];
        return simulation_.class_heap().contains(object) ? object : 0U;
    }

    [[nodiscard]] static bool class_live_tag(const fsim_vpi_handle_v1 handle)
    {
        return owned(handle)
            && (tag_of(handle) == Tag::class_object
                || tag_of(handle) == Tag::class_member);
    }

    [[nodiscard]] fsim_vpi_handle_v1 class_member_handle(
        const ClassMember& member)
    {
        const auto key = std::tuple { member.object, member.constraint,
            member.property, member.model };
        auto found = class_member_index_.find(key);
        if (found == class_member_index_.end()) {
            class_members_.push_back(member);
            found = class_member_index_.emplace(key, class_members_.size() - 1U)
                        .first;
        }
        return tagged(Tag::class_member, found->second);
    }

    [[nodiscard]] const ClassMember* class_member_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::class_member
            || serial_of(handle) >= class_members_.size()) {
            return nullptr;
        }
        const auto& member = class_members_[serial_of(handle)];
        return simulation_.class_heap().contains(member.object) ? &member
                                                                : nullptr;
    }

    // The object a class variable refers to: its value is the heap's handle,
    // or 0 for null (37.33 detail 2).
    [[nodiscard]] runtime::SystemVerilogClassHandle class_variable_object(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto value = object_value(handle);
        if (!value || value->kind != Value::Kind::bits
            || value->bits.width() == 0U) {
            return 0U;
        }
        const auto word = value->bits.low_word();
        return word.bval == 0U && simulation_.class_heap().contains(word.aval)
            ? word.aval
            : 0U;
    }

    [[nodiscard]] static std::string class_property_name(
        const std::string& identity)
    {
        const auto separator = identity.rfind("::");
        return separator == std::string::npos ? identity
                                              : identity.substr(separator + 2U);
    }

    // The registry's class property object a live property is typed by.
    [[nodiscard]] fsim_vpi_handle_v1 class_property_object(
        const ClassMember& member)
    {
        const auto& object = simulation_.class_heap().object(member.object);
        const auto& identity = object.property_names.at(member.property);
        const auto separator = identity.rfind("::");
        const auto owner = separator == std::string::npos
            ? object.dynamic_type
            : identity.substr(0U, separator);
        const auto name = class_property_name(identity);
        for (const auto handle : class_members(owner, vpiVariables)) {
            if (const auto declared = info(handle);
                declared && declared->name == name) {
                return handle;
            }
        }
        return 0U;
    }

    [[nodiscard]] const runtime::SystemVerilogClassPropertyValue&
    class_property_stored(const ClassMember& member) const
    {
        return simulation_.class_heap()
            .object(member.object)
            .properties.at(member.property);
    }

    // The value of a class property of a live object; none for a class
    // handle or a container (37.33).
    [[nodiscard]] std::optional<Value> class_property_value(
        const ClassMember& member)
    {
        using Kind = runtime::SystemVerilogClassPropertyKind;
        const auto& stored = class_property_stored(member);
        Value value;
        switch (stored.kind) {
        case Kind::String:
            value.kind = Value::Kind::string;
            value.text = stored.string;
            return value;
        case Kind::Bit2:
        case Kind::Logic4:
        case Kind::Logic9:
        case Kind::Integer:
            if (stored.packed.width() == 0U) {
                return std::nullopt;
            }
            value.bits = stored.packed.is_logic9()
                ? runtime::collapse_to_logic4(stored.packed)
                : stored.packed;
            if (const auto declared = info(class_property_object(member));
                declared && declared->type) {
                value.is_signed = declared->type->is_signed;
            }
            return value;
        default:
            return std::nullopt;
        }
    }

    fsim_vpi_status_v1 get_class_value(const fsim_vpi_handle_v1 handle,
        s_vpi_value& out, fsim_vpi_service_result_v1& result)
    {
        const auto* member = class_member_of(handle);
        const auto value = member != nullptr && !member->constraint
            ? class_property_value(*member)
            : std::nullopt;
        if (!value || !write_value(*value, out)) {
            out.format = vpiSuppressVal;
            std::memset(&out.value, 0, sizeof(out.value));
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        return succeed(result);
    }

    fsim_vpi_status_v1 put_class_value(const fsim_vpi_handle_v1 handle,
        const p_vpi_value input, const PLI_INT32 mode,
        fsim_vpi_service_result_v1& result)
    {
        using Kind = runtime::SystemVerilogClassPropertyKind;
        const auto* member = class_member_of(handle);
        if (member == nullptr || member->constraint || input == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (mode != vpiNoDelay) {
            error_message_ = "vpi_put_value: a class property takes only "
                             "vpiNoDelay";
            error_ = true;
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        auto& stored = simulation_.class_heap()
                           .object(member->object)
                           .properties.at(member->property);
        if (stored.kind == Kind::String) {
            if (input->format != vpiStringVal) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            stored.string = input->value.str != nullptr ? input->value.str : "";
            return succeed(result);
        }
        if ((stored.kind != Kind::Bit2 && stored.kind != Kind::Logic4
                && stored.kind != Kind::Logic9 && stored.kind != Kind::Integer)
            || stored.packed.width() == 0U) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto width = stored.packed.width();
        const auto value = input_value(*input, width);
        if (!value) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        auto bits = value_bits(*value, width);
        if (stored.kind == Kind::Bit2) {
            for (std::size_t bit = 0U; bit < width; ++bit) {
                if (bits.get(bit) != Logic4::one) {
                    bits.set(bit, Logic4::zero);
                }
            }
        }
        if (stored.packed.is_logic9()) {
            PackedLogic4 nine { width };
            for (std::size_t bit = 0U; bit < width; ++bit) {
                nine.set_logic9(bit, runtime::to_logic9(bits.get(bit)));
            }
            bits = std::move(nine);
        }
        stored.packed = std::move(bits);
        return succeed(result);
    }

    // The enabled state of a constraint of a live object (18.9).
    [[nodiscard]] bool class_constraint_enabled(const ClassMember& member)
    {
        const auto named
            = class_model().constraint_identity(serial_of(member.model));
        if (!named) {
            return true;
        }
        const auto& modes
            = simulation_.class_heap().object(member.object).constraint_modes;
        if (const auto found = modes.find(named->first); found != modes.end()) {
            return found->second;
        }
        for (const auto& [identity, enabled] : modes) {
            if (class_property_name(identity) == named->second) {
                return enabled;
            }
        }
        return true;
    }

    // The class typespec of a live object: the class it was created as
    // (37.33 detail 5).
    [[nodiscard]] fsim_vpi_handle_v1 class_object_typespec(
        const runtime::SystemVerilogClassHandle object)
    {
        const auto& stored = simulation_.class_heap().object(object);
        auto handle = class_model().typespec_for_name(
            stored.specialization_identity);
        if (handle == 0U) {
            handle = class_model().typespec_for_name(stored.dynamic_type);
        }
        return handle;
    }

    [[nodiscard]] std::vector<fsim_vpi_handle_v1> class_object_iterate(
        const fsim_vpi_handle_v1 handle, const PLI_INT32 type)
    {
        std::vector<fsim_vpi_handle_v1> objects;
        if (tag_of(handle) == Tag::class_member) {
            const auto* member = class_member_of(handle);
            if (member != nullptr && member->constraint) {
                objects = class_model()
                              .iterate(serial_of(member->model), type)
                              .value_or(objects);
            }
            return objects;
        }
        const auto object = class_object_of(handle);
        if (object == 0U) {
            return objects;
        }
        // The automatic properties; a static property is not part of the
        // object and is not listed.
        if (type == vpiVariables) {
            const auto count
                = simulation_.class_heap().object(object).properties.size();
            for (std::size_t index = 0U; index < count; ++index) {
                objects.push_back(class_member_handle(
                    ClassMember { false, object, index, 0U }));
            }
            return objects;
        }
        const auto spec = class_object_typespec(object);
        if (spec == 0U) {
            return objects;
        }
        auto declared = class_model()
                            .iterate(serial_of(spec), type)
                            .value_or(std::vector<fsim_vpi_handle_v1> { });
        if (type != vpiConstraint) {
            return declared;
        }
        for (const auto model : declared) {
            objects.push_back(class_member_handle(
                ClassMember { true, object, 0U, model }));
        }
        return objects;
    }

    fsim_vpi_status_v1 class_object_relation(
        const fsim_vpi_service_request_v1& request,
        const fsim_vpi_handle_v1 object_handle, const PLI_INT32 type,
        fsim_vpi_service_result_v1& result)
    {
        if (tag_of(object_handle) == Tag::class_object) {
            const auto object = class_object_of(object_handle);
            return succeed(result,
                object != 0U && type == vpiClassTypespec
                    ? class_object_typespec(object)
                    : 0U);
        }
        const auto* member = class_member_of(object_handle);
        if (member == nullptr) {
            return succeed(result);
        }
        if (type == vpiParent || type == vpiScope) {
            return succeed(result, class_object_handle(member->object));
        }
        if (member->constraint) {
            return succeed(result,
                class_model().relation(serial_of(member->model), type));
        }
        if (type == vpiClassObj) {
            const auto& stored = class_property_stored(*member);
            return succeed(result,
                stored.kind
                        == runtime::SystemVerilogClassPropertyKind::ClassHandle
                    ? class_object_handle(stored.handle)
                    : 0U);
        }
        auto inner = request;
        inner.handle = class_property_object(*member);
        return inner.handle == 0U ? succeed(result) : handle(inner, result);
    }

    fsim_vpi_status_v1 class_object_property(
        const fsim_vpi_handle_v1 object_handle, const PLI_INT32 what,
        fsim_vpi_service_result_v1& result)
    {
        const auto undefined = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(vpiUndefined));
        const auto integer = [&](const std::int64_t value) {
            return succeed(result, 0U, static_cast<std::uint64_t>(value));
        };
        // A live object and its parts are dynamic (37.3.7).
        if (what == vpiAllocScheme) {
            return class_object_of(object_handle) != 0U
                    || class_member_of(object_handle) != nullptr
                ? integer(vpiDynamicScheme)
                : succeed(result, 0U, undefined);
        }
        if (tag_of(object_handle) == Tag::class_object) {
            const auto object = class_object_of(object_handle);
            return object != 0U && what == vpiObjId
                ? integer(class_object_id(object))
                : succeed(result, 0U, undefined);
        }
        const auto* member = class_member_of(object_handle);
        if (member == nullptr) {
            return succeed(result, 0U, undefined);
        }
        if (member->constraint) {
            if (what == vpiIsConstraintEnabled) {
                return integer(class_constraint_enabled(*member) ? 1 : 0);
            }
            const auto value
                = class_model().property(serial_of(member->model), what);
            return value ? integer(*value) : succeed(result, 0U, undefined);
        }
        const auto& stored = class_property_stored(*member);
        if (what == vpiObjId) {
            return stored.kind
                    == runtime::SystemVerilogClassPropertyKind::ClassHandle
                ? integer(class_object_id(
                    simulation_.class_heap().contains(stored.handle)
                        ? stored.handle
                        : 0U))
                : succeed(result, 0U, undefined);
        }
        const auto declared = class_property_object(*member);
        if (declared == 0U) {
            return succeed(result, 0U, undefined);
        }
        fsim_vpi_service_request_v1 inner { };
        inner.handle = declared;
        inner.argument = static_cast<std::uint64_t>(what);
        return property(inner, result);
    }

    fsim_vpi_status_v1 class_object_string(
        const fsim_vpi_handle_v1 object_handle, const PLI_INT32 what,
        fsim_vpi_service_result_v1& result)
    {
        const auto* member = class_member_of(object_handle);
        if (member == nullptr || tag_of(object_handle) != Tag::class_member) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (member->constraint) {
            auto value
                = class_model().string_property(serial_of(member->model), what);
            return value ? succeed(result, 0U, 0U,
                       address(store(std::move(*value))))
                         : FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A non-static data member has no vpiFullName (37.33 detail 9).
        if (what != vpiName) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        return succeed(result, 0U, 0U,
            address(store(class_property_name(
                simulation_.class_heap()
                    .object(member->object)
                    .property_names.at(member->property)))));
    }

    // --- class object callbacks ---------------------------------------------

    // A callback of an object: obj is the object, with no value (IEEE
    // 1800-2023 38.36.1).
    void fire_object(
        const std::uint64_t serial, const fsim_vpi_handle_v1 object)
    {
        const auto found = callbacks_.find(serial);
        if (found == callbacks_.end()) {
            return;
        }
        const auto& stored = *found->second;
        auto data = stored.data;
        auto time = stored.time;
        data.obj = reinterpret_cast<vpiHandle>(
            static_cast<std::uintptr_t>(object));
        data.time = nullptr;
        data.value = nullptr;
        data.index = 0;
        if (stored.has_time && stored.time.type != vpiSuppressTime) {
            fill_time(time, object);
            data.time = &time;
        }
        const auto routine = data.cb_rtn;
        CallScope scope { *this };
        routine(&data);
    }

    void ensure_size_hook()
    {
        if (size_hooked_) {
            return;
        }
        size_hooked_ = true;
        simulation_.set_vpi_size_change_hook(
            [this](const fsim_vpi_handle_v1 memory, std::size_t, std::size_t) {
                const auto listeners = size_callbacks_;
                for (const auto& listener : listeners) {
                    if (listener.object == memory) {
                        fire_object(listener.serial, memory);
                    }
                }
            });
    }

    void ensure_reclaim_hook()
    {
        if (reclaim_hooked_) {
            return;
        }
        reclaim_hooked_ = true;
        simulation_.set_class_reclaim_hook([this]() { reclaim_class_objects(); });
    }

    // Whether a reclaim callback registered on `target` (a class typespec or
    // a class obj) hears of `object`: any object of the class typespec's
    // type (38.36.1).
    [[nodiscard]] bool class_reclaim_matches(const fsim_vpi_handle_v1 target,
        const runtime::SystemVerilogClassHandle object,
        const fsim_vpi_handle_v1 handle)
    {
        if (target == handle) {
            return true;
        }
        if (!owned(target) || tag_of(target) != Tag::class_model) {
            return false;
        }
        const auto named = class_model().typespec_class(serial_of(target));
        if (!named) {
            return false;
        }
        // The class of a design element instance is generated from the
        // specialization the typespec names (6.22).
        const auto& stored = simulation_.class_heap().object(object);
        return stored.dynamic_type == named->first
            && (named->second.empty()
                || semantic::sv::generated_specialization_source(
                       stored.specialization_identity)
                    == named->second);
    }

    // No collector runs, so the objects still alive at the end of the
    // simulation are the reclaimed ones: each is announced once, before the
    // end of simulation callbacks, and its handle is invalid afterwards
    // (IEEE 1800-2023 38.36.1 cbReclaimObj, cbEndOfObject).
    void reclaim_class_objects()
    {
        if (reclaim_callbacks_.empty()) {
            return;
        }
        for (const auto object : simulation_.class_heap().live_handles()) {
            if (!simulation_.class_heap().contains(object)) {
                continue;
            }
            const auto handle = class_object_handle(object);
            const auto listeners = reclaim_callbacks_;
            for (const auto& listener : listeners) {
                if (class_reclaim_matches(listener.object, object, handle)) {
                    fire_object(listener.serial, handle);
                }
            }
            if (const auto found = class_object_serials_.find(object);
                found != class_object_serials_.end()) {
                class_objects_[found->second] = 0U;
                class_object_serials_.erase(found);
            }
        }
        for (const auto& listener : reclaim_callbacks_) {
            callbacks_.erase(listener.serial);
        }
        reclaim_callbacks_.clear();
    }

    // --- frames, threads and object creation (IEEE 1800-2023 37.43, 37.44) ---

    [[nodiscard]] static const char* callback_reason_name(const PLI_INT32 reason)
    {
        switch (reason) {
        case cbStartOfFrame:
            return "cbStartOfFrame";
        case cbEndOfFrame:
            return "cbEndOfFrame";
        case cbStartOfThread:
            return "cbStartOfThread";
        case cbEndOfThread:
            return "cbEndOfThread";
        case cbEnterThread:
            return "cbEnterThread";
        default:
            return "cbCreateObj";
        }
    }

    [[nodiscard]] static bool frame_tag(const fsim_vpi_handle_v1 handle)
    {
        return owned(handle)
            && (tag_of(handle) == Tag::frame || tag_of(handle) == Tag::thread);
    }

    // A frame or thread handle names its object until the end callbacks of
    // the object have run (37.3.7, 37.3.8).
    [[nodiscard]] const VpiFrameModel::Frame* frame_of(
        const fsim_vpi_handle_v1 handle) const
    {
        return frames_ && owned(handle) && tag_of(handle) == Tag::frame
            ? frames_->frame(serial_of(handle))
            : nullptr;
    }

    [[nodiscard]] const VpiFrameModel::Thread* thread_of(
        const fsim_vpi_handle_v1 handle) const
    {
        return frames_ && owned(handle) && tag_of(handle) == Tag::thread
            ? frames_->thread(serial_of(handle))
            : nullptr;
    }

    void ensure_frame_hook()
    {
        if (frame_hook_ != 0U) {
            return;
        }
        if (!frames_) {
            frames_ = std::make_unique<VpiFrameModel>();
        }
        frame_hook_ = simulation_.add_execution_point_hook(
            [this](auto&, const runtime::simir::ExecutionPoint& point) {
                deliver_frame_point(point);
            });
    }

    void ensure_create_hook()
    {
        if (create_hooked_) {
            return;
        }
        create_hooked_ = true;
        ensure_frame_hook();
        simulation_.set_class_created_hook(
            [this](const runtime::SystemVerilogClassHandle object) {
                deliver_created(object);
            });
    }

    // Whether the thread belongs to a process of the design's source: the
    // kernel's own startup and initializer processes are no threads.
    [[nodiscard]] bool thread_has_source(
        const runtime::simir::ProcessId design_process)
    {
        if (!thread_sources_built_) {
            thread_sources_built_ = true;
            for (const auto& occurrence : simulation_.design_ir().processes()) {
                if (occurrence.source_process) {
                    thread_sources_.insert(static_cast<runtime::simir::ProcessId>(
                        occurrence.runtime_index));
                }
            }
        }
        return thread_sources_.contains(design_process);
    }

    void deliver_frame_point(const runtime::simir::ExecutionPoint& point)
    {
        using Kind = runtime::simir::ExecutionPointKind;
        if (point.kind == Kind::object_created) {
            // A lowered constructor completed (38.36.1 cbCreateObj).
            deliver_created(simulation_.take_constructed_class_object());
            return;
        }
        if (point.kind == Kind::thread_start
            && !thread_has_source(point.design_process)) {
            return;
        }
        for (const auto& occurrence : frames_->apply(point)) {
            deliver_frame_occurrence(occurrence);
            frames_->retire(occurrence);
        }
    }

    [[nodiscard]] static bool callee_matches(
        const std::string& name, const std::string& callee)
    {
        return name == callee || name.ends_with("." + callee)
            || name.ends_with("::" + callee);
    }

    void deliver_frame_occurrence(const VpiFrameModel::Occurrence& occurrence)
    {
        using Event = VpiFrameModel::Event;
        const bool frame_event = occurrence.event == Event::frame_start
            || occurrence.event == Event::frame_end;
        PLI_INT32 reason = cbStartOfThread;
        switch (occurrence.event) {
        case Event::thread_start:
            reason = cbStartOfThread;
            break;
        case Event::thread_end:
            reason = cbEndOfThread;
            break;
        case Event::thread_enter:
            reason = cbEnterThread;
            break;
        case Event::frame_start:
            reason = cbStartOfFrame;
            break;
        case Event::frame_end:
            reason = cbEndOfFrame;
            break;
        }
        const auto object = frame_event
            ? tagged(Tag::frame, occurrence.frame)
            : tagged(Tag::thread, occurrence.thread);
        const auto* const frame = frame_event ? frames_->frame(occurrence.frame)
                                              : nullptr;
        const auto listeners = frame_callbacks_;
        for (const auto& listener : listeners) {
            if (listener.reason != reason) {
                continue;
            }
            if (listener.object != 0U) {
                // A task or function, a frame or a thread.
                if (!listener.callee.empty()) {
                    if (frame == nullptr
                        || !callee_matches(listener.callee, frame->callee)) {
                        continue;
                    }
                } else if (listener.object != object) {
                    continue;
                }
            }
            fire_object(listener.serial, object);
        }
    }

    // The class object a completed constructor made: each cbCreateObj
    // callback on its class typespec hears of it (38.36.1).
    void deliver_created(const runtime::SystemVerilogClassHandle object)
    {
        if (object == 0U || !simulation_.class_heap().contains(object)) {
            return;
        }
        const auto handle = class_object_handle(object);
        const auto listeners = create_callbacks_;
        for (const auto& listener : listeners) {
            if (class_reclaim_matches(listener.object, object, handle)) {
                fire_object(listener.serial, handle);
            }
        }
    }

    // The statement model's node for the statement that starts at a point
    // of a process; 0 when none does.
    [[nodiscard]] fsim_vpi_handle_v1 statement_at_location(
        const runtime::simir::ProcessId design_process,
        const runtime::simir::SourceLocation& where)
    {
        const auto source = source_process(design_process);
        if (!source) {
            return 0U;
        }
        return statement_model()
            .statement_at(source->first, source->second, where.path.str(),
                where.line, where.column)
            .statement;
    }

    // The task or function call at or below `node` that calls `callee`
    // (37.43 detail 6): the statement of a task call, or the call within
    // an expression.
    [[nodiscard]] fsim_vpi_handle_v1 find_call(const fsim_vpi_handle_v1 node,
        const std::string& callee, const std::size_t depth)
    {
        const auto serial = node_of(node);
        if (!serial || depth > 8U) {
            return 0U;
        }
        const auto type = statement_model_->type(*serial);
        if (type == vpiTaskCall || type == vpiMethodTaskCall) {
            return node;
        }
        if (type == vpiFuncCall || type == vpiMethodFuncCall) {
            const auto name
                = statement_model_->string_property(*serial, vpiName);
            if (name && callee_matches(*name, callee)) {
                return node;
            }
        }
        std::vector<fsim_vpi_handle_v1> next;
        for (const auto relation : { vpiRhs, vpiLhs, vpiExpr, vpiCondition }) {
            if (const auto child = statement_model_->relation(*serial, relation);
                child && *child != 0U) {
                next.push_back(*child);
            }
        }
        for (const auto iteration : { vpiOperand, vpiArgument }) {
            if (auto children = statement_model_->iterate(*serial, iteration)) {
                next.insert(next.end(), children->begin(), children->end());
            }
        }
        for (const auto child : next) {
            if (const auto found = find_call(child, callee, depth + 1U);
                found != 0U) {
                return found;
            }
        }
        return 0U;
    }

    [[nodiscard]] fsim_vpi_handle_v1 frame_relation(
        const VpiFrameModel::Frame& frame, const PLI_INT32 type)
    {
        switch (type) {
        case vpiParent:
            return frame.parent != 0U && frames_->frame(frame.parent) != nullptr
                ? tagged(Tag::frame, frame.parent)
                : 0U;
        case vpiThread:
            return tagged(Tag::thread, frame.thread);
        // The call that activated the frame (37.43 detail 6).
        case vpiOrigin: {
            if (!frame.has_origin) {
                return 0U;
            }
            const auto statement
                = statement_at_location(frame.design_process, frame.origin);
            return statement == 0U ? 0U
                                   : find_call(statement, frame.callee, 0U);
        }
        // The statement the frame executes (37.43 detail 4).
        case vpiStmt:
            return frame.has_statement
                ? statement_at_location(frame.design_process, frame.statement)
                : 0U;
        default:
            return 0U;
        }
    }

    [[nodiscard]] fsim_vpi_handle_v1 thread_relation(
        const VpiFrameModel::Thread& thread, const PLI_INT32 type)
    {
        switch (type) {
        case vpiParent:
            return thread.parent != 0U
                    && frames_->thread(thread.parent) != nullptr
                ? tagged(Tag::thread, thread.parent)
                : 0U;
        // vpi_handle(vpiFrame, thread): the frame that is active (37.44).
        case vpiFrame: {
            const auto active = frames_->active_frame(thread.serial);
            return active == 0U ? 0U : tagged(Tag::frame, active);
        }
        // The first statement the thread runs (37.44).
        case vpiOrigin:
            return statement_at_location(thread.design_process, thread.origin);
        default:
            return 0U;
        }
    }

    // vpi_handle(vpiFrame, NULL): the active frame of the running thread
    // (37.43 detail 4).
    [[nodiscard]] fsim_vpi_handle_v1 current_frame_handle() const
    {
        if (!frames_) {
            return 0U;
        }
        const auto active
            = frames_->active_frame(frames_->current_thread());
        return active == 0U ? 0U : tagged(Tag::frame, active);
    }

    // vpi_iterate(vpiThread, NULL | thread): every live thread, or the
    // threads a thread forked (37.44).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> thread_iterate(
        const fsim_vpi_handle_v1 reference) const
    {
        std::vector<fsim_vpi_handle_v1> objects;
        if (!frames_) {
            return objects;
        }
        if (reference == 0U) {
            for (const auto serial : frames_->threads()) {
                objects.push_back(tagged(Tag::thread, serial));
            }
        } else if (const auto* const parent = thread_of(reference)) {
            for (const auto serial : parent->children) {
                objects.push_back(tagged(Tag::thread, serial));
            }
        }
        return objects;
    }

    fsim_vpi_status_v1 frame_property(const fsim_vpi_handle_v1 handle,
        const PLI_INT32 what, fsim_vpi_service_result_v1& result)
    {
        const auto undefined = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(vpiUndefined));
        // A frame is active while it is its thread's innermost one (37.43
        // detail 4). The standard gives a thread's vpiActive no meaning
        // beyond its type, so it stays undefined.
        if (what == vpiActive) {
            if (const auto* const frame = frame_of(handle)) {
                return succeed(result, 0U,
                    frames_->active_frame(frame->thread) == frame->serial
                        ? 1U
                        : 0U);
            }
        }
        // Frames and threads are transient (37.3.7).
        if (what == vpiAllocScheme) {
            return succeed(result, 0U,
                static_cast<std::uint64_t>(vpiOtherScheme));
        }
        return succeed(result, 0U, undefined);
    }

    [[nodiscard]] std::optional<std::uint64_t> node_of(
        const fsim_vpi_handle_v1 handle) const
    {
        if (!owned(handle) || tag_of(handle) != Tag::node || !statement_model_
            || !statement_model_->owns(serial_of(handle))) {
            return std::nullopt;
        }
        return serial_of(handle);
    }

    // The drivers, loads or uses of a net or variable: the continuous
    // assignments, assignment statements, forces and procedural assigns of
    // its declaring scope that write, read or mention it, and the ports that
    // connect it (IEEE 1800-2023 37.21, 37.46, 37.58). A port drives the side
    // it is an input to and loads the side it is an output from. A net also
    // has the primitive terminals on it. Statements of other scopes that name
    // the object hierarchically are not found.
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> connections_of(
        const PLI_INT32 type, const fsim_vpi_handle_v1 handle)
    {
        std::vector<fsim_vpi_handle_v1> found;
        const auto object = info(handle);
        if (!object) {
            return found;
        }
        switch (object->kind) {
        case SystemVerilogVpiObjectKind::Net:
        case SystemVerilogVpiObjectKind::Variable:
        case SystemVerilogVpiObjectKind::Memory:
        case SystemVerilogVpiObjectKind::Port:
            break;
        default:
            return found;
        }
        const auto connection = type == vpiDriver || type == vpiLocalDriver
            ? VpiStatementModel::Connection::driver
            : type == vpiLoad || type == vpiLocalLoad
            ? VpiStatementModel::Connection::load
            : VpiStatementModel::Connection::use;
        if (const auto scope = hir_scope(object->parent)) {
            found = statement_model().connections(*scope, handle, connection);
        }
        using Direction = runtime::SystemVerilogVpiDirection;
        const auto& ports = simulation_.systemverilog_vpi_ports();
        for (std::size_t index = 0U; index < ports.size(); ++index) {
            const auto& port = ports[index];
            const auto low = port.low_conn == handle;
            if (port.io_declaration || (!low && port.high_conn != handle)) {
                continue;
            }
            const auto input = port.direction == Direction::Input
                || port.direction == Direction::Inout;
            const auto output = port.direction == Direction::Output
                || port.direction == Direction::Inout;
            const auto drives = low ? input : output;
            const auto loads = low ? output : input;
            if (connection == VpiStatementModel::Connection::use
                || (connection == VpiStatementModel::Connection::driver
                        ? drives
                        : loads)) {
                found.push_back(tagged(Tag::port, index));
            }
        }
        // The terminals of the gates, switches and UDPs on a net or variable.
        if (object->kind == SystemVerilogVpiObjectKind::Net
            || object->kind == SystemVerilogVpiObjectKind::Variable) {
            append_primitive_connections(type, handle, found);
        }
        // A net or variable a port connects is published as a port object;
        // its SDF delay terms count whatever the kind.
        append_net_delay_connections(type, handle, found);
        return found;
    }

    // The vpiFuncType of a system function call: the type a registered
    // function declared, or what the standard defines for a built-in one
    // (IEEE 1800-2023 20, 37.42).
    [[nodiscard]] std::optional<std::int64_t> system_function_type(
        const std::string& name) const
    {
        if (const auto known = systf_by_name_.find(name);
            known != systf_by_name_.end()) {
            return systfs_[known->second].data.sysfunctype;
        }
        static const std::unordered_map<std::string_view, std::int64_t> builtin {
            { "$time", vpiTimeFunc }, { "$stime", vpiSizedFunc },
            { "$realtime", vpiRealFunc }, { "$itor", vpiRealFunc },
            { "$bitstoreal", vpiRealFunc }, { "$bitstoshortreal", vpiRealFunc },
            { "$sqrt", vpiRealFunc }, { "$ln", vpiRealFunc },
            { "$log10", vpiRealFunc }, { "$exp", vpiRealFunc },
            { "$pow", vpiRealFunc }, { "$floor", vpiRealFunc },
            { "$ceil", vpiRealFunc }, { "$sin", vpiRealFunc },
            { "$cos", vpiRealFunc }, { "$tan", vpiRealFunc },
            { "$asin", vpiRealFunc }, { "$acos", vpiRealFunc },
            { "$atan", vpiRealFunc }, { "$atan2", vpiRealFunc },
            { "$hypot", vpiRealFunc }, { "$sinh", vpiRealFunc },
            { "$cosh", vpiRealFunc }, { "$tanh", vpiRealFunc },
            { "$asinh", vpiRealFunc }, { "$acosh", vpiRealFunc },
            { "$atanh", vpiRealFunc }, { "$random", vpiIntFunc },
            { "$rtoi", vpiIntFunc }, { "$clog2", vpiIntFunc },
            { "$bits", vpiIntFunc }, { "$countones", vpiIntFunc },
            { "$onehot", vpiIntFunc }, { "$onehot0", vpiIntFunc },
            { "$isunknown", vpiIntFunc }, { "$dimensions", vpiIntFunc },
            { "$unpacked_dimensions", vpiIntFunc }, { "$left", vpiIntFunc },
            { "$right", vpiIntFunc }, { "$low", vpiIntFunc },
            { "$high", vpiIntFunc }, { "$increment", vpiIntFunc },
            { "$size", vpiIntFunc }, { "$test$plusargs", vpiIntFunc },
            { "$value$plusargs", vpiIntFunc }, { "$urandom_range", vpiSizedFunc },
            { "$urandom", vpiSizedFunc }, { "$realtobits", vpiSizedFunc },
            { "$shortrealtobits", vpiSizedFunc }, { "$unsigned", vpiSizedFunc },
            { "$signed", vpiSizedSignedFunc },
        };
        const auto found = builtin.find(name);
        return found == builtin.end() ? std::nullopt
                                      : std::optional { found->second };
    }

    // A user function call has its function's vpiFuncType (IEEE 1800-2023
    // 37.42).
    [[nodiscard]] std::optional<std::int64_t> call_function_type(
        const std::uint64_t serial)
    {
        const auto type = statement_model_->type(serial);
        if (type == vpiSysFuncCall) {
            const auto name
                = statement_model_->string_property(serial, vpiName);
            return name ? system_function_type(*name) : std::nullopt;
        }
        if (type != vpiFuncCall && type != vpiMethodFuncCall) {
            return std::nullopt;
        }
        const auto callee
            = statement_model_->relation(serial, vpiFunction).value_or(0U);
        const auto scope = callee != 0U && !owned(callee)
            ? hir_scope(callee)
            : std::nullopt;
        return scope ? statement_model().scope_property(*scope, vpiFuncType)
                     : std::nullopt;
    }

    // A simple or hierarchical name seen from a scope: the scope and the
    // scopes enclosing it up to the instance, then the design (IEEE
    // 1800-2023 23.8, 23.9).
    [[nodiscard]] fsim_vpi_handle_v1 name_from_scope(
        const fsim_vpi_handle_v1 scope, const std::string& name) const
    {
        auto current = scope;
        for (std::size_t depth = 0U; current != 0U && depth < 256U; ++depth) {
            const auto object = info(current);
            if (!object) {
                break;
            }
            const auto found = registry_.find(object->full_name
                + (object->full_name.ends_with("::") ? "" : ".") + name);
            if (found && found.value) {
                return found.value->handle;
            }
            if (instance_kind(object->kind)) {
                break;
            }
            // A formal or local of a task, function or named block is no
            // published object, but hides the outer names.
            if ((block_kind(object->kind)
                    || object->kind == SystemVerilogVpiObjectKind::Task
                    || object->kind == SystemVerilogVpiObjectKind::Function)
                && statement_model_) {
                if (const auto local = hir_scope(current);
                    local && statement_model_->declares(*local, name)) {
                    return 0U;
                }
            }
            current = object->parent;
        }
        const auto found = registry_.find(name);
        return found && found.value ? found.value->handle : 0U;
    }

    // Where a scope object's contents are in the HIR: the generate
    // blocks, callable and named blocks from its instance down to it.
    [[nodiscard]] static bool block_kind(
        const SystemVerilogVpiObjectKind kind) noexcept
    {
        return kind == SystemVerilogVpiObjectKind::Begin
            || kind == SystemVerilogVpiObjectKind::Fork;
    }
    // Where a named block scope is: the registry holds no source for one.
    [[nodiscard]] std::optional<VpiStatementModel::BlockSource>
    named_block_source(const fsim_vpi_handle_v1 handle)
    {
        const auto scope = hir_scope(handle);
        return scope ? statement_model().block_source(*scope) : std::nullopt;
    }
    [[nodiscard]] std::optional<VpiHirScope> hir_scope(
        const fsim_vpi_handle_v1 handle) const
    {
        VpiHirScope scope;
        scope.handle = handle;
        auto current = handle;
        for (std::size_t depth = 0U; depth < 256U; ++depth) {
            const auto object = info(current);
            if (!object) {
                return std::nullopt;
            }
            switch (object->kind) {
            case SystemVerilogVpiObjectKind::Root:
            case SystemVerilogVpiObjectKind::Module:
            case SystemVerilogVpiObjectKind::Interface:
            case SystemVerilogVpiObjectKind::Program:
                if (!object->type || !object->type->semantic_unit_id) {
                    return std::nullopt;
                }
                scope.unit = *object->type->semantic_unit_id;
                std::ranges::reverse(scope.steps);
                return scope;
            case SystemVerilogVpiObjectKind::GenerateScope:
                scope.steps.emplace_back(VpiHirScope::Step::generate, object->name);
                break;
            case SystemVerilogVpiObjectKind::Task:
            case SystemVerilogVpiObjectKind::Function:
                scope.steps.emplace_back(VpiHirScope::Step::callable, object->name);
                break;
            case SystemVerilogVpiObjectKind::Begin:
            case SystemVerilogVpiObjectKind::Fork:
                scope.steps.emplace_back(VpiHirScope::Step::block, object->name);
                break;
            case SystemVerilogVpiObjectKind::GenerateScopeArray:
                break;
            default:
                return std::nullopt;
            }
            current = object->parent;
        }
        return std::nullopt;
    }

    // --- registration ------------------------------------------------------

    fsim_vpi_status_v1 register_systf(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto* const data = pointer<s_vpi_systf_data>(request.user_data);
        if (data == nullptr || data->tfname == nullptr
            || data->tfname[0] != '$') {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        Systf systf;
        systf.data = *data;
        systf.name = data->tfname;
        systf.data.tfname = nullptr;
        systfs_.push_back(std::move(systf));
        systfs_.back().data.tfname = systfs_.back().name.data();
        systf_by_name_[systfs_.back().name] = systfs_.size() - 1U;
        return succeed(result, tagged(Tag::systf, systfs_.size() - 1U));
    }

    fsim_vpi_status_v1 systf_info(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        auto* const data = pointer<s_vpi_systf_data>(request.user_data);
        auto handle = request.handle;
        if (const auto* site = call_of(handle)) {
            handle = tagged(Tag::systf, site->systf);
        }
        if (data == nullptr || !owned(handle) || tag_of(handle) != Tag::systf
            || serial_of(handle) >= systfs_.size()) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        *data = systfs_[serial_of(handle)].data;
        return succeed(result);
    }

    [[nodiscard]] static runtime::SystemVerilogVpiTimeValue delay_of(
        const s_vpi_time& time)
    {
        runtime::SystemVerilogVpiTimeValue delay;
        if (time.type == vpiScaledRealTime) {
            delay.format = runtime::SystemVerilogVpiTimeFormat::ScaledReal;
            delay.scaled_real = time.real;
        } else {
            delay.high = static_cast<std::uint32_t>(time.high);
            delay.low = static_cast<std::uint32_t>(time.low);
        }
        return delay;
    }

    // vpiScaledRealTime is in the time unit of `object`'s module, or of
    // the simulation without one (IEEE 1800-2023 38.36.2, 38.34).
    [[nodiscard]] runtime::SystemVerilogVpiTimeValue delay_of(
        const s_vpi_time& time, const fsim_vpi_handle_v1 object) const
    {
        auto delay = delay_of(time);
        if (time.type == vpiScaledRealTime) {
            if (const auto scale = module_time_scale(object)) {
                const auto profile
                    = simulation_.systemverilog_vpi_time().profile();
                delay.scaled_real *= std::pow(10.0,
                    scale->first - static_cast<int>(profile.unit_exponent));
            }
        }
        return delay;
    }

    fsim_vpi_status_v1 register_callback(
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto* const data = pointer<s_cb_data>(request.user_data);
        if (data == nullptr || data->cb_rtn == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        auto stored = std::make_unique<Callback>();
        stored->data = *data;
        // Simulation action callbacks take no time (IEEE 1800-2023
        // 38.36.3); a caller may leave the field uninitialized.
        const auto action_reason = data->reason == cbEndOfCompile
            || data->reason == cbStartOfSimulation
            || data->reason == cbEndOfSimulation
            || data->reason == cbError || data->reason == cbPLIError
            || data->reason == cbTchkViolation
            || data->reason == cbStartOfSave || data->reason == cbEndOfSave
            || data->reason == cbStartOfRestart
            || data->reason == cbEndOfRestart
            || data->reason == cbStartOfReset
            || data->reason == cbEndOfReset
            || data->reason == cbEnterInteractive
            || data->reason == cbExitInteractive
            || data->reason == cbInteractiveScopeChange
            || data->reason == cbUnresolvedSystf
            || data->reason == cbSignal
            || data->reason == cbReclaimObj || data->reason == cbEndOfObject;
        if (!action_reason && data->time != nullptr) {
            stored->time = *data->time;
            stored->has_time = true;
        }
        // A simulation time callback needs a vpiSimTime or
        // vpiScaledRealTime time; vpiSuppressTime or a NULL time is an
        // error and creates no callback (IEEE 1800-2023 38.36.2).
        const auto time_reason = data->reason == cbAtStartOfSimTime
            || data->reason == cbReadWriteSynch
            || data->reason == cbReadOnlySynch
            || data->reason == cbNextSimTime
            || data->reason == cbAfterDelay
            || data->reason == cbAtEndOfSimTime
            || data->reason == cbNBASynch;
        if (time_reason
            && (!stored->has_time
                || (stored->time.type != vpiSimTime
                    && stored->time.type != vpiScaledRealTime))) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        // Only the object callbacks read value (IEEE 1800-2023 38.36.1);
        // other reasons may leave the field uninitialized.
        const auto value_reason = data->reason == cbValueChange
            || data->reason == cbForce || data->reason == cbRelease
            || data->reason == cbAssign || data->reason == cbDeassign
            || data->reason == cbDisable;
        if (value_reason && data->value != nullptr) {
            stored->value.format = data->value->format;
            stored->has_value = true;
        }
        const auto serial = ++next_callback_;
        const auto object = resolve(internal(data->obj));
        std::optional<runtime::SystemVerilogVpiCallbackKind> kind;
        std::optional<runtime::SystemVerilogVpiTimeValue> delay;
        fsim_vpi_handle_v1 watched = object;
        switch (data->reason) {
        case cbEndOfCompile:
            end_of_compile_.push_back(serial);
            break;
        case cbStartOfSimulation:
            kind = runtime::SystemVerilogVpiCallbackKind::StartOfSimulation;
            break;
        case cbEndOfSimulation:
            kind = runtime::SystemVerilogVpiCallbackKind::EndOfSimulation;
            break;
        case cbError:
            run_error_callbacks_.push_back(serial);
            // The callback reads the time and state of the error's time
            // step, so the v4 kernel runs no time steps the scheduler does
            // not see (docs/engine-v4-semantics.md S19).
            simulation_.open_external_observation();
            break;
        case cbPLIError:
            pli_error_callbacks_.push_back(serial);
            break;
        // `obj` may name one timing check; NULL watches them all. The
        // scheduler hook exists only while a callback does.
        case cbTchkViolation:
            if (object != 0U && !tchk_of(object)) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            tchk_callbacks_.push_back(serial);
            if (tchk_callbacks_.size() == 1U) {
                simulation_.set_verilog_timing_violation_hook(
                    [this](const std::uint32_t check) {
                        deliver_tchk_violation(check);
                    });
                simulation_.open_external_observation();
            }
            break;
        // A call site whose system task or function the modules have not
        // registered in this run (IEEE 1800-2023 38.36.2).
        case cbUnresolvedSystf:
            unresolved_callbacks_.push_back(serial);
            break;
        // `index` names the signal; it reaches the callback at the next
        // safe point (IEEE 1800-2023 38.36.3).
        case cbSignal: {
            auto trap = std::make_unique<ForeignSignalTrap>(data->index);
            if (!trap->valid()) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            signal_traps_.emplace(serial, std::move(trap));
            if (signal_hook_ == 0U) {
                signal_hook_ = simulation_.add_safe_point_hook(
                    [this](runtime::Scheduler&, runtime::SchedulerPhase) {
                        deliver_signals();
                    });
            }
            break;
        }
        // The save, restart and reset phases fire these (IEEE 1800-2023
        // 38.36.3); nothing here but the phase driver triggers them.
        case cbStartOfSave:
        case cbEndOfSave:
        case cbStartOfRestart:
        case cbEndOfRestart:
        case cbStartOfReset:
        case cbEndOfReset:
            feature_callbacks_[data->reason].push_back(serial);
            break;
        case cbValueChange:
            if (object == 0U) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            if (const auto* select = select_of(object)) {
                if (!info(select->parent)) {
                    return FSIM_VPI_STATUS_NOT_FOUND;
                }
                stored->selected = object;
                stored->selected_last = object_value(object);
                watched = select->parent;
            } else if (owned(object)) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            kind = runtime::SystemVerilogVpiCallbackKind::ValueChange;
            break;
        // After a force, release, procedural assign or deassign statement;
        // a NULL object watches every one, and a bit-select is not allowed
        // (IEEE 1800-2023 38.36.1).
        case cbForce:
        case cbRelease:
        case cbAssign:
        case cbDeassign:
            if (object != 0U && (owned(object) || !info(object))) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            force_callbacks_.push_back(serial);
            ensure_force_hook();
            break;
        // Before a statement executes (IEEE 1800-2023 38.36.1.1). The kernel
        // reports its statements only when the run keeps its execution
        // probes, which the user asks for.
        case cbStmt: {
            if (!simulation_.foreign_probes()) {
                error_message_ = "vpi_register_cb: cbStmt needs the execution "
                                 "probes of simulate --foreign-probes "
                                 "(configuration key simulate.foreign_probes)";
                error_ = true;
                return FSIM_VPI_STATUS_UNSUPPORTED;
            }
            const auto node = node_of(object);
            const auto type = handle_type(object);
            if (node ? !statement_model_->is_statement(*node)
                     : type != vpiNamedBegin && type != vpiNamedFork) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            statement_callbacks_.push_back(StatementCallback {
                serial, object, statement_trigger(object) });
            ensure_statement_hook();
            break;
        }
        // After a named block, task or function is disabled (IEEE 1800-2023
        // 38.36.1); a disable statement's execution point finds it.
        case cbDisable: {
            if (!simulation_.foreign_probes()) {
                error_message_ = "vpi_register_cb: cbDisable needs the "
                                 "execution probes of simulate "
                                 "--foreign-probes (configuration key "
                                 "simulate.foreign_probes)";
                error_ = true;
                return FSIM_VPI_STATUS_UNSUPPORTED;
            }
            const auto type = handle_type(object);
            if (type != vpiNamedBegin && type != vpiNamedFork
                && type != vpiTask && type != vpiFunction) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            disable_callbacks_.push_back(serial);
            ensure_statement_hook();
            break;
        }
        // The assertion system's changes (IEEE 1800-2023 39.4.1).
        case cbAssertionSysInitialized:
        case cbAssertionSysOn:
        case cbAssertionSysOff:
        case cbAssertionSysKill:
        case cbAssertionSysLock:
        case cbAssertionSysUnlock:
        case cbAssertionSysEnd:
        case cbAssertionSysReset:
        case cbAssertionSysEnablePassAction:
        case cbAssertionSysEnableFailAction:
        case cbAssertionSysDisablePassAction:
        case cbAssertionSysDisableFailAction:
        case cbAssertionSysEnableNonvacuousAction:
        case cbAssertionSysDisableVacuousAction:
            if (assertion_system_ended_) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            assertion_system_callbacks_.push_back(serial);
            ensure_assertion_system_hooks();
            break;
        // After a dynamic array or queue was resized (IEEE 1800-2023 37.17
        // detail 14, 38.36.1); strings and associative arrays are not
        // announced.
        case cbSizeChange: {
            const auto target = info(object);
            if (!target || target->kind != SystemVerilogVpiObjectKind::Memory
                || !target->type || !target->type->descriptor
                || (target->type->descriptor->kind
                        != runtime::SystemVerilogVpiDescriptorKind::DynamicArray
                    && target->type->descriptor->kind
                        != runtime::SystemVerilogVpiDescriptorKind::Queue)) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            size_callbacks_.push_back({ serial, object });
            ensure_size_hook();
            break;
        }
        // Before a class object is reclaimed: a class typespec hears of each
        // object of its class, a class obj of itself (IEEE 1800-2023
        // 38.36.1). Objects are reclaimed at the end of the simulation.
        case cbReclaimObj:
        case cbEndOfObject: {
            const bool typespec = owned(object)
                && tag_of(object) == Tag::class_model
                && class_model().owns(serial_of(object))
                && class_model().typespec_class(serial_of(object));
            if (!typespec
                && (!owned(object) || tag_of(object) != Tag::class_object)) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            reclaim_callbacks_.push_back({ serial, object });
            ensure_reclaim_hook();
            break;
        }
        // Frames, threads and class construction are reported only when
        // the run keeps its execution probes, which the user asks for
        // (IEEE 1800-2023 37.43, 37.44, 38.36.1).
        case cbStartOfFrame:
        case cbEndOfFrame:
        case cbStartOfThread:
        case cbEndOfThread:
        case cbEnterThread:
        case cbCreateObj: {
            if (!simulation_.foreign_probes()) {
                error_message_ = std::string { "vpi_register_cb: " }
                    + callback_reason_name(data->reason)
                    + " needs the execution probes of simulate "
                      "--foreign-probes (configuration key "
                      "simulate.foreign_probes)";
                error_ = true;
                return FSIM_VPI_STATUS_UNSUPPORTED;
            }
            if (data->reason == cbCreateObj) {
                // The object is a class typespec: any object of its class
                // is announced (38.36.1).
                const bool typespec = owned(object)
                    && tag_of(object) == Tag::class_model
                    && class_model().owns(serial_of(object))
                    && class_model().typespec_class(serial_of(object));
                if (!typespec) {
                    return FSIM_VPI_STATUS_NOT_FOUND;
                }
                create_callbacks_.push_back({ serial, object });
                ensure_create_hook();
                break;
            }
            FrameCallback listener { serial, data->reason, object, { } };
            const bool frame_reason = data->reason == cbStartOfFrame
                || data->reason == cbEndOfFrame;
            if (object != 0U) {
                // A frame callback names a task or function, and may name
                // a frame it ends; a thread callback names a thread. Every
                // thread creation is reported, so cbStartOfThread names
                // nothing.
                const auto type = handle_type(object);
                if (frame_reason && (type == vpiTask || type == vpiFunction)) {
                    const auto target = info(object);
                    if (!target) {
                        return FSIM_VPI_STATUS_NOT_FOUND;
                    }
                    listener.callee = target->name;
                } else if (!(data->reason == cbEndOfFrame
                                 && frame_of(object) != nullptr)
                    && !((data->reason == cbEndOfThread
                             || data->reason == cbEnterThread)
                        && thread_of(object) != nullptr)) {
                    return FSIM_VPI_STATUS_NOT_FOUND;
                }
            }
            frame_callbacks_.push_back(std::move(listener));
            ensure_frame_hook();
            break;
        }
        case cbAfterDelay:
            if (!stored->has_time) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            kind = runtime::SystemVerilogVpiCallbackKind::AfterDelay;
            delay = delay_of(stored->time, object);
            break;
        // The time of these callbacks names a time queue, not a delay
        // (IEEE 1800-2023 38.36.2); cbAtEndOfSimTime runs in the
        // Pre-Postponed region (4.4.3.9), after every other region of the
        // slot and before the read-only phase.
        case cbAtStartOfSimTime:
        case cbAtEndOfSimTime: {
            if (!stored->has_time) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            auto& time_service = simulation_.systemverilog_vpi_time();
            const auto requested
                = time_service.convert_delay(delay_of(stored->time, object));
            const auto now = time_service.query(
                runtime::SystemVerilogVpiTimeFormat::IntegerTicks);
            if (!requested || requested.ticks < now.ticks) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            const auto precision_ticks
                = (requested.ticks - now.ticks) * now.profile.tick_multiplier;
            delay = runtime::SystemVerilogVpiTimeValue { };
            delay->high = static_cast<std::uint32_t>(precision_ticks >> 32U);
            delay->low = static_cast<std::uint32_t>(precision_ticks);
            kind = data->reason == cbAtStartOfSimTime
                ? runtime::SystemVerilogVpiCallbackKind::AfterDelay
                : runtime::SystemVerilogVpiCallbackKind::EndOfTimeSlot;
            break;
        }
        // Immediately before the nonblocking assignments (IEEE 1800-2023
        // 38.36.2, 4.4.3.3).
        case cbNBASynch:
            kind = runtime::SystemVerilogVpiCallbackKind::Synchronization;
            break;
        case cbReadWriteSynch:
            kind = runtime::SystemVerilogVpiCallbackKind::ReadWrite;
            break;
        case cbReadOnlySynch:
            kind = runtime::SystemVerilogVpiCallbackKind::ReadOnly;
            break;
        case cbNextSimTime:
            kind = runtime::SystemVerilogVpiCallbackKind::NextTime;
            break;
        default:
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (kind) {
            runtime::SystemVerilogVpiCallbackRegistration registration;
            registration.kind = *kind;
            if (*kind == runtime::SystemVerilogVpiCallbackKind::ValueChange) {
                registration.object = watched;
            }
            if ((*kind == runtime::SystemVerilogVpiCallbackKind::ReadWrite
                    || *kind == runtime::SystemVerilogVpiCallbackKind::ReadOnly
                    || *kind
                        == runtime::SystemVerilogVpiCallbackKind::Synchronization)
                && data->reason != cbAtEndOfSimTime && stored->has_time
                && (stored->time.high != 0 || stored->time.low != 0
                    || stored->time.real != 0.0)) {
                delay = delay_of(stored->time, object);
            }
            registration.delay = delay;
            registration.at_slot_start = data->reason == cbAfterDelay
                || data->reason == cbAtStartOfSimTime;
            // An ACC link's changes keep their values until dispatched
            // (remove_callback, IEEE 1364-2001 23.97).
            registration.keeps_pending_values
                = request.flags == acc_link_callback_operation
                && *kind == runtime::SystemVerilogVpiCallbackKind::ValueChange;
            registration.callback
                = [this, serial](
                      const runtime::SystemVerilogVpiCallbackEvent& event) {
                      fire(serial, event);
                  };
            const auto registered
                = simulation_.systemverilog_vpi_callbacks().register_callback(
                    std::move(registration));
            if (!registered) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            stored->managed = registered.value;
        }
        callbacks_.emplace(serial, std::move(stored));
        return succeed(result, tagged(Tag::callback, serial));
    }

    fsim_vpi_status_v1 remove_callback(
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        if (!owned(request.handle) || tag_of(request.handle) != Tag::callback) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto found = callbacks_.find(serial_of(request.handle));
        if (found == callbacks_.end()) {
            return remove_assertion_callback(serial_of(request.handle))
                ? succeed(result, 0U, 1U)
                : FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A nonzero argument, from the ACC link host before it deletes a
        // link, delivers the changes observed so far and keeps the callback.
        if (request.argument != 0U) {
            if (!found->second->managed) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            const auto managed = *found->second->managed;
            // What the running process changed before the call is observed
            // first.
            simulation_.publish_kernel_outputs();
            return simulation_.systemverilog_vpi_callbacks().dispatch_pending(
                       managed)
                    == runtime::SystemVerilogVpiCallbackError::None
                ? succeed(result, 0U, 1U)
                : FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (found->second->managed) {
            (void)simulation_.systemverilog_vpi_callbacks().remove_callback(
                *found->second->managed);
        }
        std::erase(end_of_compile_, found->first);
        for (auto& [reason, serials] : feature_callbacks_) {
            std::erase(serials, found->first);
        }
        std::erase(run_error_callbacks_, found->first);
        std::erase(unresolved_callbacks_, found->first);
        std::erase(force_callbacks_, found->first);
        std::erase_if(statement_callbacks_, [&](const auto& listener) {
            return listener.serial == found->first;
        });
        std::erase(disable_callbacks_, found->first);
        std::erase_if(size_callbacks_, [&](const auto& listener) {
            return listener.serial == found->first;
        });
        std::erase_if(reclaim_callbacks_, [&](const auto& listener) {
            return listener.serial == found->first;
        });
        std::erase_if(create_callbacks_, [&](const auto& listener) {
            return listener.serial == found->first;
        });
        std::erase_if(frame_callbacks_, [&](const auto& listener) {
            return listener.serial == found->first;
        });
        std::erase(assertion_system_callbacks_, found->first);
        signal_traps_.erase(found->first);
        std::erase(pli_error_callbacks_, found->first);
        if (std::erase(tchk_callbacks_, found->first) != 0U
            && tchk_callbacks_.empty()) {
            simulation_.set_verilog_timing_violation_hook({ });
        }
        callbacks_.erase(found);
        return succeed(result, 0U, 1U);
    }

    fsim_vpi_status_v1 callback_info(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        auto* const data = pointer<s_cb_data>(request.user_data);
        if (data == nullptr || !owned(request.handle)
            || tag_of(request.handle) != Tag::callback) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto found = callbacks_.find(serial_of(request.handle));
        if (found == callbacks_.end()) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        auto& stored = *found->second;
        *data = stored.data;
        data->time = stored.has_time ? &stored.time : nullptr;
        data->value = stored.has_value ? &stored.value : nullptr;
        return succeed(result);
    }

    void fire(const std::uint64_t serial,
        const std::optional<runtime::SystemVerilogVpiCallbackEvent>& event,
        const bool repeat = false)
    {
        const auto found = callbacks_.find(serial);
        if (found == callbacks_.end()) {
            return;
        }
        auto& stored = *found->second;
        // A select reports only a change of its own bits, with the select's
        // value (IEEE 1800-2023 38.36.1).
        std::optional<Value> selected_value;
        if (stored.selected != 0U && event
            && event->kind == runtime::SystemVerilogVpiCallbackKind::ValueChange) {
            selected_value = object_value(stored.selected);
            if (!selected_value
                || (stored.selected_last
                    && stored.selected_last->bits == selected_value->bits)) {
                return;
            }
            stored.selected_last = selected_value;
        }
        auto data = stored.data;
        auto time = stored.time;
        auto value = stored.value;
        data.time = nullptr;
        data.value = nullptr;
        if (stored.has_time) {
            fill_time(time, internal(stored.data.obj));
            data.time = &time;
        }
        // The new value of the change that triggered the callback; an event
        // statement has no value (IEEE 1800-2023 38.36.1), while a named
        // event is an object and gets the registered value structure with
        // vpiSuppressVal, since it has no value to report.
        if (event && stored.has_value
            && event->kind == runtime::SystemVerilogVpiCallbackKind::ValueChange
            && event->object) {
            const auto object = info(*event->object);
            if (selected_value) {
                if (write_value(*selected_value, value)) {
                    data.value = &value;
                }
            } else if (!event->value) {
                if (object
                    && object->kind
                        == runtime::SystemVerilogVpiObjectKind::NamedEvent) {
                    value.format = vpiSuppressVal;
                    data.value = &value;
                }
            } else {
                const auto changed = stored_to_value(*event->value,
                    object && object->type && object->type->is_signed);
                // A class variable's value is opaque: the callback gets a
                // NULL value (IEEE 1800-2023 38.36.1).
                if (changed && !(object && class_variable(*object))
                    && write_value(*changed, value)) {
                    data.value = &value;
                }
                // An array word reports its index (IEEE 1800-2023 38.36.1).
                if (object) {
                    if (const auto index = word_index(*object)) {
                        data.index = static_cast<PLI_INT32>(*index);
                    }
                }
            }
        }
        const auto one_shot = !repeat
            && (!event
                || event->kind
                    != runtime::SystemVerilogVpiCallbackKind::ValueChange);
        const auto routine = data.cb_rtn;
        const auto read_only = std::exchange(read_only_,
            event
                && event->kind
                    == runtime::SystemVerilogVpiCallbackKind::ReadOnly);
        {
            CallScope scope { *this };
            routine(&data);
        }
        read_only_ = read_only;
        if (one_shot) {
            callbacks_.erase(serial);
        }
    }

    // --- hierarchy ----------------------------------------------------------

    [[nodiscard]] static fsim_vpi_handle_v1 internal(const vpiHandle handle)
    {
        return static_cast<fsim_vpi_handle_v1>(
            reinterpret_cast<std::uintptr_t>(handle));
    }

    [[nodiscard]] std::optional<runtime::SystemVerilogVpiObjectInfo> info(
        const fsim_vpi_handle_v1 handle) const
    {
        if (handle == 0U || owned(handle)) {
            return std::nullopt;
        }
        auto found = registry_.lookup(handle);
        if (!found) {
            return std::nullopt;
        }
        return std::move(found.value);
    }

    [[nodiscard]] static bool scope_kind(const SystemVerilogVpiObjectKind kind)
    {
        switch (kind) {
        case SystemVerilogVpiObjectKind::Root:
        case SystemVerilogVpiObjectKind::Module:
        case SystemVerilogVpiObjectKind::Interface:
        case SystemVerilogVpiObjectKind::Program:
        case SystemVerilogVpiObjectKind::Package:
        case SystemVerilogVpiObjectKind::GenerateScope:
        case SystemVerilogVpiObjectKind::Begin:
        case SystemVerilogVpiObjectKind::Fork:
        case SystemVerilogVpiObjectKind::Task:
        case SystemVerilogVpiObjectKind::Function:
            return true;
        default:
            return false;
        }
    }

    // The vpiType of a design object (IEEE 1800-2023 37, Annex M).
    [[nodiscard]] PLI_INT32 object_type(
        const runtime::SystemVerilogVpiObjectInfo& object) const
    {
        using Category = runtime::SystemVerilogVpiValueCategory;
        const auto variable_type = [&] {
            if (!object.type) {
                return vpiReg;
            }
            switch (object.type->category) {
            case Category::Real:
            case Category::ShortReal:
                return vpiRealVar;
            case Category::Time:
                return vpiTimeVar;
            case Category::String:
                return vpiStringVar;
            case Category::Event:
                return vpiNamedEvent;
            case Category::Integer4:
                return vpiIntegerVar;
            case Category::Integer2:
                switch (object.type->width) {
                case 8U:
                    return vpiByteVar;
                case 16U:
                    return vpiShortIntVar;
                case 64U:
                    return vpiLongIntVar;
                default:
                    return vpiIntVar;
                }
            case Category::Bit2:
                return vpiBitVar;
            default:
                return vpiReg;
            }
        };
        switch (object.kind) {
        case SystemVerilogVpiObjectKind::Root:
        case SystemVerilogVpiObjectKind::Module:
            // A VHDL instance is a scope of no 1800-2023 object type.
            return object.type && object.type->foreign_instance
                ? runtime::fsim_vpi_foreign_instance_type
                : vpiModule;
        case SystemVerilogVpiObjectKind::Interface:
            return vpiInterface;
        case SystemVerilogVpiObjectKind::Program:
            return vpiProgram;
        case SystemVerilogVpiObjectKind::Package:
            return vpiPackage;
        case SystemVerilogVpiObjectKind::Gate:
            return vpiGate;
        case SystemVerilogVpiObjectKind::GenerateScope:
            return vpiGenScope;
        case SystemVerilogVpiObjectKind::GenerateScopeArray:
            return vpiGenScopeArray;
        case SystemVerilogVpiObjectKind::Modport:
            return vpiModport;
        // The type specification objects are the nettype declarations
        // (IEEE 1800-2023 37.23).
        case SystemVerilogVpiObjectKind::TypeSpecification:
            return vpiNettypeDecl;
        case SystemVerilogVpiObjectKind::ClockingBlock:
            return vpiClockingBlock;
        case SystemVerilogVpiObjectKind::ClockingIo:
            return vpiClockingIODecl;
        case SystemVerilogVpiObjectKind::ModuleArray:
            return vpiModuleArray;
        case SystemVerilogVpiObjectKind::InterfaceArray:
            return vpiInterfaceArray;
        case SystemVerilogVpiObjectKind::ProgramArray:
            return vpiProgramArray;
        case SystemVerilogVpiObjectKind::Begin:
            return vpiNamedBegin;
        case SystemVerilogVpiObjectKind::Fork:
            return vpiNamedFork;
        case SystemVerilogVpiObjectKind::Task:
            return vpiTask;
        case SystemVerilogVpiObjectKind::Function:
            return vpiFunction;
        case SystemVerilogVpiObjectKind::Net:
            if (object.type && object.type->declared) {
                return declared_type(*object.type->declared, true);
            }
            return vpiNet;
        case SystemVerilogVpiObjectKind::Port:
            if (object.type && object.type->declared) {
                return declared_type(*object.type->declared, net_object(object));
            }
            // A port with an explicit data type and no variable keyword is
            // a net of that type (IEEE 1800-2023 23.2.2.3, 37.16).
            if (object.type
                && object.type->net_kind
                    != runtime::SystemVerilogVpiNetKind::None) {
                switch (object.type->category) {
                case Category::Real:
                    return vpiRealNet;
                case Category::ShortReal:
                    return vpiShortRealNet;
                case Category::Time:
                    return vpiTimeNet;
                case Category::Integer4:
                    return vpiIntegerNet;
                case Category::Integer2:
                    switch (object.type->width) {
                    case 8U:
                        return vpiByteNet;
                    case 16U:
                        return vpiShortIntNet;
                    case 64U:
                        return vpiLongIntNet;
                    default:
                        return vpiIntNet;
                    }
                case Category::Bit2:
                    return vpiBitNet;
                default:
                    return vpiNet;
                }
            }
            return variable_type();
        case SystemVerilogVpiObjectKind::Variable:
            if (class_variable(object)) {
                return vpiClassVar;
            }
            if (object.type && object.type->declared) {
                return declared_type(*object.type->declared, false);
            }
            return variable_type();
        case SystemVerilogVpiObjectKind::Parameter:
            return vpiParameter;
        // A class and its properties (IEEE 1800-2023 37.31).
        case SystemVerilogVpiObjectKind::Class:
            return vpiClassDefn;
        case SystemVerilogVpiObjectKind::ClassProperty:
            if (object.type && object.type->local_parameter) {
                return vpiParameter;
            }
            return class_variable(object) ? vpiClassVar : variable_type();
        // An unpacked array is a vpiRegArray, and its words are variables
        // of their own type; vpiMemory and vpiMemoryWord remain only as
        // iteration relations (IEEE 1800-2023 37.17 detail 4, 37.20).
        case SystemVerilogVpiObjectKind::Memory:
            return net_object(object) || (object.type
                       && object.type->net_kind
                           != runtime::SystemVerilogVpiNetKind::None)
                ? vpiNetArray
                : vpiRegArray;
        case SystemVerilogVpiObjectKind::NamedEvent:
            return vpiNamedEvent;
        case SystemVerilogVpiObjectKind::Constant:
            return vpiConstant;
        case SystemVerilogVpiObjectKind::Assertion:
            if (const auto* assertion = concurrent_assertion(object)) {
                return assertion_type(*assertion);
            }
            return vpiUndefined;
        default:
            return vpiUndefined;
        }
    }

    [[nodiscard]] PLI_INT32 handle_type(const fsim_vpi_handle_v1 handle) const
    {
        if (const auto serial = node_of(handle)) {
            return statement_model_->type(*serial);
        }
        if (const auto* site = call_of(handle)) {
            return site->function ? vpiSysFuncCall : vpiSysTaskCall;
        }
        if (const auto* named = argument(handle)) {
            if (named->object != 0U) {
                return handle_type(named->object);
            }
            return named->expression && !literal(named->text) ? vpiOperation
                                                              : vpiConstant;
        }
        if (const auto* subarray = subarray_of(handle)) {
            const auto memory = info(subarray->memory);
            return memory ? object_type(*memory) : vpiUndefined;
        }
        if (const auto* select = select_of(handle)) {
            if (select->declared) {
                return declared_type(*select->declared, select->net);
            }
            if (select->bit && select->owner != 0U) {
                return select->net ? vpiNetBit : vpiRegBit;
            }
            if (select->element) {
                const auto parent = info(select->parent);
                return parent ? object_type(*parent) : vpiUndefined;
            }
            if (!select->bit) {
                return vpiPartSelect;
            }
            const auto parent = info(select->parent);
            return parent && parent->kind == SystemVerilogVpiObjectKind::Net
                ? vpiNetBit
                : vpiRegBit;
        }
        if (constant_of(handle) != nullptr) {
            return vpiConstant;
        }
        if (range_of(handle) != nullptr) {
            return vpiRange;
        }
        if (modpath_of(handle)) {
            return vpiModPath;
        }
        if (const auto* port = port_of(handle)) {
            return port->io_declaration ? vpiIODecl : vpiPort;
        }
        if (port_bit_of(handle)) {
            return vpiPortBit;
        }
        if (reference_of(handle) != nullptr) {
            return vpiRefObj;
        }
        if (owned(handle) && tag_of(handle) == Tag::class_object) {
            return vpiClassObj;
        }
        // A frame or thread that has ended no longer exists (37.3.7).
        if (frame_tag(handle)) {
            return frame_of(handle) != nullptr ? vpiFrame
                : thread_of(handle) != nullptr ? vpiThread
                                               : vpiUndefined;
        }
        if (owned(handle) && tag_of(handle) == Tag::class_member) {
            const auto* member = class_member_of(handle);
            if (member == nullptr) {
                return vpiUndefined;
            }
            return member->constraint
                ? vpiConstraint
                : handle_type(const_cast<LiveService*>(this)
                                  ->class_property_object(*member));
        }
        if (owned(handle) && tag_of(handle) == Tag::class_model
            && class_model_ && class_model_->owns(serial_of(handle))) {
            return class_model_->type(serial_of(handle));
        }
        if (const auto* level = typespec_of(handle)) {
            return typespec_type(*level->node);
        }
        if (typespec_part_of(handle, Tag::typespec_member) != nullptr) {
            return vpiTypespecMember;
        }
        if (typespec_part_of(handle, Tag::enum_const) != nullptr) {
            return vpiEnumConst;
        }
        if (pathterm_of(handle)) {
            return vpiPathTerm;
        }
        if (const auto* primitive = primitive_of(handle)) {
            return primitive_class(primitive->kind, false);
        }
        if (const auto head = primitive_array_of(handle)) {
            return primitive_class(primitives()[*head].kind, true);
        }
        if (prim_term_of(handle)) {
            return vpiPrimTerm;
        }
        if (udp_table_of(handle, Tag::udp_defn) != nullptr) {
            return vpiUdpDefn;
        }
        if (udp_entry_of(handle)) {
            return vpiTableEntry;
        }
        if (udp_table_of(handle, Tag::udp_initial) != nullptr) {
            return vpiInitial;
        }
        if (tchk_of(handle)) {
            return vpiTchk;
        }
        if (tchkterm_of(handle)) {
            return vpiTchkTerm;
        }
        if (net_delay_index(handle, Tag::inter_mod_path)) {
            return vpiInterModPath;
        }
        if (net_delay_index(handle, Tag::delay_device)) {
            return vpiDelayDevice;
        }
        if (net_delay_index(handle, Tag::delay_term)) {
            return vpiDelayTerm;
        }
        if (owned(handle)) {
            switch (tag_of(handle)) {
            case Tag::iterator:
                return vpiIterator;
            case Tag::callback:
                return vpiCallback;
            case Tag::scheduled:
                return vpiSchedEvent;
            case Tag::clocking_skew:
                return vpiDelayControl;
            case Tag::clocking_event:
                return vpiEventControl;
            default:
                return vpiUndefined;
            }
        }
        const auto object = info(handle);
        return object ? object_type(*object) : vpiUndefined;
    }

    [[nodiscard]] runtime::SystemVerilogVpiScheduledWriteHandle
    scheduled_write(const fsim_vpi_handle_v1 handle)
    {
        return { simulation_.systemverilog_vpi_values().owner(),
            serial_of(handle) };
    }

    [[nodiscard]] static bool literal(const std::string_view text)
    {
        return !text.empty()
            && ((text.front() >= '0' && text.front() <= '9')
                || text.front() == '\'' || text.front() == '-');
    }

    // The compilation-unit package is `$unit` (IEEE 1800-2023 3.12.1).
    [[nodiscard]] static std::string package_name(const std::string& name)
    {
        return name.starts_with("fsim_unit_") ? std::string { "$unit" }
                                              : name;
    }

    // A package's full name ends in `::`, and so begins its members' full
    // names (IEEE 1800-2023 37.10, 23.6).
    [[nodiscard]] std::string presented_full_name(
        const runtime::SystemVerilogVpiObjectInfo& object) const
    {
        auto root = std::optional { object };
        for (std::size_t depth = 0U; root && root->parent != 0U
             && depth < 4096U; ++depth) {
            root = info(root->parent);
        }
        if (!root || root->kind != SystemVerilogVpiObjectKind::Package) {
            return object.full_name;
        }
        auto package = root->full_name;
        if (package.ends_with("::")) {
            package.resize(package.size() - 2U);
        }
        const auto presented = package_name(package) + "::";
        if (object.full_name == root->full_name) {
            return presented;
        }
        auto member = std::string_view { object.full_name };
        if (member.starts_with(package)) {
            member.remove_prefix(package.size());
            if (member.starts_with("::")) {
                member.remove_prefix(2U);
            } else if (member.starts_with('.')) {
                member.remove_prefix(1U);
            }
        }
        return presented + std::string { member };
    }

    // Children of a scope that match an iteration type (IEEE 1800-2023
    // 37.10 and following object diagrams).
    [[nodiscard]] bool matches(
        const PLI_INT32 type, const runtime::SystemVerilogVpiObjectInfo& object) const
    {
        const auto actual = object_type(object);
        switch (type) {
        case vpiModule:
            return actual == vpiModule || actual == vpiInterface
                || actual == vpiProgram;
        // An instance is a module, interface, program or package
        // (IEEE 1800-2023 37.10).
        case vpiInstance:
            return actual == vpiModule || actual == vpiInterface
                || actual == vpiProgram || actual == vpiPackage;
        case vpiInternalScope:
            return scope_kind(object.kind);
        // A primitive is a gate, a switch or a UDP instance (IEEE 1800-2023
        // 37.7).
        case vpiPrimitive:
            return actual == vpiGate || actual == vpiSwitch
                || actual == vpiUdp;
        case vpiMemory:
        case vpiRegArray:
            return actual == vpiRegArray;
        case vpiNetArray:
            return actual == vpiNetArray;
        case vpiMemoryWord:
            if (const auto parent = info(object.parent)) {
                return parent->kind == SystemVerilogVpiObjectKind::Memory;
            }
            return false;
        case vpiVariables:
            return actual == vpiRegArray || actual == vpiReg
                || actual == vpiIntegerVar
                || actual == vpiRealVar || actual == vpiTimeVar
                || actual == vpiIntVar || actual == vpiByteVar
                || actual == vpiShortIntVar || actual == vpiLongIntVar
                || actual == vpiBitVar || actual == vpiStringVar
                || actual == vpiStructVar || actual == vpiUnionVar
                || actual == vpiEnumVar || actual == vpiPackedArrayVar
                || actual == vpiShortRealVar || actual == vpiClassVar;
        // The words of an unpacked array iterate as vpiReg whatever their
        // element type (int, bit, real words included).
        case vpiReg:
            if (const auto parent = info(object.parent)) {
                if (parent->kind == SystemVerilogVpiObjectKind::Memory) {
                    return object_type(*parent) == vpiRegArray;
                }
            }
            return actual == vpiReg;
        // Every net data type iterates as a net (IEEE 1800-2023 37.16).
        case vpiNet:
            return actual == vpiNet || actual == vpiStructNet
                || actual == vpiUnionNet || actual == vpiEnumNet
                || actual == vpiPackedArrayNet || actual == vpiIntegerNet
                || actual == vpiIntNet || actual == vpiByteNet
                || actual == vpiShortIntNet || actual == vpiLongIntNet
                || actual == vpiBitNet || actual == vpiRealNet
                || actual == vpiShortRealNet;
        // Ports are their own objects, not the formals' nets and variables
        // (IEEE 1800-2023 37.14).
        case vpiPort:
            return false;
        default:
            return actual == type;
        }
    }

    [[nodiscard]] std::vector<fsim_vpi_handle_v1> drain(
        const runtime::SystemVerilogVpiIteratorResult iterator)
    {
        std::vector<fsim_vpi_handle_v1> objects;
        if (!iterator) {
            return objects;
        }
        while (true) {
            const auto next = registry_.scan(iterator.value);
            if (!next) {
                break;
            }
            objects.push_back(next.value);
        }
        (void)registry_.release_iterator(iterator.value);
        return objects;
    }

    [[nodiscard]] fsim_vpi_handle_v1 make_iterator(
        std::vector<fsim_vpi_handle_v1> objects)
    {
        if (objects.empty()) {
            return 0U;
        }
        const auto serial = ++next_iterator_;
        iterators_.emplace(serial, Iterator { std::move(objects), 0U });
        return tagged(Tag::iterator, serial);
    }

    fsim_vpi_status_v1 iterate(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto type = static_cast<PLI_INT32>(request.argument);
        const auto reference = resolve(request.handle);
        std::vector<fsim_vpi_handle_v1> objects;
        if (const auto serial = node_of(reference)) {
            return succeed(result, make_iterator(
                statement_model_->iterate(*serial, type).value_or(objects)));
        }
        // The members of a class obj (IEEE 1800-2023 37.33).
        if (class_live_tag(reference)) {
            return succeed(
                result, make_iterator(class_object_iterate(reference, type)));
        }
        // The threads of the run, or those a thread forked (37.44).
        if (type == vpiThread
            && (request.handle == 0U || thread_of(reference) != nullptr)) {
            return succeed(result, make_iterator(thread_iterate(reference)));
        }
        // A frame's automatic variables are not modelled: the standard's
        // frame values have no counterpart in the kernel's call stack.
        if (type == vpiAutomatics && frame_of(reference) != nullptr) {
            error_message_ = "vpi_iterate: vpiAutomatics of a frame is not "
                             "supported";
            error_ = true;
            return FSIM_VPI_STATUS_UNSUPPORTED;
        }
        // The members of a class typespec, and of a class defn (IEEE
        // 1800-2023 37.31, 37.32).
        if (owned(reference) && tag_of(reference) == Tag::class_model
            && class_model().owns(serial_of(reference))) {
            return succeed(result,
                make_iterator(class_model()
                                  .iterate(serial_of(reference), type)
                                  .value_or(objects)));
        }
        if (reference != 0U && !owned(reference)) {
            if (const auto object = info(reference)) {
                if (const auto identity = class_identity(*object)) {
                    if (auto own = class_model().defn_iterate(*identity, type)) {
                        return succeed(result, make_iterator(std::move(*own)));
                    }
                }
            }
        }
        // The typedefs an instance declares (IEEE 1800-2023 37.5, 37.12,
        // 37.25); those of generate blocks, packages and $unit are not
        // recorded.
        if (type == vpiTypedef && reference != 0U && !owned(reference)) {
            if (const auto owner = info(reference);
                owner && instance_kind(owner->kind)) {
                for (const auto& specialization :
                    simulation_.design().specializations()) {
                    if (specialization.instance != owner->full_name) {
                        continue;
                    }
                    for (const auto& declared : specialization.vpi_typedefs) {
                        if (declared.typespec) {
                            objects.push_back(make_typespec(
                                TypespecLevel { declared.typespec.get(), 0U }));
                        }
                    }
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        // The drivers and loads of a net or variable, and the statements that
        // use it (IEEE 1800-2023 37.21, 37.46, 37.58).
        if ((type == vpiDriver || type == vpiLoad || type == vpiLocalDriver
                || type == vpiLocalLoad || type == vpiUse)
            && reference != 0U && !owned(reference)) {
            return succeed(
                result, make_iterator(connections_of(type, reference)));
        }
        // The formal arguments of a task or function (IEEE 1800-2023 37.41).
        if (type == vpiIODecl && reference != 0U && !owned(reference)) {
            const auto callable = info(reference);
            if (callable
                && (callable->kind == SystemVerilogVpiObjectKind::Task
                    || callable->kind == SystemVerilogVpiObjectKind::Function)) {
                if (const auto scope = hir_scope(reference)) {
                    objects = statement_model()
                                  .scope_iterate_tf(*scope, type)
                                  .value_or(objects);
                }
                return succeed(result, make_iterator(std::move(objects)));
            }
        }
        // The processes and continuous assignments of an instance or
        // generate scope, the statements of a named block (IEEE 1800-2023
        // 37.10, 37.43, 37.44).
        if ((type == vpiProcess || type == vpiContAssign || type == vpiStmt)
            && reference != 0U && !owned(reference)) {
            if (const auto scope = hir_scope(reference)) {
                objects = statement_model()
                              .iterate_scope(*scope, type)
                              .value_or(objects);
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        if (const auto* level = typespec_of(reference)) {
            const auto copy = *level;
            return succeed(result,
                make_iterator(typespec_iteration(copy, reference, type)));
        }
        if (const auto* site = call_of(reference)) {
            if (type == vpiArgument) {
                const auto index = static_cast<std::uint64_t>(
                    site - calls_.data());
                for (std::size_t position = 0U;
                     position < site->arguments.size(); ++position) {
                    objects.push_back(tagged(
                        Tag::argument, (index << 16U) | position));
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        // A subarray holds the words under its indexes, and its own
        // dimensions (IEEE 1800-2023 37.17).
        if (const auto* subarray = subarray_of(reference)) {
            const auto copy = *subarray;
            if (type == vpiRange) {
                for (const auto& [left, right] : subarray_dimensions(copy)) {
                    objects.push_back(make_range(reference, left, right));
                }
            } else if (subarray_dimensions(copy).size() == 1U) {
                for (const auto handle :
                    drain(registry_.iterate_children(copy.memory))) {
                    const auto object = info(handle);
                    if (object && object->full_name.starts_with(copy.full_name + "[")
                        && matches(type, *object)) {
                        objects.push_back(handle);
                    }
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        // The elements of a generate scope array or an instance array
        // (IEEE 1800-2023 37.11, 37.85).
        if (const auto array = reference != 0U && !owned(reference)
                ? info(reference)
                : std::nullopt;
            array && array_kind(array->kind)) {
            if (type == vpiRange) {
                for (const auto& [left, right] : instance_array_ranges(*array)) {
                    objects.push_back(make_range(reference, left, right));
                }
                return succeed(result, make_iterator(std::move(objects)));
            }
            for (const auto element : array_elements(*array)) {
                if (const auto object = info(element);
                    object && matches(type, *object)) {
                    objects.push_back(element);
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        // The bits of a logic or bit vector, left to right (IEEE 1800-2023
        // 37.16 detail 3, 37.17).
        if (type == vpiBit && reference != 0U && !owned(reference)) {
            const auto object = info(reference);
            const auto range = declared_range(reference);
            if (object && range && object->type && !object->type->declared
                && (object->kind == SystemVerilogVpiObjectKind::Net
                    || object->kind == SystemVerilogVpiObjectKind::Variable
                    || object->kind == SystemVerilogVpiObjectKind::Port)
                && (object->type->category
                        == runtime::SystemVerilogVpiValueCategory::Logic4
                    || object->type->category
                        == runtime::SystemVerilogVpiValueCategory::Bit2)) {
                const auto [left, right] = *range;
                for (auto index = left;; index += left <= right ? 1 : -1) {
                    objects.push_back(make_select(reference, index, index, true));
                    if (index == right) {
                        break;
                    }
                }
                std::erase(objects, fsim_vpi_handle_v1 { 0U });
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        // An instance's ports, and the ports a net or variable connects
        // (IEEE 1800-2023 37.14, 37.16). A port's bits are not published.
        if (type == vpiPort || type == vpiPorts || type == vpiPortInst
            || type == vpiIODecl) {
            if (reference != 0U && !owned(reference)) {
                objects = ports_of(type, reference);
            } else if (auto path_ports = net_delay_iteration(type, reference)) {
                // An intermodule path's two ports (37.37).
                objects = std::move(*path_ports);
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        // The bits of a vector port, left to right (IEEE 1800-2023 37.14).
        if (const auto* port = port_of(reference); port != nullptr
            && type == vpiBit && !port->io_declaration
            && !port->interface_port) {
            const auto width = port_width(*port);
            const auto index = static_cast<std::uint64_t>(
                port - simulation_.systemverilog_vpi_ports().data());
            for (auto position = width; width > 1 && position > 0; --position) {
                objects.push_back(tagged(Tag::port_bit,
                    (index << 24U)
                        | static_cast<std::uint64_t>(position - 1)));
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        if (port_of(reference) != nullptr || reference_of(reference) != nullptr
            || port_bit_of(reference)) {
            return succeed(result, make_iterator(std::move(objects)));
        }
        // The members of a structure or union, the elements of a packed
        // array of them (IEEE 1800-2023 37.18, 37.26).
        if (type == vpiMember || type == vpiElement) {
            const auto declared = reference != 0U ? declared_of(reference) : nullptr;
            if (declared && type == vpiMember) {
                objects = members_of(reference);
            } else if (declared
                && (declared->kind == runtime::SystemVerilogVpiDescriptorKind::PackedArray
                    || declared->kind
                        == runtime::SystemVerilogVpiDescriptorKind::UnpackedArray)
                && !declared->ranges.empty()) {
                const auto [left, right] = declared->ranges.front();
                for (auto index = left;; index += left <= right ? 1 : -1) {
                    objects.push_back(packed_element(reference, index));
                    if (index == right) {
                        break;
                    }
                }
            }
            std::erase(objects, fsim_vpi_handle_v1 { 0U });
            return succeed(result, make_iterator(std::move(objects)));
        }
        // The dimensions of an array or vector (IEEE 1800-2023 37.17).
        if (type == vpiRange) {
            if (reference != 0U && !owned(reference) && reports_range(reference)) {
                for (const auto& [left, right] : dimension_ranges(reference)) {
                    objects.push_back(make_range(reference, left, right));
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        // The primitives and instance arrays of a scope, their prim terms,
        // and the table entries of a UDP definition (IEEE 1800-2023 37.35,
        // 37.36).
        if (auto delay_objects = net_delay_iteration(type, reference)) {
            return succeed(result, make_iterator(std::move(*delay_objects)));
        }
        if (auto primitive_objects = primitive_iteration(type, reference)) {
            return succeed(result, make_iterator(std::move(*primitive_objects)));
        }
        // A module's paths, and a path's input and output terminals.
        if (type == vpiModPath && reference != 0U && !owned(reference)) {
            const auto module = info(reference);
            if (module && module->kind == SystemVerilogVpiObjectKind::Module) {
                const auto& paths = simulation_.design().verilog_specify_paths();
                for (std::size_t index = 0U; index < paths.size(); ++index) {
                    if (paths[index].instance == module->full_name) {
                        objects.push_back(tagged(Tag::modpath, index));
                    }
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        if (const auto path = modpath_of(reference)) {
            if (type == vpiModPathIn || type == vpiModPathOut
                || type == vpiModDataPathIn) {
                const auto& info_path
                    = simulation_.design().verilog_specify_paths()[*path];
                const bool output = type == vpiModPathOut;
                const bool data = type == vpiModDataPathIn;
                const auto size = data ? path_data_signals(*path).size()
                    : output           ? info_path.destinations.size()
                                       : info_path.sources.size();
                for (std::size_t index = 0U; index < size; ++index) {
                    objects.push_back(pathterm_handle({ *path, output,
                        static_cast<std::uint32_t>(index), data }));
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        // A module's timing checks, and a check's terminals: the reference
        // and data events (IEEE 1800-2023 37.40 detail 2). The limits and
        // the notifier are not objects here.
        if (type == vpiTchk && reference != 0U && !owned(reference)) {
            const auto module = info(reference);
            if (module && module->kind == SystemVerilogVpiObjectKind::Module) {
                const auto& checks = simulation_.design().verilog_timing_checks();
                for (std::size_t index = 0U; index < checks.size(); ++index) {
                    if (checks[index].instance == module->full_name) {
                        objects.push_back(tagged(Tag::tchk, index));
                    }
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        if (const auto check = tchk_of(reference)) {
            if (type == vpiExpr) {
                objects.push_back(tchkterm_handle(*check, false));
                if (simulation_.design().verilog_timing_checks()[*check].data) {
                    objects.push_back(tchkterm_handle(*check, true));
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        // The concurrent assertions of the design or of an instance (IEEE
        // 1800-2023 39.3.1).
        if (type == vpiAssertion) {
            return succeed(result, make_iterator(assertions_in(reference)));
        }
        // Only design roots iterate from a NULL reference (IEEE 1800-2023
        // 37.10).
        if (reference == 0U && type != vpiModule && type != vpiInstance
            && type != vpiPackage && type != vpiInterface && type != vpiProgram) {
            return succeed(result);
        }
        if (reference == 0U) {
            // Packages, `$unit` first, precede the design's top instances.
            for (const auto kind : { SystemVerilogVpiObjectKind::Package,
                     SystemVerilogVpiObjectKind::Root,
                     SystemVerilogVpiObjectKind::Module,
                     SystemVerilogVpiObjectKind::Interface,
                     SystemVerilogVpiObjectKind::Program }) {
                for (const auto handle : drain(registry_.iterate_objects(kind))) {
                    const auto object = info(handle);
                    if (object && object->parent == 0U && matches(type, *object)) {
                        objects.push_back(handle);
                    }
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
        }
        const auto parent = info(reference);
        if (!parent) {
            return succeed(result);
        }
        for (const auto handle : drain(registry_.iterate_children(reference))) {
            const auto object = info(handle);
            // Names starting `$` or `@` are the elaborator's own storage,
            // not design objects.
            if (!object
                || ((object->name.starts_with('$')
                        && object->kind != SystemVerilogVpiObjectKind::ClockingBlock)
                    || object->name.starts_with('@'))) {
                continue;
            }
            if (matches(type, *object)) {
                objects.push_back(handle);
            }
        }
        // The words of an array scan from the lowest address up, as other
        // simulators order them.
        if (parent->kind == SystemVerilogVpiObjectKind::Memory) {
            const auto range = declared_range(reference);
            if (range && range->first > range->second) {
                std::ranges::reverse(objects);
            }
        }
        return succeed(result, make_iterator(std::move(objects)));
    }

    fsim_vpi_status_v1 scan(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        if (!owned(request.handle) || tag_of(request.handle) != Tag::iterator) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto found = iterators_.find(serial_of(request.handle));
        if (found == iterators_.end()) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        auto& iterator = found->second;
        if (iterator.cursor >= iterator.objects.size()) {
            // The iterator is freed when the scan reaches its end
            // (IEEE 1800-2023 38.32).
            iterators_.erase(found);
            return succeed(result);
        }
        return succeed(result, iterator.objects[iterator.cursor++]);
    }

    [[nodiscard]] fsim_vpi_handle_v1 enclosing_module(fsim_vpi_handle_v1 handle) const
    {
        while (const auto object = info(handle)) {
            if (object->kind == SystemVerilogVpiObjectKind::Root
                || object->kind == SystemVerilogVpiObjectKind::Module
                || object->kind == SystemVerilogVpiObjectKind::Interface
                || object->kind == SystemVerilogVpiObjectKind::Program) {
                return handle;
            }
            handle = object->parent;
        }
        return 0U;
    }

    [[nodiscard]] std::optional<std::pair<int, int>> module_time_scale(
        const fsim_vpi_handle_v1 handle) const
    {
        if (handle == 0U) {
            return std::nullopt;
        }
        auto scope = handle;
        if (const auto* site = call_of(handle)) {
            scope = find_call_scope(site->scope);
        } else {
            const auto reference = resolve(handle);
            const auto* select = select_of(reference);
            scope = select ? select->parent : reference;
        }
        const auto module = info(enclosing_module(scope));
        if (!module) {
            return std::nullopt;
        }
        return simulation_.systemverilog_instance_time_scale(module->full_name);
    }

    fsim_vpi_status_v1 handle(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto type = static_cast<PLI_INT32>(request.argument);
        if (type == vpiSysTfCall && request.handle == 0U) {
            return succeed(result,
                call_stack_.empty() ? 0U
                                    : tagged(Tag::call, call_stack_.back()));
        }
        if (const auto* site = call_of(request.handle)) {
            if (type == vpiScope || type == vpiModule) {
                const auto handle = find_call_scope(site->scope);
                return succeed(result,
                    type == vpiModule ? enclosing_module(handle) : handle);
            }
            if (type == vpiUserSystf) {
                return succeed(result, tagged(Tag::systf, site->systf));
            }
            return succeed(result);
        }
        const auto reference = resolve(request.handle);
        if (const auto serial = node_of(reference)) {
            if (type == vpiTypespec || type == vpiLeftRange
                || type == vpiRightRange) {
                if (const auto declared = statement_model_->declared_type(*serial)) {
                    return succeed(result, declared_type_relation(*declared, type));
                }
            }
            return succeed(result,
                statement_model_->relation(*serial, type).value_or(0U));
        }
        if (class_live_tag(reference)) {
            return class_object_relation(request, reference, type, result);
        }
        // vpi_handle(vpiFrame, NULL) is the active frame of the running
        // thread (IEEE 1800-2023 37.43 detail 4); a frame and a thread
        // relate as 37.43 and 37.44 show.
        if (type == vpiFrame && request.handle == 0U) {
            return succeed(result, current_frame_handle());
        }
        if (const auto* const frame = frame_of(reference)) {
            return succeed(result, frame_relation(*frame, type));
        }
        if (const auto* const thread = thread_of(reference)) {
            return succeed(result, thread_relation(*thread, type));
        }
        if (owned(reference) && tag_of(reference) == Tag::class_model
            && class_model().owns(serial_of(reference))) {
            return succeed(
                result, class_model().relation(serial_of(reference), type));
        }
        // A function's return type range (IEEE 1800-2023 37.41).
        if ((type == vpiLeftRange || type == vpiRightRange) && reference != 0U
            && !owned(reference)) {
            if (const auto scope = hir_scope(reference)) {
                if (const auto returned
                    = statement_model().scope_return_type(*scope);
                    returned && !returned->ranges.empty()) {
                    return succeed(result,
                        declared_type_relation(*returned, type));
                }
            }
        }
        // The statement and the return variable of a task or function (IEEE
        // 1800-2023 37.41, 37.42).
        if ((type == vpiStmt || type == vpiReturn) && reference != 0U
            && !owned(reference)) {
            if (const auto scope = hir_scope(reference)) {
                return succeed(result,
                    statement_model().scope_relation(*scope, type).value_or(0U));
            }
            return succeed(result);
        }
        // Typespecs, their members and enumeration constants (IEEE
        // 1800-2023 37.25).
        if (const auto* level = typespec_of(reference)) {
            return typespec_relation(*level, reference, type, result);
        }
        if (const auto* member
            = typespec_part_of(reference, Tag::typespec_member)) {
            const auto part = *member;
            return succeed(result, type == vpiTypespec
                    ? make_typespec({ &part.owner->members[part.index], 0U })
                    : type == vpiParent ? part.parent
                                        : 0U);
        }
        if (const auto* constant
            = typespec_part_of(reference, Tag::enum_const)) {
            return succeed(result, type == vpiParent ? constant->parent : 0U);
        }
        // A class variable's class typespec is the one it was declared with
        // (IEEE 1800-2023 37.33 detail 5).
        if ((type == vpiTypespec
                || (type == vpiClassTypespec && class_variable_handle(reference)))
            && (!owned(reference) || select_of(reference) != nullptr)) {
            const auto level = object_typespec(reference);
            return succeed(result, level ? make_typespec(*level) : 0U);
        }
        if (const auto* range = range_of(reference)) {
            const auto bounds = *range;
            switch (type) {
            case vpiParent:
                return succeed(result, bounds.parent);
            case vpiLeftRange:
                return succeed(result,
                    bounds.empty ? 0U : make_constant(bounds.left));
            case vpiRightRange:
                return succeed(result,
                    bounds.empty ? 0U : make_constant(bounds.right));
            default:
                return succeed(result);
            }
        }
        if (const auto* skew = clocking_skew_handle(reference)) {
            const auto copy = *skew;
            return succeed(result,
                type == vpiParent ? copy.owner
                : type == vpiDelay ? clocking_skew_delay(copy)
                                   : 0U);
        }
        if (const auto block = clocking_event_block(reference)) {
            if (type == vpiParent) {
                return succeed(result, block);
            }
            const auto view = clocking_view(block);
            // The event of a single sensitivity without an edge is its
            // signal; an edge operator is not published.
            if (type == vpiCondition && view
                && view->block->event.size() == 1U
                && view->block->event.front().edge
                    == semantic::sv::EdgeKind::any) {
                return succeed(result,
                    name_from_scope(view->scope,
                        view->block->event.front().signal));
            }
            return succeed(result);
        }
        if (const auto* port = port_of(reference)) {
            if (port->io_declaration) {
                switch (type) {
                case vpiParent:
                case vpiModport:
                    return succeed(result, port->instance);
                case vpiExpr:
                    return succeed(result, port->low_conn);
                default:
                    return succeed(result);
                }
            }
            switch (type) {
            case vpiParent:
            case vpiModule:
                return succeed(result, port->instance);
            case vpiLowConn:
                return succeed(result, port->interface_port
                        ? tagged(Tag::reference, serial_of(reference))
                        : port->low_conn);
            case vpiHighConn:
                if (port->has_high_expression) {
                    return succeed(result,
                        statement_model().expression_in_scope(
                            semantic::ExpressionId::from_index(
                                port->high_expression),
                            port->high_scope, reference));
                }
                return succeed(result, port->high_conn);
            default:
                return succeed(result);
            }
        }
        if (const auto bit = port_bit_of(reference)) {
            return succeed(result,
                type == vpiParent
                    ? tagged(Tag::port, static_cast<std::uint64_t>(bit->port))
                    : 0U);
        }
        if (const auto* port = reference_of(reference)) {
            switch (type) {
            case vpiParent:
                return succeed(result, port->instance);
            case vpiActual:
                return succeed(result, port->actual);
            default:
                return succeed(result);
            }
        }
        if (net_delay_handle(reference)) {
            return succeed(result, net_delay_relation(reference, type));
        }
        if (primitive_of(reference) != nullptr || primitive_array_of(reference)
            || prim_term_of(reference)
            || udp_table_of(reference, Tag::udp_defn) != nullptr
            || udp_entry_of(reference)
            || udp_table_of(reference, Tag::udp_initial) != nullptr) {
            return succeed(result, primitive_model_relation(reference, type));
        }
        if (const auto term = pathterm_of(reference)) {
            return succeed(result,
                type == vpiExpr ? pathterm_object(*term)
                : type == vpiModPath || type == vpiParent
                    ? tagged(Tag::modpath, term->path)
                    : 0U);
        }
        // A path's condition is a scheduler program, not an expression
        // object, so vpiCondition is NULL (IEEE 1800-2023 37.39).
        if (const auto path = modpath_of(reference)) {
            if (type == vpiModule) {
                return succeed(result, enclosing_module_of(path_instance(*path)));
            }
            return succeed(result,
                type == vpiInstance || type == vpiScope || type == vpiParent
                    ? path_instance(*path)
                    : 0U);
        }
        if (const auto check = tchk_of(reference)) {
            const auto& declared
                = simulation_.design().verilog_timing_checks()[*check];
            switch (type) {
            case vpiTchkRefTerm:
                return succeed(result, tchkterm_handle(*check, false));
            case vpiTchkDataTerm:
                return succeed(result,
                    declared.data ? tchkterm_handle(*check, true) : 0U);
            case vpiTchkNotifier:
                return succeed(result,
                    declared.notifier
                        ? signal_object(*declared.notifier, tchk_instance(*check))
                        : 0U);
            case vpiModule:
                return succeed(result, enclosing_module_of(tchk_instance(*check)));
            case vpiInstance:
            case vpiScope:
            case vpiParent:
                return succeed(result, tchk_instance(*check));
            default:
                return succeed(result);
            }
        }
        // A terminal's condition, like a path's, has no expression object.
        if (const auto term = tchkterm_of(reference)) {
            const auto& event = tchkterm_event(*term);
            return succeed(result,
                type == vpiExpr
                    ? signal_object(event.terminal.signal, tchk_instance(term->first))
                : type == vpiTchk || type == vpiParent
                    ? tagged(Tag::tchk, term->first)
                    : 0U);
        }
        if (const auto object = reference != 0U && !owned(reference)
                ? info(reference)
                : std::nullopt;
            object && object->kind == SystemVerilogVpiObjectKind::Modport
            && (type == vpiInterface || type == vpiParent)) {
            return succeed(result, object->parent);
        }
        if (const auto* subarray = subarray_of(reference)) {
            const auto copy = *subarray;
            const auto dimensions = subarray_dimensions(copy);
            switch (type) {
            case vpiParent:
                return succeed(result, copy.memory);
            case vpiLeftRange:
            case vpiRightRange:
                return succeed(result, dimensions.empty() ? 0U
                        : make_constant(type == vpiLeftRange
                                  ? dimensions.front().first
                                  : dimensions.front().second));
            case vpiScope:
            case vpiModule: {
                const auto memory = info(copy.memory);
                return succeed(result, !memory ? 0U
                        : type == vpiModule ? enclosing_module(memory->parent)
                                            : memory->parent);
            }
            default:
                return succeed(result);
            }
        }
        if (const auto* select = select_of(reference)) {
            switch (type) {
            case vpiParent:
                return succeed(result,
                    select->owner != 0U ? select->owner : select->parent);
            case vpiIndex:
                return succeed(result,
                    select->bit ? make_constant(select->left) : 0U);
            case vpiLeftRange:
            case vpiRightRange:
                if (const auto unpacked = declared_unpacked(reference)) {
                    return succeed(result, make_constant(type == vpiLeftRange
                            ? unpacked->ranges.front().left
                            : unpacked->ranges.front().right));
                }
                return succeed(result, select->bit ? 0U
                        : make_constant(type == vpiLeftRange ? select->left
                                                             : select->right));
            case vpiScope:
            case vpiModule: {
                const auto parent = info(select->parent);
                return succeed(result, !parent ? 0U
                        : type == vpiModule ? enclosing_module(parent->parent)
                                            : parent->parent);
            }
            default:
                return succeed(result);
            }
        }
        const auto object = info(reference);
        if (!object) {
            return succeed(result);
        }
        // An assertion's scope is its process's (IEEE 1800-2023 39.3.1).
        if (concurrent_assertion(*object) != nullptr
            && (type == vpiScope || type == vpiParent || type == vpiModule)) {
            const auto process = info(object->parent);
            const auto scope = process ? process->parent : 0U;
            return succeed(result,
                type == vpiModule ? enclosing_module(scope) : scope);
        }
        // The clocking relations of 37.48 and of an instance (14.12, 14.14).
        if (object->kind == SystemVerilogVpiObjectKind::ClockingBlock
            || object->kind == SystemVerilogVpiObjectKind::ClockingIo) {
            if (type == vpiInputSkew || type == vpiOutputSkew
                || type == vpiClockingEvent || type == vpiExpr) {
                return succeed(result, clocking_relation(reference, type));
            }
            if (type == vpiInstance
                && object->kind == SystemVerilogVpiObjectKind::ClockingBlock) {
                return succeed(result, object->parent);
            }
        }
        if (object->kind == SystemVerilogVpiObjectKind::TypeSpecification
            && (type == vpiWith || type == vpiNetTypedefAlias)) {
            return succeed(result, nettype_relation(reference, type));
        }
        if (instance_kind(object->kind)
            && (type == vpiDefaultClocking || type == vpiGlobalClocking)) {
            return succeed(result,
                clocking_declared(*object, type == vpiGlobalClocking));
        }
        if (type == vpiExtends) {
            if (const auto identity = class_identity(*object)) {
                return succeed(
                    result, class_model().defn_relation(*identity, type));
            }
        }
        // The object a class variable references; NULL when it refers to
        // none (IEEE 1800-2023 37.33 detail 5).
        if (type == vpiClassObj && class_variable(*object)) {
            return succeed(
                result, class_object_handle(class_variable_object(reference)));
        }
        switch (type) {
        case vpiScope:
        case vpiParent:
            return succeed(result, object->parent);
        case vpiModule:
            return succeed(result, enclosing_module(object->parent));
        // Range bounds and array indexes are constants (IEEE 1800-2023
        // 37.17 to 37.19).
        case vpiLeftRange:
        case vpiRightRange: {
            if (array_kind(object->kind)) {
                const auto ranges = instance_array_ranges(*object);
                if (ranges.empty()) {
                    return succeed(result);
                }
                return succeed(result, make_constant(type == vpiLeftRange
                        ? ranges.front().first
                        : ranges.front().second));
            }
            const auto range = reports_range(reference)
                ? declared_range(reference)
                : std::nullopt;
            if (!range) {
                return succeed(result);
            }
            return succeed(result, make_constant(
                type == vpiLeftRange ? range->first : range->second));
        }
        case vpiIndex: {
            const auto index = word_index(*object);
            return succeed(result, index ? make_constant(*index) : 0U);
        }
        default:
            return succeed(result);
        }
    }

    // A hierarchical name as the registry spells it: an escaped identifier
    // is followed by white space before its separating period, and a
    // simple identifier is the same name with or without the escape
    // (IEEE 1800-2023 5.6.1, 23.6).
    [[nodiscard]] static std::string hierarchical_name(const std::string_view name)
    {
        if (name.find('\\') == std::string_view::npos) {
            return std::string { name };
        }
        std::string out;
        std::size_t index { };
        while (index < name.size()) {
            if (name[index] != '\\') {
                const auto end = std::min(name.find('.', index), name.size());
                out.append(name.substr(index, end - index));
                index = end;
                if (index < name.size()) {
                    out.push_back('.');
                    ++index;
                }
                continue;
            }
            auto end = index + 1U;
            while (end < name.size()
                && std::isspace(static_cast<unsigned char>(name[end])) == 0) {
                ++end;
            }
            const auto body = name.substr(index + 1U, end - index - 1U);
            const bool simple = !body.empty()
                && (std::isalpha(static_cast<unsigned char>(body.front())) != 0
                    || body.front() == '_')
                && std::ranges::all_of(body, [](const char character) {
                       return std::isalnum(static_cast<unsigned char>(character)) != 0
                           || character == '_' || character == '$';
                   });
            if (simple) {
                out.append(body);
            } else {
                out.push_back('\\');
                out.append(body);
                out.push_back(' ');
            }
            index = end;
            while (index < name.size()
                && std::isspace(static_cast<unsigned char>(name[index])) != 0) {
                ++index;
            }
            if (index < name.size() && name[index] == '.') {
                out.push_back('.');
                ++index;
            }
        }
        return out;
    }

    // The object a hierarchy path names, where the path joins escaped
    // identifiers (which may hold periods) with plain periods and so splits
    // only against the registry's own names.
    [[nodiscard]] fsim_vpi_handle_v1 find_hierarchy_path(
        const std::string& path) const
    {
        if (const auto direct = registry_.find(path); direct && direct.value) {
            return direct.value->handle;
        }
        if (path.find('\\') == std::string::npos) {
            return 0U;
        }
        return find_escaped_path(path, std::string { });
    }

    // The scope of a call: its block when that block is published, else
    // the nearest enclosing scope that is (IEEE 1800-2023 37.4).
    [[nodiscard]] fsim_vpi_handle_v1 find_call_scope(
        const std::string& path) const
    {
        auto prefix = path;
        while (true) {
            if (const auto found = find_hierarchy_path(prefix)) {
                return found;
            }
            const auto separator = prefix.rfind('.');
            if (separator == std::string::npos) {
                return 0U;
            }
            prefix.resize(separator);
        }
    }

    [[nodiscard]] fsim_vpi_handle_v1 find_escaped_path(
        const std::string_view rest, const std::string& parent) const
    {
        for (std::size_t from = 0U;;) {
            const auto period = rest.find('.', from);
            const auto end = period == std::string_view::npos ? rest.size() : period;
            const auto component = rest.substr(0U, end);
            const bool escaped = component.starts_with('\\');
            auto full = parent.empty() ? std::string { } : parent + ".";
            full.append(component);
            if (escaped) {
                full.push_back(' ');
            }
            if (const auto found = registry_.find(full); found && found.value) {
                if (end == rest.size()) {
                    return found.value->handle;
                }
                if (const auto deeper
                    = find_escaped_path(rest.substr(end + 1U), full)) {
                    return deeper;
                }
            }
            if (!escaped || period == std::string_view::npos) {
                return 0U;
            }
            from = period + 1U;
        }
    }

    // A name without the backslash that spells an escaped identifier
    // (IEEE 1800-2023 5.6.1).
    [[nodiscard]] static std::string_view unescaped(const std::string_view name)
    {
        return name.starts_with('\\') ? name.substr(1U) : name;
    }

    // A labelled concurrent assertion is found by its name, which is its
    // process's; an unlabelled one is not found (IEEE 1800-2023 39.3.1).
    [[nodiscard]] fsim_vpi_handle_v1 labelled_assertion(
        const fsim_vpi_handle_v1 handle)
    {
        const auto object = info(handle);
        if (!object || object->kind != SystemVerilogVpiObjectKind::Process) {
            return handle;
        }
        const auto child = registry_.find_child(handle, "$assertion");
        if (!child || !child.value) {
            return handle;
        }
        const auto* assertion = concurrent_assertion(child.value->handle);
        return assertion != nullptr && assertion->explicit_label
            ? child.value->handle
            : handle;
    }

    // The object a full name names; 0 when none does.
    [[nodiscard]] fsim_vpi_handle_v1 find_full_name(const std::string& name)
    {
        const auto found = registry_.find(name);
        if (found && found.value) {
            return labelled_assertion(found.value->handle);
        }
        // A named gate, switch or UDP instance (IEEE 1800-2023 37.35).
        if (const auto primitive = primitive_index().by_name.find(name);
            primitive != primitive_index().by_name.end()) {
            return tagged(Tag::primitive, primitive->second);
        }
        // `$unit` is the compilation-unit package (IEEE 1800-2023 3.12.1,
        // 37.10), published under the unit's own name; the first unit that
        // has the member names it.
        if (name == "$unit" || name.starts_with("$unit::")) {
            const auto member = name.size() > 7U ? name.substr(7U) : std::string { };
            for (const auto kind : { SystemVerilogVpiObjectKind::Root,
                     SystemVerilogVpiObjectKind::Package }) {
                for (const auto handle : drain(registry_.iterate_objects(kind))) {
                    const auto unit = info(handle);
                    if (!unit || unit->parent != 0U) {
                        continue;
                    }
                    auto package = unit->full_name;
                    if (package.ends_with("::")) {
                        package.resize(package.size() - 2U);
                    }
                    if (package_name(package) != "$unit") {
                        continue;
                    }
                    if (member.empty()) {
                        return handle;
                    }
                    if (const auto inside = find_full_name(package + "::" + member)) {
                        return inside;
                    }
                }
            }
        }
        if (const auto member = find_declared_path(name)) {
            return member;
        }
        return find_reference_path(name);
    }

    // `name` with each component that names a VHDL instance, or an object
    // a VHDL architecture declares, spelled as the registry spells it. A
    // VHDL basic identifier is the same in either case (IEEE 1076-2019
    // 15.4.2); an extended identifier and every name inside a
    // SystemVerilog scope keep their case (IEEE 1800-2023 5.6). Empty when
    // no component differs.
    [[nodiscard]] std::string vhdl_case_name(const std::string_view name)
    {
        const auto folded = [](const std::string_view text) {
            std::string out { text };
            std::ranges::transform(out, out.begin(), [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return out;
        };
        std::vector<std::string_view> components;
        std::size_t start { };
        int depth { };
        bool escaped { };
        for (std::size_t index = 0U; index <= name.size(); ++index) {
            const char character = index < name.size() ? name[index] : '.';
            if (escaped) {
                escaped = std::isspace(static_cast<unsigned char>(character)) == 0
                    && index < name.size();
                if (escaped) {
                    continue;
                }
            }
            if (character == '\\') {
                escaped = true;
            } else if (character == '[') {
                ++depth;
            } else if (character == ']') {
                --depth;
            } else if (character == '.' && depth == 0) {
                components.push_back(name.substr(start, index - start));
                start = index + 1U;
            }
        }
        // A root, or a child of a VHDL region, may be a VHDL name; a child
        // of a SystemVerilog scope is found by its exact name.
        const auto vhdl_region = [](const std::optional<runtime::SystemVerilogVpiObjectInfo>& object) {
            return object && object->type && object->type->foreign_instance;
        };
        std::string out;
        fsim_vpi_handle_v1 scope { };
        bool changed { };
        for (std::size_t index = 0U; index < components.size(); ++index) {
            const auto component = components[index];
            const auto prefix = out.empty() ? std::string { } : out + ".";
            fsim_vpi_handle_v1 next { };
            if (const auto found = registry_.find(prefix + std::string { component });
                found && found.value) {
                next = found.value->handle;
                out = prefix + std::string { component };
            } else if (!component.starts_with('\\')
                && (scope == 0U || vhdl_region(info(scope)))) {
                std::vector<fsim_vpi_handle_v1> candidates;
                if (scope == 0U) {
                    for (const auto kind : { SystemVerilogVpiObjectKind::Root,
                             SystemVerilogVpiObjectKind::Module }) {
                        for (const auto handle : drain(registry_.iterate_objects(kind))) {
                            const auto object = info(handle);
                            if (object && object->parent == 0U && vhdl_region(object)) {
                                candidates.push_back(handle);
                            }
                        }
                    }
                } else {
                    candidates = drain(registry_.iterate_children(scope));
                }
                const auto wanted = folded(component);
                for (const auto handle : candidates) {
                    const auto object = info(handle);
                    if (object && folded(object->name) == wanted) {
                        next = handle;
                        out = object->full_name;
                        changed = true;
                        break;
                    }
                }
            }
            if (next == 0U) {
                // The rest names nothing the registry holds.
                for (auto rest = index; rest < components.size(); ++rest) {
                    out += (out.empty() ? "" : ".") + std::string { components[rest] };
                }
                break;
            }
            scope = next;
        }
        return changed ? out : std::string { };
    }

    fsim_vpi_status_v1 handle_by_name(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        if (request.text == nullptr) {
            return succeed(result);
        }
        const std::string name
            = hierarchical_name({ request.text, request.text_size });
        const auto found_or_folded = [this](const std::string& full) {
            if (const auto handle = find_full_name(full)) {
                return handle;
            }
            const auto spelled = vhdl_case_name(full);
            return spelled.empty() ? fsim_vpi_handle_v1 { } : find_full_name(spelled);
        };
        if (request.handle == 0U) {
            return succeed(result, found_or_folded(name));
        }
        const auto scope = info(resolve(request.handle));
        if (!scope) {
            return succeed(result);
        }
        const auto scoped = scope->full_name
            + (scope->full_name.ends_with("::") ? "" : ".") + name;
        if (const auto handle = found_or_folded(scoped)) {
            return succeed(result, handle);
        }
        const auto found = registry_.find(name);
        return succeed(result, found && found.value
                ? labelled_assertion(found.value->handle)
                                                    : find_declared_path(name));
    }

    fsim_vpi_status_v1 handle_by_index(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        return succeed(result,
            index_handle(resolve(request.handle),
                static_cast<PLI_INT32>(request.argument)));
    }

    // The object `reference[index]` names (IEEE 1800-2023 38.24); 0 when
    // none does.
    [[nodiscard]] fsim_vpi_handle_v1 index_handle(
        const fsim_vpi_handle_v1 reference, const PLI_INT32 index)
    {
        if (const auto* subarray = subarray_of(reference)) {
            const auto copy = *subarray;
            return index_array(copy.memory, copy.depth, copy.name,
                copy.full_name, index);
        }
        if (const auto* select = select_of(reference)) {
            if (select->declared) {
                return declared_index(reference, index);
            }
            if (!select->element) {
                return 0U;
            }
            const auto copy = *select;
            return make_element(copy.parent, copy.offset, copy.dimensions,
                copy.suffix, index);
        }
        const auto object = info(reference);
        if (!object) {
            return 0U;
        }
        const auto found = registry_.find(
            object->full_name + "[" + std::to_string(index) + "]");
        if (found && found.value) {
            return found.value->handle;
        }
        if (object->type && object->type->declared
            && (object->type->declared->kind
                    == runtime::SystemVerilogVpiDescriptorKind::PackedArray
                || object->type->declared->kind
                    == runtime::SystemVerilogVpiDescriptorKind::UnpackedArray)) {
            return packed_element(reference, index);
        }
        if (object->kind == SystemVerilogVpiObjectKind::Memory) {
            return index_array(reference, 0U, object->name,
                object->full_name, index);
        }
        // An index of a vector with several packed dimensions selects an
        // element of the leftmost one (IEEE 1800-2023 7.4.5).
        if (const auto dimensions = dimension_ranges(reference);
            dimensions.size() > 1U) {
            return make_element(reference, 0U, dimensions, "", index);
        }
        // A vector's index names a bit-select (IEEE 1800-2023 37.18).
        return make_select(reference, index, index, true);
    }

    // The integer an expression handle holds now: a constant, a parameter
    // or the current value of a variable; none when it has an x or z bit.
    [[nodiscard]] std::optional<std::int64_t> handle_integer(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto value = object_value(handle);
        if (!value || value->kind != Value::Kind::bits) {
            return std::nullopt;
        }
        for (std::size_t bit = 0U; bit < value->bits.width(); ++bit) {
            const auto level = value->bits.get(bit);
            if (level != Logic4::zero && level != Logic4::one) {
                return std::nullopt;
            }
        }
        return signed_bits(value->bits, value->is_signed);
    }

    // The registry object a select expression of the statement model
    // stands for, once its base and indexes are known (IEEE 1800-2023
    // 37.18, 37.19, 37.59); 0 when they are not. The select's name and
    // value are those of that object.
    [[nodiscard]] fsim_vpi_handle_v1 select_expression_object(
        const fsim_vpi_handle_v1 expression, const std::size_t depth = 0U)
    {
        const auto serial = node_of(expression);
        if (!serial || depth > 16U) {
            return 0U;
        }
        const auto type = statement_model_->type(*serial);
        if (type != vpiBitSelect && type != vpiVarSelect
            && type != vpiPartSelect && type != vpiIndexedPartSelect) {
            return 0U;
        }
        const auto parent
            = statement_model_->relation(*serial, vpiParent).value_or(0U);
        const auto base = node_of(parent)
            ? select_expression_object(parent, depth + 1U)
            : parent;
        if (base == 0U
            || (owned(base) && select_of(base) == nullptr
                && subarray_of(base) == nullptr)) {
            return 0U;
        }
        const auto number
            = [&](const PLI_INT32 relation) -> std::optional<std::int64_t> {
            const auto operand
                = statement_model_->relation(*serial, relation).value_or(0U);
            return operand != 0U ? handle_integer(operand) : std::nullopt;
        };
        if (type == vpiVarSelect) {
            auto current = base;
            const auto indices
                = statement_model_->iterate(*serial, vpiIndex)
                      .value_or(std::vector<fsim_vpi_handle_v1> { });
            for (const auto index : indices) {
                const auto value = handle_integer(index);
                if (!value || current == 0U) {
                    return 0U;
                }
                current = index_handle(current, static_cast<PLI_INT32>(*value));
            }
            return current;
        }
        if (type == vpiBitSelect) {
            const auto index = number(vpiIndex);
            return index ? index_handle(base, static_cast<PLI_INT32>(*index))
                         : fsim_vpi_handle_v1 { };
        }
        if (type == vpiPartSelect) {
            const auto left = number(vpiLeftRange);
            const auto right = number(vpiRightRange);
            return left && right ? make_select(base, *left, *right, false)
                                 : fsim_vpi_handle_v1 { };
        }
        // `base +: width` and `base -: width` (IEEE 1800-2023 11.5.1).
        const auto start = number(vpiBaseExpr);
        const auto width = number(vpiWidthExpr);
        const auto range = declared_range(base);
        const auto direction
            = statement_model_->property(*serial, vpiIndexedPartSelectType);
        if (!start || !width || *width < 1 || !range || !direction) {
            return 0U;
        }
        const auto plus = *direction == vpiPosIndexed;
        const auto descending = range->first >= range->second;
        const auto far = plus ? *start + *width - 1 : *start - *width + 1;
        const auto left = plus == descending ? far : *start;
        const auto right = plus == descending ? *start : far;
        return make_select(base, left, right, false);
    }

    // The element of a multidimensional array at one index per unpacked
    // dimension, left to right (IEEE 1800-2023 38.24).
    fsim_vpi_status_v1 handle_by_multi_index(
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto object = info(resolve(request.handle));
        const auto* const indexes = pointer<PLI_INT32>(request.user_data);
        if (!object || indexes == nullptr) {
            return succeed(result);
        }
        auto name = object->full_name;
        for (std::uint64_t index = 0U; index < request.argument; ++index) {
            name += "[" + std::to_string(indexes[index]) + "]";
        }
        const auto found = registry_.find(name);
        return succeed(result, found && found.value ? found.value->handle : 0U);
    }

    // A port's size follows the formal; a port without a low connection
    // has size 0 (IEEE 1800-2023 37.14 detail 11).
    [[nodiscard]] std::int64_t port_width(
        const runtime::SystemVerilogVpiPort& port) const
    {
        const auto object = info(port.low_conn);
        if (!object || !object->type) {
            return 0;
        }
        if (const auto& declared = object->type->declared) {
            return static_cast<std::int64_t>(
                runtime::systemverilog_vpi_declared_bits(*declared));
        }
        return static_cast<std::int64_t>(object->type->width);
    }

    // The bit of a port that `handle` is.
    struct PortBitRef {
        std::size_t port { };
        std::uint32_t position { };
    };
    [[nodiscard]] std::optional<PortBitRef> port_bit_of(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto& ports = simulation_.systemverilog_vpi_ports();
        if (!owned(handle) || tag_of(handle) != Tag::port_bit) {
            return std::nullopt;
        }
        const auto serial = serial_of(handle);
        const auto index = static_cast<std::size_t>(serial >> 24U);
        if (index >= ports.size()) {
            return std::nullopt;
        }
        return PortBitRef { index, static_cast<std::uint32_t>(serial & 0xFFFFFFU) };
    }

    // The properties of a port (IEEE 1800-2023 37.14). Its size, scalar and
    // vector follow the formal; a port without a low connection has size 0.
    fsim_vpi_status_v1 port_property(const runtime::SystemVerilogVpiPort& port,
        const PLI_INT32 property, fsim_vpi_service_result_v1& result)
    {
        const auto integer = [&](const std::int64_t value) {
            return succeed(result, 0U, static_cast<std::uint64_t>(value));
        };
        const auto size = [&]() { return port_width(port); };
        switch (property) {
        case vpiPortIndex:
            return integer(port.index);
        case vpiDirection:
            return integer(port.ref ? vpiRef
                    : port.direction == runtime::SystemVerilogVpiDirection::Input
                    ? vpiInput
                    : port.direction == runtime::SystemVerilogVpiDirection::Output
                    ? vpiOutput
                    : vpiInout);
        case vpiPortType:
            return integer(port.modport_port ? vpiModportPort
                    : port.interface_port    ? vpiInterfacePort
                                             : vpiPort);
        case vpiConnByName:
            return integer(port.connected_by_name ? 1 : 0);
        case vpiExplicitName:
            return integer(0);
        case vpiSize:
            return integer(size());
        case vpiScalar:
            return integer(size() == 1 ? 1 : 0);
        case vpiVector:
            return integer(size() > 1 ? 1 : 0);
        default:
            return succeed(result, 0U, static_cast<std::uint64_t>(
                static_cast<std::int64_t>(vpiUndefined)));
        }
    }

    fsim_vpi_status_v1 property(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto property = static_cast<PLI_INT32>(request.argument);
        const auto undefined = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(vpiUndefined));
        const auto integer = [&](const std::int64_t value) {
            return succeed(result, 0U, static_cast<std::uint64_t>(value));
        };
        // Each call returns a new identifier for a data set of vpi_put_data
        // (IEEE 1800-2023 38.9, 38.31).
        if (property == vpiSaveRestartID) {
            return integer(++last_save_restart_id_);
        }
        if (property == vpiTimeUnit || property == vpiTimePrecision) {
            // An object answers with its module's `timescale; a null
            // handle with the simulation time unit (IEEE 1800-2023 38.6).
            if (const auto scale = module_time_scale(request.handle)) {
                return integer(property == vpiTimeUnit ? scale->first
                                                       : scale->second);
            }
            const auto profile = simulation_.systemverilog_vpi_time().profile();
            return integer(property == vpiTimeUnit ? profile.unit_exponent
                                                   : profile.precision_exponent);
        }
        if (property == vpiType) {
            return integer(handle_type(request.handle));
        }
        if (class_live_tag(request.handle)) {
            return class_object_property(request.handle, property, result);
        }
        if (frame_tag(request.handle)) {
            return frame_property(request.handle, property, result);
        }
        if (owned(request.handle) && tag_of(request.handle) == Tag::class_model
            && class_model().owns(serial_of(request.handle))) {
            const auto value
                = class_model().property(serial_of(request.handle), property);
            return value ? integer(*value) : succeed(result, 0U, undefined);
        }
        if (const auto serial = node_of(request.handle)) {
            auto value = statement_model_->property(*serial, property);
            if (!value && property == vpiFuncType) {
                value = call_function_type(*serial);
            }
            // A system task or function is user-defined when a plug-in
            // registered it (IEEE 1800-2023 37.42).
            if (!value && property == vpiUserDefn) {
                const auto type = statement_model_->type(*serial);
                if (type == vpiSysFuncCall || type == vpiSysTaskCall) {
                    const auto name
                        = statement_model_->string_property(*serial, vpiName);
                    value = name && systf_by_name_.contains(*name) ? 1 : 0;
                }
            }
            return value ? integer(*value) : succeed(result, 0U, undefined);
        }
        if (const auto value = primitive_model_property(request.handle, property)) {
            return integer(*value);
        }
        if (const auto value = net_delay_property(request.handle, property)) {
            return integer(*value);
        }
        if (const auto path = modpath_of(request.handle)) {
            return modpath_property(*path, property, result);
        }
        if (const auto term = pathterm_of(request.handle)) {
            return pathterm_property(*term, property, result);
        }
        if (const auto check = tchk_of(request.handle)) {
            return tchk_property(*check, property, result);
        }
        if (const auto term = tchkterm_of(request.handle)) {
            return integer(property == vpiEdge
                    ? tchk_edge_mask(tchkterm_event(*term))
                    : vpiUndefined);
        }
        if (const auto* level = typespec_of(request.handle)) {
            const auto& node = *level->node;
            bool empty = false;
            switch (property) {
            // A class named in the source that has no declaration: a
            // built-in class (IEEE 1800-2023 37.32).
            case vpiClassType:
                return node.kind == TypespecKind::Class
                    ? integer(VpiClassModel::class_type_of(node.name))
                    : succeed(result, 0U, undefined);
            case vpiSigned:
                return integer(node.is_signed ? 1 : 0);
            case vpiPacked:
                return node.kind == TypespecKind::Struct
                        || node.kind == TypespecKind::Union
                    ? integer(node.packed ? 1 : 0)
                    : succeed(result, 0U, undefined);
            case vpiTagged:
                return node.kind == TypespecKind::Union
                    ? integer(node.tagged ? 1 : 0)
                    : succeed(result, 0U, undefined);
            case vpiVector:
                return integer(node.kind == TypespecKind::PackedArray
                        || ((node.kind == TypespecKind::Logic
                                || node.kind == TypespecKind::Bit)
                            && !typespec_ranges(*level, empty).empty())
                        ? 1
                        : 0);
            case vpiArrayType:
                if (node.kind != TypespecKind::Array) {
                    return succeed(result, 0U, undefined);
                }
                switch (node.array_kind) {
                case runtime::SystemVerilogVpiArrayKind::Static:
                    return integer(vpiStaticArray);
                case runtime::SystemVerilogVpiArrayKind::Dynamic:
                    return integer(vpiDynamicArray);
                case runtime::SystemVerilogVpiArrayKind::Associative:
                    return integer(vpiAssocArray);
                case runtime::SystemVerilogVpiArrayKind::Queue:
                    return integer(vpiQueueArray);
                }
                return succeed(result, 0U, undefined);
            default:
                return succeed(result, 0U, undefined);
            }
        }
        if (typespec_part_of(request.handle, Tag::typespec_member) != nullptr) {
            return property == vpiRandType ? integer(vpiNotRand)
                                           : succeed(result, 0U, undefined);
        }
        if (const auto* constant
            = typespec_part_of(request.handle, Tag::enum_const)) {
            return property == vpiSize
                ? integer(enum_constant_width(*constant->owner))
                : succeed(result, 0U, undefined);
        }
        // A scheduled event reports whether it is still queued
        // (IEEE 1800-2023 38.34).
        if (owned(request.handle)
            && tag_of(request.handle) == Tag::scheduled) {
            if (property != vpiScheduled) {
                return succeed(result, 0U, undefined);
            }
            const auto status = simulation_.systemverilog_vpi_values().status(
                scheduled_write(request.handle));
            return integer(status
                    && status.status
                        == runtime::SystemVerilogVpiScheduledWriteStatus::Pending
                ? 1
                : 0);
        }
        if (const auto* port = port_of(request.handle)) {
            return port_property(*port, property, result);
        }
        // A port bit has no name or index (37.14 detail 7); it is one bit.
        if (port_bit_of(request.handle)) {
            return property == vpiSize || property == vpiScalar
                ? integer(1)
                : property == vpiVector ? integer(0)
                                        : succeed(result, 0U, undefined);
        }
        if (reference_of(request.handle) != nullptr) {
            return property == vpiGeneric
                ? succeed(result, 0U, 0U)
                : succeed(result, 0U, static_cast<std::uint64_t>(
                      static_cast<std::int64_t>(vpiUndefined)));
        }
        if (const auto* range = range_of(request.handle)) {
            return property == vpiSize
                ? integer(range->left >= range->right
                          ? range->left - range->right + 1
                          : range->right - range->left + 1)
                : succeed(result, 0U, undefined);
        }
        if (const auto* site = call_of(request.handle)) {
            switch (property) {
            case vpiLineNo:
                return integer(site->line);
            case vpiFuncType:
                return integer(site->function
                        ? systfs_[site->systf].data.sysfunctype
                        : vpiUndefined);
            case vpiSize:
                return integer(site->function ? site->width : 0);
            default:
                return succeed(result, 0U, undefined);
            }
        }
        if (const auto* named = argument(request.handle);
            named != nullptr && named->object == 0U) {
            switch (property) {
            case vpiSize:
                return integer(static_cast<std::int64_t>(
                    natural_width(named->value)));
            case vpiConstType:
                if (named->string_literal) {
                    return integer(vpiStringConst);
                }
                if (named->real || named->value.kind == Value::Kind::real) {
                    return integer(vpiRealConst);
                }
                if (named->text.find("'b") != std::string::npos
                    || named->text.find("'B") != std::string::npos) {
                    return integer(vpiBinaryConst);
                }
                if (named->text.find("'h") != std::string::npos
                    || named->text.find("'H") != std::string::npos) {
                    return integer(vpiHexConst);
                }
                if (named->text.find("'o") != std::string::npos
                    || named->text.find("'O") != std::string::npos) {
                    return integer(vpiOctConst);
                }
                return integer(vpiDecConst);
            case vpiSigned:
                return integer(named->value.is_signed ? 1 : 0);
            default:
                return succeed(result, 0U, undefined);
            }
        }
        if (const auto* subarray = subarray_of(resolve(request.handle))) {
            switch (property) {
            case vpiSize: {
                std::uint64_t size = 1U;
                for (const auto& range : subarray_dimensions(*subarray)) {
                    size *= extent(range);
                }
                return integer(static_cast<std::int64_t>(size));
            }
            case vpiArray:
                return integer(1);
            default:
                return succeed(result, 0U, undefined);
            }
        }
        if (const auto* select = select_of(resolve(request.handle))) {
            if (select->declared) {
                if (property == vpiArrayMember) {
                    return integer(select->unpacked_element ? 1 : 0);
                }
                if (const auto answer = declared_property(*select->declared,
                        property, select->member,
                        !select->member && !select->unpacked_element)) {
                    return integer(*answer);
                }
            }
            switch (property) {
            case vpiSize:
                return integer(select->width);
            case vpiScalar:
                return integer(select->width == 1U ? 1 : 0);
            case vpiVector:
                return integer(select->width > 1U ? 1 : 0);
            case vpiSigned:
                return integer(0);
            default:
                return succeed(result, 0U, undefined);
            }
        }
        if (const auto* constant = constant_of(request.handle)) {
            switch (property) {
            case vpiSize:
                return integer(static_cast<std::int64_t>(natural_width(constant->value)));
            case vpiConstType:
                return integer(constant->const_type);
            case vpiSigned:
                return integer(constant->value.is_signed ? 1 : 0);
            default:
                return succeed(result, 0U, undefined);
            }
        }
        const auto object = info(resolve(request.handle));
        if (!object) {
            return succeed(result, 0U, undefined);
        }
        // A concurrent assertion's own properties (IEEE 1800-2023 37.50):
        // `cover sequence`, and a clock the assertion did not write.
        if (const auto* assertion = concurrent_assertion(*object);
            assertion != nullptr
            && (property == vpiIsCoverSequence
                || property == vpiIsClockInferred)) {
            return integer(property == vpiIsCoverSequence
                    ? (assertion->kind
                                == semantic::sv::ConcurrentAssertionKind::cover
                            && assertion->form
                                == semantic::sv::ConcurrentAssertionForm::sequence
                        ? 1
                        : 0)
                    : (clock_inferred(*assertion_unit(*object), *assertion)
                            ? 1
                            : 0));
        }
        // A class defn's properties, and a class property's (IEEE 1800-2023
        // 37.17, 37.31). The object a class variable references is live
        // state, which is not published: vpiObjId is 0, no object.
        if (const auto identity = class_identity(*object)) {
            if (const auto value
                = class_model().defn_property(*identity, property)) {
                return integer(*value);
            }
        }
        if (object->kind == SystemVerilogVpiObjectKind::ClassProperty) {
            if (const auto owner = info(object->parent)) {
                if (const auto identity = class_identity(*owner)) {
                    if (const auto value = class_model().member_property(
                            *identity, object->name, property)) {
                        return integer(*value);
                    }
                }
            }
        }
        // The identifier of the object a class variable refers to, 0 for
        // none (IEEE 1800-2023 37.33 detail 2).
        if (property == vpiObjId && class_variable(*object)) {
            return integer(
                class_object_id(class_variable_object(request.handle)));
        }
        // A task's or function's own properties (IEEE 1800-2023 37.41).
        if ((object->kind == SystemVerilogVpiObjectKind::Task
                || object->kind == SystemVerilogVpiObjectKind::Function)
            && (property == vpiSize || property == vpiSigned
                || property == vpiFuncType || property == vpiMethod
                || property == vpiVirtual || property == vpiVisibility
                || property == vpiAccessType)) {
            if (const auto scope = hir_scope(resolve(request.handle))) {
                if (const auto value
                    = statement_model().scope_property(*scope, property)) {
                    return integer(*value);
                }
            }
            return succeed(result, 0U, undefined);
        }
        const auto width = object->type ? object->type->width : 0U;
        if (object->type && object->type->declared) {
            if (const auto answer = declared_property(
                    *object->type->declared, property, false, false)) {
                return integer(*answer);
            }
        }
        switch (property) {
        case vpiConstType:
            // A parameter's constant type follows its value's type
            // (IEEE 1800-2023 37.32).
            if (object->kind != SystemVerilogVpiObjectKind::Parameter
                || !object->type) {
                return succeed(result, 0U, undefined);
            }
            if (object->type->string_literal) {
                return integer(vpiStringConst);
            }
            switch (object->type->category) {
            case runtime::SystemVerilogVpiValueCategory::String:
                return integer(vpiStringConst);
            case runtime::SystemVerilogVpiValueCategory::Real:
            case runtime::SystemVerilogVpiValueCategory::ShortReal:
                return integer(vpiRealConst);
            case runtime::SystemVerilogVpiValueCategory::Integer2:
            case runtime::SystemVerilogVpiValueCategory::Integer4:
                return integer(vpiDecConst);
            default:
                return integer(vpiBinaryConst);
            }
        case vpiSize:
            if (object->kind == SystemVerilogVpiObjectKind::Memory) {
                return integer(object->live_children);
            }
            if (array_kind(object->kind)) {
                return integer(static_cast<std::int64_t>(
                    array_elements(*object).size()));
            }
            return integer(width);
        case vpiSigned:
            return integer(object->type && object->type->is_signed ? 1 : 0);
        case vpiScalar:
            return integer(vector_shape(*object) == VectorShape::scalar ? 1 : 0);
        case vpiVector:
            return integer(vector_shape(*object) == VectorShape::vector ? 1 : 0);
        case vpiLineNo:
            if (const auto* assertion = concurrent_assertion(*object)) {
                const auto location = assertion_location(*assertion);
                return integer(location ? location->second : 0);
            }
            if (!object->source && block_kind(object->kind)) {
                if (const auto block = named_block_source(request.handle)) {
                    return integer(block->line);
                }
            }
            if (object->source) {
                return integer(object->source->line);
            }
            // A class keeps its declaration's place on its type.
            return integer(object->type ? object->type->source_line : 0U);
        case vpiPrimType:
            return object->kind == SystemVerilogVpiObjectKind::Gate
                    && object->type
                ? integer(primitive_type(object->type->definition_name))
                : succeed(result, 0U, undefined);
        case vpiTopModule:
            return integer(object->parent == 0U ? 1 : 0);
        case vpiDefLineNo:
            if (!instance_kind(object->kind) || !object->type
                || object->type->definition_line == 0U) {
                return succeed(result, 0U, undefined);
            }
            return integer(object->type->definition_line);
        case vpiCellInstance:
            return integer(object->type && object->type->is_cell ? 1 : 0);
        // A top-level instance, and an element of an instance array (IEEE
        // 1800-2023 37.10).
        case vpiTop:
            return instance_kind(object->kind)
                ? integer(object->parent == 0U ? 1 : 0)
                : succeed(result, 0U, undefined);
        case vpiArrayMember:
            return instance_kind(object->kind)
                    || object->kind == SystemVerilogVpiObjectKind::GenerateScope
                ? integer(instance_array_member(*object) ? 1 : 0)
                : succeed(result, 0U, undefined);
        // The directive state where an instance's definition is (IEEE
        // 1800-2023 22.8, 22.9, 37.10); there are no delay modes or
        // decryption, so no delay mode and nothing protected.
        case vpiDefNetType:
            if (!instance_kind(object->kind) || !object->type) {
                return succeed(result, 0U, undefined);
            }
            return integer(object->type->default_net_kind
                    == runtime::SystemVerilogVpiNetKind::None
                ? vpiNone
                : net_type(object->type->default_net_kind));
        case vpiUnconnDrive:
            if (!instance_kind(object->kind) || !object->type) {
                return succeed(result, 0U, undefined);
            }
            switch (object->type->unconnected_drive) {
            case runtime::SystemVerilogVpiUnconnectedDrive::Pull0:
                return integer(vpiPull0);
            case runtime::SystemVerilogVpiUnconnectedDrive::Pull1:
                return integer(vpiPull1);
            case runtime::SystemVerilogVpiUnconnectedDrive::None:
                break;
            }
            return integer(vpiHighZ);
        case vpiDefDelayMode:
            return instance_kind(object->kind)
                ? integer(vpiDelayModeNone)
                : succeed(result, 0U, undefined);
        case vpiProtected:
            return instance_kind(object->kind)
                    || object->kind == SystemVerilogVpiObjectKind::GenerateScope
                ? integer(0)
                : succeed(result, 0U, undefined);
        // A generate block named by the implicit-name rule (IEEE 1800-2023
        // 27.6, 37.85 detail 2).
        case vpiImplicitDecl:
            if (object->kind == SystemVerilogVpiObjectKind::GenerateScope) {
                return integer(implicit_generate_scope(*object) ? 1 : 0);
            }
            if (object->kind == SystemVerilogVpiObjectKind::Net) {
                const auto parent = info(object->parent);
                return integer(parent && instance_kind(parent->kind)
                            && parent->type
                            && std::ranges::find(parent->type->implicit_nets,
                                   object->name)
                                != parent->type->implicit_nets.end()
                        ? 1
                        : 0);
            }
            return succeed(result, 0U, undefined);
        case vpiLocalParam:
            if (object->kind != SystemVerilogVpiObjectKind::Parameter
                && !(object->kind == SystemVerilogVpiObjectKind::ClassProperty
                    && object->type && object->type->local_parameter)) {
                return succeed(result, 0U, undefined);
            }
            return integer(
                object->type && object->type->local_parameter ? 1 : 0);
        case vpiInputEdge:
        case vpiOutputEdge:
            if (const auto view = clocking_view(request.handle)) {
                return integer(clocking_edge(
                    clocking_skew_of(*view, property == vpiOutputEdge)));
            }
            return succeed(result, 0U, undefined);
        case vpiAutomatic:
            return integer(object->type
                        && object->type->lifetime
                            == runtime::SystemVerilogVpiLifetime::Automatic
                    ? 1
                    : 0);
        case vpiDirection:
            // A clockvar's direction is its declaration's (37.48).
            if (object->kind == SystemVerilogVpiObjectKind::ClockingIo) {
                const auto view = clocking_view(request.handle);
                if (!view || view->signal == nullptr) {
                    return succeed(result, 0U, undefined);
                }
                using Direction = semantic::sv::Direction;
                switch (view->signal->direction) {
                case Direction::input:
                    return integer(vpiInput);
                case Direction::output:
                    return integer(vpiOutput);
                default:
                    return integer(vpiInout);
                }
            }
            // A formal's net or variable has no direction; its port has
            // (IEEE 1800-2023 37.14, 37.16, 37.17).
            if (!object->type
                || object->kind == SystemVerilogVpiObjectKind::Port) {
                return succeed(result, 0U, undefined);
            }
            switch (object->type->direction) {
            case runtime::SystemVerilogVpiDirection::Input:
                return integer(vpiInput);
            case runtime::SystemVerilogVpiDirection::Output:
                return integer(vpiOutput);
            case runtime::SystemVerilogVpiDirection::Inout:
                return integer(vpiInout);
            case runtime::SystemVerilogVpiDirection::None:
                break;
            }
            return succeed(result, 0U, undefined);
        case vpiNetType:
            return object->type
                    && object->type->net_kind
                        != runtime::SystemVerilogVpiNetKind::None
                ? integer(net_type(object->type->net_kind))
                : succeed(result, 0U, undefined);
        case vpiArray:
            return integer(object->kind == SystemVerilogVpiObjectKind::Memory
                    ? 1
                    : 0);
        // The kind of an array: static, dynamic, associative or queue
        // (IEEE 1800-2023 37.17).
        case vpiArrayType:
            if (object->kind != SystemVerilogVpiObjectKind::Memory
                || !object->type || !object->type->descriptor) {
                return succeed(result, 0U, undefined);
            }
            switch (object->type->descriptor->kind) {
            case runtime::SystemVerilogVpiDescriptorKind::DynamicArray:
                return integer(vpiDynamicArray);
            case runtime::SystemVerilogVpiDescriptorKind::AssociativeArray:
                return integer(vpiAssocArray);
            case runtime::SystemVerilogVpiDescriptorKind::Queue:
                return integer(vpiQueueArray);
            default:
                return integer(vpiStaticArray);
            }
        default:
            return succeed(result, 0U, undefined);
        }
    }

    // The name of an object type: its macro in vpi_user.h or
    // sv_vpi_user.h (IEEE 1800-2023 37.3.2, Annex K, Annex M).
    [[nodiscard]] static const char* type_name(const PLI_INT32 type)
    {
        switch (type) {
        case vpiAlways:
            return "vpiAlways";
        case vpiAssignStmt:
            return "vpiAssignStmt";
        case vpiAssignment:
            return "vpiAssignment";
        case vpiBegin:
            return "vpiBegin";
        case vpiCase:
            return "vpiCase";
        case vpiCaseItem:
            return "vpiCaseItem";
        case vpiConstant:
            return "vpiConstant";
        case vpiContAssign:
            return "vpiContAssign";
        case vpiDeassign:
            return "vpiDeassign";
        case vpiDefParam:
            return "vpiDefParam";
        case vpiDelayControl:
            return "vpiDelayControl";
        case vpiDisable:
            return "vpiDisable";
        case vpiEventControl:
            return "vpiEventControl";
        case vpiEventStmt:
            return "vpiEventStmt";
        case vpiFor:
            return "vpiFor";
        case vpiForce:
            return "vpiForce";
        case vpiForever:
            return "vpiForever";
        case vpiFork:
            return "vpiFork";
        case vpiFuncCall:
            return "vpiFuncCall";
        case vpiFunction:
            return "vpiFunction";
        case vpiGate:
            return "vpiGate";
        case vpiIf:
            return "vpiIf";
        case vpiIfElse:
            return "vpiIfElse";
        case vpiInitial:
            return "vpiInitial";
        case vpiIntegerVar:
            return "vpiIntegerVar";
        case vpiInterModPath:
            return "vpiInterModPath";
        case vpiIterator:
            return "vpiIterator";
        case vpiIODecl:
            return "vpiIODecl";
        case vpiMemory:
            return "vpiMemory";
        case vpiMemoryWord:
            return "vpiMemoryWord";
        case vpiModPath:
            return "vpiModPath";
        case vpiModule:
            return "vpiModule";
        case runtime::fsim_vpi_foreign_instance_type:
            return "fsimVpiForeignInstance";
        case vpiNamedBegin:
            return "vpiNamedBegin";
        case vpiNamedEvent:
            return "vpiNamedEvent";
        case vpiNamedFork:
            return "vpiNamedFork";
        case vpiNet:
            return "vpiNet";
        case vpiNetBit:
            return "vpiNetBit";
        case vpiNullStmt:
            return "vpiNullStmt";
        case vpiOperation:
            return "vpiOperation";
        case vpiParamAssign:
            return "vpiParamAssign";
        case vpiParameter:
            return "vpiParameter";
        case vpiPartSelect:
            return "vpiPartSelect";
        case vpiPathTerm:
            return "vpiPathTerm";
        case vpiPort:
            return "vpiPort";
        case vpiPortBit:
            return "vpiPortBit";
        case vpiPrimTerm:
            return "vpiPrimTerm";
        case vpiRealVar:
            return "vpiRealVar";
        case vpiReg:
            return "vpiReg";
        case vpiRegBit:
            return "vpiRegBit";
        case vpiRelease:
            return "vpiRelease";
        case vpiRepeat:
            return "vpiRepeat";
        case vpiRepeatControl:
            return "vpiRepeatControl";
        case vpiSchedEvent:
            return "vpiSchedEvent";
        case vpiSpecParam:
            return "vpiSpecParam";
        case vpiSwitch:
            return "vpiSwitch";
        case vpiSysFuncCall:
            return "vpiSysFuncCall";
        case vpiSysTaskCall:
            return "vpiSysTaskCall";
        case vpiTableEntry:
            return "vpiTableEntry";
        case vpiTask:
            return "vpiTask";
        case vpiTaskCall:
            return "vpiTaskCall";
        case vpiTchk:
            return "vpiTchk";
        case vpiTchkTerm:
            return "vpiTchkTerm";
        case vpiTimeVar:
            return "vpiTimeVar";
        case vpiTimeQueue:
            return "vpiTimeQueue";
        case vpiUdp:
            return "vpiUdp";
        case vpiUdpDefn:
            return "vpiUdpDefn";
        case vpiUserSystf:
            return "vpiUserSystf";
        case vpiVarSelect:
            return "vpiVarSelect";
        case vpiWait:
            return "vpiWait";
        case vpiWhile:
            return "vpiWhile";
        case vpiAttribute:
            return "vpiAttribute";
        case vpiBitSelect:
            return "vpiBitSelect";
        case vpiCallback:
            return "vpiCallback";
        case vpiDelayTerm:
            return "vpiDelayTerm";
        case vpiDelayDevice:
            return "vpiDelayDevice";
        case vpiFrame:
            return "vpiFrame";
        case vpiGateArray:
            return "vpiGateArray";
        case vpiModuleArray:
            return "vpiModuleArray";
        case vpiPrimitiveArray:
            return "vpiPrimitiveArray";
        case vpiNetArray:
            return "vpiNetArray";
        case vpiRange:
            return "vpiRange";
        case vpiRegArray:
            return "vpiRegArray";
        case vpiSwitchArray:
            return "vpiSwitchArray";
        case vpiUdpArray:
            return "vpiUdpArray";
        case vpiContAssignBit:
            return "vpiContAssignBit";
        case vpiNamedEventArray:
            return "vpiNamedEventArray";
        case vpiIndexedPartSelect:
            return "vpiIndexedPartSelect";
        case vpiGenScopeArray:
            return "vpiGenScopeArray";
        case vpiGenScope:
            return "vpiGenScope";
        case vpiGenVar:
            return "vpiGenVar";
        case vpiPackage:
            return "vpiPackage";
        case vpiInterface:
            return "vpiInterface";
        case vpiProgram:
            return "vpiProgram";
        case vpiInterfaceArray:
            return "vpiInterfaceArray";
        case vpiProgramArray:
            return "vpiProgramArray";
        case vpiTypespec:
            return "vpiTypespec";
        case vpiModport:
            return "vpiModport";
        case vpiInterfaceTfDecl:
            return "vpiInterfaceTfDecl";
        case vpiRefObj:
            return "vpiRefObj";
        case vpiTypeParameter:
            return "vpiTypeParameter";
        case vpiLongIntVar:
            return "vpiLongIntVar";
        case vpiShortIntVar:
            return "vpiShortIntVar";
        case vpiIntVar:
            return "vpiIntVar";
        case vpiShortRealVar:
            return "vpiShortRealVar";
        case vpiByteVar:
            return "vpiByteVar";
        case vpiClassVar:
            return "vpiClassVar";
        case vpiStringVar:
            return "vpiStringVar";
        case vpiEnumVar:
            return "vpiEnumVar";
        case vpiStructVar:
            return "vpiStructVar";
        case vpiUnionVar:
            return "vpiUnionVar";
        case vpiBitVar:
            return "vpiBitVar";
        case vpiClassObj:
            return "vpiClassObj";
        case vpiChandleVar:
            return "vpiChandleVar";
        case vpiPackedArrayVar:
            return "vpiPackedArrayVar";
        case vpiVirtualInterfaceVar:
            return "vpiVirtualInterfaceVar";
        case vpiLongIntTypespec:
            return "vpiLongIntTypespec";
        case vpiShortRealTypespec:
            return "vpiShortRealTypespec";
        case vpiByteTypespec:
            return "vpiByteTypespec";
        case vpiShortIntTypespec:
            return "vpiShortIntTypespec";
        case vpiIntTypespec:
            return "vpiIntTypespec";
        case vpiClassTypespec:
            return "vpiClassTypespec";
        case vpiStringTypespec:
            return "vpiStringTypespec";
        case vpiChandleTypespec:
            return "vpiChandleTypespec";
        case vpiEnumTypespec:
            return "vpiEnumTypespec";
        case vpiEnumConst:
            return "vpiEnumConst";
        case vpiIntegerTypespec:
            return "vpiIntegerTypespec";
        case vpiTimeTypespec:
            return "vpiTimeTypespec";
        case vpiRealTypespec:
            return "vpiRealTypespec";
        case vpiStructTypespec:
            return "vpiStructTypespec";
        case vpiUnionTypespec:
            return "vpiUnionTypespec";
        case vpiBitTypespec:
            return "vpiBitTypespec";
        case vpiLogicTypespec:
            return "vpiLogicTypespec";
        case vpiArrayTypespec:
            return "vpiArrayTypespec";
        case vpiVoidTypespec:
            return "vpiVoidTypespec";
        case vpiTypespecMember:
            return "vpiTypespecMember";
        case vpiPackedArrayTypespec:
            return "vpiPackedArrayTypespec";
        case vpiSequenceTypespec:
            return "vpiSequenceTypespec";
        case vpiPropertyTypespec:
            return "vpiPropertyTypespec";
        case vpiEventTypespec:
            return "vpiEventTypespec";
        case vpiInterfaceTypespec:
            return "vpiInterfaceTypespec";
        case vpiClockingBlock:
            return "vpiClockingBlock";
        case vpiClockingIODecl:
            return "vpiClockingIODecl";
        case vpiClassDefn:
            return "vpiClassDefn";
        case vpiConstraint:
            return "vpiConstraint";
        case vpiConstraintOrdering:
            return "vpiConstraintOrdering";
        case vpiDistItem:
            return "vpiDistItem";
        case vpiAliasStmt:
            return "vpiAliasStmt";
        case vpiThread:
            return "vpiThread";
        case vpiMethodFuncCall:
            return "vpiMethodFuncCall";
        case vpiMethodTaskCall:
            return "vpiMethodTaskCall";
        case vpiAssert:
            return "vpiAssert";
        case vpiAssume:
            return "vpiAssume";
        case vpiCover:
            return "vpiCover";
        case vpiRestrict:
            return "vpiRestrict";
        case vpiDisableCondition:
            return "vpiDisableCondition";
        case vpiClockingEvent:
            return "vpiClockingEvent";
        case vpiPropertyDecl:
            return "vpiPropertyDecl";
        case vpiPropertySpec:
            return "vpiPropertySpec";
        case vpiPropertyExpr:
            return "vpiPropertyExpr";
        case vpiMulticlockSequenceExpr:
            return "vpiMulticlockSequenceExpr";
        case vpiClockedSeq:
            return "vpiClockedSeq";
        case vpiClockedProp:
            return "vpiClockedProp";
        case vpiPropertyInst:
            return "vpiPropertyInst";
        case vpiSequenceDecl:
            return "vpiSequenceDecl";
        case vpiCaseProperty:
            return "vpiCaseProperty";
        case vpiCasePropertyItem:
            return "vpiCasePropertyItem";
        case vpiSequenceInst:
            return "vpiSequenceInst";
        case vpiImmediateAssert:
            return "vpiImmediateAssert";
        case vpiImmediateAssume:
            return "vpiImmediateAssume";
        case vpiImmediateCover:
            return "vpiImmediateCover";
        case vpiReturn:
            return "vpiReturn";
        case vpiAnyPattern:
            return "vpiAnyPattern";
        case vpiTaggedPattern:
            return "vpiTaggedPattern";
        case vpiStructPattern:
            return "vpiStructPattern";
        case vpiDoWhile:
            return "vpiDoWhile";
        case vpiOrderedWait:
            return "vpiOrderedWait";
        case vpiWaitFork:
            return "vpiWaitFork";
        case vpiDisableFork:
            return "vpiDisableFork";
        case vpiExpectStmt:
            return "vpiExpectStmt";
        case vpiForeachStmt:
            return "vpiForeachStmt";
        case vpiReturnStmt:
            return "vpiReturnStmt";
        case vpiFinal:
            return "vpiFinal";
        case vpiExtends:
            return "vpiExtends";
        case vpiDistribution:
            return "vpiDistribution";
        case vpiSeqFormalDecl:
            return "vpiSeqFormalDecl";
        case vpiPropFormalDecl:
            return "vpiPropFormalDecl";
        case vpiEnumNet:
            return "vpiEnumNet";
        case vpiIntegerNet:
            return "vpiIntegerNet";
        case vpiTimeNet:
            return "vpiTimeNet";
        case vpiUnionNet:
            return "vpiUnionNet";
        case vpiShortRealNet:
            return "vpiShortRealNet";
        case vpiRealNet:
            return "vpiRealNet";
        case vpiByteNet:
            return "vpiByteNet";
        case vpiShortIntNet:
            return "vpiShortIntNet";
        case vpiIntNet:
            return "vpiIntNet";
        case vpiLongIntNet:
            return "vpiLongIntNet";
        case vpiBitNet:
            return "vpiBitNet";
        case vpiInterconnectNet:
            return "vpiInterconnectNet";
        case vpiInterconnectArray:
            return "vpiInterconnectArray";
        case vpiStructNet:
            return "vpiStructNet";
        case vpiBreak:
            return "vpiBreak";
        case vpiContinue:
            return "vpiContinue";
        case vpiPackedArrayNet:
            return "vpiPackedArrayNet";
        case vpiNettypeDecl:
            return "vpiNettypeDecl";
        case vpiConstraintExpr:
            return "vpiConstraintExpr";
        case vpiElseConst:
            return "vpiElseConst";
        case vpiImplication:
            return "vpiImplication";
        case vpiConstrIf:
            return "vpiConstrIf";
        case vpiConstrIfElse:
            return "vpiConstrIfElse";
        case vpiConstrForEach:
            return "vpiConstrForEach";
        case vpiSoftDisable:
            return "vpiSoftDisable";
        case vpiLetDecl:
            return "vpiLetDecl";
        case vpiLetExpr:
            return "vpiLetExpr";
        default:
            return nullptr;
        }
    }

    fsim_vpi_status_v1 string_property(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto property = static_cast<PLI_INT32>(request.argument);
        const auto text = [&](std::string value) {
            return succeed(result, 0U, 0U, address(store(std::move(value))));
        };
        // The location exists only while the tool saves or restarts
        // (IEEE 1800-2023 38.9, 38.31).
        if (property == vpiSaveRestartLocation) {
            return save_phase_ == SavePhase::none
                ? FSIM_VPI_STATUS_NOT_FOUND
                : text(save_location_);
        }
        if (property == vpiType) {
            const auto* const name = type_name(handle_type(request.handle));
            return name == nullptr ? FSIM_VPI_STATUS_NOT_FOUND : text(name);
        }
        if (class_live_tag(request.handle)) {
            return class_object_string(request.handle, property, result);
        }
        if (owned(request.handle) && tag_of(request.handle) == Tag::class_model
            && class_model().owns(serial_of(request.handle))) {
            auto value = class_model().string_property(
                serial_of(request.handle), property);
            return value ? text(std::move(*value)) : FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (const auto serial = node_of(request.handle)) {
            auto value = statement_model_->string_property(*serial, property);
            if (value) {
                return text(std::move(*value));
            }
            // A select is named like the object it selects (IEEE 1800-2023
            // 37.18, 37.19).
            if (property == vpiName || property == vpiFullName) {
                if (const auto selected = select_expression_object(request.handle);
                    selected != 0U) {
                    auto inner = request;
                    inner.handle = selected;
                    return string_property(inner, result);
                }
            }
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A typespec is named by its typedef (IEEE 1800-2023 37.25 detail
        // 1); a member and an enumeration constant by their own names.
        if (const auto* level = typespec_of(request.handle)) {
            return property == vpiName && level->dropped == 0U
                    && !level->node->name.empty()
                ? text(level->node->name)
                : FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (const auto* member
            = typespec_part_of(request.handle, Tag::typespec_member)) {
            return property == vpiName
                ? text(member->owner->member_names[member->index])
                : FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (const auto* constant
            = typespec_part_of(request.handle, Tag::enum_const)) {
            const auto& literal = constant->owner->enum_constants[constant->index];
            if (property == vpiName) {
                return text(literal.name);
            }
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A port is named but has no full name (IEEE 1800-2023 37.14).
        if (const auto* port = port_of(request.handle)) {
            return property == vpiName ? text(port->name)
                                       : FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (const auto value = primitive_model_string(request.handle, property)) {
            return text(*value);
        }
        if (primitive_of(request.handle) != nullptr
            || primitive_array_of(request.handle)
            || udp_table_of(request.handle, Tag::udp_defn) != nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A path or a timing check names the file that declares it.
        if (const auto path = modpath_of(request.handle)) {
            return property == vpiFile
                ? text(simulation_.design().verilog_specify_paths()[*path]
                           .source.source_name.str())
                : FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (const auto check = tchk_of(request.handle)) {
            return property == vpiFile
                ? text(simulation_.design().verilog_timing_checks()[*check]
                           .source.path.str())
                : FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (port_bit_of(request.handle)) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (const auto* port = reference_of(request.handle)) {
            return property == vpiName ? text(port->name)
                : property == vpiFullName ? text(reference_full_name(*port))
                                          : FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (const auto* site = call_of(request.handle)) {
            switch (property) {
            case vpiName:
                return text(site->name);
            case vpiFile:
                return text(site->file);
            default:
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
        }
        if (const auto* named = argument(request.handle);
            named != nullptr && named->object == 0U) {
            if (property == vpiName && !named->expression
                && !named->string_literal) {
                return text(named->text);
            }
            if (property == vpiDecompile) {
                return text(named->text);
            }
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (const auto* subarray = subarray_of(resolve(request.handle))) {
            if (property != vpiName && property != vpiFullName) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            return text(property == vpiName ? subarray->name
                                            : subarray->full_name);
        }
        if (const auto* select = select_of(resolve(request.handle))) {
            const auto parent = info(select->parent);
            if (!parent || (property != vpiName && property != vpiFullName)) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            // A member is named alone; its full name carries its prefixes
            // (IEEE 1800-2023 37.17 detail 28).
            if (!select->name.empty()) {
                return text(property == vpiName ? select->name
                                                : parent->full_name + select->suffix);
            }
            const auto suffix = !select->suffix.empty() ? select->suffix
                : select->bit
                ? "[" + std::to_string(select->left) + "]"
                : "[" + std::to_string(select->left) + ":"
                    + std::to_string(select->right) + "]";
            return text((property == vpiName ? parent->name : parent->full_name)
                + suffix);
        }
        if (const auto* constant = constant_of(request.handle)) {
            if (property == vpiDecompile) {
                return text(std::to_string(signed_bits(constant->value.bits, true)));
            }
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto object = info(resolve(request.handle));
        if (!object) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        // An assertion is named by its label, and its process is its
        // place in the hierarchy (IEEE 1800-2023 39.3.1).
        if (concurrent_assertion(*object) != nullptr
            && (property == vpiName || property == vpiFullName)) {
            const auto process = info(object->parent);
            return property == vpiName
                ? text(std::string { unescaped(process->name) })
                : text(presented_full_name(*process));
        }
        if (property == vpiFile) {
            if (const auto* assertion = concurrent_assertion(*object)) {
                auto location = assertion_location(*assertion);
                return location && !location->first.empty()
                    ? text(std::move(location->first))
                    : FSIM_VPI_STATUS_NOT_FOUND;
            }
        }
        switch (property) {
        case vpiName:
            // An array word is named by its array and index
            // (IEEE 1800-2023 37.19).
            if (object->name.starts_with('[')) {
                if (const auto parent = info(object->parent)) {
                    return text(std::string { unescaped(parent->name) }
                        + object->name);
                }
            }
            if (object->kind == SystemVerilogVpiObjectKind::Package) {
                return text(package_name(object->name));
            }
            return text(std::string { unescaped(object->name) });
        case vpiFullName:
            return text(presented_full_name(*object));
        case vpiFile:
            if (!object->source && block_kind(object->kind)) {
                if (const auto block = named_block_source(request.handle);
                    block && !block->file.empty()) {
                    return text(block->file);
                }
            }
            return object->source ? text(object->source->file)
                                  : FSIM_VPI_STATUS_NOT_FOUND;
        // Where the instance's definition is, and its library; without a
        // configuration its cell is its definition (IEEE 1800-2023 33.4,
        // 37.10).
        case vpiDefFile:
            return instance_kind(object->kind) && object->type
                    && !object->type->definition_path.empty()
                ? text(object->type->definition_path)
                : FSIM_VPI_STATUS_NOT_FOUND;
        case vpiLibrary:
        case vpiCell: {
            if (!instance_kind(object->kind) || !object->type
                || object->type->semantic_unit.empty()) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            auto unit = std::string_view { object->type->semantic_unit };
            const auto separator = unit.find("::");
            if (separator == std::string_view::npos) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            if (property == vpiLibrary) {
                return text(std::string { unit.substr(0U, separator) });
            }
            unit.remove_prefix(separator + 2U);
            return text(std::string { unit.substr(0U, unit.find('(')) });
        }
        case vpiDefName: {
            if (object->type && !object->type->definition_name.empty()) {
                return text(object->type->definition_name);
            }
            if (!object->type || object->type->semantic_unit.empty()) {
                return text(object->name);
            }
            auto unit = std::string_view { object->type->semantic_unit };
            if (const auto separator = unit.rfind("::");
                separator != std::string_view::npos) {
                unit.remove_prefix(separator + 2U);
            }
            if (const auto parameters = unit.find('(');
                parameters != std::string_view::npos) {
                unit = unit.substr(0U, parameters);
            }
            return text(std::string { unit });
        }
        default:
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
    }

    // A concurrent assertion whose property has no clocking event of its own
    // takes its clock from the context (IEEE 1800-2023 16.16). The frontend
    // declares an inline property, clock included, as a named property.
    [[nodiscard]] static bool clock_inferred(const semantic::sv::Unit& unit,
        const semantic::sv::ConcurrentAssertion& assertion)
    {
        const auto first = std::ranges::find_if(assertion.property_tokens,
            [](const auto& token) { return token.text != "("; });
        if (first == assertion.property_tokens.end()) {
            return true;
        }
        if (first->text == "@") {
            return false;
        }
        const auto declared = std::ranges::find(unit.assertion_declarations,
            first->text, &semantic::sv::AssertionDeclaration::name);
        return declared == unit.assertion_declarations.end()
            || !declared->clock;
    }

    // --- values -------------------------------------------------------------

    // The value of a `$time`, `$stime` or `$realtime` call: the simulation
    // time in the time unit of the call's module (IEEE 1800-2023 20.3,
    // 37.42 detail 4).
    [[nodiscard]] std::optional<Value> system_time_value(
        const std::uint64_t serial) const
    {
        if (statement_model_->type(serial) != vpiSysFuncCall) {
            return std::nullopt;
        }
        const auto name = statement_model_->string_property(serial, vpiName);
        if (!name || (*name != "$time" && *name != "$stime" && *name != "$realtime")) {
            return std::nullopt;
        }
        s_vpi_time time { };
        time.type = vpiScaledRealTime;
        fill_time(time, statement_model_->relation(serial, vpiScope).value_or(0U));
        Value value;
        if (*name == "$realtime") {
            value.kind = Value::Kind::real;
            value.real = time.real;
            return value;
        }
        const auto ticks = static_cast<std::uint64_t>(std::llround(time.real));
        value.bits = *name == "$time" ? integer_bits(ticks, 64U)
                                      : integer_bits(ticks & 0xFFFFFFFFU, 32U);
        return value;
    }

    [[nodiscard]] std::optional<Value> object_value(
        const fsim_vpi_handle_v1 handle) const
    {
        if (const auto serial = node_of(handle)) {
            auto constant = statement_model_->value(*serial);
            if (!constant) {
                return system_time_value(*serial);
            }
            Value value;
            value.kind = constant->kind == VpiStatementValue::Kind::real
                ? Value::Kind::real
                : constant->kind == VpiStatementValue::Kind::string
                ? Value::Kind::string
                : Value::Kind::bits;
            value.bits = std::move(constant->bits);
            value.real = constant->real;
            value.text = std::move(constant->text);
            value.is_signed = constant->is_signed;
            return value;
        }
        if (const auto* named = argument(handle)) {
            if (named->object == 0U) {
                return named->value;
            }
            return object_value(named->object);
        }
        if (const auto* site = call_of(handle)) {
            return site->function ? std::optional<Value> { site->result }
                                  : std::nullopt;
        }
        if (const auto* select = select_of(handle)) {
            const auto parent = object_value(select->parent);
            if (!parent) {
                return std::nullopt;
            }
            const auto bits = value_bits(*parent, natural_width(*parent));
            Value value;
            value.bits = PackedLogic4 { select->width, Logic4::x };
            for (std::uint32_t bit = 0U; bit < select->width; ++bit) {
                if (select->offset + bit < bits.width()) {
                    value.bits.set(bit, bits.get(select->offset + bit));
                }
            }
            return value;
        }
        if (const auto* constant = constant_of(handle)) {
            return constant->value;
        }
        if (const auto* constant = typespec_part_of(handle, Tag::enum_const)) {
            Value value;
            value.bits
                = constant->owner->enum_constants[constant->index].value;
            if (!constant->owner->element.empty()) {
                value.is_signed = constant->owner->element.front().is_signed;
            }
            return value;
        }
        if (const auto value = primitive_model_value(handle)) {
            return value;
        }
        if (owned(handle)) {
            return std::nullopt;
        }
        const auto stored = registry_.stored_value(handle);
        if (!stored || !stored.value) {
            return std::nullopt;
        }
        const auto type = registry_.type_info(handle);
        return stored_to_value(*stored.value,
            type && type.value && type.value->is_signed);
    }

    [[nodiscard]] const char* store(std::string text)
    {
        auto& slot = strings_[string_cursor_++ % strings_.size()];
        slot = std::move(text);
        return slot.c_str();
    }

    void fill_time(s_vpi_time& time, const fsim_vpi_handle_v1 object) const
    {
        auto& service = simulation_.systemverilog_vpi_time();
        if (time.type == vpiScaledRealTime) {
            const auto query
                = service.query(runtime::SystemVerilogVpiTimeFormat::ScaledReal);
            time.real = query.value.scaled_real;
            // In the time unit of the object's module (IEEE 1800-2023 38.13).
            if (const auto scale = module_time_scale(object)) {
                time.real *= std::pow(10.0,
                    static_cast<int>(query.profile.unit_exponent) - scale->first);
            }
            return;
        }
        if (time.type == vpiSimTime) {
            const auto query = service.query(
                runtime::SystemVerilogVpiTimeFormat::IntegerTicks);
            time.high = static_cast<PLI_UINT32>(query.value.high);
            time.low = static_cast<PLI_UINT32>(query.value.low);
        }
    }

    [[nodiscard]] bool write_value(const Value& value, s_vpi_value& out)
    {
        const auto width = std::max<std::size_t>(natural_width(value), 1U);
        switch (out.format) {
        case vpiBinStrVal:
            out.value.str = const_cast<PLI_BYTE8*>(
                store(binary_string(value_bits(value, width))));
            return true;
        case vpiOctStrVal:
            out.value.str = const_cast<PLI_BYTE8*>(
                store(radix_string(value_bits(value, width), 3U)));
            return true;
        case vpiHexStrVal:
            out.value.str = const_cast<PLI_BYTE8*>(
                store(radix_string(value_bits(value, width), 4U)));
            return true;
        case vpiDecStrVal:
            if (value.kind == Value::Kind::real) {
                out.value.str = const_cast<PLI_BYTE8*>(store(std::to_string(
                    static_cast<long long>(std::llround(value.real)))));
                return true;
            }
            out.value.str = const_cast<PLI_BYTE8*>(store(
                decimal_string(value_bits(value, width), value.is_signed)));
            return true;
        case vpiScalarVal: {
            const auto bit = value_bits(value, width).get(0U);
            out.value.scalar = bit == Logic4::zero ? vpi0
                : bit == Logic4::one               ? vpi1
                : bit == Logic4::z                 ? vpiZ
                                                   : vpiX;
            return true;
        }
        case vpiIntVal:
            out.value.integer = static_cast<PLI_INT32>(
                value.kind == Value::Kind::real
                    ? std::llround(value.real)
                    : signed_bits(value_bits(value, width), value.is_signed));
            return true;
        case vpiRealVal:
            if (value.kind == Value::Kind::real) {
                out.value.real = value.real;
            } else {
                const auto bits = value_bits(value, width);
                out.value.real = value.is_signed
                    ? static_cast<double>(signed_bits(bits, true))
                    : static_cast<double>(low_bits(bits));
            }
            return true;
        case vpiStringVal:
            out.value.str = const_cast<PLI_BYTE8*>(store(
                value.kind == Value::Kind::string
                    ? value.text
                    : bytes_string(value_bits(value, width))));
            return true;
        case vpiVectorVal: {
            const auto bits = value_bits(value, width);
            auto& words = vectors_[vector_cursor_++ % vectors_.size()];
            words.assign((width + 31U) / 32U, s_vpi_vecval { 0, 0 });
            for (std::size_t bit = 0U; bit < width; ++bit) {
                const auto state = bits.get(bit);
                const auto mask = PLI_UINT32 { 1 } << (bit % 32U);
                auto& word = words[bit / 32U];
                if (state == Logic4::one || state == Logic4::x) {
                    word.aval |= mask;
                }
                if (state == Logic4::z || state == Logic4::x) {
                    word.bval |= mask;
                }
            }
            out.value.vector = words.data();
            return true;
        }
        case vpiTimeVal: {
            const auto bits = low_bits(value_bits(value, 64U));
            auto& time = times_[time_cursor_++ % times_.size()];
            time.type = vpiSimTime;
            time.high = static_cast<PLI_UINT32>(bits >> 32U);
            time.low = static_cast<PLI_UINT32>(bits);
            time.real = 0.0;
            out.value.time = &time;
            return true;
        }
        case vpiObjTypeVal:
            out.format = value.kind == Value::Kind::real ? vpiRealVal
                : value.kind == Value::Kind::string      ? vpiStringVal
                : width == 1U                            ? vpiScalarVal
                : width <= 32U                           ? vpiIntVal
                                                         : vpiVectorVal;
            return write_value(value, out);
        case vpiSuppressVal:
            return true;
        default:
            return false;
        }
    }

    fsim_vpi_status_v1 get_value(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        auto* const out = pointer<s_vpi_value>(request.user_data);
        if (out == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A delay term has the value of its net (IEEE 1800-2023 37.45).
        if (net_delay_index(request.handle, Tag::delay_term)) {
            auto redirected = request;
            redirected.handle = delay_term_object(request.handle);
            return redirected.handle != 0U ? get_value(redirected, result)
                                           : FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A live class property has a value; a class obj, a class variable
        // and a constraint have none (IEEE 1800-2023 37.33, 38.36.1).
        if (class_live_tag(request.handle)) {
            return get_class_value(request.handle, *out, result);
        }
        // A table entry has a string and a vector value only (IEEE 1800-2023
        // 37.36 detail 1).
        if (const auto entry = udp_entry_of(request.handle)) {
            if (!write_entry_value(entry->first, entry->second, *out)) {
                out->format = vpiSuppressVal;
                std::memset(&out->value, 0, sizeof(out->value));
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            return succeed(result);
        }
        // A scalar's strength (IEEE 1800-2023 38.15).
        if (out->format == vpiStrengthVal) {
            if (!write_strength_value(request.handle, *out)) {
                out->format = vpiSuppressVal;
                std::memset(&out->value, 0, sizeof(out->value));
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            return succeed(result);
        }
        // A select expression reads the object it selects (IEEE 1800-2023
        // 37.19, 37.59).
        const auto selected = select_expression_object(request.handle);
        const auto source = selected != 0U ? selected : request.handle;
        const auto value = valueless(resolve(source)) || class_scope_object(source)
                || class_variable_handle(resolve(source))
            ? std::nullopt
            : object_value(source);
        if (!value || !write_value(*value, *out)) {
            // A value that cannot be read leaves no stale data in the
            // caller's structure.
            out->format = vpiSuppressVal;
            std::memset(&out->value, 0, sizeof(out->value));
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        return succeed(result);
    }

    // The value a write supplies, as bits of the target width or as a real
    // or string (IEEE 1800-2023 38.35 vpi_put_value).
    [[nodiscard]] static std::optional<Value> input_value(
        const s_vpi_value& input, const std::size_t width)
    {
        Value value;
        switch (input.format) {
        case vpiBinStrVal:
            value.bits = parse_radix(input.value.str ? input.value.str : "", 1U, width);
            return value;
        case vpiOctStrVal:
            value.bits = parse_radix(input.value.str ? input.value.str : "", 3U, width);
            return value;
        case vpiHexStrVal:
            value.bits = parse_radix(input.value.str ? input.value.str : "", 4U, width);
            return value;
        case vpiDecStrVal:
            value.bits = parse_decimal(input.value.str ? input.value.str : "", width);
            return value;
        case vpiScalarVal:
            value.bits = PackedLogic4 { width, Logic4::zero };
            value.bits.set(0U,
                input.value.scalar == vpi1        ? Logic4::one
                    : input.value.scalar == vpi0  ? Logic4::zero
                    : input.value.scalar == vpiZ  ? Logic4::z
                                                  : Logic4::x);
            return value;
        case vpiStrengthVal:
            // Only a scalar takes a strength value (IEEE 1800-2023 38.34);
            // the put writes its logic value.
            if (input.value.strength == nullptr || width != 1U) {
                return std::nullopt;
            }
            value.bits = PackedLogic4 { 1U,
                input.value.strength->logic == vpi1       ? Logic4::one
                    : input.value.strength->logic == vpi0 ? Logic4::zero
                    : input.value.strength->logic == vpiZ ? Logic4::z
                                                          : Logic4::x };
            return value;
        case vpiIntVal:
            value.bits = resized(
                integer_bits(static_cast<std::uint32_t>(input.value.integer), 32U),
                width, true);
            value.is_signed = true;
            return value;
        case vpiRealVal:
            value.kind = Value::Kind::real;
            value.real = input.value.real;
            return value;
        case vpiStringVal:
            value.kind = Value::Kind::string;
            value.text = input.value.str ? input.value.str : "";
            return value;
        case vpiVectorVal: {
            if (input.value.vector == nullptr) {
                return std::nullopt;
            }
            value.bits = PackedLogic4 { width, Logic4::zero };
            for (std::size_t bit = 0U; bit < width; ++bit) {
                const auto& word = input.value.vector[bit / 32U];
                const auto mask = PLI_UINT32 { 1 } << (bit % 32U);
                const bool aval = (static_cast<PLI_UINT32>(word.aval) & mask) != 0U;
                const bool bval = (static_cast<PLI_UINT32>(word.bval) & mask) != 0U;
                value.bits.set(bit,
                    aval ? (bval ? Logic4::x : Logic4::one)
                         : (bval ? Logic4::z : Logic4::zero));
            }
            return value;
        }
        case vpiTimeVal:
            if (input.value.time == nullptr) {
                return std::nullopt;
            }
            value.bits = integer_bits(
                (static_cast<std::uint64_t>(input.value.time->high) << 32U)
                    | input.value.time->low,
                width);
            return value;
        default:
            return std::nullopt;
        }
    }

    // A stored value of the same representation as the object's current one.
    [[nodiscard]] std::optional<runtime::SystemVerilogVpiStoredValue> stored_for(
        const fsim_vpi_handle_v1 handle, const Value& value) const
    {
        const auto current = registry_.stored_value(handle);
        if (!current || !current.value) {
            return std::nullopt;
        }
        runtime::SystemVerilogVpiStoredValue stored;
        stored.strength = current.value->strength;
        const auto& payload = current.value->payload;
        if (const auto* bits = std::get_if<runtime::PackedBit2>(&payload)) {
            const auto source = value_bits(value, bits->width());
            runtime::PackedBit2 next { bits->width() };
            for (std::size_t bit = 0U; bit < bits->width(); ++bit) {
                next.set(bit, source.get(bit) == Logic4::one);
            }
            stored.payload = std::move(next);
        } else if (const auto* logic = std::get_if<PackedLogic4>(&payload)) {
            auto next = value_bits(value, logic->width());
            if (logic->is_logic9()) {
                PackedLogic4 nine { logic->width() };
                for (std::size_t bit = 0U; bit < logic->width(); ++bit) {
                    nine.set_logic9(bit, runtime::to_logic9(next.get(bit)));
                }
                next = std::move(nine);
            }
            stored.payload = std::move(next);
        } else if (const auto* logic9
            = std::get_if<runtime::PackedLogic9>(&payload)) {
            stored.payload = runtime::expand_to_logic9(
                value_bits(value, logic9->width()));
        } else if (std::holds_alternative<double>(payload)
            || std::holds_alternative<float>(payload)) {
            double real = value.real;
            if (value.kind != Value::Kind::real) {
                const auto integral = value_bits(value, natural_width(value));
                real = value.is_signed
                    ? static_cast<double>(signed_bits(integral, true))
                    : static_cast<double>(low_bits(integral));
            }
            if (std::holds_alternative<float>(payload)) {
                stored.payload = static_cast<float>(real);
            } else {
                stored.payload = real;
            }
        } else if (std::holds_alternative<std::string>(payload)) {
            stored.payload = value.kind == Value::Kind::string
                ? value.text
                : bytes_string(value_bits(value, natural_width(value)));
        } else if (std::holds_alternative<std::uint64_t>(payload)) {
            stored.payload = low_bits(value_bits(value, 64U));
        } else {
            return std::nullopt;
        }
        return stored;
    }

    fsim_vpi_status_v1 put_value(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        struct PutValueArguments {
            p_vpi_value value { };
            p_vpi_time time { };
        };
        const auto* const arguments
            = pointer<PutValueArguments>(request.user_data);
        if (arguments == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto flags = static_cast<PLI_INT32>(request.argument);
        const auto mode = flags & 0xFFF;
        // Cancelling a scheduled event also frees its handle; cancelling one
        // that has occurred is not an error (IEEE 1800-2023 38.34).
        if (mode == vpiCancelEvent) {
            if (!owned(request.handle)
                || tag_of(request.handle) != Tag::scheduled) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            auto& values = simulation_.systemverilog_vpi_values();
            const auto handle = scheduled_write(request.handle);
            (void)values.cancel(handle);
            (void)values.release(handle);
            return succeed(result);
        }
        // A system function's calltf returns its value by writing the call
        // (IEEE 1800-2023 38.35).
        if (owned(request.handle) && tag_of(request.handle) == Tag::call) {
            if (serial_of(request.handle) >= calls_.size()
                || arguments->value == nullptr) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            auto& site = calls_[serial_of(request.handle)];
            // A string is no value of a real function (IEEE 1800-2023 38.34).
            if (arguments->value->format == vpiStringVal
                && systfs_[site.systf].data.sysfunctype == vpiRealFunc) {
                error_message_ = "vpi_put_value: vpiStringVal cannot be put "
                                 "to a vpiRealFunc system function "
                                 "(IEEE 1800-2023 38.34)";
                error_ = true;
                return FSIM_VPI_STATUS_INVALID_ARGUMENT;
            }
            const auto value = input_value(*arguments->value, site.width);
            if (!value || !site.function) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            site.result.kind = Value::Kind::bits;
            site.result.bits = value_bits(*value, site.width);
            return succeed(result);
        }
        if (const auto* primitive = primitive_of(request.handle)) {
            return put_primitive_value(*primitive, flags, arguments->value, result);
        }
        auto object = resolve(request.handle);
        const auto requested = object;
        if (class_live_tag(object)) {
            return put_class_value(object, arguments->value, mode, result);
        }
        if (class_variable_handle(object)) {
            error_message_ = "vpi_put_value: the value of a class variable is "
                             "opaque (IEEE 1800-2023 38.36.1)";
            error_ = true;
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (valueless(object)) {
            error_message_ = "vpi_put_value: an unpacked structure or union "
                             "has no value (IEEE 1800-2023 37.17)";
            error_ = true;
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (class_scope_object(object)) {
            error_message_ = "vpi_put_value: a class defn's variables hold no "
                             "value (IEEE 1800-2023 37.31 detail 2)";
            error_ = true;
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A value the write derives from the target's current one: a
        // select's bits merged into its parent, or a toggled event.
        std::optional<Value> prepared;
        if (const auto* select = select_of(object)) {
            object = select->parent;
            if (mode != vpiReleaseFlag) {
                if (arguments->value == nullptr) {
                    return FSIM_VPI_STATUS_NOT_FOUND;
                }
                const auto current = object_value(object);
                const auto part = input_value(*arguments->value, select->width);
                if (!current || !part) {
                    return FSIM_VPI_STATUS_NOT_FOUND;
                }
                auto bits = value_bits(*current, natural_width(*current));
                const auto incoming = value_bits(*part, select->width);
                for (std::uint32_t bit = 0U; bit < select->width; ++bit) {
                    if (select->offset + bit < bits.width()) {
                        bits.set(select->offset + bit, incoming.get(bit));
                    }
                }
                Value value;
                value.bits = std::move(bits);
                prepared = std::move(value);
            }
        }
        const auto target = info(object);
        if (!target || !target->type) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        auto& values = simulation_.systemverilog_vpi_values();
        if (mode == vpiReleaseFlag) {
            if (values.apply(object, runtime::SystemVerilogVpiWriteKind::Release)
                != runtime::SystemVerilogVpiValueError::None) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            // value_p reports the released object itself (IEEE 1800-2023
            // 38.34), so a select reports its own bits, not its parent's.
            if (arguments->value != nullptr) {
                if (const auto current = object_value(requested)) {
                    (void)write_value(*current, *arguments->value);
                }
            }
            return succeed(result);
        }
        // A put to a named event triggers it and ignores the value
        // (IEEE 1800-2023 38.35); the put carries a one-bit trigger token.
        const bool event
            = target->kind == SystemVerilogVpiObjectKind::NamedEvent;
        if (!event && arguments->value == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A string onto a real variable and a strength onto a vector are
        // illegal (IEEE 1800-2023 38.34).
        if (!event) {
            using Category = runtime::SystemVerilogVpiValueCategory;
            const auto format = arguments->value->format;
            const bool real = target->type->category == Category::Real
                || target->type->category == Category::ShortReal;
            const auto width = select_of(requested) != nullptr
                ? select_of(requested)->width
                : target->type->width;
            if (format == vpiStringVal && real) {
                error_message_ = "vpi_put_value: vpiStringVal cannot be put "
                                 "to a real variable (IEEE 1800-2023 38.34)";
                error_ = true;
                return FSIM_VPI_STATUS_INVALID_ARGUMENT;
            }
            if (format == vpiStrengthVal && width > 1U) {
                error_message_ = "vpi_put_value: vpiStrengthVal cannot be put "
                                 "to a vector (IEEE 1800-2023 38.34)";
                error_ = true;
                return FSIM_VPI_STATUS_INVALID_ARGUMENT;
            }
        }
        std::optional<runtime::SystemVerilogVpiStoredValue> stored;
        if (event) {
            stored.emplace();
            stored->payload = PackedLogic4 { 1U, Logic4::one };
        } else {
            const auto value = prepared ? prepared
                                        : input_value(*arguments->value,
                                              std::max<std::size_t>(
                                                  target->type->width, 1U));
            if (!value) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            stored = stored_for(object, *value);
        }
        if (!stored) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (mode == vpiForceFlag) {
            return values.apply(object, runtime::SystemVerilogVpiWriteKind::Force,
                       std::move(*stored))
                    == runtime::SystemVerilogVpiValueError::None
                ? succeed(result)
                : FSIM_VPI_STATUS_NOT_FOUND;
        }
        // A read-only callback neither writes values nor schedules events
        // before the next scheduled event (IEEE 1800-2023 38.36.2).
        if (read_only_ && (mode == vpiNoDelay || arguments->time == nullptr)) {
            error_message_ = "vpi_put_value: no write in a cbReadOnlySynch "
                             "callback";
            error_ = true;
            return FSIM_VPI_STATUS_INVALID_ARGUMENT;
        }
        if (mode == vpiNoDelay || arguments->time == nullptr) {
            return values.apply(object,
                       runtime::SystemVerilogVpiWriteKind::Deposit,
                       std::move(*stored))
                    == runtime::SystemVerilogVpiValueError::None
                ? succeed(result)
                : FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto delay = simulation_.systemverilog_vpi_time().convert_delay(
            delay_of(*arguments->time, object));
        if (!delay) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (read_only_) {
            const auto next = simulation_.next_pending_time();
            if (delay.ticks == 0U
                || (next && simulation_.now() + delay.ticks < *next)) {
                error_message_ = "vpi_put_value: a cbReadOnlySynch callback "
                                 "cannot schedule an event before the next "
                                 "scheduled event";
                error_ = true;
                return FSIM_VPI_STATUS_INVALID_ARGUMENT;
            }
        }
        const auto scheduled = values.schedule(object,
            runtime::SystemVerilogVpiWriteKind::Deposit, std::move(*stored),
            delay.ticks,
            mode == vpiInertialDelay
                ? runtime::SystemVerilogVpiDelayPolicy::Inertial
                : mode == vpiTransportDelay
                ? runtime::SystemVerilogVpiDelayPolicy::ModifiedTransport
                : runtime::SystemVerilogVpiDelayPolicy::Transport,
            (flags & vpiReturnEvent) != 0);
        if (!scheduled) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        return succeed(result,
            (flags & vpiReturnEvent) != 0
                ? tagged(Tag::scheduled, scheduled.value.id)
                : 0U);
    }

    // --- array sections (IEEE 1800-2023 38.16 and 38.35) --------------------

    struct ValueArrayArguments {
        p_vpi_arrayvalue value { };
        PLI_INT32* indexes { };
        PLI_UINT32 count { };
    };

    // The elements of an unpacked array in the order the array value
    // routines visit them: the rightmost unpacked range varies fastest.
    void array_elements(const fsim_vpi_handle_v1 array,
        std::vector<fsim_vpi_handle_v1>& elements)
    {
        const auto parent = info(array);
        if (!parent) {
            return;
        }
        for (const auto handle : drain(registry_.iterate_children(array))) {
            const auto element = info(handle);
            if (!element
                || !element->full_name.starts_with(parent->full_name + "[")) {
                continue;
            }
            const auto before = elements.size();
            array_elements(handle, elements);
            if (elements.size() == before) {
                elements.push_back(handle);
            }
        }
    }

    // The `count` elements starting at one index per unpacked dimension.
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>> array_section(
        const fsim_vpi_handle_v1 handle, const ValueArrayArguments& arguments)
    {
        const auto array = resolve(handle);
        const auto object = info(array);
        if (!object || arguments.indexes == nullptr) {
            return std::nullopt;
        }
        std::vector<fsim_vpi_handle_v1> elements;
        array_elements(array, elements);
        if (elements.empty()) {
            return std::nullopt;
        }
        const auto first = info(elements.front());
        const auto dimensions = first
            ? static_cast<std::size_t>(std::ranges::count(
                  std::string_view { first->full_name }.substr(
                      object->full_name.size()),
                  '['))
            : 0U;
        auto start = object->full_name;
        for (std::size_t index = 0U; index < dimensions; ++index) {
            start += "[" + std::to_string(arguments.indexes[index]) + "]";
        }
        const auto position = std::ranges::find_if(elements,
            [&](const fsim_vpi_handle_v1 element) {
                const auto found = info(element);
                return found && found->full_name == start;
            });
        if (position == elements.end()
            || static_cast<std::size_t>(std::distance(position, elements.end()))
                < arguments.count) {
            return std::nullopt;
        }
        return std::vector<fsim_vpi_handle_v1>(
            position, position + static_cast<std::ptrdiff_t>(arguments.count));
    }

    [[nodiscard]] std::size_t element_width(const fsim_vpi_handle_v1 element) const
    {
        const auto object = info(element);
        return object && object->type
            ? std::max<std::size_t>(object->type->width, 1U)
            : 1U;
    }

    // The storage one array value format needs for `count` elements.
    [[nodiscard]] static std::optional<std::size_t> array_value_bytes(
        const PLI_UINT32 format, const std::size_t width, const std::size_t count)
    {
        switch (format) {
        case vpiIntVal:
            return count * sizeof(PLI_INT32);
        case vpiShortIntVal:
            return count * sizeof(PLI_INT16);
        case vpiLongIntVal:
            return count * sizeof(PLI_INT64);
        case vpiRealVal:
            return count * sizeof(double);
        case vpiShortRealVal:
            return count * sizeof(float);
        case vpiTimeVal:
            return count * sizeof(s_vpi_time);
        case vpiVectorVal:
            return count * ((width + 31U) / 32U) * sizeof(s_vpi_vecval);
        case vpiRawFourStateVal:
            return count * ((width + 7U) / 8U) * 2U;
        case vpiRawTwoStateVal:
            return count * ((width + 7U) / 8U);
        default:
            return std::nullopt;
        }
    }

    fsim_vpi_status_v1 get_value_array(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto* const arguments = pointer<ValueArrayArguments>(request.user_data);
        if (arguments == nullptr || arguments->value == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        auto& out = *arguments->value;
        const auto fail = [&] {
            out.value.rawvals = nullptr;
            return FSIM_VPI_STATUS_NOT_FOUND;
        };
        const auto section = array_section(request.handle, *arguments);
        if (!section) {
            return fail();
        }
        const auto width = element_width(section->front());
        const auto bytes = array_value_bytes(out.format, width, section->size());
        if (!bytes) {
            return fail();
        }
        if ((out.flags & vpiUserAllocFlag) == 0U) {
            array_buffer_.assign(*bytes, std::byte { });
            out.value.rawvals = reinterpret_cast<PLI_BYTE8*>(array_buffer_.data());
        } else if (out.value.rawvals == nullptr) {
            return fail();
        }
        const auto words = (width + 31U) / 32U;
        const auto groups = (width + 7U) / 8U;
        for (std::size_t index = 0U; index < section->size(); ++index) {
            const auto value = object_value((*section)[index]);
            if (!value) {
                return fail();
            }
            const auto bits = value_bits(*value, width);
            const auto integral = [&] {
                return value->kind == Value::Kind::real
                    ? static_cast<std::uint64_t>(static_cast<std::int64_t>(value->real))
                    : low_bits(bits);
            };
            const auto real = [&] {
                return value->kind == Value::Kind::real ? value->real
                    : value->is_signed
                    ? static_cast<double>(signed_bits(bits, true))
                    : static_cast<double>(low_bits(bits));
            };
            switch (out.format) {
            case vpiIntVal:
                out.value.integers[index] = static_cast<PLI_INT32>(
                    static_cast<std::uint32_t>(integral()));
                break;
            case vpiShortIntVal:
                out.value.shortints[index] = static_cast<PLI_INT16>(
                    static_cast<std::uint16_t>(integral()));
                break;
            case vpiLongIntVal:
                out.value.longints[index] = static_cast<PLI_INT64>(integral());
                break;
            case vpiRealVal:
                out.value.reals[index] = real();
                break;
            case vpiShortRealVal:
                out.value.shortreals[index] = static_cast<float>(real());
                break;
            case vpiTimeVal: {
                const auto time = integral();
                out.value.times[index] = s_vpi_time { vpiSimTime,
                    static_cast<PLI_UINT32>(time >> 32U),
                    static_cast<PLI_UINT32>(time), 0.0 };
                break;
            }
            case vpiVectorVal: {
                auto* const element = out.value.vectors + index * words;
                std::fill_n(element, words, s_vpi_vecval { 0U, 0U });
                for (std::size_t bit = 0U; bit < width; ++bit) {
                    const auto state = bits.get(bit);
                    const auto mask = PLI_UINT32 { 1 } << (bit % 32U);
                    if (state == Logic4::one || state == Logic4::x) {
                        element[bit / 32U].aval |= mask;
                    }
                    if (state == Logic4::z || state == Logic4::x) {
                        element[bit / 32U].bval |= mask;
                    }
                }
                break;
            }
            case vpiRawFourStateVal:
            case vpiRawTwoStateVal: {
                const auto four = out.format == vpiRawFourStateVal;
                auto* const element = reinterpret_cast<unsigned char*>(
                    out.value.rawvals) + index * groups * (four ? 2U : 1U);
                std::fill_n(element, groups * (four ? 2U : 1U), 0U);
                for (std::size_t bit = 0U; bit < width; ++bit) {
                    const auto state = bits.get(bit);
                    const auto mask = static_cast<unsigned char>(1U << (bit % 8U));
                    if (state == Logic4::one || (four && state == Logic4::x)) {
                        element[bit / 8U] |= mask;
                    }
                    if (four && (state == Logic4::z || state == Logic4::x)) {
                        element[groups + bit / 8U] |= mask;
                    }
                }
                break;
            }
            default:
                return fail();
            }
        }
        return succeed(result);
    }

    fsim_vpi_status_v1 put_value_array(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto* const arguments = pointer<ValueArrayArguments>(request.user_data);
        if (arguments == nullptr || arguments->value == nullptr
            || arguments->value->value.rawvals == nullptr
            || (arguments->value->flags & ~PLI_UINT32 { vpiOneValue | vpiPropagateOff })
                != 0U) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto& in = *arguments->value;
        const auto section = array_section(request.handle, *arguments);
        if (!section) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        const auto width = element_width(section->front());
        const auto words = (width + 31U) / 32U;
        const auto groups = (width + 7U) / 8U;
        auto& values = simulation_.systemverilog_vpi_values();
        for (std::size_t position = 0U; position < section->size(); ++position) {
            const auto index = (in.flags & vpiOneValue) != 0U ? 0U : position;
            Value value;
            switch (in.format) {
            case vpiIntVal:
                value.bits = resized(integer_bits(
                    static_cast<std::uint32_t>(in.value.integers[index]), 32U),
                    width, true);
                break;
            case vpiShortIntVal:
                value.bits = resized(integer_bits(
                    static_cast<std::uint16_t>(in.value.shortints[index]), 16U),
                    width, true);
                break;
            case vpiLongIntVal:
                value.bits = resized(integer_bits(
                    static_cast<std::uint64_t>(in.value.longints[index]), 64U),
                    width, true);
                break;
            case vpiRealVal:
                value.kind = Value::Kind::real;
                value.real = in.value.reals[index];
                break;
            case vpiShortRealVal:
                value.kind = Value::Kind::real;
                value.real = in.value.shortreals[index];
                break;
            case vpiTimeVal: {
                const auto& time = in.value.times[index];
                value.bits = integer_bits(
                    (static_cast<std::uint64_t>(time.high) << 32U) | time.low, width);
                break;
            }
            case vpiVectorVal: {
                const auto* const element = in.value.vectors + index * words;
                value.bits = PackedLogic4 { width, Logic4::zero };
                for (std::size_t bit = 0U; bit < width; ++bit) {
                    const auto mask = PLI_UINT32 { 1 } << (bit % 32U);
                    const bool aval = (element[bit / 32U].aval & mask) != 0U;
                    const bool bval = (element[bit / 32U].bval & mask) != 0U;
                    value.bits.set(bit, aval ? (bval ? Logic4::x : Logic4::one)
                                             : (bval ? Logic4::z : Logic4::zero));
                }
                break;
            }
            case vpiRawFourStateVal:
            case vpiRawTwoStateVal: {
                const auto four = in.format == vpiRawFourStateVal;
                const auto* const element = reinterpret_cast<const unsigned char*>(
                    in.value.rawvals) + index * groups * (four ? 2U : 1U);
                value.bits = PackedLogic4 { width, Logic4::zero };
                for (std::size_t bit = 0U; bit < width; ++bit) {
                    const auto mask = static_cast<unsigned char>(1U << (bit % 8U));
                    const bool aval = (element[bit / 8U] & mask) != 0U;
                    const bool bval = four && (element[groups + bit / 8U] & mask) != 0U;
                    value.bits.set(bit, aval ? (bval ? Logic4::x : Logic4::one)
                                             : (bval ? Logic4::z : Logic4::zero));
                }
                break;
            }
            default:
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            const auto element = (*section)[position];
            auto stored = stored_for(element, value);
            if (!stored
                || values.apply(element, runtime::SystemVerilogVpiWriteKind::Deposit,
                       std::move(*stored))
                    != runtime::SystemVerilogVpiValueError::None) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
        }
        return succeed(result);
    }

    fsim_vpi_status_v1 get_time(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        auto* const time = pointer<s_vpi_time>(request.user_data);
        if (time == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        fill_time(*time, request.handle);
        return succeed(result);
    }

    fsim_vpi_status_v1 check_error(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        if (!error_) {
            return succeed(result, 0U, 0U);
        }
        if (auto* const out = pointer<s_vpi_error_info>(request.user_data)) {
            out->state = vpiRun;
            out->level = vpiError;
            out->message = error_message_.data();
            out->product = product_.data();
            out->code = error_code_.data();
            out->file = nullptr;
            out->line = 0;
        }
        return succeed(result, 0U, static_cast<std::uint64_t>(vpiError));
    }

    fsim_vpi_status_v1 vlog_info(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        auto* const out = pointer<s_vpi_vlog_info>(request.user_data);
        if (out == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        out->argc = static_cast<PLI_INT32>(argv_.size() - 1U);
        out->argv = argv_.data();
        out->product = product_.data();
        out->version = version_.data();
        return succeed(result, 0U, 1U);
    }

    fsim_vpi_status_v1 control(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto operation = static_cast<PLI_INT32>(request.argument);
        if (assertion_control_operation(operation)) {
            return assertion_control(operation, request, result);
        }
        if (operation == vpiReset) {
            return reset_control(request, result);
        }
        if (operation != vpiFinish && operation != vpiStop) {
            return FSIM_VPI_STATUS_UNSUPPORTED;
        }
        // The extra argument is the diagnostic level of $stop and $finish
        // (IEEE 1800-2023 38.4, 20.2).
        std::int32_t level = 1;
        if (auto* const arguments = pointer<va_list>(request.user_data)) {
            va_list copy;
            va_copy(copy, *arguments);
            level = va_arg(copy, int);
            va_end(copy);
        }
        report_diagnostics(operation == vpiFinish ? "$finish" : "$stop", level);
        // Inside calltf the calling process ends at the call (the lowered
        // call stops on the result); elsewhere the request ends the current
        // time step (IEEE 1800-2023 38.4).
        if (operation == vpiFinish && !call_stack_.empty()
            && !calls_[call_stack_.back()].function) {
            finish_requested_ = true;
            return succeed(result, 0U, 1U);
        }
        runtime::SystemVerilogVpiControlRequest control;
        control.operation = operation == vpiFinish
            ? runtime::SystemVerilogVpiControlOperation::Finish
            : runtime::SystemVerilogVpiControlOperation::Stop;
        return simulation_.systemverilog_vpi_control().submit(control)
            ? succeed(result, 0U, 1U)
            : FSIM_VPI_STATUS_NOT_FOUND;
    }

    // The callbacks of one save, restart or reset reason run once each; a
    // routine may register another for the next occurrence.
    void deliver_feature(const PLI_INT32 reason)
    {
        const auto found = feature_callbacks_.find(reason);
        if (found == feature_callbacks_.end()) {
            return;
        }
        const auto serials = std::exchange(found->second, { });
        for (const auto serial : serials) {
            fire(serial, std::nullopt);
        }
    }

    // When an implementation restarts, the only VPI callbacks that exist
    // are those for cbStartOfRestart and cbEndOfRestart (IEEE 1800-2023
    // 38.36.3).
    void drop_callbacks_for_restart()
    {
        std::vector<std::uint64_t> doomed;
        for (const auto& [serial, stored] : callbacks_) {
            if (stored->data.reason != cbStartOfRestart
                && stored->data.reason != cbEndOfRestart) {
                doomed.push_back(serial);
            }
        }
        for (const auto& [serial, stored] : assertion_callbacks_) {
            (void)stored;
            doomed.push_back(serial);
        }
        for (const auto serial : doomed) {
            fsim_vpi_service_request_v1 request { };
            request.handle = tagged(Tag::callback, serial);
            fsim_vpi_service_result_v1 ignored { };
            (void)remove_callback(request, ignored);
        }
    }

    // vpi_control(vpiReset, stop_value, reset_value, diagnostics_value)
    // executes $reset once the calling routine returns (IEEE 1800-2023
    // 38.4, D.8); the engine performs it at the next safe point.
    fsim_vpi_status_v1 reset_control(
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        auto* const arguments = pointer<va_list>(request.user_data);
        if (arguments == nullptr) {
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        Simulation::ResetRequest reset;
        va_list copy;
        va_copy(copy, *arguments);
        reset.stop_value = va_arg(copy, int);
        reset.reset_value = va_arg(copy, int);
        reset.diagnostics_value = va_arg(copy, int);
        va_end(copy);
        if (!simulation_.request_reset(reset)) {
            error_message_ = "vpi_control: vpiReset needs an engine that "
                             "restores the time-zero state of the run";
            error_ = true;
            return FSIM_VPI_STATUS_UNSUPPORTED;
        }
        report_diagnostics("$reset", reset.diagnostics_value);
        return succeed(result, 0U, 1U);
    }

    // --- save, restart and reset (IEEE 1800-2023 38.9, 38.31, 38.36.3) ----

    fsim_vpi_status_v1 get_data(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto id = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(request.argument));
        const auto size = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(request.argument >> 32U));
        auto* const out = pointer<std::byte>(request.user_data);
        // Only a cbStartOfRestart or cbEndOfRestart routine reads saved
        // data; 0 is the failure return.
        if (save_phase_ != SavePhase::restart || out == nullptr || size <= 0) {
            return succeed(result, 0U, 0U);
        }
        const auto found = save_data_.find(id);
        if (found == save_data_.end()) {
            return succeed(result, 0U, 0U);
        }
        // What is left of the data set, zero-filled beyond it; each call
        // starts where the last one ended.
        auto& data = found->second;
        const auto wanted = static_cast<std::size_t>(size);
        const auto left = data.bytes.size() - data.cursor;
        const auto count = std::min(wanted, left);
        if (count != 0U) {
            std::memcpy(out, data.bytes.data() + data.cursor, count);
        }
        std::memset(out + count, 0, wanted - count);
        data.cursor += count;
        return succeed(result, 0U, count);
    }

    fsim_vpi_status_v1 put_data(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto id = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(request.argument));
        const auto size = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(request.argument >> 32U));
        const auto* const in = pointer<const std::byte>(request.user_data);
        // Only a cbStartOfSave or cbEndOfSave routine writes saved data,
        // and numOfBytes is greater than zero; 0 is the failure return.
        if (save_phase_ != SavePhase::save || in == nullptr || size <= 0) {
            return succeed(result, 0U, 0U);
        }
        auto& data = save_data_[id];
        data.bytes.append(reinterpret_cast<const char*>(in),
            static_cast<std::size_t>(size));
        return succeed(result, 0U, static_cast<std::uint64_t>(size));
    }

    // Table 20-1: level 0 prints nothing, 1 the simulation time and the
    // location, 2 also the memory use. The fsim $finish/$stop statements
    // print nothing, so this is the only place the levels have an effect.
    void report_diagnostics(
        const std::string_view task, const std::int32_t level)
    {
        if (level < 1) {
            return;
        }
        s_vpi_time time { };
        time.type = vpiSimTime;
        fill_time(time, 0U);
        const auto ticks = (static_cast<std::uint64_t>(time.high) << 32U)
            | time.low;
        std::string text { task };
        text += " called at time " + std::to_string(ticks);
        if (!call_stack_.empty()) {
            const auto& site = calls_[call_stack_.back()];
            if (!site.file.empty()) {
                text += " (" + site.file + ":" + std::to_string(site.line) + ")";
            }
        }
        text += '\n';
#if defined(__unix__) || defined(__APPLE__)
        if (level >= 2) {
            rusage usage { };
            if (getrusage(RUSAGE_SELF, &usage) == 0) {
                text += "  memory: " + std::to_string(usage.ru_maxrss)
                    + " peak resident set (platform units)\n";
            }
        }
#endif
        print_(text);
    }

    // The text a printf-style request formats, or nothing on a format error.
    [[nodiscard]] static std::optional<std::string> format_text(
        const fsim_vpi_service_request_v1& request)
    {
        auto* const arguments = pointer<va_list>(request.user_data);
        std::string format { request.text, request.text_size };
        if (arguments == nullptr) {
            return format;
        }
        va_list measure;
        va_copy(measure, *arguments);
        const auto size = std::vsnprintf(nullptr, 0U, format.c_str(), measure);
        va_end(measure);
        if (size < 0) {
            return std::nullopt;
        }
        std::string text;
        text.resize(std::min<std::size_t>(
            static_cast<std::size_t>(size), maximum_printed_bytes));
        va_list copy;
        va_copy(copy, *arguments);
        (void)std::vsnprintf(text.data(), text.size() + 1U, format.c_str(), copy);
        va_end(copy);
        return text;
    }

    fsim_vpi_status_v1 print(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        if (request.text == nullptr) {
            return succeed(result);
        }
        const auto text = format_text(request);
        if (!text) {
            return succeed(result);
        }
        print_(*text);
        return succeed(result, 0U, text->size());
    }

    // vpi_mcd_printf writes channel 1 to the output channel and the other
    // channels to their files, but never an fd (IEEE 1800-2023 38.28). The
    // result is the character count, or EOF when a write failed.
    fsim_vpi_status_v1 mcd_print(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        if (request.text == nullptr) {
            return succeed(result);
        }
        const auto descriptor = static_cast<std::uint32_t>(request.argument);
        // The set MSB marks an fd, whose low bits are not channels.
        if ((descriptor & 0x80000000U) != 0U) {
            return succeed(result);
        }
        const auto text = format_text(request);
        if (!text) {
            return succeed(result, 0U, static_cast<std::uint64_t>(
                static_cast<std::int64_t>(-1)));
        }
        if ((descriptor & 1U) != 0U) {
            print_(*text);
        }
        if (!simulation_.mcd_write(descriptor, *text)) {
            return succeed(result, 0U, static_cast<std::uint64_t>(
                static_cast<std::int64_t>(-1)));
        }
        return succeed(result, 0U, text->size());
    }

    Simulation& simulation_;
    runtime::SystemVerilogVpiObjectRegistry& registry_;
    PrintSink print_;
    std::vector<std::unique_ptr<platform::DynamicLibrary>> libraries_;
    std::deque<Systf> systfs_;
    std::unordered_map<std::string, std::size_t> systf_by_name_;
    std::vector<CallSite> calls_;
    std::unordered_map<std::string, std::size_t> call_by_identity_;
    std::vector<std::size_t> call_stack_;
    bool finish_requested_ { };
    std::uint64_t next_callback_ { };
    std::unordered_map<std::uint64_t, std::unique_ptr<Callback>> callbacks_;
    std::vector<std::uint64_t> end_of_compile_;
    // The save, restart and reset callbacks by reason.
    std::map<PLI_INT32, std::vector<std::uint64_t>> feature_callbacks_;
    // The phase of the save/restart driver; vpi_put_data and vpi_get_data
    // are legal only in its save and restart phases.
    enum class SavePhase : std::uint8_t { none, save, restart };
    // The bytes of one save/restart id, and how many vpi_get_data has read.
    struct SaveData {
        std::string bytes;
        std::size_t cursor { };
    };
    SavePhase save_phase_ { SavePhase::none };
    std::string save_location_;
    std::map<std::int32_t, SaveData> save_data_;
    std::int32_t last_save_restart_id_ { };
    // The cbError and cbPLIError callbacks, which run on every error.
    std::vector<std::uint64_t> run_error_callbacks_;
    std::vector<std::uint64_t> unresolved_callbacks_;
    std::vector<std::uint64_t> pli_error_callbacks_;
    // The cbTchkViolation callbacks (IEEE 1800-2023 38.36.3).
    std::vector<std::uint64_t> tchk_callbacks_;
    bool in_pli_error_ { };
    // The cbSignal callbacks and their traps, delivered at safe points.
    std::map<std::uint64_t, std::unique_ptr<ForeignSignalTrap>> signal_traps_;
    std::uint64_t signal_hook_ { };
    std::uint64_t next_iterator_ { };
    std::unordered_map<std::uint64_t, Iterator> iterators_;
    std::unordered_map<fsim_vpi_handle_v1, void*> user_data_;
    std::array<std::string, 16> strings_;
    std::vector<std::byte> array_buffer_;
    std::vector<Select> selects_;
    std::vector<Constant> constants_;
    std::vector<ClockingSkewHandle> clocking_skews_;
    std::vector<fsim_vpi_handle_v1> clocking_events_;
    std::unique_ptr<VpiStatementModel> statement_model_;
    std::unique_ptr<VpiClassModel> class_model_;
    // The registry's class defn object of each declaration identity.
    std::map<std::string, fsim_vpi_handle_v1> class_defns_;
    bool class_defns_built_ { };
    // The live class objects handed out as handles, by serial: the class
    // heap's handle, 0 once the object was reclaimed (IEEE 1800-2023 37.33).
    std::vector<runtime::SystemVerilogClassHandle> class_objects_;
    std::unordered_map<runtime::SystemVerilogClassHandle, std::size_t>
        class_object_serials_;
    std::vector<ClassMember> class_members_;
    std::map<std::tuple<runtime::SystemVerilogClassHandle, bool, std::size_t,
                 fsim_vpi_handle_v1>,
        std::size_t>
        class_member_index_;
    // A callback on a dynamic array or queue (cbSizeChange) or on a class
    // typespec or object (cbReclaimObj, cbEndOfObject).
    struct ObjectCallback {
        std::uint64_t serial { };
        fsim_vpi_handle_v1 object { };
    };
    std::vector<ObjectCallback> size_callbacks_;
    std::vector<ObjectCallback> reclaim_callbacks_;
    std::vector<ObjectCallback> create_callbacks_;
    bool size_hooked_ { };
    bool reclaim_hooked_ { };
    bool create_hooked_ { };
    // The frames and threads of a probe run, and the callbacks on them: the
    // reason, the object they were placed on (0 for all), and for a task or
    // function the name of its callee.
    struct FrameCallback {
        std::uint64_t serial { };
        PLI_INT32 reason { };
        fsim_vpi_handle_v1 object { };
        std::string callee;
    };
    std::unique_ptr<VpiFrameModel> frames_;
    std::vector<FrameCallback> frame_callbacks_;
    std::uint64_t frame_hook_ { };
    std::unordered_set<runtime::simir::ProcessId> thread_sources_;
    bool thread_sources_built_ { };
    // A force or release a process executed, waiting for the next safe
    // point.
    struct ForceEvent {
        runtime::simir::ProcessId process { };
        runtime::simir::SignalId signal { };
        std::size_t offset { };
        std::size_t width { };
        bool release { };
    };
    std::vector<ForceEvent> pending_forces_;
    std::vector<std::uint64_t> force_callbacks_;
    std::uint64_t force_hook_ { };
    std::uint64_t force_delivery_hook_ { };
    // cbStmt callbacks: the object each was placed on, and the statement
    // whose execution point runs it.
    struct StatementCallback {
        std::uint64_t serial { };
        fsim_vpi_handle_v1 object { };
        fsim_vpi_handle_v1 trigger { };
    };
    std::vector<StatementCallback> statement_callbacks_;
    std::uint64_t statement_hook_ { };
    // cbDisable callbacks, and the objects disabled since the last safe
    // point.
    std::vector<std::uint64_t> disable_callbacks_;
    std::vector<fsim_vpi_handle_v1> pending_disables_;
    std::uint64_t disable_hook_ { };
    // The statement at each (design process, line, column) execution point.
    std::map<std::tuple<runtime::simir::ProcessId, const std::string*,
                 std::uint32_t, std::uint32_t>,
        VpiStatementModel::StatementPoint>
        statement_points_;
    std::map<std::uint64_t, AssertionCallback> assertion_callbacks_;
    std::vector<std::uint64_t> assertion_system_callbacks_;
    std::set<fsim_vpi_handle_v1> locked_assertions_;
    std::vector<runtime::SystemVerilogVpiCallbackHandle> assertion_system_hooks_;
    bool assertion_system_hooked_ { };
    bool assertion_start_hooked_ { };
    bool assertion_system_locked_ { };
    bool assertion_system_ended_ { };
    std::vector<Range> ranges_;
    std::vector<SubArray> subarrays_;
    std::vector<TypespecLevel> typespecs_;
    std::map<std::pair<const void*, std::size_t>, std::size_t> typespec_index_;
    std::vector<TypespecPart> typespec_members_;
    std::vector<TypespecPart> enum_constants_;
    // Typespecs of a function's return variable and formals, built from
    // the declaration's HIR type (IEEE 1800-2023 37.41).
    std::deque<runtime::SystemVerilogVpiTypespec> declared_typespec_nodes_;
    std::map<std::tuple<std::string, bool,
                 std::vector<std::pair<std::int64_t, std::int64_t>>>,
        fsim_vpi_handle_v1>
        declared_typespecs_;
    std::map<fsim_vpi_handle_v1,
        std::shared_ptr<const runtime::SystemVerilogVpiTypespec>>
        derived_typespecs_;
    // vpi_put_delays tables awaiting the next scheduler safe point, and the
    // safe-point hook that applies them.
    // A path's delay table and pulse tables a put waits to apply.
    struct PathTimingUpdate {
        std::vector<runtime::SimulationTick> delays;
        std::vector<runtime::SimulationTick> reject;
        std::vector<runtime::SimulationTick> error;
    };
    std::map<std::uint32_t, PathTimingUpdate> queued_path_delays_;
    std::map<std::uint32_t, std::vector<std::int64_t>> queued_check_limits_;
    // What the last vpi_put_delays gave each object, kept for the minimum,
    // typical and maximum values the simulation does not use.
    std::unordered_map<std::uint32_t, DelayRows> primitive_delay_rows_;
    std::unordered_map<std::uint32_t, DelayRows> path_delay_rows_;
    std::unordered_map<std::uint32_t, DelayRows> check_delay_rows_;
    // The twelve delays vpi_put_delays gave an SDF net delay, by its index.
    std::map<std::uint32_t, std::vector<runtime::SimulationTick>> net_delay_overrides_;
    std::uint64_t path_delay_hook_ { };
    std::size_t string_cursor_ { };
    std::array<std::vector<s_vpi_vecval>, 16> vectors_;
    std::size_t vector_cursor_ { };
    std::array<s_vpi_strengthval, 16> strengths_ { };
    std::size_t strength_cursor_ { };
    // Built on first use from the elaborated primitive records.
    mutable std::optional<PrimitiveIndex> primitive_index_;
    std::array<s_vpi_time, 16> times_ { };
    std::size_t time_cursor_ { };
    std::vector<std::string> argv_storage_;
    std::vector<char*> argv_;
    std::string product_ { "FSIM" };
    std::string version_ { fsim::version };
    // The message outlives later routines, such as the vpi_printf that
    // reports it.
    bool error_ { };
    std::string error_message_;
    // Inside a cbReadOnlySynch callback.
    bool read_only_ { };
    std::string error_code_ { "FSIM-VPI-001" };
    // The calls the VHPI companion's callbacks make (calls()), innermost
    // last.
    std::vector<std::unique_ptr<CallScope>> companion_calls_;
};

} // namespace

namespace {

class ElaborationModules final : public VpiElaborationModules {
public:
    [[nodiscard]] bool load(const std::span<const std::string> modules,
        diagnostic::Engine& diagnostics)
    {
        for (const auto& module : modules) {
            std::string error;
            auto library = load_module(parse_module(module), host_, error);
            if (!library) {
                diagnostics.error("FSIM-VPI-LOAD-001",
                    "cannot load VPI module '" + module + "': " + error);
                return false;
            }
            libraries_.push_back(std::move(library));
        }
        return true;
    }

    [[nodiscard]] std::span<const elaboration::UserSystemTask>
    tasks() const noexcept override
    {
        return host_.tasks;
    }

    // user_data names the unresolved system task or function
    // (IEEE 1800-2023 38.36.3).
    [[nodiscard]] bool resolve(const std::string& name) override
    {
        const auto before = host_.tasks.size();
        std::string text { name };
        const auto callbacks = host_.unresolved;
        for (const auto& registered : callbacks) {
            CallScope scope { host_ };
            if (!scope.entered()) {
                break;
            }
            auto data = registered;
            data.obj = nullptr;
            data.time = nullptr;
            data.value = nullptr;
            data.user_data = text.data();
            (void)data.cb_rtn(&data);
        }
        return std::ranges::any_of(
            std::span { host_.tasks }.subspan(before),
            [&](const auto& task) { return task.name == name; });
    }

private:
    RegistrationHost host_;
    std::vector<std::unique_ptr<platform::DynamicLibrary>> libraries_;
};

} // namespace

std::unique_ptr<VpiElaborationModules> register_vpi_user_system_tasks(
    const std::span<const std::string> modules,
    diagnostic::Engine& diagnostics)
{
    auto loaded = std::make_unique<ElaborationModules>();
    if (!loaded->load(modules, diagnostics)) {
        return nullptr;
    }
    return loaded;
}

std::unique_ptr<VpiForeignService> attach_vpi_modules(
    Simulation& simulation,
    const std::span<const std::string> modules,
    const std::size_t process_count,
    VpiForeignService::PrintSink print,
    const std::span<const std::string> arguments,
    DpiForeignService* const dpi,
    diagnostic::Engine& diagnostics,
    ForeignCompanion companion)
{
    auto service = std::make_unique<LiveService>(
        simulation, std::move(print), arguments);
    service->set_companion(std::move(companion));
    if (!service->load(modules, diagnostics)) {
        return nullptr;
    }
    auto* const live = service.get();
    if (dpi != nullptr) {
        // An import's C code on a helper thread may call the VPI too.
        dpi->set_thread_entry([live](const std::function<void()>& run) {
            CallScope scope { *live };
            run();
        });
    }
    simulation.set_systemverilog_dpi_function_hook(
        [live, dpi](const std::string_view identity,
            std::vector<PackedLogic4>& actuals,
            std::vector<std::string>& strings,
            const std::span<const std::string> names,
            const std::span<const std::uint8_t>) {
            if (identity.starts_with(
                    elaboration::user_system_call_identity_prefix)) {
                return live->call(identity, actuals, names);
            }
            if (dpi == nullptr) {
                throw std::runtime_error {
                    "SystemVerilog DPI function '" + std::string { identity }
                    + "' has no loaded implementation"
                };
            }
            // A context import may call the VPI (IEEE 1800-2023 35.5.3).
            if (DpiForeignService::context_import(identity)) {
                CallScope scope { *live };
                return dpi->call(identity, actuals, strings);
            }
            return dpi->call(identity, actuals, strings);
        });
    simulation.add_lifecycle_hook(
        [live](const Simulation::LifecycleEvent event,
            const std::string_view location) {
            live->lifecycle(event, location);
        });
    service->compile(process_count);
    return service;
}

} // namespace fsim::app::application_detail
