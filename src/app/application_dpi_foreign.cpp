// SPDX-License-Identifier: Apache-2.0
#include "application_dpi_foreign.hpp"

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/foreign_call_process.hpp"
#include "fsim/runtime/svdpi_bridge.h"
#include "fsim/support/native_filesystem.hpp"

#include "vpi_user.h"

#if defined(FSIM_HAS_LIBFFI)
#include <ffi.h>
#endif

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace fsim::app::application_detail {

namespace {

#if defined(FSIM_HAS_LIBFFI)

using runtime::Logic4;
using runtime::PackedLogic4;

struct CType;

// One formal or the result of a C signature: `&` marks a scalar, array or
// structure passed by pointer; B and V vectors always pass by pointer (IEEE
// 1800-2023 H.8). An open array `A` passes an svOpenArrayHandle (H.12) for
// elements described by `layout`, over the unpacked `bounds` (left, right per
// dimension); `&` then marks an output or inout. A dimension of a dynamic
// array or queue actual has `?` bounds, 0 upwards to its run-time size. A
// sized unpacked array `D` passes its elements by pointer, normalized so
// that the lowest index comes first (H.7.3, H.7.6); a structure `R` passes a
// pointer to a C structure (H.7.8). `width` is the bits of a value or of one
// element, and `element` its code.
struct Parameter {
    char code { };
    bool reference { };
    std::size_t width { };
    char element { };
    std::vector<std::int32_t> bounds;
    std::shared_ptr<const CType> layout;
    // The first of the actuals this formal takes and how many: a formal
    // is one actual, except an array of strings, which is one string
    // actual per element, leftmost element first (IEEE 1800-2023 H.7.4).
    std::size_t first_actual { };
    std::size_t actual_count { 1U };
};

// The bound of a dimension whose size the actual gives at run time.
constexpr std::int32_t runtime_extent
    = std::numeric_limits<std::int32_t>::min();

struct Signature {
    std::string linkage;
    std::vector<Parameter> parameters;
    char result { };
    bool context { };
    // The actuals of all formals; a string result follows them.
    std::size_t actual_total { };
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

struct CMember;

// The C representation of one value (IEEE 1800-2023 H.7.4, H.7.7, H.7.8): a
// leaf of one signature code, or a structure laid out as a C compiler does.
struct CType {
    char code { };
    // The bits of a vector; the leaf's own bits otherwise.
    std::size_t width { };
    // The bits of the value as one stream, the first member first.
    std::size_t bits { };
    std::size_t size { 1U };
    std::size_t align { 1U };
    std::vector<CMember> members;
};

struct CMember {
    CType type;
    // Left and right bound per unpacked dimension, outermost first.
    std::vector<std::int32_t> bounds;
    std::size_t count { 1U };
    std::size_t offset { };
};

[[nodiscard]] std::size_t extent_of(
    const std::int64_t left, const std::int64_t right)
{
    return static_cast<std::size_t>(left >= right ? left - right
                                                  : right - left)
        + 1U;
}

// The element count of the dimensions in `bounds`.
[[nodiscard]] std::size_t element_count(
    const std::vector<std::int32_t>& bounds)
{
    std::size_t count = 1U;
    for (std::size_t index = 0U; index + 1U < bounds.size(); index += 2U) {
        count *= extent_of(bounds[index], bounds[index + 1U]);
    }
    return count;
}

// Size, alignment and stream bits of a type whose members are laid out.
void finish_type(CType& type)
{
    if (type.code == 'R') {
        std::size_t offset { };
        type.align = 1U;
        type.bits = 0U;
        for (auto& member : type.members) {
            member.count = element_count(member.bounds);
            const auto align = member.type.align;
            offset = (offset + align - 1U) / align * align;
            member.offset = offset;
            offset += member.count * member.type.size;
            type.align = std::max(type.align, align);
            type.bits += member.count * member.type.bits;
        }
        type.size = std::max<std::size_t>(
            (offset + type.align - 1U) / type.align * type.align, 1U);
        return;
    }
    const auto words = (type.width + 31U) / 32U;
    if (type.code == 'z') {
        // A string is a `const char*` (H.7.4).
        type.width = sizeof(const char*) * 8U;
        type.bits = type.width;
        type.size = sizeof(const char*);
        type.align = type.size;
    } else if (type.code == 'B') {
        type.bits = type.width;
        type.size = words * sizeof(std::uint32_t);
        type.align = sizeof(std::uint32_t);
    } else if (type.code == 'V') {
        // svLogicVecVal holds an aval and a bval word.
        type.bits = type.width;
        type.size = words * 2U * sizeof(std::uint32_t);
        type.align = sizeof(std::uint32_t);
    } else {
        type.width = code_width(type.code);
        type.bits = type.width;
        type.size = std::max<std::size_t>(type.bits / 8U, 1U);
        type.align = type.size;
    }
}

// A sized or open array whose elements are strings.
[[nodiscard]] bool is_string_array(const Parameter& parameter)
{
    return (parameter.code == 'A' || parameter.code == 'D')
        && parameter.element == 'z';
}

[[noreturn]] void malformed_identity(const std::string_view identity)
{
    throw std::runtime_error { "malformed DPI import identity '"
        + std::string { identity } + "'" };
}

[[nodiscard]] CType parse_type(const std::string_view text,
    std::size_t& position, const std::string_view identity)
{
    const auto malformed = [&] { malformed_identity(identity); };
    const auto number = [&]() -> std::int64_t {
        const auto begin = position;
        if (position < text.size() && text[position] == '-') {
            ++position;
        }
        while (position < text.size() && text[position] >= '0'
            && text[position] <= '9') {
            ++position;
        }
        std::int64_t value { };
        const auto result = std::from_chars(
            text.data() + begin, text.data() + position, value);
        if (begin == position || result.ec != std::errc { }) {
            malformed();
        }
        return value;
    };
    if (position >= text.size()) {
        malformed();
    }
    CType type;
    type.code = text[position++];
    if (type.code == 'R') {
        if (position >= text.size() || text[position] != '(') {
            malformed();
        }
        ++position;
        for (;;) {
            CMember member;
            member.type = parse_type(text, position, identity);
            while (position < text.size() && text[position] == '/') {
                ++position;
                member.bounds.push_back(static_cast<std::int32_t>(number()));
                if (position >= text.size() || text[position] != '/') {
                    malformed();
                }
                ++position;
                member.bounds.push_back(static_cast<std::int32_t>(number()));
            }
            type.members.push_back(std::move(member));
            if (position >= text.size()) {
                malformed();
            }
            const auto next = text[position++];
            if (next == ')') {
                break;
            }
            if (next != ';') {
                malformed();
            }
        }
    } else if (type.code == 'B' || type.code == 'V') {
        type.width = static_cast<std::size_t>(number());
        if (type.width == 0U) {
            malformed();
        }
    }
    finish_type(type);
    return type;
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
    const auto malformed = [&] { malformed_identity(identity); };
    while (!arguments.empty()) {
        // A structure's members are `;`-separated, so a comma ends a
        // formal.
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
            malformed();
        }
        parameter.code = text.front();
        if (parameter.code == 'A' || parameter.code == 'D') {
            std::size_t position = 1U;
            auto layout = std::make_shared<CType>(
                parse_type(text, position, identity));
            parameter.element = layout->code;
            parameter.width = layout->bits;
            parameter.layout = std::move(layout);
            auto rest = text.substr(position);
            if (!rest.empty()) {
                if (rest.front() != '/') {
                    malformed();
                }
                rest.remove_prefix(1U);
            }
            while (!rest.empty()) {
                const auto next = rest.find('/');
                const auto bound = rest.substr(0U, next);
                parameter.bounds.push_back(bound == "?"
                        ? runtime_extent
                        : static_cast<std::int32_t>(
                              std::stol(std::string { bound })));
                rest = next == std::string_view::npos
                    ? std::string_view { }
                    : rest.substr(next + 1U);
            }
            // An open array may have no unpacked dimension (`bit []`); a
            // sized one has them all, and its sizes are known.
            if (parameter.width == 0U || parameter.bounds.size() % 2U != 0U
                || (parameter.code == 'D'
                    && (parameter.bounds.empty()
                        || std::ranges::any_of(parameter.bounds,
                            [](const std::int32_t bound) {
                                return bound == runtime_extent;
                            })))) {
                malformed();
            }
        } else if (parameter.code == 'R') {
            std::size_t position = 0U;
            auto layout = std::make_shared<CType>(
                parse_type(text, position, identity));
            if (position != text.size()) {
                malformed();
            }
            parameter.element = 'R';
            parameter.width = layout->bits;
            parameter.layout = std::move(layout);
        } else if (parameter.code == 'B' || parameter.code == 'V') {
            parameter.width = static_cast<std::size_t>(
                std::stoull(std::string { text.substr(1U) }));
        } else {
            parameter.width = code_width(parameter.code);
        }
        parameter.first_actual = signature.actual_total;
        if (is_string_array(parameter)) {
            // The strings pass one per actual, so every dimension is sized.
            if (std::ranges::any_of(parameter.bounds,
                    [](const std::int32_t bound) {
                        return bound == runtime_extent;
                    })) {
                malformed();
            }
            parameter.actual_count = element_count(parameter.bounds);
        }
        signature.actual_total += parameter.actual_count;
        signature.parameters.push_back(std::move(parameter));
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
    // An open array's elements, bounds and handle.
    std::vector<unsigned char> bytes;
    std::vector<std::int32_t> bounds;
    fsim_svdpi_open_array_v3 open { };
    // An array of strings: the C pointer of each element (IEEE 1800-2023
    // H.7.4); an open array's `bytes` is unused.
    std::vector<const char*> texts;
};

void store_actual(Slot& slot, const Parameter& parameter, const PackedLogic4& value,
    const std::string& text);
[[nodiscard]] PackedLogic4 load_actual(const Slot& slot, const Parameter& parameter);

// The next `bits` of a value held as one stream, its first bit most
// significant; bits past the stream read as 0.
[[nodiscard]] PackedLogic4 take_bits(const PackedLogic4& stream,
    std::size_t& position, const std::size_t bits)
{
    PackedLogic4 part(bits, Logic4::zero);
    const auto total = stream.width();
    for (std::size_t index = 0U; index < bits; ++index) {
        if (position + index < total) {
            part.set(bits - 1U - index, stream.get(total - 1U - position - index));
        }
    }
    position += bits;
    return part;
}

void put_bits(PackedLogic4& stream, std::size_t& position,
    const PackedLogic4& part)
{
    const auto total = stream.width();
    for (std::size_t index = 0U; index < part.width(); ++index) {
        if (position + index < total) {
            stream.set(total - 1U - position - index,
                part.get(part.width() - 1U - index));
        }
    }
    position += part.width();
}

[[nodiscard]] Parameter leaf_parameter(const CType& type)
{
    Parameter leaf;
    leaf.code = type.code;
    leaf.width = type.width;
    return leaf;
}

// A leaf value in its C bytes (IEEE 1800-2023 H.7.4, H.7.7, H.10.1).
void encode_leaf(const CType& type, const PackedLogic4& value,
    unsigned char* const target)
{
    Slot item;
    const std::string none;
    store_actual(item, leaf_parameter(type), value, none);
    if (type.code == 'B') {
        std::memcpy(target, item.bits.data(), type.size);
    } else if (type.code == 'V') {
        std::memcpy(target, item.logic.data(), type.size);
    } else {
        std::memcpy(target, &item.scalar, type.size);
    }
}

[[nodiscard]] PackedLogic4 decode_leaf(
    const CType& type, const unsigned char* const source)
{
    Slot item;
    if (type.code == 'B') {
        item.bits.resize(type.size / sizeof(std::uint32_t));
        std::memcpy(item.bits.data(), source, type.size);
    } else if (type.code == 'V') {
        item.logic.resize(type.size / sizeof(LogicWord));
        std::memcpy(item.logic.data(), source, type.size);
    } else {
        std::memcpy(&item.scalar, source, type.size);
    }
    return load_actual(item, leaf_parameter(type));
}

// The place in C order of the `ordinal`th element of an array in stream
// order (leftmost element first). In a normalized array the element with
// the lowest index comes first, so a descending dimension runs backwards
// (IEEE 1800-2023 H.7.3, H.7.6).
[[nodiscard]] std::size_t c_position(const std::vector<std::int32_t>& bounds,
    std::size_t ordinal, const bool normalize)
{
    std::size_t position { };
    std::size_t stride = 1U;
    for (std::size_t dimension = bounds.size() / 2U; dimension-- > 0U;) {
        const auto left = bounds[2U * dimension];
        const auto right = bounds[2U * dimension + 1U];
        const auto size = extent_of(left, right);
        auto index = ordinal % size;
        ordinal /= size;
        if (normalize && left >= right) {
            index = size - 1U - index;
        }
        position += index * stride;
        stride *= size;
    }
    return position;
}

// A value of `type` from the stream into its C bytes, structure members in
// C layout and array members in normalized order.
void encode_value(const CType& type, const PackedLogic4& stream,
    std::size_t& position, unsigned char* const target)
{
    if (type.code != 'R') {
        encode_leaf(type, take_bits(stream, position, type.bits), target);
        return;
    }
    for (const auto& member : type.members) {
        for (std::size_t ordinal = 0U; ordinal < member.count; ++ordinal) {
            encode_value(member.type, stream, position,
                target + member.offset
                    + c_position(member.bounds, ordinal, true)
                        * member.type.size);
        }
    }
}

void decode_value(const CType& type, const unsigned char* const source,
    PackedLogic4& stream, std::size_t& position)
{
    if (type.code != 'R') {
        put_bits(stream, position, decode_leaf(type, source));
        return;
    }
    for (const auto& member : type.members) {
        for (std::size_t ordinal = 0U; ordinal < member.count; ++ordinal) {
            decode_value(member.type,
                source + member.offset
                    + c_position(member.bounds, ordinal, true)
                        * member.type.size,
                stream, position);
        }
    }
}

// The pointer a sized array passes, or the handle of an open array over
// `data`.
void publish_array(Slot& slot, const Parameter& parameter, void* const data)
{
    const auto& layout = *parameter.layout;
    const auto stride = layout.size;
    if (parameter.code != 'A') {
        slot.scalar.pointer = data;
        return;
    }
    slot.open.magic = FSIM_SVDPI_OPEN_ARRAY_MAGIC;
    slot.open.struct_size = static_cast<std::uint32_t>(sizeof(slot.open));
    slot.open.dimensions = static_cast<std::int32_t>(slot.bounds.size() / 2U);
    const bool vector = layout.code == 'B' || layout.code == 'V';
    slot.open.packed_left
        = vector ? static_cast<std::int32_t>(layout.width) - 1 : 0;
    slot.open.packed_right = 0;
    slot.open.element_code = static_cast<std::uint32_t>(layout.code);
    slot.open.element_width = static_cast<std::uint32_t>(layout.bits);
    slot.open.element_bytes = static_cast<std::uint32_t>(stride);
    slot.open.data = data;
    slot.open.bounds = slot.bounds.data();
    slot.scalar.pointer = &slot.open;
}

// The elements of an array of strings (IEEE 1800-2023 35.5.6, H.7.4): one
// `const char*` each, in the order of an array of any other element (a
// sized array normalized, an open array in the actual's order). The
// strings are the formal's actuals, leftmost element first.
void store_strings(Slot& slot, const Parameter& parameter,
    const std::vector<std::string>& strings)
{
    const auto count = parameter.actual_count;
    if (parameter.first_actual + count > strings.size()) {
        throw std::runtime_error {
            "DPI string-array actual does not match its dimensions" };
    }
    slot.bounds = parameter.bounds;
    slot.texts.assign(count, nullptr);
    const bool open = parameter.code == 'A';
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const auto place = open ? ordinal
                                : c_position(slot.bounds, ordinal, true);
        slot.texts[place] = strings[parameter.first_actual + ordinal].c_str();
    }
    publish_array(slot, parameter, slot.texts.data());
}

// The strings the C code left in an output or inout array of strings. All
// are read before any is stored: the C code may have exchanged elements.
void load_strings(const Slot& slot, const Parameter& parameter,
    std::vector<std::string>& strings)
{
    const auto count = parameter.actual_count;
    std::vector<std::string> result(count);
    const bool open = parameter.code == 'A';
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const auto place = open ? ordinal
                                : c_position(slot.bounds, ordinal, true);
        const auto* const text = slot.texts[place];
        result[ordinal] = text == nullptr ? std::string { } : text;
    }
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        strings[parameter.first_actual + ordinal] = std::move(result[ordinal]);
    }
}

// The elements of an array actual, leftmost element in the most significant
// bits of `value`, in their C layout (H.12.1). An open array keeps the
// actual's own order under its original bounds; a sized one is normalized.
void store_array(Slot& slot, const Parameter& parameter,
    const PackedLogic4& value)
{
    const auto& layout = *parameter.layout;
    // A run-time sized dimension takes its size from the actual's bits.
    slot.bounds = parameter.bounds;
    std::size_t count = 1U;
    std::optional<std::size_t> extent_dimension;
    for (std::size_t index = 0U; index + 1U < slot.bounds.size(); index += 2U) {
        if (slot.bounds[index] == runtime_extent) {
            extent_dimension = index;
            continue;
        }
        count *= extent_of(slot.bounds[index], slot.bounds[index + 1U]);
    }
    if (extent_dimension) {
        const auto row_bits = count * layout.bits;
        if (row_bits == 0U || value.width() % row_bits != 0U
            || value.width() / row_bits
                > static_cast<std::size_t>(
                    std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error {
                "DPI open-array actual does not match its dimensions" };
        }
        const auto size = value.width() / row_bits;
        slot.bounds[*extent_dimension] = 0;
        slot.bounds[*extent_dimension + 1U]
            = static_cast<std::int32_t>(size) - 1;
        count *= size;
    }
    const bool open = parameter.code == 'A';
    const auto stride = layout.size;
    slot.bytes.assign(count * stride, 0U);
    std::size_t position { };
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const auto place = open ? ordinal
                                : c_position(slot.bounds, ordinal, true);
        encode_value(layout, value, position,
            slot.bytes.data() + place * stride);
    }
    publish_array(slot, parameter, slot.bytes.data());
}

[[nodiscard]] PackedLogic4 load_array(
    const Slot& slot, const Parameter& parameter)
{
    const auto& layout = *parameter.layout;
    // The sizes the call used, run-time sized dimensions resolved.
    const auto& bounds = slot.bounds.empty() ? parameter.bounds : slot.bounds;
    const auto count = element_count(bounds);
    const bool open = parameter.code == 'A';
    PackedLogic4 value(count * layout.bits, Logic4::zero);
    std::size_t position { };
    for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const auto place = open ? ordinal : c_position(bounds, ordinal, true);
        decode_value(layout, slot.bytes.data() + place * layout.size, value,
            position);
    }
    return value;
}

// A structure actual in its C layout (H.7.8).
void store_structure(Slot& slot, const Parameter& parameter,
    const PackedLogic4& value)
{
    const auto& layout = *parameter.layout;
    slot.bytes.assign(layout.size, 0U);
    std::size_t position { };
    encode_value(layout, value, position, slot.bytes.data());
    slot.scalar.pointer = slot.bytes.data();
}

[[nodiscard]] PackedLogic4 load_structure(
    const Slot& slot, const Parameter& parameter)
{
    const auto& layout = *parameter.layout;
    PackedLogic4 value(layout.bits, Logic4::zero);
    std::size_t position { };
    decode_value(layout, slot.bytes.data(), value, position);
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
    case 'D':
        store_array(slot, parameter, value);
        break;
    case 'R':
        store_structure(slot, parameter, value);
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
    case 'D':
        return load_array(slot, parameter);
    case 'R':
        return load_structure(slot, parameter);
    default:
        return PackedLogic4 { };
    }
}

// Vectors, arrays and structures pass by pointer in every direction.
[[nodiscard]] bool by_pointer(const char code)
{
    return code == 'B' || code == 'V' || code == 'A' || code == 'D'
        || code == 'R';
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
            &Service::time_unit,
            &Service::time_precision,
        }
    {
        fsim_svdpi_set_export_dispatcher_v3(&Service::export_call, this);
        runtime::DpiCallProcess::install(&Service::process_disabled_notice, this);
    }

    ~Service() override
    {
        runtime::DpiCallProcess::install(nullptr, nullptr);
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
            if (!support::native_fs::exists(path, error_code)) {
                const auto shared = std::filesystem::path { name + ".so" };
                if (support::native_fs::exists(shared, error_code)) {
                    path = shared;
                }
            }
            // dlopen searches the loader path for a bare name.
            if (support::native_fs::exists(path, error_code)) {
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
        raise_pending_error();
        check_task_status(import, call);
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
        // The process the call chain runs for, and the position of its
        // import call in the process's operations.
        std::uint64_t process { };
        std::uint32_t position { };
        // The call chain's current scope while the simulation side runs
        // the export (IEEE 1800-2023 35.5.3): it starts as the scope of the
        // import declaration and svSetScope changes it.
        Scope* scope { };
        // The pending export call was handed to a service that has not
        // returned it yet.
        bool handed { };
        // The pending export call was rejected: the C code gets -1.
        bool failed { };
        // The disable protocol (35.9): the chain was disabled while its
        // export call ran, and the C code acknowledged it.
        bool disabled { };
        bool acked { };
        // A protocol or scope error the C code caused, raised once the
        // call chain has ended.
        std::string error;
    };

    void prepare(const Import& import, Call& call,
        const std::vector<PackedLogic4>& actuals,
        const std::vector<std::string>& strings)
    {
        const auto& parameters = import.signature.parameters;
        // A string result returns in one more string actual after the
        // formals (IEEE 1800-2023 H.7.4: const char*).
        const bool string_result = import.signature.result == 'z';
        if (actuals.size()
                != import.signature.actual_total + (string_result ? 1U : 0U)
            || (string_result && strings.size() != actuals.size())) {
            throw std::runtime_error { "DPI import '"
                + import.signature.linkage + "' actual count mismatch" };
        }
        call.slots.resize(parameters.size());
        call.values.resize(parameters.size());
        for (std::size_t index = 0U; index < parameters.size(); ++index) {
            const auto& parameter = parameters[index];
            auto& slot = call.slots[index];
            if (is_string_array(parameter)) {
                store_strings(slot, parameter, strings);
            } else {
                store_actual(slot, parameter, actuals[parameter.first_actual],
                    parameter.first_actual < strings.size()
                        ? strings[parameter.first_actual]
                        : empty_);
            }
            if (parameter.reference && !by_pointer(parameter.code)) {
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
        if (!context) {
            noncontext_import = &import;
        }
        ffi_call(&import.cif, import.function, call.result, call.values.data());
        noncontext_import = nullptr;
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
            const bool aggregate = parameter.code == 'A'
                || parameter.code == 'D' || parameter.code == 'R';
            if (aggregate ? !parameter.reference
                          : (!parameter.reference && !vector)) {
                continue;
            }
            if (parameter.code == 'z') {
                if (parameter.first_actual < strings.size()) {
                    const auto* const text = call.slots[index].scalar.text;
                    strings[parameter.first_actual]
                        = text == nullptr ? std::string { } : text;
                }
                continue;
            }
            if (is_string_array(parameter)) {
                load_strings(call.slots[index], parameter, strings);
                continue;
            }
            actuals[parameter.first_actual]
                = load_actual(call.slots[index], parameter);
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
        Parameter scalar;
        scalar.code = code;
        scalar.width = code_width(code);
        return load_actual(returned, scalar);
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
        state.process = runtime::DpiCallProcess::current();
        state.position = runtime::DpiCallProcess::position();
        if (const auto caller = caller_of(identity)) {
            scope_ = &scope_named(caller->instance);
            file_ = std::string { caller->file };
            line_ = caller->line;
        }
        state.scope = scope_;
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
        const auto found = find_flight(runtime::DpiCallProcess::current());
        if (found == flights_.end()) {
            throw std::runtime_error { "DPI import '"
                + std::string { identity } + "' finished without a start" };
        }
        auto flight = std::move(*found);
        flights_.erase(found);
        {
            std::unique_lock lock { flight->mutex };
            flight->turn.wait(lock, [&] { return !flight->c_turn; });
            if (!flight->done) {
                // The C code waits for an export nobody serves.
                auto message = flight->error.empty()
                    ? "DPI import '" + std::string { identity }
                        + "' returned while its C code called an export"
                    : flight->error;
                flight->thread.detach();
                (void)flight.release();
                throw std::runtime_error { std::move(message) };
            }
        }
        flight->thread.join();
        scope_ = flight->saved_scope;
        file_ = flight->saved_file;
        line_ = flight->saved_line;
        if (!flight->error.empty()) {
            throw std::runtime_error { flight->error };
        }
        check_task_status(*flight->import, flight->call);
        return complete(*flight->import, flight->call, actuals, strings);
    }

    // A disable ends or redirects `process` (IEEE 1800-2023 35.9). If the
    // process is suspended in an export call of an import call chain, the
    // C code gets the disabled state: the export call returns 1 and
    // svIsDisabledState() is true. The C code runs to the end of the import
    // now, and the protocol it has to follow is checked.
    void process_disabled(const std::uint64_t process,
        const std::uint32_t begin, const std::uint32_t end)
    {
        const auto found = find_flight(process);
        if (found == flights_.end()) {
            return;
        }
        // A disable that does not cover the import call but the export call
        // inside it targets the exported subroutine itself: the export
        // returns 0 and the import is not in the disabled state (IEEE
        // 1800-2023 35.9).
        const auto position = (*found)->position;
        if (position != runtime::DpiCallProcess::unknown_position
            && (position < begin || position >= end)) {
            return;
        }
        auto flight = std::move(*found);
        flights_.erase(found);
        {
            std::unique_lock lock { flight->mutex };
            // The C code waits in an export call (or has returned).
            flight->turn.wait(lock, [&] { return !flight->c_turn; });
            if (!flight->done) {
                flight->disabled = true;
                flight->c_turn = true;
                flight->turn.notify_all();
                flight->turn.wait(lock, [&] { return !flight->c_turn; });
            }
        }
        flight->thread.join();
        scope_ = flight->saved_scope;
        file_ = flight->saved_file;
        line_ = flight->saved_line;
        std::string error = std::move(flight->error);
        if (error.empty() && flight->disabled) {
            const auto& signature = flight->import->signature;
            // b) an imported task returns 1, c) an imported function
            // acknowledges the disabled state.
            if (signature.result == 'T') {
                if (task_status(flight->call) != 1) {
                    error = "DPI import task '" + signature.linkage
                        + "' did not return 1 after a disable (IEEE "
                          "1800-2023 35.9 b)";
                }
            } else if (!flight->acked) {
                error = "DPI import function '" + signature.linkage
                    + "' returned after a disable without calling "
                      "svAckDisabledState (IEEE 1800-2023 35.9 c)";
            }
        }
        if (!error.empty()) {
            throw std::runtime_error { std::move(error) };
        }
    }

    static void process_disabled_notice(void* const user_data,
        const std::uint64_t process, const std::uint32_t begin,
        const std::uint32_t end)
    {
        self(user_data).process_disabled(process, begin, end);
    }

    // The last call chain started for `process`.
    [[nodiscard]] std::vector<std::unique_ptr<Flight>>::iterator find_flight(
        const std::uint64_t process)
    {
        auto found = flights_.end();
        for (auto flight = flights_.begin(); flight != flights_.end();
             ++flight) {
            if ((*flight)->process == process) {
                found = flight;
            }
        }
        return found;
    }

    // An imported task returns its disable status (IEEE 1800-2023 35.9).
    [[nodiscard]] static std::int32_t task_status(const Call& call)
    {
        return static_cast<std::int32_t>(
            *reinterpret_cast<const ffi_arg*>(call.result));
    }

    // Item b): a task returns 0 unless it returns due to a disable, which
    // the kernel hands over (process_disabled) before the task returns.
    static void check_task_status(const Import& import, const Call& call)
    {
        if (import.signature.result == 'T' && task_status(call) != 0) {
            throw std::runtime_error { "DPI import task '"
                + import.signature.linkage
                + "' returned a nonzero disable status without a disable "
                  "(IEEE 1800-2023 35.9 b)" };
        }
    }

    void raise_pending_error()
    {
        if (!pending_error_.empty()) {
            throw std::runtime_error { std::exchange(pending_error_, { }) };
        }
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
        case 'R':
            return parameter.layout->size;
        case 'D':
            return element_count(parameter.bounds) * parameter.layout->size;
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
        case 'D':
        case 'R':
            return slot.bytes.data();
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
        const auto active = find_flight(runtime::DpiCallProcess::current());
        if (active == flights_.end()) {
            throw std::runtime_error {
                "a DPI export service ran outside a context import" };
        }
        auto& flight = **active;
        const auto signature
            = parse_signature(identity.substr(0U, identity.find('|')));
        const auto name = std::string_view { signature.linkage }.substr(
            export_prefix.size());
        if (name == "next" || name == "serve") {
            std::unique_lock lock { flight.mutex };
            if (name == "next") {
                flight.turn.wait(lock, [&] { return !flight.c_turn; });
            }
            if (flight.done) {
                return from_bits(0U, 32U);
            }
            if (name == "serve" && flight.c_turn) {
                return from_bits(0U, 32U);
            }
            // The export call is served by the scope the call chain is in
            // (IEEE 1800-2023 35.5.3): the scope of this service's instance
            // or generate block instance, or any scope for the exports of
            // the compilation unit, which a step names `$unit` (35.5.2).
            const auto caller = caller_of(identity);
            const std::string_view scope_text
                = name == "serve" && strings.size() > 1U
                ? std::string_view { strings[1U] }
                : std::string_view { };
            const bool any_scope = scope_text == "$unit";
            const std::string_view own
                = !scope_text.empty() && !any_scope ? scope_text
                : caller                            ? caller->instance
                                                    : std::string_view { };
            const bool local = any_scope
                || (flight.scope != nullptr && flight.scope->name == own);
            if (name == "next" && flight.handed) {
                // A step of every other scope has run and none served the
                // call.
                return reject(flight, "DPI export '" + flight.name
                        + "' called in the scope '"
                        + (flight.scope == nullptr ? std::string { }
                                                   : flight.scope->name)
                        + "', which does not declare it (IEEE 1800-2023 "
                          "35.5.3)");
            }
            std::string_view names { strings.empty() ? empty_ : strings.front() };
            std::uint64_t ordinal { };
            while (local && !names.empty()) {
                ++ordinal;
                const auto space = names.find(' ');
                if (names.substr(0U, space) == flight.name) {
                    flight.handed = true;
                    return from_bits(ordinal, 32U);
                }
                names = space == std::string_view::npos
                    ? std::string_view { }
                    : names.substr(space + 1U);
            }
            if (name == "next") {
                // The scope does not declare the export: the steps of the
                // other scopes, and of the compilation unit, get to serve
                // it.
                flight.handed = true;
                return from_bits(0xFFFFFFFFU, 32U);
            }
            return from_bits(0U, 32U);
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
        flight.handed = false;
        flight.c_turn = true;
        flight.turn.notify_all();
        return PackedLogic4(1U, Logic4::zero);
    }

    // Ends the pending export call with an error: the C code gets -1 and
    // the call chain's service stops; the error is raised once the import
    // returns. The caller holds the flight's mutex.
    static PackedLogic4 reject(Flight& flight, std::string message)
    {
        if (flight.error.empty()) {
            flight.error = std::move(message);
        }
        flight.handed = false;
        flight.failed = true;
        flight.c_turn = true;
        flight.turn.notify_all();
        return from_bits(0U, 32U);
    }

    // An export call from the C code of the current thread's import. It
    // returns 0, or 1 when the export returned due to a disable (IEEE
    // 1800-2023 35.7, 35.9 a), or -1 when the call is an error.
    static int export_call(void* const user_data, const char* const name,
        void** const arguments, void* const result)
    {
        auto& service = self(user_data);
        auto* const flight = current_flight;
        if (flight == nullptr) {
            if (noncontext_import != nullptr) {
                // A noncontext import cannot call an export (IEEE 1800-2023
                // 35.5.3); the error is raised once the C code returns.
                if (service.pending_error_.empty()) {
                    service.pending_error_ = "DPI import '"
                        + noncontext_import->signature.linkage
                        + "' is not a context import and cannot call the "
                          "export '"
                        + name + "' (IEEE 1800-2023 35.5.3)";
                }
                return -1;
            }
            std::fprintf(stderr,
                "error: DPI export '%s' called outside a context import of "
                "its scope\n",
                name);
            return -1;
        }
        std::unique_lock lock { flight->mutex };
        const auto fail = [&](std::string message) {
            if (flight->error.empty()) {
                flight->error = std::move(message);
            }
            return -1;
        };
        if (flight->disabled) {
            // Item d): no export call once the import is disabled.
            return fail("DPI import '" + flight->import->signature.linkage
                + "' calls the export '" + name
                + "' after it entered the disabled state (IEEE 1800-2023 "
                  "35.9 d)");
        }
        if (!flight->error.empty()) {
            // The call chain failed on an earlier export call.
            return -1;
        }
        // The call chain's scope returns to its value at the export call
        // when the export returns (35.5.3).
        flight->scope = service.scope_;
        flight->name = name;
        flight->arguments = arguments;
        flight->result = result;
        flight->c_turn = false;
        flight->turn.notify_all();
        flight->turn.wait(lock, [&] { return flight->c_turn; });
        service.scope_ = flight->scope;
        if (flight->failed) {
            flight->failed = false;
            return -1;
        }
        return flight->disabled ? 1 : 0;
    }

    static thread_local Flight* current_flight;
    // The noncontext import whose C code the simulation thread runs.
    static thread_local const Import* noncontext_import;

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
        // Every instance of the design is a scope (IEEE 1800-2023 H.9.3),
        // whether or not a context call has reached it yet.
        auto& service = self(user_data);
        const auto found = service.scopes_.find(name);
        if (found != service.scopes_.end()) {
            return found->second.get();
        }
        if (!service.clock_.instance) {
            return nullptr;
        }
        const auto canonical = service.clock_.instance(name);
        return canonical ? &service.scope_named(*canonical) : nullptr;
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

    // svIsDisabledState and svAckDisabledState (IEEE 1800-2023 35.9): the
    // call chain of the calling thread was disabled while its export call
    // ran.
    static int disabled(void*)
    {
        return current_flight != nullptr && current_flight->disabled ? 1 : 0;
    }

    static void acknowledge_disabled(void*)
    {
        if (current_flight != nullptr) {
            current_flight->acked = true;
        }
    }

    // The time unit and precision exponents of `scope`; the simulation's
    // for a null scope (IEEE 1800-2023 H.13).
    static std::pair<int, int> scale_of(
        const Service& service, const svScope scope)
    {
        const auto& clock = service.clock_;
        if (scope != nullptr && clock.time_scale) {
            if (const auto scale = clock.time_scale(scope_of(scope)->name)) {
                return *scale;
            }
        }
        return std::pair { clock.unit, clock.precision };
    }

    static int time(
        void* const user_data, const svScope scope, svTimeVal* const value)
    {
        const auto& service = self(user_data);
        if (value == nullptr || !service.clock_.ticks) {
            return -1;
        }
        const auto ticks = service.clock_.ticks();
        if (value->type == vpiSimTime) {
            value->high = static_cast<PLI_UINT32>(ticks >> 32U);
            value->low = static_cast<PLI_UINT32>(ticks);
            return 0;
        }
        if (value->type != vpiScaledRealTime) {
            return -1;
        }
        // A tick is 10^precision seconds, the unit 10^unit.
        const auto shift = scale_of(service, scope).first
            - service.clock_.precision;
        const auto power = std::pow(10.0, std::abs(shift));
        value->real = shift >= 0 ? static_cast<double>(ticks) / power
                                 : static_cast<double>(ticks) * power;
        return 0;
    }

    static int time_unit(
        void* const user_data, const svScope scope, std::int32_t* const exponent)
    {
        if (exponent == nullptr) {
            return -1;
        }
        *exponent = scale_of(self(user_data), scope).first;
        return 0;
    }

    static int time_precision(
        void* const user_data, const svScope scope, std::int32_t* const exponent)
    {
        if (exponent == nullptr) {
            return -1;
        }
        *exponent = scale_of(self(user_data), scope).second;
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
    // An error the C code of a noncontext import caused.
    std::string pending_error_;
};

thread_local Service::Flight* Service::current_flight { };
thread_local const Import* Service::noncontext_import { };

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
