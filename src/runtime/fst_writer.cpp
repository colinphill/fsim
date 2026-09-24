// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_writer.hpp"

#include "fsim/runtime/fst_compression.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ostream>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif

namespace fsim::runtime {
namespace {

    using Bytes = std::vector<std::uint8_t>;

    constexpr std::uint8_t fst_block_header = 0;
    constexpr std::uint8_t fst_block_value_changes = 8;
    constexpr std::uint8_t fst_block_geometry = 3;
    constexpr std::uint8_t fst_block_hierarchy = 4;
    constexpr std::uint8_t fst_block_skip = 0xff;
    constexpr std::uint8_t fst_scope = 0xfe;
    constexpr std::uint8_t fst_upscope = 0xff;
    constexpr std::uint8_t fst_attribute_begin = 0xfc;
    constexpr std::uint8_t fst_attribute_miscellaneous = 0;
    constexpr std::uint8_t fst_miscellaneous_comment = 0;
    constexpr std::uint8_t fst_scope_module = 0;
    constexpr std::uint8_t fst_scope_vhdl_architecture = 12;
    constexpr std::uint8_t fst_scope_vhdl_block = 17;
    constexpr std::uint8_t fst_direction_implicit = 0;
    constexpr std::uint8_t fst_var_integer = 1;
    constexpr std::uint8_t fst_var_real = 3;
    constexpr std::uint8_t fst_var_time = 8;
    constexpr std::uint8_t fst_var_wire = 16;
    constexpr std::uint8_t fst_var_realtime = 20;
    constexpr std::uint8_t fst_var_string = 21;
    constexpr std::uint8_t fst_var_enum = 28;
    constexpr std::uint8_t fst_var_shortreal = 29;
    constexpr std::uint64_t fst_header_section_length = 329;
    constexpr std::array<std::uint8_t, 8> fst_endian_test {
        0x69, 0x57, 0x14, 0x8b, 0x0a, 0xbf, 0x05, 0x40
    };
    void require_bounded_name(
        std::string_view value,
        const FstWriterLimits& limits);

    [[nodiscard]] std::size_t buffered_value_bytes(
        const FstEncodedValue& value,
        const std::size_t fixed_bytes)
    {
        const auto payload_bytes
            = value.payload_kind() == FstPayloadKind::Real
            ? sizeof(std::uint64_t)
            : value.string_bytes().size();
        if (value.canonical_type().size()
                > std::numeric_limits<std::size_t>::max() - payload_bytes
            || fixed_bytes > std::numeric_limits<std::size_t>::max()
                    - payload_bytes - value.canonical_type().size()) {
            throw std::length_error("FST buffered value size overflows");
        }
        return fixed_bytes + payload_bytes + value.canonical_type().size();
    }

    void append_u8(Bytes& output, const std::uint8_t value)
    {
        output.push_back(value);
    }

    void append_be64(Bytes& output, const std::uint64_t value)
    {
        for (int shift = 56; shift >= 0; shift -= 8) {
            output.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void append_le64(Bytes& output, const std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            output.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void append_varint(Bytes& output, std::uint64_t value)
    {
        do {
            auto byte = static_cast<std::uint8_t>(value & 0x7fU);
            value >>= 7U;
            if (value != 0) {
                byte |= 0x80U;
            }
            output.push_back(byte);
        } while (value != 0);
    }

    void append_c_string(Bytes& output, const std::string_view value)
    {
        output.insert(output.end(), value.begin(), value.end());
        output.push_back(0);
    }

    void append_sized_text(std::string& output, const std::string_view value)
    {
        output += std::to_string(value.size());
        output.push_back(':');
        output.append(value);
    }

    [[nodiscard]] std::string canonical_provenance_record(
        const TraceSourceDeclaration& source,
        const TraceDeclarationKind kind,
        const TraceSignalId signal,
        const TraceSignalId target)
    {
        std::string result = "fsim-trace-provenance-v1|";
        result.push_back(
            kind == TraceDeclarationKind::Variable ? 'v' : 'a');
        result.push_back('|');
        result += std::to_string(signal.value);
        result.push_back('|');
        result += std::to_string(target.value);
        result.push_back('|');
        result += std::to_string(static_cast<std::uint8_t>(source.kind));
        result.push_back('|');
        result += std::to_string(static_cast<std::uint8_t>(source.language));
        result.push_back('|');
        append_sized_text(result, source.root_identity);
        append_sized_text(result, source.library);
        append_sized_text(result, source.owner_identity);
        append_sized_text(result, source.canonical_name);
        return result;
    }

    [[nodiscard]] std::string canonical_declaration_record(
        const TraceSourceDeclaration& source,
        const TraceTypeDeclaration& type,
        const TraceDeclarationKind kind,
        const TraceSignalId signal,
        const TraceSignalId target,
        const std::string_view path)
    {
        std::string result = "fsim-trace-declaration-v1|";
        result.push_back(
            kind == TraceDeclarationKind::Variable ? 'v' : 'a');
        result.push_back('|');
        result += std::to_string(signal.value);
        result.push_back('|');
        result += std::to_string(target.value);
        result.push_back('|');
        result += std::to_string(static_cast<std::uint8_t>(type.kind));
        result.push_back('|');
        result += std::to_string(static_cast<std::uint8_t>(type.scalar_kind));
        result.push_back('|');
        result += std::to_string(type.width);
        result.push_back('|');
        result += std::to_string(static_cast<std::uint8_t>(source.kind));
        result.push_back('|');
        result += std::to_string(static_cast<std::uint8_t>(source.language));
        result.push_back('|');
        append_sized_text(result, path);
        append_sized_text(result, type.canonical_metadata);
        append_sized_text(result, source.root_identity);
        append_sized_text(result, source.library);
        append_sized_text(result, source.owner_identity);
        append_sized_text(result, source.canonical_name);
        return result;
    }

    void append_declaration_record(
        Bytes& output,
        const TraceSourceDeclaration& source,
        const TraceTypeDeclaration& type,
        const TraceDeclarationKind kind,
        const TraceSignalId signal,
        const TraceSignalId target,
        const std::string_view path,
        const FstWriterLimits& limits)
    {
        const auto record = canonical_declaration_record(
            source, type, kind, signal, target, path);
        require_bounded_name(record, limits);
        append_u8(output, fst_attribute_begin);
        append_u8(output, fst_attribute_miscellaneous);
        append_u8(output, fst_miscellaneous_comment);
        append_c_string(output, record);
        append_varint(output, 0U);
    }

    void append_provenance_record(
        Bytes& output,
        const TraceSourceDeclaration& source,
        const TraceDeclarationKind kind,
        const TraceSignalId signal,
        const TraceSignalId target,
        const FstWriterLimits& limits)
    {
        if (source.language == TraceLanguage::Unknown) {
            return;
        }
        const auto record
            = canonical_provenance_record(source, kind, signal, target);
        require_bounded_name(record, limits);
        append_u8(output, fst_attribute_begin);
        append_u8(output, fst_attribute_miscellaneous);
        append_u8(output, fst_miscellaneous_comment);
        append_c_string(output, record);
        append_varint(output, 0U);
    }

    [[nodiscard]] std::uint8_t fst_scope_type(
        const TraceScopeDeclaration& scope,
        const TraceSourceDeclaration& source) noexcept
    {
        if (source.language != TraceLanguage::Vhdl) {
            return fst_scope_module;
        }
        return scope.parent.value == 0U
            ? fst_scope_vhdl_architecture
            : fst_scope_vhdl_block;
    }

    [[nodiscard]] std::uint8_t fst_variable_type(
        const TraceTypeDeclaration& type)
    {
        switch (type.kind) {
        case TraceTypeKind::Packed:
            return fst_var_wire;
        case TraceTypeKind::SystemVerilogString:
            return fst_var_string;
        case TraceTypeKind::Enumeration:
            return fst_var_enum;
        case TraceTypeKind::VhdlPhysical:
            return fst_var_integer;
        case TraceTypeKind::VhdlTime:
            return fst_var_time;
        case TraceTypeKind::VhdlLogic9:
        case TraceTypeKind::TypedLeaf:
            return fst_var_wire;
        case TraceTypeKind::SystemVerilogScalar:
            switch (type.scalar_kind) {
            case SystemVerilogScalarKind::ShortReal:
                return fst_var_shortreal;
            case SystemVerilogScalarKind::Real:
                return fst_var_real;
            case SystemVerilogScalarKind::Realtime:
                return fst_var_realtime;
            case SystemVerilogScalarKind::Time:
                return fst_var_time;
            case SystemVerilogScalarKind::Chandle:
                return fst_var_wire;
            case SystemVerilogScalarKind::None:
                break;
            }
            break;
        }
        throw std::invalid_argument("unsupported FST trace type");
    }

    [[nodiscard]] std::size_t fst_storage_width(
        const TraceTypeDeclaration& type)
    {
        if (type.kind == TraceTypeKind::SystemVerilogString) {
            return 0;
        }
        if (type.kind == TraceTypeKind::SystemVerilogScalar
            && (type.scalar_kind == SystemVerilogScalarKind::ShortReal
                || type.scalar_kind == SystemVerilogScalarKind::Real
                || type.scalar_kind == SystemVerilogScalarKind::Realtime)) {
            return 8;
        }
        if (type.kind == TraceTypeKind::VhdlLogic9) {
            if (type.width > std::numeric_limits<std::size_t>::max() / 4U) {
                throw std::length_error("FST Logic9 storage width overflows");
            }
            return type.width * 4U;
        }
        return type.width;
    }

    void require_type_profile(const TraceTypeDeclaration& type)
    {
        if (type.kind == TraceTypeKind::Packed) {
            if (!type.canonical_metadata.empty()) {
                throw std::invalid_argument(
                    "FST packed declaration has unexpected type metadata");
            }
            return;
        }
        if (type.kind == TraceTypeKind::SystemVerilogString) {
            if (type.canonical_metadata != canonical_fst_string_type()) {
                throw std::invalid_argument(
                    "FST string declaration lacks canonical type metadata");
            }
            return;
        }
        if (type.kind == TraceTypeKind::SystemVerilogScalar) {
            if (type.scalar_kind == SystemVerilogScalarKind::ShortReal
                || type.scalar_kind == SystemVerilogScalarKind::Real
                || type.scalar_kind == SystemVerilogScalarKind::Realtime) {
                if (type.canonical_metadata
                    != canonical_fst_systemverilog_real_type(type.scalar_kind)) {
                    throw std::invalid_argument(
                        "FST real declaration lacks canonical type metadata");
                }
            } else if (!type.canonical_metadata.empty()) {
                throw std::invalid_argument(
                    "FST scalar declaration has unexpected type metadata");
            }
            return;
        }
        const auto expected_prefix = type.kind == TraceTypeKind::TypedLeaf
            ? "fsim.fst.leaf.v1;"
            : "fsim.fst.type.v1;";
        if (!type.canonical_metadata.starts_with(expected_prefix)) {
            throw std::invalid_argument(
                "FST extended declaration lacks canonical type metadata");
        }
    }

    void require_bounded_name(
        const std::string_view name,
        const FstWriterLimits& limits)
    {
        if (name.empty() || name.size() > limits.maximum_name_bytes
            || name.find('\0') != std::string_view::npos) {
            throw std::invalid_argument("FST hierarchy name is empty or too large");
        }
    }

    void require_value_profile(
        const TraceTypeDeclaration& type,
        const FstEncodedValue& value)
    {
        if (value.width() != type.width) {
            throw std::invalid_argument(
                "FST value width does not match its declaration");
        }
        if (type.canonical_metadata != value.canonical_type()) {
            throw std::invalid_argument(
                "FST value type metadata does not match its declaration");
        }
        if (type.kind == TraceTypeKind::Packed) {
            if (!fst_profile_is_packed(value.profile())) {
                throw std::invalid_argument(
                    "FST packed declaration has an incompatible value profile");
            }
            return;
        }
        const auto expected = type.kind == TraceTypeKind::SystemVerilogString
            ? FstValueProfile::SystemVerilogString
            : type.kind == TraceTypeKind::Enumeration
            ? FstValueProfile::Enumeration
            : type.kind == TraceTypeKind::VhdlPhysical
            ? FstValueProfile::VhdlPhysical
            : type.kind == TraceTypeKind::VhdlTime
            ? FstValueProfile::VhdlTime
            : type.kind == TraceTypeKind::VhdlLogic9
            ? FstValueProfile::VhdlLogic9
            : type.kind == TraceTypeKind::TypedLeaf
            ? FstValueProfile::TypedLeaf
            : type.scalar_kind == SystemVerilogScalarKind::ShortReal
            ? FstValueProfile::SystemVerilogShortReal
            : type.scalar_kind == SystemVerilogScalarKind::Real
            ? FstValueProfile::SystemVerilogReal
            : type.scalar_kind == SystemVerilogScalarKind::Realtime
            ? FstValueProfile::SystemVerilogRealtime
            : type.scalar_kind == SystemVerilogScalarKind::Time
            ? FstValueProfile::SystemVerilogTime
            : type.scalar_kind == SystemVerilogScalarKind::Chandle
            ? FstValueProfile::SystemVerilogOpaqueHandle
            : throw std::invalid_argument("unsupported FST trace type");
        if (value.profile() != expected) {
            throw std::invalid_argument(
                "FST scalar declaration has an incompatible value profile");
        }
        const auto expected_payload = type.kind == TraceTypeKind::SystemVerilogString
            ? FstPayloadKind::String
            : type.kind == TraceTypeKind::SystemVerilogScalar
                && (type.scalar_kind == SystemVerilogScalarKind::ShortReal
                    || type.scalar_kind == SystemVerilogScalarKind::Real
                    || type.scalar_kind == SystemVerilogScalarKind::Realtime)
            ? FstPayloadKind::Real
            : FstPayloadKind::Symbols;
        if (value.payload_kind() != expected_payload) {
            throw std::invalid_argument(
                "FST value payload does not match its declaration");
        }
    }

    [[nodiscard]] bool all_binary(const std::string_view symbols) noexcept
    {
        return std::ranges::all_of(
            symbols, [](const char symbol) { return symbol == '0' || symbol == '1'; });
    }

    void append_packed_binary(Bytes& output, const std::string_view symbols)
    {
        for (std::size_t offset = 0; offset < symbols.size(); offset += 8U) {
            std::uint8_t byte = 0;
            const auto count = std::min<std::size_t>(8U, symbols.size() - offset);
            for (std::size_t bit = 0; bit < count; ++bit) {
                if (symbols[offset + bit] == '1') {
                    byte |= static_cast<std::uint8_t>(1U << (7U - bit));
                }
            }
            append_u8(output, byte);
        }
    }

    [[nodiscard]] std::string logic9_binary_symbols(
        const std::string_view symbols)
    {
        std::string result;
        result.reserve(symbols.size() * 4U);
        for (const auto symbol : symbols) {
            const auto parsed = parse_logic9(symbol);
            if (!parsed) {
                throw std::invalid_argument("unsupported FST Logic9 symbol");
            }
            const auto encoded = static_cast<std::uint8_t>(*parsed);
            for (int shift = 3; shift >= 0; --shift) {
                result.push_back((encoded & (1U << shift)) != 0U ? '1' : '0');
            }
        }
        return result;
    }

    void append_timed_wave_change(
        Bytes& output,
        const std::uint64_t time_delta,
        const std::string_view symbols)
    {
        if (symbols.size() == 1U) {
            if (time_delta > (std::numeric_limits<std::uint64_t>::max() >> 2U)) {
                throw std::length_error("FST scalar time delta overflows");
            }
            const auto value = symbols.front() == '0' ? 0U
                : symbols.front() == '1'              ? 2U
                : symbols.front() == 'x'              ? 1U
                : symbols.front() == 'z'              ? 3U
                                                      : throw std::invalid_argument("unsupported FST scalar symbol");
            append_varint(output, (time_delta << 2U) | value);
            return;
        }
        if (time_delta > (std::numeric_limits<std::uint64_t>::max() >> 1U)) {
            throw std::length_error("FST vector time delta overflows");
        }
        if (all_binary(symbols)) {
            append_varint(output, time_delta << 1U);
            append_packed_binary(output, symbols);
        } else {
            append_varint(output, (time_delta << 1U) | 1U);
            output.insert(output.end(), symbols.begin(), symbols.end());
        }
    }

    void append_timed_wave_change(
        Bytes& output,
        const std::uint64_t time_delta,
        const TraceTypeDeclaration& type,
        const FstPayloadKind payload_kind,
        const std::string_view payload,
        const std::uint64_t real_bits)
    {
        if (time_delta > (std::numeric_limits<std::uint64_t>::max() >> 1U)) {
            throw std::length_error("FST typed time delta overflows");
        }
        if (payload_kind == FstPayloadKind::Real) {
            append_varint(output, (time_delta << 1U) | 1U);
            append_le64(output, real_bits);
            return;
        }
        if (payload_kind == FstPayloadKind::String) {
            append_varint(output, time_delta << 1U);
            append_varint(output, payload.size());
            output.insert(
                output.end(), payload.begin(), payload.end());
            return;
        }
        if (type.kind == TraceTypeKind::VhdlLogic9) {
            append_timed_wave_change(
                output, time_delta, logic9_binary_symbols(payload));
            return;
        }
        append_timed_wave_change(output, time_delta, payload);
    }

    void append_timed_wave_change(
        Bytes& output,
        const std::uint64_t time_delta,
        const TraceTypeDeclaration& type,
        const FstEncodedValue& value)
    {
        append_timed_wave_change(output, time_delta, type,
            value.payload_kind(),
            value.payload_kind() == FstPayloadKind::Real
                ? std::string_view { }
                : value.payload_kind() == FstPayloadKind::String
                ? value.string_bytes()
                : value.symbols(),
            value.real_bits());
    }

    void append_unknown_initial_value(
        Bytes& output,
        const TraceTypeDeclaration& type)
    {
        if (type.kind == TraceTypeKind::SystemVerilogString) {
            return;
        }
        if (type.kind == TraceTypeKind::SystemVerilogScalar
            && (type.scalar_kind == SystemVerilogScalarKind::ShortReal
                || type.scalar_kind == SystemVerilogScalarKind::Real
                || type.scalar_kind == SystemVerilogScalarKind::Realtime)) {
            append_le64(output, UINT64_C(0x7ff8000000000000));
            return;
        }
        output.resize(output.size() + fst_storage_width(type), 'x');
    }

    [[nodiscard]] Bytes encode_positions(
        const std::vector<std::optional<std::uint64_t>>& positions)
    {
        Bytes result;
        std::uint64_t previous_positive = 0;
        std::uint64_t zero_run = 0;
        const auto flush_zeros = [&] {
            if (zero_run != 0) {
                append_varint(result, zero_run << 1U);
                zero_run = 0;
            }
        };
        for (const auto position : positions) {
            if (!position) {
                ++zero_run;
                continue;
            }
            flush_zeros();
            if (*position <= previous_positive
                || *position - previous_positive
                    > (std::numeric_limits<std::uint64_t>::max() >> 1U)) {
                throw std::length_error("FST value position exceeds its field");
            }
            const auto delta = *position - previous_positive;
            append_varint(result, (delta << 1U) | 1U);
            previous_positive = *position;
        }
        flush_zeros();
        return result;
    }

} // namespace

namespace {

    constexpr std::size_t fst_event_merge_fan_in = 8U;
    constexpr std::size_t fst_event_merge_record_overhead
        = 2U * fst_event_merge_fan_in + 4U;

    struct SpoolEvent {
        TraceEvent event;
        std::uint64_t insertion_order { };
        std::uint64_t handle { };
        std::uint64_t time_index { };
        FstValueProfile profile { FstValueProfile::LogicVector };
        FstPayloadKind payload_kind { FstPayloadKind::Symbols };
        std::size_t width { };
        std::uint64_t real_bits { };
        std::string canonical_type;
        std::string payload;
    };

    [[nodiscard]] std::size_t encoded_event_work_bytes(
        const FstEncodedValue& value)
    {
        const auto payload_size
            = value.payload_kind() == FstPayloadKind::Real
            ? 0U
            : value.payload_kind() == FstPayloadKind::String
            ? value.string_bytes().size()
            : value.symbols().size();
        if (value.canonical_type().size()
                > std::numeric_limits<std::size_t>::max()
                    - sizeof(SpoolEvent)
            || payload_size
                > std::numeric_limits<std::size_t>::max()
                    - sizeof(SpoolEvent) - value.canonical_type().size()) {
            throw std::length_error("FST event-work size overflows");
        }
        return sizeof(SpoolEvent) + value.canonical_type().size()
            + payload_size;
    }

    [[nodiscard]] std::size_t spool_event_work_bytes(
        const SpoolEvent& event)
    {
        constexpr auto maximum
            = std::numeric_limits<std::size_t>::max();
        if (event.canonical_type.size() > maximum - sizeof(SpoolEvent)
            || event.payload.size()
                > maximum - sizeof(SpoolEvent) - event.canonical_type.size()) {
            throw std::length_error("FST event-work size overflows");
        }
        return sizeof(SpoolEvent) + event.canonical_type.size()
            + event.payload.size();
    }

    [[nodiscard]] SpoolEvent spool_event(
        const TraceEvent& event,
        const FstEncodedValue& value,
        const std::uint64_t insertion_order)
    {
        SpoolEvent result;
        result.event = event;
        result.insertion_order = insertion_order;
        result.profile = value.profile();
        result.payload_kind = value.payload_kind();
        result.width = value.width();
        result.real_bits = value.real_bits();
        result.canonical_type.assign(value.canonical_type());
        if (value.payload_kind() == FstPayloadKind::String) {
            result.payload.assign(value.string_bytes());
        } else if (value.payload_kind() == FstPayloadKind::Symbols) {
            result.payload.assign(value.symbols());
        }
        return result;
    }

    [[nodiscard]] bool event_order_less(
        const SpoolEvent& left,
        const SpoolEvent& right) noexcept
    {
        if (trace_event_precedes(left.event, right.event)) {
            return true;
        }
        if (trace_event_precedes(right.event, left.event)) {
            return false;
        }
        return left.insertion_order < right.insertion_order;
    }

    [[nodiscard]] bool same_event_identity(
        const TraceEvent& left,
        const TraceEvent& right) noexcept
    {
        return left.time == right.time && left.delta == right.delta
            && left.region == right.region && left.sequence == right.sequence
            && left.signal == right.signal;
    }

    [[nodiscard]] bool indexed_event_order_less(
        const SpoolEvent& left,
        const SpoolEvent& right) noexcept
    {
        if (left.handle != right.handle) {
            return left.handle < right.handle;
        }
        if (left.time_index != right.time_index) {
            return left.time_index < right.time_index;
        }
        return event_order_less(left, right);
    }

    class FstTempWorkspace final {
    public:
        explicit FstTempWorkspace(const std::filesystem::path& parent)
        {
            std::error_code error;
            auto base = parent.empty()
                ? std::filesystem::temp_directory_path(error)
                : parent;
            if (error || base.empty()) {
                throw std::runtime_error(
                    "failed to locate FST temporary directory");
            }
#if defined(_WIN32)
            PSECURITY_DESCRIPTOR security_descriptor = nullptr;
            constexpr auto private_owner_dacl = L"D:P(A;OICI;FA;;;OW)";
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    private_owner_dacl, SDDL_REVISION_1,
                    &security_descriptor, nullptr)) {
                throw std::system_error(
                    static_cast<int>(GetLastError()), std::system_category(),
                    "failed to create FST workspace security descriptor");
            }
            const auto release_descriptor = [](void* descriptor) noexcept {
                if (descriptor != nullptr) {
                    static_cast<void>(LocalFree(descriptor));
                }
            };
            std::unique_ptr<void, decltype(release_descriptor)>
                descriptor_guard { security_descriptor, release_descriptor };
            SECURITY_ATTRIBUTES security_attributes {
                sizeof(SECURITY_ATTRIBUTES), security_descriptor, FALSE
            };
            static std::atomic<std::uint64_t> next_workspace { 0U };
            const auto clock_value = static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            for (std::uint64_t attempt = 0; attempt < 100U; ++attempt) {
                const auto sequence = next_workspace.fetch_add(
                    1U, std::memory_order_relaxed);
                root_ = base / ("fsim-fst-" + std::to_string(clock_value)
                    + "-" + std::to_string(sequence));
                if (CreateDirectoryW(root_.c_str(), &security_attributes)) {
                    return;
                }
                const auto create_error = GetLastError();
                if (create_error != ERROR_ALREADY_EXISTS
                    && create_error != ERROR_FILE_EXISTS) {
                    throw std::system_error(static_cast<int>(create_error),
                        std::system_category(),
                        "failed to create FST temporary workspace");
                }
            }
            throw std::runtime_error(
                "failed to create FST temporary workspace");
#else
            auto native_template = (base / "fsim-fst-XXXXXX").native();
            native_template.push_back('\0');
            if (::mkdtemp(native_template.data()) == nullptr) {
                throw std::system_error(errno, std::generic_category(),
                    "failed to create FST temporary workspace");
            }
            try {
                root_ = std::filesystem::path { native_template.data() };
            } catch (...) {
                static_cast<void>(::rmdir(native_template.data()));
                throw;
            }
#endif
        }

        ~FstTempWorkspace()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root_, ignored);
        }

        FstTempWorkspace(const FstTempWorkspace&) = delete;
        FstTempWorkspace& operator=(const FstTempWorkspace&) = delete;

        [[nodiscard]] std::uint64_t allocate_run_id()
        {
            if (next_run_ == std::numeric_limits<std::uint64_t>::max()) {
                throw std::length_error("FST temporary run count overflows");
            }
            return next_run_++;
        }

        [[nodiscard]] std::filesystem::path run_path(
            const std::uint64_t id) const
        {
            return root_ / ("run-" + std::to_string(id) + ".bin");
        }

        [[nodiscard]] std::filesystem::path named_path(
            const std::string_view name) const
        {
            return root_ / std::string { name };
        }

        void remove_run(const std::uint64_t id) const
        {
            std::error_code ignored;
            std::filesystem::remove(run_path(id), ignored);
        }

    private:
        std::filesystem::path root_;
        std::uint64_t next_run_ { };
    };

    template <typename Value>
    void write_native(std::ostream& output, const Value value)
    {
        static_assert(std::is_trivially_copyable_v<Value>);
        output.write(reinterpret_cast<const char*>(&value), sizeof(Value));
        if (!output) {
            throw std::runtime_error(
                "failed to write FST temporary work file");
        }
    }

    template <typename Value>
    [[nodiscard]] Value read_native(std::istream& input)
    {
        static_assert(std::is_trivially_copyable_v<Value>);
        Value value { };
        input.read(reinterpret_cast<char*>(&value), sizeof(Value));
        if (input.gcount() != static_cast<std::streamsize>(sizeof(Value))) {
            throw std::runtime_error(
                "truncated FST temporary work file");
        }
        return value;
    }

    void write_spool_event(std::ostream& output, const SpoolEvent& event)
    {
        write_native(output, event.event.signal.value);
        write_native(output, event.event.time);
        write_native(output, event.event.delta);
        write_native(output, event.event.sequence);
        write_native(output, event.insertion_order);
        write_native(output, event.handle);
        write_native(output, event.time_index);
        write_native(output, static_cast<std::uint8_t>(event.event.region));
        write_native(output, static_cast<std::uint8_t>(event.profile));
        write_native(output, static_cast<std::uint8_t>(event.payload_kind));
        write_native(output, static_cast<std::uint64_t>(event.width));
        write_native(output, event.real_bits);
        write_native(output,
            static_cast<std::uint64_t>(event.canonical_type.size()));
        write_native(output, static_cast<std::uint64_t>(event.payload.size()));
        output.write(event.canonical_type.data(),
            static_cast<std::streamsize>(event.canonical_type.size()));
        output.write(event.payload.data(),
            static_cast<std::streamsize>(event.payload.size()));
        if (!output) {
            throw std::runtime_error(
                "failed to write FST temporary work file");
        }
    }

    [[nodiscard]] SpoolEvent read_spool_event(
        std::istream& input,
        const FstWriterLimits& limits)
    {
        SpoolEvent event;
        event.event.signal.value = read_native<std::uint64_t>(input);
        event.event.time = read_native<SimulationTick>(input);
        event.event.delta = read_native<std::uint64_t>(input);
        event.event.sequence = read_native<std::uint64_t>(input);
        event.insertion_order = read_native<std::uint64_t>(input);
        event.handle = read_native<std::uint64_t>(input);
        event.time_index = read_native<std::uint64_t>(input);
        const auto region = read_native<std::uint8_t>(input);
        const auto profile = read_native<std::uint8_t>(input);
        const auto payload_kind = read_native<std::uint8_t>(input);
        const auto width = read_native<std::uint64_t>(input);
        event.real_bits = read_native<std::uint64_t>(input);
        const auto type_size = read_native<std::uint64_t>(input);
        const auto payload_size = read_native<std::uint64_t>(input);
        if (region > static_cast<std::uint8_t>(TraceRegion::Callback)
            || profile > static_cast<std::uint8_t>(FstValueProfile::TypedLeaf)
            || payload_kind > static_cast<std::uint8_t>(FstPayloadKind::String)
            || width > std::numeric_limits<std::size_t>::max()
            || type_size > limits.maximum_type_metadata_bytes
            || payload_size > limits.maximum_value_bytes
            || type_size > std::numeric_limits<std::size_t>::max()
            || payload_size > std::numeric_limits<std::size_t>::max()) {
            throw std::runtime_error(
                "invalid FST temporary work record");
        }
        event.event.region = static_cast<TraceRegion>(region);
        event.profile = static_cast<FstValueProfile>(profile);
        event.payload_kind = static_cast<FstPayloadKind>(payload_kind);
        event.width = static_cast<std::size_t>(width);
        event.canonical_type.resize(static_cast<std::size_t>(type_size));
        event.payload.resize(static_cast<std::size_t>(payload_size));
        input.read(event.canonical_type.data(),
            static_cast<std::streamsize>(event.canonical_type.size()));
        if (input.gcount()
            != static_cast<std::streamsize>(event.canonical_type.size())) {
            throw std::runtime_error(
                "truncated FST temporary work file");
        }
        input.read(event.payload.data(),
            static_cast<std::streamsize>(event.payload.size()));
        if (input.gcount()
            != static_cast<std::streamsize>(event.payload.size())) {
            throw std::runtime_error(
                "truncated FST temporary work file");
        }
        return event;
    }

    class FstRunReader final {
    public:
        FstRunReader(
            const FstTempWorkspace& workspace,
            const std::uint64_t id,
            const FstWriterLimits& limits)
            : input_(workspace.run_path(id), std::ios::binary)
            , limits_(limits)
        {
            if (!input_) {
                throw std::runtime_error(
                    "failed to open FST temporary work file");
            }
            remaining_ = read_native<std::uint64_t>(input_);
        }

        [[nodiscard]] bool next(SpoolEvent& event)
        {
            if (remaining_ == 0U) {
                return false;
            }
            event = read_spool_event(input_, limits_);
            --remaining_;
            return true;
        }

    private:
        std::ifstream input_;
        FstWriterLimits limits_;
        std::uint64_t remaining_ { };
    };

    template <typename Less, typename Callback>
    void for_each_merged_run_event(
        const FstTempWorkspace& workspace,
        const std::vector<std::uint64_t>& run_ids,
        const FstWriterLimits& limits,
        Less less,
        Callback callback)
    {
        if (run_ids.size() > fst_event_merge_fan_in) {
            throw std::logic_error("FST merge fan-in exceeded its limit");
        }
        std::vector<FstRunReader> readers;
        std::vector<std::optional<SpoolEvent>> current;
        readers.reserve(run_ids.size());
        current.resize(run_ids.size());
        for (const auto id : run_ids) {
            readers.emplace_back(workspace, id, limits);
            const auto reader_index = readers.size() - 1U;
            SpoolEvent first;
            if (readers[reader_index].next(first)) {
                current[reader_index] = std::move(first);
            }
        }
        const auto compare = [&](const std::size_t left,
                                 const std::size_t right) {
            return less(*current[right], *current[left]);
        };
        std::priority_queue<std::size_t, std::vector<std::size_t>,
            decltype(compare)> queue { compare };
        for (std::size_t index = 0; index < current.size(); ++index) {
            if (current[index]) {
                queue.push(index);
            }
        }
        while (!queue.empty()) {
            const auto reader = queue.top();
            queue.pop();
            callback(std::move(*current[reader]));
            current[reader].reset();
            SpoolEvent next;
            if (readers[reader].next(next)) {
                current[reader] = std::move(next);
                queue.push(reader);
            }
        }
    }

    template <typename Less>
    [[nodiscard]] std::uint64_t merge_run_group(
        FstTempWorkspace& workspace,
        const std::vector<std::uint64_t>& inputs,
        const FstWriterLimits& limits,
        Less less)
    {
        if (inputs.empty() || inputs.size() > fst_event_merge_fan_in) {
            throw std::logic_error("invalid FST temporary merge group");
        }
        const auto output_id = workspace.allocate_run_id();
        std::fstream output(workspace.run_path(output_id),
            std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error(
                "failed to create FST temporary work file");
        }
        write_native(output, std::uint64_t { 0U });
        std::uint64_t count = 0;
        for_each_merged_run_event(workspace, inputs, limits, less,
            [&](SpoolEvent event) {
                if (count == std::numeric_limits<std::uint64_t>::max()) {
                    throw std::length_error(
                        "FST temporary event count overflows");
                }
                write_spool_event(output, event);
                ++count;
            });
        output.seekp(0);
        write_native(output, count);
        output.close();
        if (!output) {
            throw std::runtime_error(
                "failed to finish FST temporary work file");
        }
        for (const auto id : inputs) {
            workspace.remove_run(id);
        }
        return output_id;
    }

    template <typename Less>
    class FstSortedRunSet final {
    public:
        void clear() noexcept { levels_.clear(); }

        void add(
            FstTempWorkspace& workspace,
            const std::uint64_t run_id,
            const FstWriterLimits& limits,
            Less less)
        {
            auto current_id = run_id;
            std::size_t level = 0;
            while (true) {
                if (level == levels_.size()) {
                    levels_.emplace_back();
                }
                levels_[level].push_back(current_id);
                if (levels_[level].size() < fst_event_merge_fan_in) {
                    return;
                }
                auto inputs = std::move(levels_[level]);
                levels_[level].clear();
                current_id = merge_run_group(
                    workspace, inputs, limits, less);
                ++level;
            }
        }

        [[nodiscard]] std::vector<std::uint64_t> finish(
            FstTempWorkspace& workspace,
            const FstWriterLimits& limits,
            Less less)
        {
            std::vector<std::uint64_t> runs;
            for (auto& level : levels_) {
                runs.insert(runs.end(), level.begin(), level.end());
            }
            levels_.clear();
            while (runs.size() > fst_event_merge_fan_in) {
                std::vector<std::uint64_t> next;
                next.reserve((runs.size() + fst_event_merge_fan_in - 1U)
                    / fst_event_merge_fan_in);
                for (std::size_t offset = 0; offset < runs.size();
                     offset += fst_event_merge_fan_in) {
                    const auto count = std::min(
                        fst_event_merge_fan_in, runs.size() - offset);
                    if (count == 1U) {
                        next.push_back(runs[offset]);
                    } else {
                        std::vector<std::uint64_t> group(
                            runs.begin() + static_cast<std::ptrdiff_t>(offset),
                            runs.begin() + static_cast<std::ptrdiff_t>(offset + count));
                        next.push_back(merge_run_group(
                            workspace, group, limits, less));
                    }
                }
                runs = std::move(next);
            }
            return runs;
        }

    private:
        std::vector<std::vector<std::uint64_t>> levels_;
    };

    [[nodiscard]] std::size_t varint_size(std::uint64_t value) noexcept
    {
        std::size_t result = 1U;
        while (value >= 0x80U) {
            value >>= 7U;
            ++result;
        }
        return result;
    }

    void append_varint_to_stream(std::ostream& output, std::uint64_t value)
    {
        std::array<char, 10> bytes { };
        std::size_t size = 0;
        do {
            auto byte = static_cast<std::uint8_t>(value & 0x7fU);
            value >>= 7U;
            if (value != 0U) {
                byte |= 0x80U;
            }
            bytes[size++] = static_cast<char>(byte);
        } while (value != 0U);
        output.write(bytes.data(), static_cast<std::streamsize>(size));
        if (!output) {
            throw std::runtime_error(
                "failed to write FST temporary work file");
        }
    }

    void checked_add_size(std::size_t& total, const std::size_t value)
    {
        if (value > std::numeric_limits<std::size_t>::max() - total) {
            throw std::length_error("FST container size overflows");
        }
        total += value;
    }

} // namespace

struct FstWriter::Impl {
    using EventRuns = FstSortedRunSet<decltype(&event_order_less)>;
    using IndexedRuns = FstSortedRunSet<decltype(&indexed_event_order_less)>;

    explicit Impl(
        std::ostream& destination,
        const std::int8_t scale,
        FstWriterLimits writer_limits,
        const FstWriterCompression writer_compression)
        : output(destination)
        , timescale_exponent(scale)
        , limits(std::move(writer_limits))
        , compression(writer_compression)
    {
        if (scale > 0 || scale < -18) {
            throw std::invalid_argument(
                "FST timescale exponent must be between -18 and 0");
        }
        if (compression > FstWriterCompression::Deterministic) {
            throw std::invalid_argument("unknown FST writer compression profile");
        }
        if (limits.maximum_scopes == 0 || limits.maximum_signals == 0
            || limits.maximum_events == 0
            || limits.maximum_timestamps == 0
            || limits.maximum_name_bytes == 0
            || limits.maximum_type_metadata_bytes == 0
            || limits.maximum_value_bytes == 0
            || limits.maximum_hierarchy_bytes == 0
            || limits.maximum_buffer_bytes == 0
            || limits.maximum_event_work_bytes == 0
            || limits.maximum_container_bytes < 330U) {
            throw std::invalid_argument("FST writer limits must be nonzero");
        }
    }

    std::ostream& output;
    std::int8_t timescale_exponent;
    FstWriterLimits limits;
    FstWriterCompression compression;
    std::unique_ptr<TraceDeclarationModel> declarations;
    std::unordered_map<std::uint64_t, FstEncodedValue> initial_values;
    EventRuns event_runs;
    std::vector<SpoolEvent> event_buffer;
    std::size_t event_buffer_payload_bytes { };
    std::size_t retained_bytes { };
    std::size_t buffered_bytes { };
    std::size_t event_count { };
    std::uint64_t next_insertion_order { };
    std::uint64_t published_bytes { };
    std::unique_ptr<FstTempWorkspace> workspace;
    SimulationTick initial_time { };
    FstWriterState state { FstWriterState::configuring };
    std::string failure;
    bool started { };

    void release_buffers() noexcept
    {
        std::vector<SpoolEvent> { }.swap(event_buffer);
        event_buffer_payload_bytes = 0;
        initial_values.clear();
        declarations.reset();
        event_runs.clear();
        workspace.reset();
        retained_bytes = 0;
        buffered_bytes = 0;
    }

    void fail(const std::string_view message) noexcept
    {
        if (state == FstWriterState::failed) {
            return;
        }
        state = FstWriterState::failed;
        try {
            failure.assign(message);
        } catch (...) {
        }
        release_buffers();
    }

    void abandon() noexcept
    {
        if (state != FstWriterState::complete
            && state != FstWriterState::failed) {
            fail("FST writer was destroyed before a clean close");
        }
    }

    [[noreturn]] void reject_lifecycle(const std::string_view message)
    {
        fail(message);
        throw std::logic_error(std::string { message });
    }

    void require_buffer_capacity(
        const std::size_t bytes,
        const std::string_view message)
    {
        if (buffered_bytes > limits.maximum_buffer_bytes
            || bytes > limits.maximum_buffer_bytes - buffered_bytes) {
            throw std::length_error(std::string { message });
        }
    }

    void update_buffered_bytes()
    {
        if (event_buffer.capacity()
            > std::numeric_limits<std::size_t>::max() / sizeof(SpoolEvent)) {
            throw std::length_error("FST event-work size overflows");
        }
        auto event_work
            = event_buffer.capacity() * sizeof(SpoolEvent);
        checked_add_size(event_work, event_buffer_payload_bytes);
        if (retained_bytes > std::numeric_limits<std::size_t>::max()
                - event_work) {
            throw std::length_error("FST buffered size overflows");
        }
        buffered_bytes = retained_bytes + event_work;
    }

    void ensure_workspace()
    {
        if (!workspace) {
            workspace = std::make_unique<FstTempWorkspace>(
                limits.temporary_directory);
        }
    }

    template <typename Runs, typename Less>
    void spill_event_buffer(Runs& runs, Less less)
    {
        if (event_buffer.empty()) {
            return;
        }
        ensure_workspace();
        std::ranges::sort(event_buffer, less);
        if (event_buffer.size()
            > std::numeric_limits<std::uint64_t>::max()) {
            throw std::length_error("FST temporary event count overflows");
        }
        const auto run_id = workspace->allocate_run_id();
        std::ofstream run(workspace->run_path(run_id),
            std::ios::binary | std::ios::trunc);
        if (!run) {
            throw std::runtime_error(
                "failed to create FST temporary work file");
        }
        write_native(run, static_cast<std::uint64_t>(event_buffer.size()));
        for (const auto& event : event_buffer) {
            write_spool_event(run, event);
        }
        run.close();
        if (!run) {
            throw std::runtime_error(
                "failed to finish FST temporary work file");
        }
        std::vector<SpoolEvent> { }.swap(event_buffer);
        event_buffer_payload_bytes = 0;
        update_buffered_bytes();
        runs.add(*workspace, run_id, limits, less);
    }

    template <typename Runs, typename Less>
    void stage_spool_event(
        SpoolEvent event,
        Runs& runs,
        Less less,
        const std::size_t work_limit)
    {
        const auto event_work = spool_event_work_bytes(event);
        const auto event_payload_bytes
            = event.canonical_type.size() + event.payload.size();
        if (event_work
            > limits.maximum_event_work_bytes
                / fst_event_merge_record_overhead) {
            throw std::length_error(
                "FST individual event exceeds its permitted event-work budget");
        }
        if (retained_bytes > limits.maximum_buffer_bytes
            || event_work > limits.maximum_buffer_bytes - retained_bytes) {
            throw std::length_error("FST change buffer exceeds its limit");
        }

        const auto capacity_fits = [&](const std::size_t capacity) {
            if (capacity
                > std::numeric_limits<std::size_t>::max()
                    / sizeof(SpoolEvent)) {
                return false;
            }
            auto work = capacity * sizeof(SpoolEvent);
            if (event_buffer_payload_bytes
                    > std::numeric_limits<std::size_t>::max()
                        - event_payload_bytes) {
                return false;
            }
            const auto payload_bytes
                = event_buffer_payload_bytes + event_payload_bytes;
            if (payload_bytes
                > std::numeric_limits<std::size_t>::max() - work) {
                return false;
            }
            work += payload_bytes;
            return work <= work_limit
                && retained_bytes <= limits.maximum_buffer_bytes
                && work <= limits.maximum_buffer_bytes - retained_bytes;
        };

        auto target_capacity = event_buffer.capacity();
        if (event_buffer.size() == target_capacity) {
            if (target_capacity == 0U) {
                target_capacity = 1U;
            } else if (target_capacity
                > std::numeric_limits<std::size_t>::max() / 2U) {
                target_capacity = event_buffer.size() + 1U;
            } else {
                target_capacity *= 2U;
            }
        }
        if (!capacity_fits(target_capacity)) {
            spill_event_buffer(runs, less);
            target_capacity = 1U;
            if (!capacity_fits(target_capacity)) {
                throw std::length_error(
                    "FST change buffer exceeds its limit");
            }
        }
        if (event_buffer.capacity() < target_capacity) {
            event_buffer.reserve(target_capacity);
        }
        event_buffer.push_back(std::move(event));
        event_buffer_payload_bytes += event_payload_bytes;
        update_buffered_bytes();
        if (buffered_bytes > limits.maximum_buffer_bytes
            || event_buffer.capacity() * sizeof(SpoolEvent)
                    + event_buffer_payload_bytes
                > work_limit) {
            throw std::length_error(
                "FST event-work buffer exceeds its limit");
        }
    }

    [[nodiscard]] bool has_root_declarations() const
    {
        return std::ranges::any_of(
            declarations->entries(),
            [&](const TraceDeclarationEntry& entry) {
                return (entry.kind == TraceDeclarationKind::Variable
                               ? declarations->variable(entry.id).scope
                               : declarations->alias(entry.id).scope)
                           .value
                    == 0U;
            });
    }

    [[nodiscard]] Bytes hierarchy(
        std::unordered_map<std::uint64_t, std::uint64_t>& handles,
        std::vector<const TraceTypeDeclaration*>& types) const
    {
        const auto entries = declarations->entries();
        std::unordered_map<std::uint64_t, std::vector<TraceDeclarationEntry>>
            by_scope;
        for (const auto& entry : entries) {
            const auto scope = entry.kind == TraceDeclarationKind::Variable
                ? declarations->variable(entry.id).scope
                : declarations->alias(entry.id).scope;
            by_scope[scope.value].push_back(entry);
        }

        const auto assign_scope_handles = [&](const TraceScopeId scope) {
            if (const auto found = by_scope.find(scope.value);
                found != by_scope.end()) {
                for (const auto& entry : found->second) {
                    if (entry.kind != TraceDeclarationKind::Variable) {
                        continue;
                    }
                    const auto& variable = declarations->variable(entry.id);
                    const auto [handle, inserted] = handles.emplace(
                        variable.id.value,
                        static_cast<std::uint64_t>(handles.size()) + 1U);
                    if (!inserted) {
                        throw std::logic_error(
                            "FST variable handle was assigned twice");
                    }
                    static_cast<void>(handle);
                    types.push_back(&declarations->type(variable.type));
                }
            }
        };
        const auto assign_handles = [&](
                                        const TraceScopeId scope,
                                        auto&& assign_handles_ref) -> void {
            assign_scope_handles(scope);
            for (const auto& child : declarations->scopes()) {
                if (child.parent == scope) {
                    assign_handles_ref(child.id, assign_handles_ref);
                }
            }
        };
        assign_handles({ }, assign_handles);

        Bytes result;
        const auto emit_scope_entries = [&](const TraceScopeId scope) {
            if (const auto found = by_scope.find(scope.value);
                found != by_scope.end()) {
                for (const auto& entry : found->second) {
                    const TraceVariableDeclaration* variable = nullptr;
                    const TraceSourceDeclaration* source = nullptr;
                    TraceSignalId variable_id;
                    std::string_view reference;
                    std::string_view path;
                    std::uint64_t alias_handle = 0;
                    if (entry.kind == TraceDeclarationKind::Variable) {
                        variable = &declarations->variable(entry.id);
                        variable_id = entry.id;
                        reference = variable->reference;
                        path = variable->hierarchical_name;
                        source = &declarations->source(variable->source);
                    } else {
                        const auto& alias = declarations->alias(entry.id);
                        variable = &declarations->variable(alias.target);
                        variable_id = alias.target;
                        reference = alias.reference;
                        path = alias.hierarchical_name;
                        source = &declarations->source(alias.source);
                    }
                    require_bounded_name(reference, limits);
                    const auto& type = declarations->type(variable->type);
                    if (type.canonical_metadata.size()
                        > limits.maximum_type_metadata_bytes) {
                        throw std::length_error(
                            "FST type metadata exceeds its writer limit");
                    }
                    require_type_profile(type);
                    const auto handle = handles.find(variable_id.value);
                    if (handle == handles.end()) {
                        throw std::logic_error(
                            "FST declaration target lacks a handle");
                    }
                    if (entry.kind == TraceDeclarationKind::Alias) {
                        alias_handle = handle->second;
                    }
                    append_declaration_record(
                        result, *source, type, entry.kind, entry.id,
                        entry.kind == TraceDeclarationKind::Alias
                            ? variable_id
                            : TraceSignalId { },
                        path, limits);
                    append_provenance_record(
                        result, *source, entry.kind, entry.id,
                        entry.kind == TraceDeclarationKind::Alias
                            ? variable_id
                            : TraceSignalId { },
                        limits);
                    append_u8(result, fst_variable_type(type));
                    append_u8(result, fst_direction_implicit);
                    append_c_string(result, reference);
                    append_varint(result, fst_storage_width(type));
                    append_varint(result, alias_handle);
                }
            }
        };
        const auto emit_entries = [&](
                                      const TraceScopeId scope,
                                      auto&& emit_entries_ref) -> void {
            if (scope.value == 0U && by_scope.contains(0U)) {
                append_u8(result, fst_scope);
                append_u8(result, fst_scope_module);
                append_c_string(result, "__fsim_root");
                append_c_string(result, { });
                emit_scope_entries(scope);
                append_u8(result, fst_upscope);
            } else {
                emit_scope_entries(scope);
            }
            for (const auto& child : declarations->scopes()) {
                if (child.parent != scope) {
                    continue;
                }
                require_bounded_name(child.name, limits);
                const auto& source = declarations->source(child.source);
                if (!source.owner_identity.empty()) {
                    require_bounded_name(source.owner_identity, limits);
                }
                append_u8(result, fst_scope);
                append_u8(result, fst_scope_type(child, source));
                append_c_string(result, child.name);
                append_c_string(result, source.owner_identity);
                emit_entries_ref(child.id, emit_entries_ref);
                append_u8(result, fst_upscope);
            }
        };
        emit_entries({ }, emit_entries);
        return result;
    }

    [[nodiscard]] Bytes header(
        const SimulationTick final_time,
        const std::size_t handle_count) const
    {
        Bytes result;
        append_u8(result, fst_block_header);
        append_be64(result, fst_header_section_length);
        append_be64(result, initial_time);
        append_be64(result, final_time);
        result.insert(
            result.end(), fst_endian_test.begin(), fst_endian_test.end());
        append_be64(result, 0);
        append_be64(result, declarations->scopes().size()
                + static_cast<std::size_t>(has_root_declarations()));
        append_be64(result, declarations->entries().size());
        append_be64(result, handle_count);
        append_be64(result, handle_count == 0 ? 0 : 1);
        append_u8(result, static_cast<std::uint8_t>(timescale_exponent));
        const auto profile = compression == FstWriterCompression::Deterministic
            ? kFstDeterministicContainerProfile
            : kFstStoredContainerProfile;
        result.insert(result.end(), profile.begin(), profile.end());
        result.resize(result.size() + 128U - profile.size(), 0);
        result.resize(result.size() + 119U, 0);
        append_u8(result, 0);
        append_be64(result, 0);
        if (result.size() != 330U) {
            throw std::logic_error("FST header size is not canonical");
        }
        return result;
    }

    void write_output_bytes(const char* data, std::size_t size)
    {
        constexpr std::size_t chunk_size = 64U * 1024U;
        while (size != 0U) {
            const auto count = std::min(size, chunk_size);
            output.write(data, static_cast<std::streamsize>(count));
            if (!output) {
                throw std::runtime_error("failed to publish FST output");
            }
            data += count;
            size -= count;
        }
    }

    void write_output_bytes(const Bytes& bytes)
    {
        if (!bytes.empty()) {
            write_output_bytes(
                reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
    }

    void write_output_be64(const std::uint64_t value)
    {
        Bytes bytes;
        append_be64(bytes, value);
        write_output_bytes(bytes);
    }

    void write_output_varint(std::uint64_t value)
    {
        Bytes bytes;
        append_varint(bytes, value);
        write_output_bytes(bytes);
    }

    void copy_workspace_file(
        const std::filesystem::path& path,
        const std::uint64_t expected_size)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error(
                "failed to read FST temporary work file");
        }
        std::array<char, 64U * 1024U> buffer { };
        auto remaining = expected_size;
        while (remaining != 0U) {
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buffer.size()));
            input.read(buffer.data(), static_cast<std::streamsize>(count));
            if (input.gcount() != static_cast<std::streamsize>(count)) {
                throw std::runtime_error(
                    "truncated FST temporary work file");
            }
            write_output_bytes(buffer.data(), count);
            remaining -= count;
        }
    }

    void publish_container(const SimulationTick final_time)
    {
        if (!declarations) {
            throw std::logic_error("FST declarations were not provided");
        }
        if (final_time < initial_time) {
            throw std::invalid_argument("FST final time precedes initial time");
        }
        if (declarations->scopes().size() > limits.maximum_scopes
            || declarations->entries().size() > limits.maximum_signals) {
            throw std::length_error("FST declaration count exceeds its limit");
        }

        std::unordered_map<std::uint64_t, std::uint64_t> handles;
        std::vector<const TraceTypeDeclaration*> types;
        auto hierarchy_bytes = hierarchy(handles, types);
        if (hierarchy_bytes.size() > limits.maximum_hierarchy_bytes) {
            throw std::length_error("FST hierarchy block exceeds its limit");
        }
        ensure_workspace();

        const auto times_path = workspace->named_path("timestamps.bin");
        std::ofstream times_output(
            times_path, std::ios::binary | std::ios::trunc);
        if (!times_output) {
            throw std::runtime_error(
                "failed to create FST temporary work file");
        }
        std::size_t times_size = 0;
        std::size_t timestamp_count = 0;
        std::optional<SimulationTick> previous_timestamp;
        const auto append_timestamp = [&](const SimulationTick timestamp) {
            if (timestamp_count == limits.maximum_timestamps) {
                throw std::length_error(
                    "FST timestamp count exceeds its limit");
            }
            const auto delta = previous_timestamp
                ? timestamp - *previous_timestamp
                : timestamp;
            append_varint_to_stream(times_output, delta);
            checked_add_size(times_size, varint_size(delta));
            previous_timestamp = timestamp;
            ++timestamp_count;
        };
        if (!initial_values.empty()) {
            append_timestamp(initial_time);
        }

        spill_event_buffer(event_runs, &event_order_less);
        auto first_pass_runs = event_runs.finish(
            *workspace, limits, &event_order_less);
        event_runs.clear();

        IndexedRuns indexed_runs;
        std::optional<TraceEvent> previous_event;
        std::size_t indexed_count = 0;
        for_each_merged_run_event(
            *workspace, first_pass_runs, limits, &event_order_less,
            [&](SpoolEvent event) {
                if (event.event.time > final_time) {
                    throw std::invalid_argument(
                        "FST change time exceeds final time");
                }
                if (previous_event
                    && same_event_identity(*previous_event, event.event)) {
                    throw std::invalid_argument(
                        "FST change ordering identity is duplicated");
                }
                previous_event = event.event;
                const auto handle = handles.find(event.event.signal.value);
                if (handle == handles.end()) {
                    throw std::out_of_range(
                        "FST change references an undeclared signal");
                }
                if (!previous_timestamp
                    || event.event.time != *previous_timestamp) {
                    append_timestamp(event.event.time);
                }
                event.handle = handle->second;
                event.time_index
                    = static_cast<std::uint64_t>(timestamp_count - 1U);
                stage_spool_event(
                    std::move(event), indexed_runs,
                    &indexed_event_order_less,
                    limits.maximum_event_work_bytes / 4U);
                ++indexed_count;
            });
        if (indexed_count != event_count) {
            throw std::runtime_error(
                "FST temporary merge lost an event");
        }
        for (const auto run_id : first_pass_runs) {
            workspace->remove_run(run_id);
        }
        times_output.close();
        if (!times_output) {
            throw std::runtime_error(
                "failed to finish FST temporary work file");
        }
        spill_event_buffer(indexed_runs, &indexed_event_order_less);
        auto indexed_pass_runs = indexed_runs.finish(
            *workspace, limits, &indexed_event_order_less);

        std::size_t bits_size = 0;
        Bytes initial_bits;
        std::vector<TraceSignalId> signals(types.size());
        for (const auto& [signal, handle] : handles) {
            if (handle == 0U || handle > signals.size()) {
                throw std::logic_error(
                    "FST signal handle is outside its geometry block");
            }
            signals[static_cast<std::size_t>(handle - 1U)] = { signal };
        }
        for (const auto* type : types) {
            const auto width = fst_storage_width(*type);
            if (width > limits.maximum_container_bytes - bits_size) {
                throw std::length_error(
                    "FST initial value frame exceeds its limit");
            }
            bits_size += width;
            append_unknown_initial_value(initial_bits, *type);
        }
        if (initial_bits.size() != bits_size) {
            throw std::logic_error("FST initial value width is inconsistent");
        }
        auto stored_bits = initial_bits;
        const auto& initial_compression = fst_compression_profile(
            FstCompressionKind::InitialValueZlibFixedV1);
        if (compression == FstWriterCompression::Deterministic
            && initial_bits.size()
                >= initial_compression.minimum_input_bytes) {
            stored_bits = compress_fst_block(initial_bits,
                FstCompressionKind::InitialValueZlibFixedV1,
                { limits.maximum_container_bytes,
                    limits.maximum_container_bytes })
                              .bytes;
        }

        const auto waves_path = workspace->named_path("waves.bin");
        std::ofstream waves_output(
            waves_path, std::ios::binary | std::ios::trunc);
        if (!waves_output) {
            throw std::runtime_error(
                "failed to create FST temporary work file");
        }
        std::vector<std::optional<std::uint64_t>> wave_positions(types.size());
        std::uint64_t waves_size = 0;
        std::uint64_t active_chain_count = 0;
        std::size_t current_handle = 0;
        bool handle_started = false;
        std::optional<std::uint64_t> previous_time_index;
        const auto append_wave_bytes = [&](const Bytes& bytes) {
            if (bytes.size()
                > limits.maximum_container_bytes
                    - static_cast<std::size_t>(waves_size)) {
                throw std::length_error(
                    "FST value frame exceeds its container limit");
            }
            if (!bytes.empty()) {
                waves_output.write(
                    reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
                if (!waves_output) {
                    throw std::runtime_error(
                        "failed to write FST temporary work file");
                }
                waves_size += static_cast<std::uint64_t>(bytes.size());
            }
        };
        const auto append_chain_prefix = [&](const std::size_t handle_index) {
            if (waves_size == std::numeric_limits<std::uint64_t>::max()) {
                throw std::length_error("FST value position exceeds its field");
            }
            wave_positions[handle_index] = waves_size + 1U;
            Bytes marker;
            append_varint(marker, 0U);
            append_wave_bytes(marker);
            if (active_chain_count == std::numeric_limits<std::uint64_t>::max()) {
                throw std::length_error("FST active chain count overflows");
            }
            ++active_chain_count;
        };
        const auto start_handle = [&](const std::size_t handle_index) {
            handle_started = true;
            previous_time_index.reset();
            const auto found = initial_values.find(
                signals[handle_index].value);
            if (found != initial_values.end()) {
                append_chain_prefix(handle_index);
                const auto& type = *types[handle_index];
                Bytes initial_wave;
                append_timed_wave_change(
                    initial_wave, 0U, type, found->second);
                append_wave_bytes(initial_wave);
                previous_time_index = 0U;
            }
        };
        const auto append_event = [&](const SpoolEvent& event) {
            const auto handle_index
                = static_cast<std::size_t>(event.handle - 1U);
            if (handle_index >= types.size()
                || event.canonical_type != types[handle_index]->canonical_metadata
                || event.width != types[handle_index]->width) {
                throw std::runtime_error(
                    "FST temporary event does not match its declaration");
            }
            if (!handle_started || current_handle != handle_index) {
                while (!handle_started || current_handle < handle_index) {
                    if (handle_started) {
                        ++current_handle;
                    }
                    start_handle(current_handle);
                }
            }
            const auto time_delta = previous_time_index
                ? event.time_index - *previous_time_index
                : event.time_index;
            if (!previous_time_index) {
                append_chain_prefix(handle_index);
            }
            Bytes change_bytes;
            append_timed_wave_change(change_bytes, time_delta,
                *types[handle_index], event.payload_kind, event.payload,
                event.real_bits);
            append_wave_bytes(change_bytes);
            previous_time_index = event.time_index;
        };
        for_each_merged_run_event(
            *workspace, indexed_pass_runs, limits,
            &indexed_event_order_less,
            [&](SpoolEvent event) { append_event(event); });
        for (const auto run_id : indexed_pass_runs) {
            workspace->remove_run(run_id);
        }
        if (!types.empty()) {
            if (!handle_started) {
                current_handle = 0;
                start_handle(current_handle);
            }
            while (current_handle + 1U < types.size()) {
                ++current_handle;
                start_handle(current_handle);
            }
        }
        waves_output.close();
        if (!waves_output) {
            throw std::runtime_error(
                "failed to finish FST temporary work file");
        }
        if (waves_size > limits.maximum_container_bytes
            || active_chain_count > waves_size) {
            throw std::length_error(
                "FST value frame exceeds its container limit");
        }
        const auto memory_required
            = waves_size - active_chain_count;
        const auto positions = encode_positions(wave_positions);

        Bytes geometry;
        for (const auto* type : types) {
            append_varint(geometry, fst_storage_width(*type));
        }
        Bytes geometry_payload;
        append_be64(geometry_payload, geometry.size());
        append_be64(geometry_payload, geometry.size());
        geometry_payload.insert(
            geometry_payload.end(), geometry.begin(), geometry.end());

        const auto compressed_hierarchy = compress_fst_block(
            hierarchy_bytes, FstCompressionKind::HierarchyGzipStoreV1,
            { limits.maximum_hierarchy_bytes,
                limits.maximum_container_bytes });
        Bytes hierarchy_payload;
        append_be64(hierarchy_payload, hierarchy_bytes.size());
        hierarchy_payload.insert(
            hierarchy_payload.end(),
            compressed_hierarchy.bytes.begin(),
            compressed_hierarchy.bytes.end());

        const auto header_bytes = header(final_time, types.size());
        std::size_t values_payload_size = 0;
        if (!types.empty()) {
            values_payload_size = 24U;
            checked_add_size(values_payload_size, varint_size(bits_size));
            checked_add_size(
                values_payload_size, varint_size(stored_bits.size()));
            checked_add_size(
                values_payload_size, varint_size(types.size()));
            checked_add_size(values_payload_size, stored_bits.size());
            const auto encoded_chains
                = active_chain_count == 0U ? 0U : types.size();
            checked_add_size(
                values_payload_size, varint_size(encoded_chains));
            checked_add_size(values_payload_size, 1U);
            if (waves_size > std::numeric_limits<std::size_t>::max()) {
                throw std::length_error("FST value frame size overflows");
            }
            checked_add_size(
                values_payload_size, static_cast<std::size_t>(waves_size));
            checked_add_size(values_payload_size, positions.size());
            checked_add_size(values_payload_size, 8U);
            checked_add_size(values_payload_size, times_size);
            checked_add_size(values_payload_size, 8U);
            checked_add_size(values_payload_size, 8U);
            checked_add_size(values_payload_size, 8U);
        }

        std::size_t total_size = header_bytes.size();
        if (types.empty()) {
            checked_add_size(total_size, 36U);
        } else {
            checked_add_size(total_size, 9U);
            checked_add_size(total_size, values_payload_size);
        }
        checked_add_size(total_size, 9U);
        checked_add_size(total_size, geometry_payload.size());
        checked_add_size(total_size, 9U);
        checked_add_size(total_size, hierarchy_payload.size());
        if (total_size > limits.maximum_container_bytes) {
            throw std::length_error("FST container exceeds its limit");
        }

        write_output_bytes(header_bytes);
        if (types.empty()) {
            std::array<std::uint8_t, 36> skip_block { };
            skip_block[0] = fst_block_skip;
            write_output_bytes(reinterpret_cast<const char*>(skip_block.data()),
                skip_block.size());
        } else {
            const auto block_type = fst_block_value_changes;
            write_output_bytes(
                reinterpret_cast<const char*>(&block_type), sizeof(block_type));
            write_output_be64(
                static_cast<std::uint64_t>(values_payload_size) + 8U);
            write_output_be64(initial_time);
            write_output_be64(final_time);
            write_output_be64(memory_required);
            write_output_varint(bits_size);
            write_output_varint(stored_bits.size());
            write_output_varint(types.size());
            write_output_bytes(stored_bits);
            const auto encoded_chains
                = active_chain_count == 0U ? 0U : types.size();
            write_output_varint(encoded_chains);
            const auto marker = static_cast<std::uint8_t>('4');
            write_output_bytes(
                reinterpret_cast<const char*>(&marker), sizeof(marker));
            copy_workspace_file(waves_path, waves_size);
            write_output_bytes(positions);
            write_output_be64(positions.size());
            copy_workspace_file(times_path, times_size);
            write_output_be64(times_size);
            write_output_be64(times_size);
            write_output_be64(timestamp_count);
        }

        const auto geometry_type = fst_block_geometry;
        write_output_bytes(
            reinterpret_cast<const char*>(&geometry_type), sizeof(geometry_type));
        write_output_be64(
            static_cast<std::uint64_t>(geometry_payload.size()) + 8U);
        write_output_bytes(geometry_payload);
        const auto hierarchy_type = fst_block_hierarchy;
        write_output_bytes(reinterpret_cast<const char*>(&hierarchy_type),
            sizeof(hierarchy_type));
        write_output_be64(
            static_cast<std::uint64_t>(hierarchy_payload.size()) + 8U);
        write_output_bytes(hierarchy_payload);
        output.flush();
        if (!output) {
            throw std::runtime_error("failed to flush FST output");
        }
        published_bytes = total_size;
    }
};

FstWriter::FstWriter(
    std::ostream& output,
    const std::int8_t timescale_exponent,
    FstWriterLimits limits,
    const FstWriterCompression compression)
    : impl_(std::make_unique<Impl>(
        output, timescale_exponent, limits, compression))
{
}

FstWriter::~FstWriter()
{
    if (impl_) {
        impl_->abandon();
    }
}
FstWriter::FstWriter(FstWriter&&) noexcept = default;
FstWriter& FstWriter::operator=(FstWriter&& other) noexcept
{
    if (this != &other) {
        if (impl_) {
            impl_->abandon();
        }
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void FstWriter::declare(const TraceDeclarationModel& model)
{
    if (!impl_) {
        throw std::logic_error("FST writer was moved from");
    }
    if (impl_->state == FstWriterState::failed) {
        throw std::logic_error(
            "FST writer is terminal after failure: " + impl_->failure);
    }
    if (impl_->state != FstWriterState::configuring) {
        impl_->reject_lifecycle(
            "cannot replace FST declarations after begin");
    }
    impl_->buffered_bytes = 0;
    try {
        impl_->declarations = std::make_unique<TraceDeclarationModel>(model);
        if (impl_->declarations->scopes().size() > impl_->limits.maximum_scopes
            || impl_->declarations->entries().size()
                > impl_->limits.maximum_signals) {
            throw std::length_error(
                "FST declaration count exceeds its limit");
        }
        std::unordered_map<std::uint64_t, std::uint64_t> handles;
        std::vector<const TraceTypeDeclaration*> types;
        const auto hierarchy = impl_->hierarchy(handles, types);
        if (hierarchy.size() > impl_->limits.maximum_hierarchy_bytes) {
            throw std::length_error("FST hierarchy block exceeds its limit");
        }
        impl_->require_buffer_capacity(
            hierarchy.size(), "FST declaration buffer exceeds its limit");
        impl_->retained_bytes = hierarchy.size();
        impl_->buffered_bytes = hierarchy.size();
    } catch (const std::bad_alloc& error) {
        impl_->fail(error.what());
        throw;
    } catch (const std::length_error& error) {
        impl_->fail(error.what());
        throw;
    } catch (...) {
        impl_->declarations.reset();
        impl_->buffered_bytes = 0;
        throw;
    }
}

void FstWriter::begin(const SimulationTick initial_time)
{
    if (!impl_) {
        throw std::logic_error("FST writer was moved from");
    }
    if (impl_->state == FstWriterState::failed) {
        throw std::logic_error(
            "FST writer is terminal after failure: " + impl_->failure);
    }
    if (impl_->state != FstWriterState::configuring) {
        impl_->reject_lifecycle("FST writer has already begun");
    }
    if (!impl_->declarations) {
        impl_->reject_lifecycle("FST declarations must precede begin");
    }
    impl_->initial_time = initial_time;
    impl_->started = true;
    impl_->state = FstWriterState::open;
}

void FstWriter::set_initial_value(
    const TraceSignalId signal,
    const FstEncodedValue& value)
{
    if (!impl_) {
        throw std::logic_error("FST writer was moved from");
    }
    if (impl_->state == FstWriterState::failed) {
        throw std::logic_error(
            "FST writer is terminal after failure: " + impl_->failure);
    }
    if (impl_->state != FstWriterState::open) {
        impl_->reject_lifecycle(
            "FST initial values require an open writer");
    }
    const auto& variable = impl_->declarations->variable(signal);
    const auto& type = impl_->declarations->type(variable.type);
    require_value_profile(type, value);
    if (value.string_bytes().size() > impl_->limits.maximum_value_bytes) {
        constexpr std::string_view message
            = "FST value exceeds its writer limit";
        impl_->fail(message);
        throw std::length_error(std::string { message });
    }
    if (impl_->initial_values.contains(signal.value)) {
        impl_->reject_lifecycle(
            "FST initial value was already provided");
    }
    try {
        const auto bytes = buffered_value_bytes(value, sizeof(signal.value));
        impl_->require_buffer_capacity(
            bytes, "FST initial-value buffer exceeds its limit");
        impl_->initial_values.emplace(signal.value, value);
        impl_->retained_bytes += bytes;
        impl_->buffered_bytes = impl_->retained_bytes
            + impl_->event_buffer.capacity() * sizeof(SpoolEvent)
            + impl_->event_buffer_payload_bytes;
    } catch (const std::length_error& error) {
        impl_->fail(error.what());
        throw;
    } catch (const std::exception& error) {
        impl_->fail(error.what());
        throw;
    } catch (...) {
        impl_->fail("unknown FST initial-value buffering failure");
        throw;
    }
}

void FstWriter::change(
    const TraceEvent& event,
    const FstEncodedValue& value)
{
    if (!impl_) {
        throw std::logic_error("FST writer was moved from");
    }
    if (impl_->state == FstWriterState::failed) {
        throw std::logic_error(
            "FST writer is terminal after failure: " + impl_->failure);
    }
    if (impl_->state != FstWriterState::open) {
        impl_->reject_lifecycle("FST changes require an open writer");
    }
    if (event.time < impl_->initial_time) {
        throw std::invalid_argument("FST change precedes initial time");
    }
    const auto& variable = impl_->declarations->variable(event.signal);
    const auto& type = impl_->declarations->type(variable.type);
    require_value_profile(type, value);
    if (value.string_bytes().size() > impl_->limits.maximum_value_bytes) {
        constexpr std::string_view message
            = "FST value exceeds its writer limit";
        impl_->fail(message);
        throw std::length_error(std::string { message });
    }
    try {
        if (impl_->event_count == impl_->limits.maximum_events) {
            throw std::length_error("FST change count exceeds its limit");
        }
        if (impl_->next_insertion_order
            == std::numeric_limits<std::uint64_t>::max()) {
            throw std::length_error("FST change ordering identity overflows");
        }
        const auto event_work = encoded_event_work_bytes(value);
        if (event_work
            > impl_->limits.maximum_event_work_bytes
                / fst_event_merge_record_overhead) {
            throw std::length_error(
                "FST individual event exceeds its permitted event-work budget");
        }
        if (impl_->retained_bytes > impl_->limits.maximum_buffer_bytes
            || event_work > impl_->limits.maximum_buffer_bytes
                - impl_->retained_bytes) {
            throw std::length_error("FST change buffer exceeds its limit");
        }
        auto record = spool_event(
            event, value, impl_->next_insertion_order);
        impl_->stage_spool_event(
            std::move(record), impl_->event_runs, &event_order_less,
            impl_->limits.maximum_event_work_bytes / 2U);
        ++impl_->next_insertion_order;
        ++impl_->event_count;
    } catch (const std::length_error& error) {
        impl_->fail(error.what());
        throw;
    } catch (const std::exception& error) {
        impl_->fail(error.what());
        throw;
    } catch (...) {
        impl_->fail("unknown FST change buffering failure");
        throw;
    }
}

void FstWriter::flush()
{
    if (!impl_) {
        throw std::logic_error("FST writer was moved from");
    }
    if (impl_->state == FstWriterState::failed) {
        throw std::logic_error(
            "FST writer is terminal after failure: " + impl_->failure);
    }
    if (impl_->state != FstWriterState::open) {
        impl_->reject_lifecycle("FST flush requires an open writer");
    }
    try {
        impl_->spill_event_buffer(
            impl_->event_runs, &event_order_less);
        impl_->output.flush();
        if (!impl_->output) {
            throw std::runtime_error("failed to flush FST output");
        }
    } catch (const std::exception& error) {
        impl_->fail(error.what());
        throw;
    } catch (...) {
        impl_->fail("unknown FST flush failure");
        throw;
    }
}

void FstWriter::close(const SimulationTick final_time)
{
    if (!impl_) {
        throw std::logic_error("FST writer was moved from");
    }
    if (impl_->state == FstWriterState::failed) {
        throw std::logic_error(
            "FST writer is terminal after failure: " + impl_->failure);
    }
    if (impl_->state != FstWriterState::open) {
        impl_->reject_lifecycle(impl_->state == FstWriterState::complete
                ? "FST writer is already closed"
                : "FST writer has not begun");
    }
    try {
        impl_->publish_container(final_time);
        impl_->state = FstWriterState::complete;
        impl_->release_buffers();
    } catch (const std::exception& error) {
        impl_->fail(error.what());
        throw;
    } catch (...) {
        impl_->fail("unknown FST close failure");
        throw;
    }
}

bool FstWriter::begun() const noexcept
{
    return impl_ && impl_->started;
}

bool FstWriter::closed() const noexcept
{
    return impl_ && impl_->state == FstWriterState::complete;
}

std::uint64_t FstWriter::bytes_written() const noexcept
{
    return impl_ ? impl_->published_bytes : 0;
}

FstWriterStatus FstWriter::status() const
{
    if (!impl_) {
        return { FstWriterState::failed, 0, 0,
            "FST writer was moved from" };
    }
    return { impl_->state, impl_->buffered_bytes,
        impl_->published_bytes, impl_->failure };
}

} // namespace fsim::runtime
