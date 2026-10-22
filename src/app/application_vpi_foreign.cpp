// SPDX-License-Identifier: Apache-2.0
#include "application_vpi_foreign.hpp"

#include "application_dpi_foreign.hpp"

#include "application_internal.hpp"
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/vpi_bridge.h"
#include "fsim/runtime/vpi_system.hpp"

#include "sv_vpi_user.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <utility>

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
};

class CallScope final {
public:
    explicit CallScope(Host& host)
        : context_ {
            FSIM_VPI_CONTEXT_ABI_VERSION,
            static_cast<std::uint32_t>(sizeof(fsim_vpi_call_context_v1)),
            &host,
            &Host::entry,
        }
    {
        entered_ = fsim_vpi_call_context_enter_v1(&context_) == 0;
    }
    ~CallScope()
    {
        if (entered_) {
            (void)fsim_vpi_call_context_leave_v1(&context_);
        }
    }
    CallScope(const CallScope&) = delete;
    CallScope& operator=(const CallScope&) = delete;

    [[nodiscard]] bool entered() const noexcept { return entered_; }

private:
    fsim_vpi_call_context_v1 context_;
    bool entered_ { };
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
    if (!std::filesystem::exists(spec.path, error)) {
        for (const auto* extension : { ".vpi", ".so" }) {
            auto candidate = spec.path;
            candidate += extension;
            if (std::filesystem::exists(candidate, error)) {
                spec.path = std::move(candidate);
                break;
            }
        }
    }
    if (std::filesystem::exists(spec.path, error)) {
        spec.path = std::filesystem::absolute(spec.path, error);
    }
    return spec;
}

// Run a module's startup routines (vlog_startup_routines, IEEE 1800-2023
// 36.9.1) or its named bootstrap function inside the host's call context.
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
    auto* const table = library->symbol("vlog_startup_routines", error);
    if (table == nullptr) {
        return nullptr;
    }
    auto* const routines = static_cast<void (**)()>(table);
    for (std::size_t index = 0U;
         index < maximum_startup_routines && routines[index] != nullptr;
         ++index) {
        routines[index]();
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
        case Routine::RegisterCallback:
            return succeed(result, tagged(Tag::callback, ++callbacks_));
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
    std::string version_ { "3" };
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
        // The simulation may outlive the service; no foreign routine may
        // run after it is gone.
        for (const auto& [serial, callback] : callbacks_) {
            if (callback->managed) {
                (void)simulation_.systemverilog_vpi_callbacks()
                    .remove_callback(*callback->managed);
            }
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
        for (runtime::simir::ProcessId process = 0U; process < process_count;
             ++process) {
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

    fsim_vpi_status_v1 invoke(const Routine routine,
        const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result) override
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
            if (status != FSIM_VPI_STATUS_OK) {
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
        case Routine::Iterate:
            return iterate(request, result);
        case Routine::Scan:
            return scan(request, result);
        case Routine::GetProperty:
        case Routine::GetProperty64:
            return property(request, result);
        case Routine::GetStringProperty:
            return string_property(request, result);
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
        case Routine::Control:
        case Routine::VariableControl:
            return control(request, result);
        case Routine::Print:
        case Routine::VariablePrint:
            return print(request, result);
        case Routine::McdPrint:
        case Routine::McdVariablePrint:
            if ((request.argument & 1U) == 0U) {
                return succeed(result);
            }
            return print(request, result);
        case Routine::Flush:
        case Routine::McdFlush:
            (void)std::fflush(stdout);
            return succeed(result);
        case Routine::McdName:
            if (request.argument == 1U) {
                return succeed(result, 0U, 0U, address(store("stdout")));
            }
            return FSIM_VPI_STATUS_NOT_FOUND;
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
    struct Callback {
        s_cb_data data { };
        s_vpi_time time { };
        s_vpi_value value { };
        bool has_time { };
        bool has_value { };
        std::optional<runtime::SystemVerilogVpiCallbackHandle> managed;
    };
    struct Iterator {
        std::vector<fsim_vpi_handle_v1> objects;
        std::size_t cursor { };
    };
    // A bit-select or part-select of a vector (IEEE 1800-2023 37.18):
    // `width` bits of the parent starting `offset` bits above its LSB.
    struct Select {
        fsim_vpi_handle_v1 parent { };
        std::uint32_t offset { };
        std::uint32_t width { 1U };
        std::int64_t left { };
        std::int64_t right { };
        bool bit { };
    };
    // A constant the object model reaches through a relation, such as a
    // range bound or an index.
    struct Constant {
        Value value;
        PLI_INT32 const_type { vpiDecConst };
    };
    // One dimension of an array or vector (IEEE 1800-2023 37.22 vpiRange).
    struct Range {
        fsim_vpi_handle_v1 parent { };
        std::int64_t left { };
        std::int64_t right { };
    };

    // --- selects and constants ----------------------------------------------

    // The declared range of a vector, or the unpacked range of an array,
    // left bound first.
    [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>> declared_range(
        const fsim_vpi_handle_v1 handle) const
    {
        const auto object = info(handle);
        if (!object || !object->type) {
            return std::nullopt;
        }
        const auto& descriptor = object->type->descriptor;
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

    // Every dimension of an object, leftmost first: an array's unpacked
    // dimensions, or a vector's packed ones (IEEE 1800-2023 37.17).
    [[nodiscard]] std::vector<std::pair<std::int64_t, std::int64_t>>
    dimension_ranges(const fsim_vpi_handle_v1 handle) const
    {
        std::vector<std::pair<std::int64_t, std::int64_t>> result;
        const auto object = info(handle);
        if (!object || !object->type) {
            return result;
        }
        if (object->kind == SystemVerilogVpiObjectKind::Memory) {
            const auto* level = object->type->descriptor.get();
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
            static_cast<std::uint32_t>(*high - *low + 1), left, right, bit };
        for (std::size_t index = 0U; index < selects_.size(); ++index) {
            const auto& known = selects_[index];
            if (known.parent == parent && known.offset == select.offset
                && known.width == select.width && known.bit == bit) {
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

    // The index of an array word, from the name the registry gives it.
    [[nodiscard]] std::optional<std::int64_t> word_index(
        const runtime::SystemVerilogVpiObjectInfo& word) const
    {
        const auto parent = info(word.parent);
        if (!parent || parent->kind != SystemVerilogVpiObjectKind::Memory) {
            return std::nullopt;
        }
        const auto open = word.full_name.rfind('[');
        if (open == std::string::npos || word.full_name.back() != ']') {
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
        const auto systf = systf_by_name_.find(site.name);
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
            const auto candidate = prefix.empty() ? name : prefix + "." + name;
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
            || data->reason == cbUnresolvedSystf;
        if (!action_reason && data->time != nullptr) {
            stored->time = *data->time;
            stored->has_time = true;
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
        case cbValueChange:
            if (object == 0U || owned(object)) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            kind = runtime::SystemVerilogVpiCallbackKind::ValueChange;
            break;
        case cbAfterDelay:
            if (!stored->has_time) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            kind = runtime::SystemVerilogVpiCallbackKind::AfterDelay;
            delay = delay_of(stored->time, object);
            break;
        // The time of these callbacks names a time queue, not a delay
        // (IEEE 1800-2023 38.36.2); cbAtEndOfSimTime runs after the
        // nonblocking assignments, before the read-only phase.
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
                : runtime::SystemVerilogVpiCallbackKind::ReadWrite;
            break;
        }
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
                registration.object = object;
            }
            if ((*kind == runtime::SystemVerilogVpiCallbackKind::ReadWrite
                    || *kind == runtime::SystemVerilogVpiCallbackKind::ReadOnly)
                && data->reason != cbAtEndOfSimTime && stored->has_time
                && (stored->time.high != 0 || stored->time.low != 0
                    || stored->time.real != 0.0)) {
                delay = delay_of(stored->time, object);
            }
            registration.delay = delay;
            registration.at_slot_start = data->reason == cbAfterDelay
                || data->reason == cbAtStartOfSimTime;
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
            return FSIM_VPI_STATUS_NOT_FOUND;
        }
        if (found->second->managed) {
            (void)simulation_.systemverilog_vpi_callbacks().remove_callback(
                *found->second->managed);
        }
        std::erase(end_of_compile_, found->first);
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
        const std::optional<runtime::SystemVerilogVpiCallbackEvent>& event)
    {
        const auto found = callbacks_.find(serial);
        if (found == callbacks_.end()) {
            return;
        }
        const auto& stored = *found->second;
        auto data = stored.data;
        auto time = stored.time;
        auto value = stored.value;
        data.time = nullptr;
        data.value = nullptr;
        if (stored.has_time) {
            fill_time(time, internal(stored.data.obj));
            data.time = &time;
        }
        if (event && stored.has_value
            && event->kind == runtime::SystemVerilogVpiCallbackKind::ValueChange
            && event->object) {
            if (const auto read = object_value(*event->object)) {
                (void)write_value(*read, value);
            }
            data.value = &value;
        }
        const auto one_shot = !event
            || event->kind != runtime::SystemVerilogVpiCallbackKind::ValueChange;
        const auto routine = data.cb_rtn;
        {
            CallScope scope { *this };
            routine(&data);
        }
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
            return vpiModule;
        case SystemVerilogVpiObjectKind::Interface:
            return vpiInterface;
        case SystemVerilogVpiObjectKind::Program:
            return vpiProgram;
        case SystemVerilogVpiObjectKind::Package:
            return vpiPackage;
        case SystemVerilogVpiObjectKind::GenerateScope:
            return vpiGenScope;
        case SystemVerilogVpiObjectKind::Begin:
            return vpiNamedBegin;
        case SystemVerilogVpiObjectKind::Fork:
            return vpiNamedFork;
        case SystemVerilogVpiObjectKind::Task:
            return vpiTask;
        case SystemVerilogVpiObjectKind::Function:
            return vpiFunction;
        case SystemVerilogVpiObjectKind::Net:
            return vpiNet;
        case SystemVerilogVpiObjectKind::Port:
            return object.type
                    && object.type->net_kind
                        != runtime::SystemVerilogVpiNetKind::None
                ? vpiNet
                : variable_type();
        case SystemVerilogVpiObjectKind::Variable:
            return variable_type();
        case SystemVerilogVpiObjectKind::Parameter:
            return vpiParameter;
        // An unpacked array is a vpiRegArray, and its words are variables
        // of their own type; vpiMemory and vpiMemoryWord remain only as
        // iteration relations (IEEE 1800-2023 37.17 detail 4, 37.20).
        case SystemVerilogVpiObjectKind::Memory:
            return vpiRegArray;
        case SystemVerilogVpiObjectKind::NamedEvent:
            return vpiNamedEvent;
        case SystemVerilogVpiObjectKind::Constant:
            return vpiConstant;
        default:
            return vpiUndefined;
        }
    }

    [[nodiscard]] PLI_INT32 handle_type(const fsim_vpi_handle_v1 handle) const
    {
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
        if (const auto* select = select_of(handle)) {
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
        if (owned(handle)) {
            return tag_of(handle) == Tag::iterator ? vpiIterator : vpiUndefined;
        }
        const auto object = info(handle);
        return object ? object_type(*object) : vpiUndefined;
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
        case vpiMemory:
        case vpiRegArray:
            return object.kind == SystemVerilogVpiObjectKind::Memory;
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
                || actual == vpiBitVar || actual == vpiStringVar;
        case vpiReg:
            return actual == vpiReg;
        case vpiNet:
            return actual == vpiNet;
        case vpiPort:
            return object.kind == SystemVerilogVpiObjectKind::Port;
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
        // The dimensions of an array or vector (IEEE 1800-2023 37.17).
        if (type == vpiRange) {
            if (reference != 0U && !owned(reference)) {
                for (const auto& [left, right] : dimension_ranges(reference)) {
                    objects.push_back(make_range(reference, left, right));
                }
            }
            return succeed(result, make_iterator(std::move(objects)));
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
            if (!object || object->name.starts_with('$')
                || object->name.starts_with('@')) {
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
            const auto found = registry_.find(site->scope);
            scope = found && found.value ? found.value->handle : 0U;
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
                const auto scope = registry_.find(site->scope);
                const auto handle
                    = scope && scope.value ? scope.value->handle : 0U;
                return succeed(result,
                    type == vpiModule ? enclosing_module(handle) : handle);
            }
            if (type == vpiUserSystf) {
                return succeed(result, tagged(Tag::systf, site->systf));
            }
            return succeed(result);
        }
        const auto reference = resolve(request.handle);
        if (const auto* range = range_of(reference)) {
            const auto bounds = *range;
            switch (type) {
            case vpiParent:
                return succeed(result, bounds.parent);
            case vpiLeftRange:
                return succeed(result, make_constant(bounds.left));
            case vpiRightRange:
                return succeed(result, make_constant(bounds.right));
            default:
                return succeed(result);
            }
        }
        if (const auto* select = select_of(reference)) {
            switch (type) {
            case vpiParent:
                return succeed(result, select->parent);
            case vpiIndex:
                return succeed(result,
                    select->bit ? make_constant(select->left) : 0U);
            case vpiLeftRange:
                return succeed(result,
                    select->bit ? 0U : make_constant(select->left));
            case vpiRightRange:
                return succeed(result,
                    select->bit ? 0U : make_constant(select->right));
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
            const auto range = declared_range(reference);
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

    fsim_vpi_status_v1 handle_by_name(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        if (request.text == nullptr) {
            return succeed(result);
        }
        const std::string name { request.text, request.text_size };
        if (request.handle == 0U) {
            const auto found = registry_.find(name);
            return succeed(result, found && found.value ? found.value->handle : 0U);
        }
        const auto scope = info(resolve(request.handle));
        if (!scope) {
            return succeed(result);
        }
        auto found = registry_.find(scope->full_name + "." + name);
        if (!found || !found.value) {
            found = registry_.find(name);
        }
        return succeed(result, found && found.value ? found.value->handle : 0U);
    }

    fsim_vpi_status_v1 handle_by_index(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto object = info(resolve(request.handle));
        if (!object) {
            return succeed(result);
        }
        const auto index = static_cast<PLI_INT32>(request.argument);
        const auto found = registry_.find(
            object->full_name + "[" + std::to_string(index) + "]");
        if (found && found.value) {
            return succeed(result, found.value->handle);
        }
        // A vector's index names a bit-select (IEEE 1800-2023 37.18).
        return succeed(result,
            make_select(resolve(request.handle), index, index, true));
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

    fsim_vpi_status_v1 property(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        const auto property = static_cast<PLI_INT32>(request.argument);
        const auto undefined = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(vpiUndefined));
        const auto integer = [&](const std::int64_t value) {
            return succeed(result, 0U, static_cast<std::uint64_t>(value));
        };
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
        if (const auto* select = select_of(resolve(request.handle))) {
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
        const auto width = object->type ? object->type->width : 0U;
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
            return integer(width);
        case vpiSigned:
            return integer(object->type && object->type->is_signed ? 1 : 0);
        case vpiScalar:
            return integer(width == 1U ? 1 : 0);
        case vpiVector:
            return integer(width > 1U ? 1 : 0);
        case vpiLineNo:
            return integer(object->source ? object->source->line : 0);
        case vpiTopModule:
            return integer(object->parent == 0U ? 1 : 0);
        case vpiCellInstance:
            return integer(object->type && object->type->is_cell ? 1 : 0);
        case vpiAutomatic:
            return integer(object->type
                        && object->type->lifetime
                            == runtime::SystemVerilogVpiLifetime::Automatic
                    ? 1
                    : 0);
        case vpiDirection:
            if (!object->type) {
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
                ? integer(static_cast<std::int64_t>(object->type->net_kind))
                : succeed(result, 0U, undefined);
        case vpiArray:
            return integer(object->kind == SystemVerilogVpiObjectKind::Memory
                    ? 1
                    : 0);
        default:
            return succeed(result, 0U, undefined);
        }
    }

    [[nodiscard]] static const char* type_name(const PLI_INT32 type)
    {
        switch (type) {
        case vpiModule:
            return "vpiModule";
        case vpiNet:
            return "vpiNet";
        case vpiReg:
            return "vpiReg";
        case vpiIntegerVar:
            return "vpiIntegerVar";
        case vpiRealVar:
            return "vpiRealVar";
        case vpiTimeVar:
            return "vpiTimeVar";
        case vpiParameter:
            return "vpiParameter";
        case vpiMemory:
            return "vpiMemory";
        case vpiMemoryWord:
            return "vpiMemoryWord";
        case vpiRegArray:
            return "vpiRegArray";
        case vpiRange:
            return "vpiRange";
        case vpiNamedEvent:
            return "vpiNamedEvent";
        case vpiNamedBegin:
            return "vpiNamedBegin";
        case vpiNamedFork:
            return "vpiNamedFork";
        case vpiGenScope:
            return "vpiGenScope";
        case vpiTask:
            return "vpiTask";
        case vpiFunction:
            return "vpiFunction";
        case vpiConstant:
            return "vpiConstant";
        case vpiOperation:
            return "vpiOperation";
        case vpiSysTaskCall:
            return "vpiSysTaskCall";
        case vpiSysFuncCall:
            return "vpiSysFuncCall";
        case vpiPort:
            return "vpiPort";
        case vpiIterator:
            return "vpiIterator";
        case vpiIntVar:
            return "vpiIntVar";
        case vpiShortIntVar:
            return "vpiShortIntVar";
        case vpiLongIntVar:
            return "vpiLongIntVar";
        case vpiByteVar:
            return "vpiByteVar";
        case vpiBitVar:
            return "vpiBitVar";
        case vpiStringVar:
            return "vpiStringVar";
        case vpiEnumVar:
            return "vpiEnumVar";
        case vpiStructVar:
            return "vpiStructVar";
        case vpiPackage:
            return "vpiPackage";
        case vpiInterface:
            return "vpiInterface";
        case vpiProgram:
            return "vpiProgram";
        case vpiNetBit:
            return "vpiNetBit";
        case vpiRegBit:
            return "vpiRegBit";
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
        if (property == vpiType) {
            const auto* const name = type_name(handle_type(request.handle));
            return name == nullptr ? FSIM_VPI_STATUS_NOT_FOUND : text(name);
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
        if (const auto* select = select_of(resolve(request.handle))) {
            const auto parent = info(select->parent);
            if (!parent || (property != vpiName && property != vpiFullName)) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            const auto suffix = select->bit
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
        switch (property) {
        case vpiName:
            // An array word is named by its array and index
            // (IEEE 1800-2023 37.19).
            if (object->name.starts_with('[')) {
                if (const auto parent = info(object->parent)) {
                    return text(parent->name + object->name);
                }
            }
            if (object->kind == SystemVerilogVpiObjectKind::Package) {
                return text(package_name(object->name));
            }
            return text(object->name);
        case vpiFullName:
            return text(presented_full_name(*object));
        case vpiFile:
            return object->source ? text(object->source->file)
                                  : FSIM_VPI_STATUS_NOT_FOUND;
        case vpiDefName: {
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

    // --- values -------------------------------------------------------------

    [[nodiscard]] std::optional<Value> object_value(
        const fsim_vpi_handle_v1 handle) const
    {
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
        const auto value = object_value(request.handle);
        if (!value || !write_value(*value, *out)) {
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
        // A system function's calltf returns its value by writing the call
        // (IEEE 1800-2023 38.35).
        if (owned(request.handle) && tag_of(request.handle) == Tag::call) {
            if (serial_of(request.handle) >= calls_.size()
                || arguments->value == nullptr) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            auto& site = calls_[serial_of(request.handle)];
            const auto value = input_value(*arguments->value, site.width);
            if (!value || !site.function) {
                return FSIM_VPI_STATUS_NOT_FOUND;
            }
            site.result.kind = Value::Kind::bits;
            site.result.bits = value_bits(*value, site.width);
            return succeed(result);
        }
        auto object = resolve(request.handle);
        const auto requested = object;
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
        const auto scheduled = values.schedule(object,
            runtime::SystemVerilogVpiWriteKind::Deposit, std::move(*stored),
            delay.ticks,
            mode == vpiInertialDelay
                ? runtime::SystemVerilogVpiDelayPolicy::Inertial
                : runtime::SystemVerilogVpiDelayPolicy::Transport);
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
        if (operation != vpiFinish && operation != vpiStop) {
            return FSIM_VPI_STATUS_UNSUPPORTED;
        }
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

    fsim_vpi_status_v1 print(const fsim_vpi_service_request_v1& request,
        fsim_vpi_service_result_v1& result)
    {
        if (request.text == nullptr) {
            return succeed(result);
        }
        auto* const arguments = pointer<va_list>(request.user_data);
        std::string format { request.text, request.text_size };
        std::string text;
        if (arguments == nullptr) {
            text = format;
        } else {
            va_list measure;
            va_copy(measure, *arguments);
            const auto size = std::vsnprintf(nullptr, 0U, format.c_str(), measure);
            va_end(measure);
            if (size < 0) {
                return succeed(result);
            }
            text.resize(std::min<std::size_t>(
                static_cast<std::size_t>(size), maximum_printed_bytes));
            va_list copy;
            va_copy(copy, *arguments);
            (void)std::vsnprintf(text.data(), text.size() + 1U, format.c_str(), copy);
            va_end(copy);
        }
        print_(text);
        return succeed(result, 0U, text.size());
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
    std::uint64_t next_iterator_ { };
    std::unordered_map<std::uint64_t, Iterator> iterators_;
    std::unordered_map<fsim_vpi_handle_v1, void*> user_data_;
    std::array<std::string, 16> strings_;
    std::vector<std::byte> array_buffer_;
    std::vector<Select> selects_;
    std::vector<Constant> constants_;
    std::vector<Range> ranges_;
    std::size_t string_cursor_ { };
    std::array<std::vector<s_vpi_vecval>, 16> vectors_;
    std::size_t vector_cursor_ { };
    std::array<s_vpi_time, 16> times_ { };
    std::size_t time_cursor_ { };
    std::vector<std::string> argv_storage_;
    std::vector<char*> argv_;
    std::string product_ { "FSIM" };
    std::string version_ { "3" };
    // The message outlives later routines, such as the vpi_printf that
    // reports it.
    bool error_ { };
    std::string error_message_;
    std::string error_code_ { "FSIM-VPI-001" };
};

} // namespace

std::optional<std::vector<elaboration::UserSystemTask>>
register_vpi_user_system_tasks(
    const std::span<const std::string> modules,
    diagnostic::Engine& diagnostics)
{
    RegistrationHost host;
    std::vector<std::unique_ptr<platform::DynamicLibrary>> libraries;
    for (const auto& module : modules) {
        std::string error;
        auto library = load_module(parse_module(module), host, error);
        if (!library) {
            diagnostics.error("FSIM-VPI-LOAD-001",
                "cannot load VPI module '" + module + "': " + error);
            return std::nullopt;
        }
        libraries.push_back(std::move(library));
    }
    return std::move(host.tasks);
}

std::unique_ptr<VpiForeignService> attach_vpi_modules(
    Simulation& simulation,
    const std::span<const std::string> modules,
    const std::size_t process_count,
    VpiForeignService::PrintSink print,
    const std::span<const std::string> arguments,
    DpiForeignService* const dpi,
    diagnostic::Engine& diagnostics)
{
    auto service = std::make_unique<LiveService>(
        simulation, std::move(print), arguments);
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
    service->compile(process_count);
    return service;
}

} // namespace fsim::app::application_detail
