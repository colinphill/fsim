// SPDX-License-Identifier: Apache-2.0
#include "application_dpi_foreign.hpp"

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/svdpi_bridge.h"

#include "vpi_user.h"

#if defined(FSIM_HAS_LIBFFI)
#include <ffi.h>
#endif

#include <algorithm>
#include <bit>
#include <charconv>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace fsim::app::application_detail {

namespace {

using runtime::Logic4;
using runtime::PackedLogic4;

// One formal or the result of a C signature: `&` marks a scalar passed by
// pointer; B and V vectors always pass by pointer (IEEE 1800-2023 H.8). An
// open array `A` passes an svOpenArrayHandle (H.12) for elements of type
// `element`, `width` bits each, over the unpacked `bounds` (left, right per
// dimension); `&` then marks an output or inout.
struct Parameter {
    char code { };
    bool reference { };
    std::size_t width { };
    char element { };
    std::vector<std::int32_t> bounds;
};

struct Signature {
    std::string linkage;
    std::vector<Parameter> parameters;
    char result { };
    bool context { };
};

[[nodiscard]] std::size_t code_width(const char code)
{
    switch (code) {
    case 'c':
    case 'C':
        return 8U;
    case 's':
    case 'S':
        return 16U;
    case 'i':
    case 'I':
    case 'f':
    case 'T':
        return 32U;
    case 'l':
    case 'L':
    case 'd':
    case 'p':
        return 64U;
    case 'b':
    case 'g':
    case 'v':
        return 1U;
    default:
        return 0U;
    }
}

[[nodiscard]] Signature parse_signature(const std::string_view identity)
{
    const auto hash = identity.find('#');
    const auto colon = hash == std::string_view::npos
        ? std::string_view::npos
        : identity.find(':', hash);
    if (colon == std::string_view::npos || colon + 1U >= identity.size()) {
        throw std::runtime_error { "malformed DPI import identity '"
            + std::string { identity } + "'" };
    }
    Signature signature;
    signature.linkage = std::string { identity.substr(0U, hash) };
    signature.result = identity[colon + 1U];
    signature.context = identity.substr(colon + 2U).starts_with("@c");
    auto arguments = identity.substr(hash + 1U, colon - hash - 1U);
    while (!arguments.empty()) {
        const auto comma = arguments.find(',');
        auto text = arguments.substr(0U, comma);
        arguments = comma == std::string_view::npos
            ? std::string_view { }
            : arguments.substr(comma + 1U);
        Parameter parameter;
        if (text.starts_with('&')) {
            parameter.reference = true;
            text.remove_prefix(1U);
        }
        if (text.empty()) {
            throw std::runtime_error { "malformed DPI import identity '"
                + std::string { identity } + "'" };
        }
        parameter.code = text.front();
        if (parameter.code == 'A') {
            if (text.size() < 2U) {
                throw std::runtime_error { "malformed DPI import identity '"
                    + std::string { identity } + "'" };
            }
            parameter.element = text[1U];
            auto rest = text.substr(2U);
            const auto slash = rest.find('/');
            const auto width_text = rest.substr(0U, slash);
            parameter.width = width_text.empty()
                ? code_width(parameter.element)
                : static_cast<std::size_t>(
                      std::stoull(std::string { width_text }));
            rest = slash == std::string_view::npos ? std::string_view { }
                                                   : rest.substr(slash + 1U);
            while (!rest.empty()) {
                const auto next = rest.find('/');
                parameter.bounds.push_back(static_cast<std::int32_t>(
                    std::stol(std::string { rest.substr(0U, next) })));
                rest = next == std::string_view::npos
                    ? std::string_view { }
                    : rest.substr(next + 1U);
            }
            if (parameter.width == 0U || parameter.bounds.empty()
                || parameter.bounds.size() % 2U != 0U) {
                throw std::runtime_error { "malformed DPI import identity '"
                    + std::string { identity } + "'" };
            }
        } else if (parameter.code == 'B' || parameter.code == 'V') {
            parameter.width = static_cast<std::size_t>(
                std::stoull(std::string { text.substr(1U) }));
        } else {
            parameter.width = code_width(parameter.code);
        }
        signature.parameters.push_back(parameter);
    }
    return signature;
}

// Two-state bits of a value; x and z read as 0 (IEEE 1800-2023 H.7.2).
[[nodiscard]] std::uint64_t known_bits(const PackedLogic4& value)
{
    std::uint64_t bits { };
    const auto width = std::min<std::size_t>(value.width(), 64U);
    for (std::size_t index = 0U; index < width; ++index) {
        if (value.get(index) == Logic4::one) {
            bits |= std::uint64_t { 1 } << index;
        }
    }
    return bits;
}

[[nodiscard]] PackedLogic4 from_bits(
    const std::uint64_t bits, const std::size_t width)
{
    PackedLogic4 value(width, Logic4::zero);
    for (std::size_t index = 0U; index < width && index < 64U; ++index) {
        if (((bits >> index) & 1U) != 0U) {
            value.set(index, Logic4::one);
        }
    }
    return value;
}

// svLogic and svLogicVecVal encode by meaning, not by fsim's state order:
// sv_0 0, sv_1 1, sv_z 2, sv_x 3; aval/bval 00 0, 10 1, 01 z, 11 x
// (IEEE 1800-2023 H.10.1.1).
[[nodiscard]] std::uint8_t sv_logic(const Logic4 state)
{
    switch (state) {
    case Logic4::zero:
        return 0U;
    case Logic4::one:
        return 1U;
    case Logic4::z:
        return 2U;
    default:
        return 3U;
    }
}

[[nodiscard]] Logic4 from_sv_logic(const std::uint8_t value)
{
    switch (value & 3U) {
    case 0U:
        return Logic4::zero;
    case 1U:
        return Logic4::one;
    case 2U:
        return Logic4::z;
    default:
        return Logic4::x;
    }
}

// The caller of a context import: `|<instance>|<file>|<line>` after the
// signature.
struct Caller {
    std::string_view instance;
    std::string_view file;
    int line { };
};

[[nodiscard]] std::optional<Caller> caller_of(const std::string_view identity)
{
    const auto bar = identity.find('|');
    if (bar == std::string_view::npos) {
        return std::nullopt;
    }
    auto rest = identity.substr(bar + 1U);
    const auto first = rest.find('|');
    const auto last = rest.rfind('|');
    if (first == std::string_view::npos || last == first) {
        return std::nullopt;
    }
    Caller caller;
    caller.instance = rest.substr(0U, first);
    caller.file = rest.substr(first + 1U, last - first - 1U);
    const auto line = rest.substr(last + 1U);
    (void)std::from_chars(line.data(), line.data() + line.size(), caller.line);
    return caller;
}

struct LogicWord {
    std::uint32_t aval { };
    std::uint32_t bval { };
};

#if defined(FSIM_HAS_LIBFFI)

[[nodiscard]] ffi_type* scalar_type(const char code)
{
    switch (code) {
    case 'c':
        return &ffi_type_sint8;
    case 'C':
    case 'b':
    case 'g':
        return &ffi_type_uint8;
    case 's':
        return &ffi_type_sint16;
    case 'S':
        return &ffi_type_uint16;
    case 'i':
    case 'T':
        return &ffi_type_sint32;
    case 'I':
        return &ffi_type_uint32;
    case 'l':
        return &ffi_type_sint64;
    case 'L':
        return &ffi_type_uint64;
    case 'd':
        return &ffi_type_double;
    case 'f':
        return &ffi_type_float;
    case 'v':
        return &ffi_type_void;
    default:
        return &ffi_type_pointer;
    }
}

// The C value of one actual; small scalars are held in `scalar`, strings
// in `text`, vectors in `bits` or `logic`.
struct Slot {
    union {
        std::int8_t i8;
        std::uint8_t u8;
        std::int16_t i16;
        std::uint16_t u16;
        std::int32_t i32;
        std::uint32_t u32;
        std::int64_t i64;
        std::uint64_t u64;
        double f64;
        float f32;
        void* pointer;
        const char* text;
    } scalar { };
    std::vector<std::uint32_t> bits;
    std::vector<LogicWord> logic;
    void* reference { };
    // An open array's elements and handle.
    std::vector<unsigned char> bytes;
    fsim_svdpi_open_array_v3 open { };
};

// The C size of one open-array element (IEEE 1800-2023 H.10).
[[nodiscard]] std::size_t element_bytes(const Parameter& parameter)
{
    const auto words = (parameter.width + 31U) / 32U;
    switch (parameter.element) {
    case 'B':
        return words * sizeof(std::uint32_t);
    case 'V':
        return words * sizeof(LogicWord);
    case 's':
    case 'S':
        return 2U;
    case 'i':
    case 'I':
        return 4U;
    case 'l':
    case 'L':
        return 8U;
    default:
        return 1U;
    }
}

[[nodiscard]] std::size_t open_elements(const Parameter& parameter)
{
    std::size_t count = 1U;
    for (std::size_t index = 0U; index + 1U < parameter.bounds.size();
         index += 2U) {
        const auto left = std::int64_t { parameter.bounds[index] };
        const auto right = std::int64_t { parameter.bounds[index + 1U] };
        count *= static_cast<std::size_t>(
            (left >= right ? left - right : right - left) + 1);
    }
    return count;
}

void store_actual(Slot& slot, const Parameter& parameter, const PackedLogic4& value,
    const std::string& text);
[[nodiscard]] PackedLogic4 load_actual(const Slot& slot, const Parameter& parameter);

// The elements of an open-array actual, leftmost element in the most
// significant bits of `value`, in their canonical C layout (H.12.1).
void store_open_array(Slot& slot, const Parameter& parameter,
    const PackedLogic4& value)
{
    const auto count = open_elements(parameter);
    const auto stride = element_bytes(parameter);
    const Parameter element { parameter.element, false, parameter.width, { }, { } };
    slot.bytes.assign(count * stride, 0U);
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const auto offset = (count - 1U - ordinal) * parameter.width;
        PackedLogic4 bits(parameter.width, Logic4::zero);
        for (std::size_t bit = 0U; bit < parameter.width; ++bit) {
            if (offset + bit < value.width()) {
                bits.set(bit, value.get(offset + bit));
            }
        }
        Slot item;
        store_actual(item, element, bits, { });
        auto* const target = slot.bytes.data() + ordinal * stride;
        if (parameter.element == 'B') {
            std::memcpy(target, item.bits.data(), stride);
        } else if (parameter.element == 'V') {
            std::memcpy(target, item.logic.data(), stride);
        } else {
            std::memcpy(target, &item.scalar, stride);
        }
    }
    slot.open.magic = FSIM_SVDPI_OPEN_ARRAY_MAGIC;
    slot.open.struct_size = static_cast<std::uint32_t>(sizeof(slot.open));
    slot.open.dimensions
        = static_cast<std::int32_t>(parameter.bounds.size() / 2U);
    const bool vector = parameter.element == 'B' || parameter.element == 'V';
    slot.open.packed_left
        = vector ? static_cast<std::int32_t>(parameter.width) - 1 : 0;
    slot.open.packed_right = 0;
    slot.open.element_code = static_cast<std::uint32_t>(parameter.element);
    slot.open.element_width = static_cast<std::uint32_t>(parameter.width);
    slot.open.element_bytes = static_cast<std::uint32_t>(stride);
    slot.open.data = slot.bytes.data();
    slot.open.bounds = parameter.bounds.data();
    slot.scalar.pointer = &slot.open;
}

[[nodiscard]] PackedLogic4 load_open_array(
    const Slot& slot, const Parameter& parameter)
{
    const auto count = open_elements(parameter);
    const auto stride = element_bytes(parameter);
    const Parameter element { parameter.element, false, parameter.width, { }, { } };
    PackedLogic4 value(count * parameter.width, Logic4::zero);
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const auto* const source = slot.bytes.data() + ordinal * stride;
        Slot item;
        if (parameter.element == 'B') {
            item.bits.resize(stride / sizeof(std::uint32_t));
            std::memcpy(item.bits.data(), source, stride);
        } else if (parameter.element == 'V') {
            item.logic.resize(stride / sizeof(LogicWord));
            std::memcpy(item.logic.data(), source, stride);
        } else {
            std::memcpy(&item.scalar, source, stride);
        }
        const auto bits = load_actual(item, element);
        const auto offset = (count - 1U - ordinal) * parameter.width;
        for (std::size_t bit = 0U; bit < parameter.width && bit < bits.width();
             ++bit) {
            value.set(offset + bit, bits.get(bit));
        }
    }
    return value;
}

void store_actual(Slot& slot, const Parameter& parameter, const PackedLogic4& value,
    const std::string& text)
{
    const auto raw = known_bits(value);
    switch (parameter.code) {
    case 'c':
    case 'C':
        slot.scalar.u8 = static_cast<std::uint8_t>(raw);
        break;
    case 's':
    case 'S':
        slot.scalar.u16 = static_cast<std::uint16_t>(raw);
        break;
    case 'i':
    case 'I':
        slot.scalar.u32 = static_cast<std::uint32_t>(raw);
        break;
    case 'l':
    case 'L':
    case 'p':
        slot.scalar.u64 = raw;
        break;
    case 'd':
        slot.scalar.f64 = std::bit_cast<double>(raw);
        break;
    case 'f':
        slot.scalar.f32 = std::bit_cast<float>(
            static_cast<std::uint32_t>(raw));
        break;
    case 'b':
        slot.scalar.u8 = static_cast<std::uint8_t>(raw & 1U);
        break;
    case 'g':
        slot.scalar.u8 = value.width() == 0U ? 3U : sv_logic(value.get(0U));
        break;
    case 'z':
        slot.scalar.text = text.c_str();
        break;
    case 'B': {
        slot.bits.assign((parameter.width + 31U) / 32U, 0U);
        for (std::size_t index = 0U;
             index < parameter.width && index < value.width(); ++index) {
            if (value.get(index) == Logic4::one) {
                slot.bits[index / 32U] |= std::uint32_t { 1 } << (index % 32U);
            }
        }
        slot.scalar.pointer = slot.bits.data();
        break;
    }
    case 'V': {
        slot.logic.assign((parameter.width + 31U) / 32U, LogicWord { });
        for (std::size_t index = 0U; index < parameter.width; ++index) {
            const auto code = sv_logic(
                index < value.width() ? value.get(index) : Logic4::zero);
            const auto mask = std::uint32_t { 1 } << (index % 32U);
            auto& word = slot.logic[index / 32U];
            if (code == 1U || code == 3U) {
                word.aval |= mask;
            }
            if (code >= 2U) {
                word.bval |= mask;
            }
        }
        slot.scalar.pointer = slot.logic.data();
        break;
    }
    case 'A':
        store_open_array(slot, parameter, value);
        break;
    default:
        throw std::runtime_error { "unsupported DPI argument type" };
    }
}

[[nodiscard]] PackedLogic4 load_actual(const Slot& slot, const Parameter& parameter)
{
    switch (parameter.code) {
    case 'c':
    case 'C':
        return from_bits(slot.scalar.u8, 8U);
    case 's':
    case 'S':
        return from_bits(slot.scalar.u16, 16U);
    case 'i':
    case 'I':
        return from_bits(slot.scalar.u32, 32U);
    case 'l':
    case 'L':
    case 'p':
        return from_bits(slot.scalar.u64, 64U);
    case 'd':
        return from_bits(std::bit_cast<std::uint64_t>(slot.scalar.f64), 64U);
    case 'f':
        return from_bits(std::bit_cast<std::uint32_t>(slot.scalar.f32), 32U);
    case 'b':
        return from_bits(slot.scalar.u8 & 1U, 1U);
    case 'g':
        return PackedLogic4(1U, from_sv_logic(slot.scalar.u8));
    case 'B': {
        PackedLogic4 value(parameter.width, Logic4::zero);
        for (std::size_t index = 0U; index < parameter.width; ++index) {
            if (((slot.bits[index / 32U] >> (index % 32U)) & 1U) != 0U) {
                value.set(index, Logic4::one);
            }
        }
        return value;
    }
    case 'V': {
        PackedLogic4 value(parameter.width, Logic4::zero);
        for (std::size_t index = 0U; index < parameter.width; ++index) {
            const auto& word = slot.logic[index / 32U];
            const auto shift = index % 32U;
            const auto aval = (word.aval >> shift) & 1U;
            const auto bval = (word.bval >> shift) & 1U;
            value.set(index, from_sv_logic(static_cast<std::uint8_t>(
                                 bval == 0U ? aval : (aval == 0U ? 2U : 3U))));
        }
        return value;
    }
    case 'A':
        return load_open_array(slot, parameter);
    default:
        return PackedLogic4 { };
    }
}

struct Import {
    Signature signature;
    void (*function)() { };
    ffi_cif cif { };
    std::vector<ffi_type*> types;
};

// One instance scope of context imports (IEEE 1800-2023 H.9.3); svScope
// is the record's address.
struct Scope {
    std::string name;
    std::unordered_map<void*, void*> user_data;
};

class Service final : public DpiForeignService {
public:
    Service()
        : context_ {
            FSIM_SVDPI_CONTEXT_ABI_VERSION,
            static_cast<std::uint32_t>(sizeof(fsim_svdpi_call_context_v3)),
            this,
            &Service::get_scope,
            &Service::set_scope,
            &Service::scope_name,
            &Service::find_scope,
            &Service::put_user_data,
            &Service::get_user_data,
            &Service::caller_info,
            &Service::disabled,
            &Service::acknowledge_disabled,
            &Service::time,
            &Service::time_scale,
            &Service::time_scale,
        }
    {
        fsim_svdpi_set_export_dispatcher_v3(&Service::export_call, this);
    }

    ~Service() override
    {
        fsim_svdpi_set_export_dispatcher_v3(nullptr, nullptr);
        // The simulation ended inside an export (as by $finish): the C code
        // that called it never resumes.
        for (auto& flight : flights_) {
            flight->thread.detach();
            (void)flight.release();
        }
    }

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    void set_clock(Clock clock) override { clock_ = std::move(clock); }

    [[nodiscard]] bool load(const std::span<const std::string> libraries,
        diagnostic::Engine& diagnostics)
    {
        for (const auto& name : libraries) {
            std::filesystem::path path { name };
            std::error_code error_code;
            if (!std::filesystem::exists(path, error_code)) {
                const auto shared = std::filesystem::path { name + ".so" };
                if (std::filesystem::exists(shared, error_code)) {
                    path = shared;
                }
            }
            // dlopen searches the loader path for a bare name.
            if (std::filesystem::exists(path, error_code)) {
                path = std::filesystem::absolute(path, error_code);
            }
            std::string error;
            auto library = platform::DynamicLibrary::open(path, error, true);
            if (!library) {
                diagnostics.error("FSIM-DPI-LOAD-001",
                    "cannot load DPI library '" + name + "': " + error);
                return false;
            }
            libraries_.push_back(std::move(library));
        }
        return true;
    }

    void set_thread_entry(ThreadEntry entry) override
    {
        thread_entry_ = std::move(entry);
    }

    PackedLogic4 call(const std::string_view identity,
        std::vector<PackedLogic4>& actuals,
        std::vector<std::string>& strings) override
    {
        if (identity.starts_with("!x")) {
            start(identity.substr(2U), actuals, strings);
            return PackedLogic4(1U, Logic4::zero);
        }
        if (identity.starts_with("!f")) {
            return finish(identity.substr(2U), actuals, strings);
        }
        if (identity.starts_with(export_prefix)) {
            return serve_export(identity, actuals, strings);
        }
        auto& import = resolve(identity);
        Call call;
        prepare(import, call, actuals, strings);
        const auto caller = import.signature.context
            ? caller_of(identity)
            : std::nullopt;
        if (caller) {
            // A context import may call another; each restores its caller.
            const auto saved_scope = scope_;
            const auto saved_file = file_;
            const auto saved_line = line_;
            scope_ = &scope_named(caller->instance);
            file_ = std::string { caller->file };
            line_ = caller->line;
            invoke(import, call, true);
            scope_ = saved_scope;
            file_ = saved_file;
            line_ = saved_line;
        } else {
            invoke(import, call, false);
        }
        return complete(import, call, actuals, strings);
    }

private:
    static constexpr std::string_view export_prefix { "__fsim_dpi_export_" };

    // The C values of one call's actuals.
    struct Call {
        std::vector<Slot> slots;
        std::vector<void*> values;
        // Small integer results widen to ffi_arg.
        alignas(16) std::uint64_t result[2] { };
    };

    // A context import call whose C code runs on a helper thread, so that
    // the calling process can run the DPI exports that code calls (IEEE
    // 1800-2023 35.5.3). The two threads take turns.
    struct Flight {
        Import* import { };
        Call call;
        std::vector<std::string> strings;
        std::thread thread;
        std::mutex mutex;
        std::condition_variable turn;
        bool c_turn { true };
        bool done { };
        // The pending export call.
        std::string name;
        void** arguments { };
        void* result { };
        // Strings returned to C stay valid until the import returns.
        std::deque<std::string> returned;
        Scope* saved_scope { };
        std::string saved_file;
        int saved_line { };
    };

    void prepare(const Import& import, Call& call,
        const std::vector<PackedLogic4>& actuals,
        const std::vector<std::string>& strings)
    {
        const auto& parameters = import.signature.parameters;
        // A string result returns in one more string actual after the
        // formals (IEEE 1800-2023 H.7.4: const char*).
        const bool string_result = import.signature.result == 'z';
        if (actuals.size() != parameters.size() + (string_result ? 1U : 0U)
            || (string_result && strings.size() != actuals.size())) {
            throw std::runtime_error { "DPI import '"
                + import.signature.linkage + "' actual count mismatch" };
        }
        call.slots.resize(parameters.size());
        call.values.resize(parameters.size());
        for (std::size_t index = 0U; index < parameters.size(); ++index) {
            const auto& parameter = parameters[index];
            auto& slot = call.slots[index];
            store_actual(slot, parameter, actuals[index],
                index < strings.size() ? strings[index] : empty_);
            const bool vector = parameter.code == 'B' || parameter.code == 'V'
                || parameter.code == 'A';
            if (parameter.reference && !vector) {
                slot.reference = &slot.scalar;
                call.values[index] = &slot.reference;
            } else {
                call.values[index] = &slot.scalar;
            }
        }
    }

    void invoke(Import& import, Call& call, const bool context)
    {
        const bool entered = context
            && fsim_svdpi_call_context_enter_v3(&context_) == 0;
        ffi_call(&import.cif, import.function, call.result, call.values.data());
        if (entered) {
            (void)fsim_svdpi_call_context_leave_v3(&context_);
        }
    }

    PackedLogic4 complete(const Import& import, Call& call,
        std::vector<PackedLogic4>& actuals, std::vector<std::string>& strings)
    {
        const auto& parameters = import.signature.parameters;
        for (std::size_t index = 0U; index < parameters.size(); ++index) {
            const auto& parameter = parameters[index];
            const bool vector = parameter.code == 'B' || parameter.code == 'V';
            if ((!parameter.reference && !vector)
                || (parameter.code == 'A' && !parameter.reference)) {
                continue;
            }
            if (parameter.code == 'z') {
                if (index < strings.size()) {
                    const auto* const text = call.slots[index].scalar.text;
                    strings[index] = text == nullptr ? std::string { } : text;
                }
                continue;
            }
            actuals[index] = load_actual(call.slots[index], parameter);
        }
        Slot returned;
        const auto code = import.signature.result;
        if (code == 'z') {
            const char* text { };
            std::memcpy(&text, call.result, sizeof(text));
            strings.back() = text == nullptr ? std::string { } : text;
            return PackedLogic4(1U, Logic4::zero);
        }
        if (code == 'v' || code == 'T') {
            return PackedLogic4(1U, Logic4::zero);
        }
        const auto* const bytes
            = reinterpret_cast<const unsigned char*>(call.result);
        switch (code) {
        case 'd':
            std::memcpy(&returned.scalar.f64, bytes, sizeof(double));
            break;
        case 'f':
            std::memcpy(&returned.scalar.f32, bytes, sizeof(float));
            break;
        case 'c':
        case 's':
        case 'i':
        case 'l':
            returned.scalar.i64 = static_cast<std::int64_t>(
                *reinterpret_cast<const ffi_sarg*>(bytes));
            break;
        default:
            returned.scalar.u64 = static_cast<std::uint64_t>(
                *reinterpret_cast<const ffi_arg*>(bytes));
            break;
        }
        return load_actual(
            returned, Parameter { code, false, code_width(code), { }, { } });
    }

    // Start a context import whose C code may call exports: the caller
    // serves those calls until the code returns, then finishes the call.
    void start(const std::string_view identity,
        const std::vector<PackedLogic4>& actuals,
        const std::vector<std::string>& strings)
    {
        auto& import = resolve(identity);
        auto flight = std::make_unique<Flight>();
        auto& state = *flight;
        state.import = &import;
        state.strings = strings;
        prepare(import, state.call, actuals, state.strings);
        state.saved_scope = scope_;
        state.saved_file = file_;
        state.saved_line = line_;
        if (const auto caller = caller_of(identity)) {
            scope_ = &scope_named(caller->instance);
            file_ = std::string { caller->file };
            line_ = caller->line;
        }
        flights_.push_back(std::move(flight));
        state.thread = std::thread { [this, &state] {
            current_flight = &state;
            const auto run = [&] { invoke(*state.import, state.call, true); };
            if (thread_entry_) {
                thread_entry_(run);
            } else {
                run();
            }
            const std::lock_guard lock { state.mutex };
            state.done = true;
            state.c_turn = false;
            state.turn.notify_all();
        } };
    }

    PackedLogic4 finish(const std::string_view identity,
        std::vector<PackedLogic4>& actuals, std::vector<std::string>& strings)
    {
        if (flights_.empty()) {
            throw std::runtime_error { "DPI import '"
                + std::string { identity } + "' finished without a start" };
        }
        auto flight = std::move(flights_.back());
        flights_.pop_back();
        {
            std::unique_lock lock { flight->mutex };
            flight->turn.wait(lock, [&] { return !flight->c_turn; });
            if (!flight->done) {
                // The C code waits for an export nobody serves.
                flight->thread.detach();
                (void)flight.release();
                throw std::runtime_error { "DPI import '"
                    + std::string { identity }
                    + "' returned while its C code called an export" };
            }
        }
        flight->thread.join();
        scope_ = flight->saved_scope;
        file_ = flight->saved_file;
        line_ = flight->saved_line;
        return complete(*flight->import, flight->call, actuals, strings);
    }

    // The C bytes of one DPI value and where a slot holds them.
    [[nodiscard]] static std::size_t c_bytes(const Parameter& parameter)
    {
        const auto words = (parameter.width + 31U) / 32U;
        switch (parameter.code) {
        case 'B':
            return words * sizeof(std::uint32_t);
        case 'V':
            return words * sizeof(LogicWord);
        case 'z':
            return sizeof(const char*);
        case 'A':
            throw std::runtime_error {
                "an open array argument of a DPI export is not supported" };
        default:
            return code_width(parameter.code) / 8U == 0U
                ? 1U
                : code_width(parameter.code) / 8U;
        }
    }

    [[nodiscard]] static void* c_data(Slot& slot, const Parameter& parameter)
    {
        switch (parameter.code) {
        case 'B':
            return slot.bits.data();
        case 'V':
            return slot.logic.data();
        default:
            return &slot.scalar;
        }
    }

    // The hidden imports through which a unit's service runs the exports
    // that a context import's C code calls: `next` waits for an export
    // call (returning its 1-based position among the unit's exports) or
    // for the C code to return (0); `args_<mask>__<linkage>` delivers the
    // export's actuals; `return_<mask>__<linkage>` hands back its result
    // and outputs and lets the C code continue. The mask is `r` (a result)
    // or `v`, then per formal `i` (input), `o` (output) or `b` (inout).
    PackedLogic4 serve_export(const std::string_view identity,
        std::vector<PackedLogic4>& actuals, std::vector<std::string>& strings)
    {
        if (flights_.empty()) {
            throw std::runtime_error {
                "a DPI export service ran outside a context import" };
        }
        auto& flight = *flights_.back();
        const auto signature
            = parse_signature(identity.substr(0U, identity.find('|')));
        const auto name = std::string_view { signature.linkage }.substr(
            export_prefix.size());
        if (name == "next") {
            std::unique_lock lock { flight.mutex };
            flight.turn.wait(lock, [&] { return !flight.c_turn; });
            if (flight.done) {
                return from_bits(0U, 32U);
            }
            std::string_view names { strings.empty() ? empty_ : strings.front() };
            std::uint64_t ordinal { };
            while (!names.empty()) {
                ++ordinal;
                const auto space = names.find(' ');
                if (names.substr(0U, space) == flight.name) {
                    return from_bits(ordinal, 32U);
                }
                names = space == std::string_view::npos
                    ? std::string_view { }
                    : names.substr(space + 1U);
            }
            throw std::runtime_error { "DPI export '" + flight.name
                + "' is not exported by the scope of the calling import" };
        }
        Import hidden;
        hidden.signature = signature;
        Call call;
        prepare(hidden, call, actuals, strings);
        const auto& parameters = signature.parameters;
        // The directions of the export's formals follow the result mark.
        const auto prefix = name.substr(0U, name.find('_'));
        const auto mask = name.substr(prefix.size() + 1U,
            name.find("__") - prefix.size() - 1U);
        if (prefix == "args") {
            for (std::size_t index = 0U; index < parameters.size(); ++index) {
                if (index + 1U < mask.size() && mask[index + 1U] != 'o') {
                    std::memcpy(c_data(call.slots[index], parameters[index]),
                        flight.arguments[index], c_bytes(parameters[index]));
                }
            }
            (void)complete(hidden, call, actuals, strings);
            return PackedLogic4(1U, Logic4::zero);
        }
        if (prefix != "return") {
            throw std::runtime_error { "unknown DPI export service '"
                + std::string { name } + "'" };
        }
        const auto deliver = [&](void* const target, const std::size_t index) {
            const auto& parameter = parameters[index];
            if (parameter.code == 'z') {
                const auto* const text = flight.returned
                                             .emplace_back(strings[index])
                                             .c_str();
                std::memcpy(target, &text, sizeof(text));
                return;
            }
            std::memcpy(target, c_data(call.slots[index], parameter),
                c_bytes(parameter));
        };
        const std::size_t first = mask.starts_with('r') ? 1U : 0U;
        if (first == 1U && flight.result != nullptr && !parameters.empty()) {
            deliver(flight.result, 0U);
        }
        for (std::size_t index = 1U; index < mask.size(); ++index) {
            const auto parameter = first + index - 1U;
            if (mask[index] != 'i' && parameter < parameters.size()) {
                deliver(flight.arguments[index - 1U], parameter);
            }
        }
        const std::lock_guard lock { flight.mutex };
        flight.c_turn = true;
        flight.turn.notify_all();
        return PackedLogic4(1U, Logic4::zero);
    }

    // An export call from the C code of the current thread's import.
    static int export_call(void* const user_data, const char* const name,
        void** const arguments, void* const result)
    {
        (void)user_data;
        auto* const flight = current_flight;
        if (flight == nullptr) {
            std::fprintf(stderr,
                "error: DPI export '%s' called outside a context import of "
                "its scope\n",
                name);
            return -1;
        }
        std::unique_lock lock { flight->mutex };
        flight->name = name;
        flight->arguments = arguments;
        flight->result = result;
        flight->c_turn = false;
        flight->turn.notify_all();
        flight->turn.wait(lock, [&] { return flight->c_turn; });
        return 0;
    }

    static thread_local Flight* current_flight;

    Scope& scope_named(const std::string_view name)
    {
        auto& scope = scopes_[std::string { name }];
        if (!scope) {
            scope = std::make_unique<Scope>();
            scope->name = std::string { name };
        }
        return *scope;
    }

    static Service& self(void* const user_data)
    {
        return *static_cast<Service*>(user_data);
    }

    static Scope* scope_of(const svScope scope)
    {
        return static_cast<Scope*>(scope);
    }

    static svScope get_scope(void* const user_data)
    {
        return self(user_data).scope_;
    }

    static svScope set_scope(void* const user_data, const svScope scope)
    {
        auto& service = self(user_data);
        auto* const previous = service.scope_;
        service.scope_ = scope_of(scope);
        return previous;
    }

    static const char* scope_name(void*, const svScope scope)
    {
        return scope == nullptr ? nullptr : scope_of(scope)->name.c_str();
    }

    static svScope find_scope(void* const user_data, const char* const name)
    {
        if (name == nullptr) {
            return nullptr;
        }
        const auto& scopes = self(user_data).scopes_;
        const auto found = scopes.find(name);
        return found == scopes.end() ? nullptr : found->second.get();
    }

    static int put_user_data(
        void*, const svScope scope, void* const key, void* const value)
    {
        if (scope == nullptr || key == nullptr) {
            return -1;
        }
        scope_of(scope)->user_data[key] = value;
        return 0;
    }

    static void* get_user_data(void*, const svScope scope, void* const key)
    {
        if (scope == nullptr || key == nullptr) {
            return nullptr;
        }
        const auto& data = scope_of(scope)->user_data;
        const auto found = data.find(key);
        return found == data.end() ? nullptr : found->second;
    }

    static int caller_info(void* const user_data, const char** const file,
        int* const line)
    {
        const auto& service = self(user_data);
        if (file != nullptr) {
            *file = service.file_.c_str();
        }
        if (line != nullptr) {
            *line = service.line_;
        }
        return 1;
    }

    static int disabled(void*) { return 0; }

    static void acknowledge_disabled(void*) { }

    static int time(void* const user_data, const svScope, svTimeVal* const value)
    {
        const auto& clock = self(user_data).clock_;
        if (value == nullptr || !clock.ticks) {
            return -1;
        }
        const auto ticks = clock.ticks();
        value->type = vpiSimTime;
        value->high = static_cast<PLI_UINT32>(ticks >> 32U);
        value->low = static_cast<PLI_UINT32>(ticks);
        value->real = static_cast<double>(ticks);
        return 0;
    }

    static int time_scale(
        void* const user_data, const svScope, std::int32_t* const exponent)
    {
        if (exponent == nullptr) {
            return -1;
        }
        *exponent = self(user_data).clock_.precision;
        return 0;
    }

    Import& resolve(const std::string_view full_identity)
    {
        // Call sites of one import share its C call description.
        const auto identity = full_identity.substr(
            0U, full_identity.find('|'));
        const auto found = imports_.find(std::string { identity });
        if (found != imports_.end()) {
            return *found->second;
        }
        auto import = std::make_unique<Import>();
        import->signature = parse_signature(identity);
        void* symbol { };
        std::string error;
        for (const auto& library : libraries_) {
            symbol = library->symbol(import->signature.linkage, error);
            if (symbol != nullptr) {
                break;
            }
        }
        if (symbol == nullptr) {
            symbol = platform::DynamicLibrary::default_symbol(
                import->signature.linkage);
        }
        if (symbol == nullptr) {
            throw std::runtime_error { "SystemVerilog DPI import '"
                + import->signature.linkage
                + "' has no loaded C implementation" };
        }
        import->function = reinterpret_cast<void (*)()>(symbol);
        for (const auto& parameter : import->signature.parameters) {
            import->types.push_back(parameter.reference
                    ? &ffi_type_pointer
                    : scalar_type(parameter.code));
        }
        if (ffi_prep_cif(&import->cif, FFI_DEFAULT_ABI,
                static_cast<unsigned>(import->types.size()),
                scalar_type(import->signature.result),
                import->types.data())
            != FFI_OK) {
            throw std::runtime_error { "cannot prepare the C call of DPI import '"
                + import->signature.linkage + "'" };
        }
        auto& stored = *imports_.emplace(std::string { identity },
                                    std::move(import))
                            .first->second;
        return stored;
    }

    std::vector<std::unique_ptr<platform::DynamicLibrary>> libraries_;
    std::vector<std::unique_ptr<Flight>> flights_;
    ThreadEntry thread_entry_;
    std::unordered_map<std::string, std::unique_ptr<Import>> imports_;
    std::unordered_map<std::string, std::unique_ptr<Scope>> scopes_;
    fsim_svdpi_call_context_v3 context_;
    Clock clock_;
    Scope* scope_ { };
    std::string file_;
    int line_ { };
    std::string empty_;
};

thread_local Service::Flight* Service::current_flight { };

#endif

} // namespace

bool DpiForeignService::context_import(const std::string_view identity)
{
    return identity.substr(0U, identity.find('|')).ends_with("@c");
}

std::unique_ptr<DpiForeignService> load_dpi_libraries(
    const std::span<const std::string> libraries,
    diagnostic::Engine& diagnostics)
{
#if defined(FSIM_HAS_LIBFFI)
    auto service = std::make_unique<Service>();
    if (!service->load(libraries, diagnostics)) {
        return nullptr;
    }
    return service;
#else
    (void)libraries;
    diagnostics.error("FSIM-DPI-LOAD-001",
        "this fsim build has no DPI-C call support (libffi not found)");
    return nullptr;
#endif
}

} // namespace fsim::app::application_detail
