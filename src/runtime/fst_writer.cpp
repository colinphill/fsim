// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_writer.hpp"

#include "fsim/runtime/fst_change_encoder.hpp"
#include "fsim/runtime/fst_compression.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

    void append_block(
        Bytes& output,
        const std::uint8_t type,
        const Bytes& payload)
    {
        if (payload.size() > std::numeric_limits<std::uint64_t>::max() - 8U) {
            throw std::length_error("FST block length exceeds the container field");
        }
        append_u8(output, type);
        append_be64(output, static_cast<std::uint64_t>(payload.size()) + 8U);
        output.insert(output.end(), payload.begin(), payload.end());
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
        const FstEncodedValue& value)
    {
        if (time_delta > (std::numeric_limits<std::uint64_t>::max() >> 1U)) {
            throw std::length_error("FST typed time delta overflows");
        }
        if (value.payload_kind() == FstPayloadKind::Real) {
            append_varint(output, (time_delta << 1U) | 1U);
            append_le64(output, value.real_bits());
            return;
        }
        if (value.payload_kind() == FstPayloadKind::String) {
            append_varint(output, time_delta << 1U);
            append_varint(output, value.string_bytes().size());
            output.insert(
                output.end(),
                value.string_bytes().begin(),
                value.string_bytes().end());
            return;
        }
        if (type.kind == TraceTypeKind::VhdlLogic9) {
            append_timed_wave_change(
                output, time_delta, logic9_binary_symbols(value.symbols()));
            return;
        }
        append_timed_wave_change(output, time_delta, value.symbols());
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

struct FstWriter::Impl {
    explicit Impl(
        std::ostream& destination,
        const std::int8_t scale,
        FstWriterLimits writer_limits,
        const FstWriterCompression writer_compression)
        : output(destination)
        , timescale_exponent(scale)
        , limits(writer_limits)
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
            || limits.maximum_container_bytes < 330U) {
            throw std::invalid_argument("FST writer limits must be nonzero");
        }
        changes = std::make_unique<FstChangeEncoder>(FstChangeEncoderLimits {
            limits.maximum_events,
            limits.maximum_timestamps,
            limits.maximum_buffer_bytes });
    }

    std::ostream& output;
    std::int8_t timescale_exponent;
    FstWriterLimits limits;
    FstWriterCompression compression;
    std::unique_ptr<TraceDeclarationModel> declarations;
    std::unordered_map<std::uint64_t, FstEncodedValue> initial_values;
    std::unique_ptr<FstChangeEncoder> changes;
    SimulationTick initial_time { };
    std::size_t buffered_bytes { };
    std::uint64_t published_bytes { };
    FstWriterState state { FstWriterState::configuring };
    std::string failure;
    bool started { };

    void release_buffers() noexcept
    {
        changes.reset();
        initial_values.clear();
        declarations.reset();
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
            fail(message);
            throw std::length_error(std::string { message });
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
        append_be64(result, declarations->scopes().size() + static_cast<std::size_t>(has_root_declarations()));
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

    [[nodiscard]] Bytes container(
        const SimulationTick final_time,
        const FstOrderedChanges& ordered_changes) const
    {
        if (!declarations) {
            throw std::logic_error("FST declarations were not provided");
        }
        if (final_time < initial_time) {
            throw std::invalid_argument("FST final time precedes initial time");
        }
        if (!ordered_changes.values().empty()
            && ordered_changes.values().back().event.time > final_time) {
            throw std::invalid_argument("FST change time exceeds final time");
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

        auto result = header(final_time, types.size());
        if (!types.empty()) {
            std::size_t bits_size = 0;
            for (const auto* type : types) {
                const auto width = fst_storage_width(*type);
                if (width > limits.maximum_container_bytes - bits_size) {
                    throw std::length_error("FST initial value frame exceeds its limit");
                }
                bits_size += width;
            }
            Bytes initial_bits;
            Bytes waves;
            std::vector<std::optional<std::uint64_t>> wave_positions;
            std::uint64_t active_chain_count = 0;
            std::vector<TraceSignalId> signals(types.size());
            for (const auto& [signal, handle] : handles) {
                signals.at(static_cast<std::size_t>(handle - 1U)) = { signal };
            }
            std::vector<SimulationTick> timestamps;
            if (!initial_values.empty()) {
                timestamps.push_back(initial_time);
            }
            for (const auto& change : ordered_changes.values()) {
                if (timestamps.empty() || timestamps.back() != change.event.time) {
                    timestamps.push_back(change.event.time);
                }
            }
            if (!std::ranges::is_sorted(timestamps)) {
                throw std::logic_error("FST timestamps are not ordered");
            }
            if (timestamps.size() > limits.maximum_timestamps) {
                throw std::length_error("FST timestamp count exceeds its limit");
            }
            std::unordered_map<SimulationTick, std::uint64_t> time_indices;
            for (std::size_t index = 0; index < timestamps.size(); ++index) {
                time_indices.emplace(timestamps[index], index);
            }
            std::vector<std::vector<const FstValueChange*>> changes_by_handle(
                types.size());
            for (const auto& change : ordered_changes.values()) {
                const auto found = handles.find(change.event.signal.value);
                if (found == handles.end()) {
                    throw std::out_of_range(
                        "FST change references an undeclared signal");
                }
                changes_by_handle.at(
                                     static_cast<std::size_t>(found->second - 1U))
                    .push_back(&change);
            }
            for (std::size_t index = 0; index < signals.size(); ++index) {
                const auto signal = signals[index];
                const auto& type = *types[index];
                append_unknown_initial_value(initial_bits, type);
                std::optional<std::uint64_t> previous_time_index;
                if (const auto found = initial_values.find(signal.value);
                    found != initial_values.end()) {
                    if (waves.size()
                        == std::numeric_limits<std::uint64_t>::max()) {
                        throw std::length_error(
                            "FST value position exceeds its field");
                    }
                    wave_positions.push_back(
                        static_cast<std::uint64_t>(waves.size()) + 1U);
                    append_varint(waves, 0U);
                    ++active_chain_count;
                    const auto time_index = time_indices.at(initial_time);
                    append_timed_wave_change(
                        waves, time_index, type, found->second);
                    previous_time_index = time_index;
                } else {
                    wave_positions.push_back(std::nullopt);
                }
                for (const auto* change : changes_by_handle[index]) {
                    const auto time_index
                        = time_indices.at(change->event.time);
                    const auto time_delta = previous_time_index
                        ? time_index - previous_time_index.value()
                        : time_index;
                    if (!previous_time_index) {
                        if (waves.size()
                            == std::numeric_limits<std::uint64_t>::max()) {
                            throw std::length_error(
                                "FST value position exceeds its field");
                        }
                        wave_positions[index]
                            = static_cast<std::uint64_t>(waves.size()) + 1U;
                        append_varint(waves, 0U);
                        ++active_chain_count;
                    }
                    append_timed_wave_change(
                        waves, time_delta, type, change->value);
                    previous_time_index = time_index;
                }
            }
            auto stored_bits = initial_bits;
            const auto& compression_profile = fst_compression_profile(
                FstCompressionKind::InitialValueZlibFixedV1);
            if (compression == FstWriterCompression::Deterministic
                && initial_bits.size()
                >= compression_profile.minimum_input_bytes) {
                stored_bits = compress_fst_block(initial_bits,
                    FstCompressionKind::InitialValueZlibFixedV1,
                    { limits.maximum_container_bytes,
                        limits.maximum_container_bytes })
                                  .bytes;
            }
            const auto positions = encode_positions(wave_positions);
            Bytes times;
            std::optional<SimulationTick> previous_time;
            for (const auto timestamp : timestamps) {
                append_varint(times,
                    previous_time ? timestamp - previous_time.value()
                                  : timestamp);
                previous_time = timestamp;
            }
            if (stored_bits.size() > limits.maximum_container_bytes
                || waves.size()
                    > limits.maximum_container_bytes - stored_bits.size()
                || positions.size()
                    > limits.maximum_container_bytes - stored_bits.size()
                        - waves.size()
                || times.size() > limits.maximum_container_bytes
                        - stored_bits.size()
                        - waves.size() - positions.size()) {
                throw std::length_error(
                    "FST value frame exceeds its container limit");
            }
            const auto memory_required = waves.size()
                - static_cast<std::size_t>(active_chain_count);
            Bytes values_payload;
            append_be64(values_payload, initial_time);
            append_be64(values_payload, final_time);
            append_be64(values_payload, memory_required);
            append_varint(values_payload, bits_size);
            append_varint(values_payload, stored_bits.size());
            append_varint(values_payload, types.size());
            values_payload.insert(
                values_payload.end(), stored_bits.begin(), stored_bits.end());
            append_varint(
                values_payload, active_chain_count == 0U ? 0U : types.size());
            append_u8(values_payload, '4');
            values_payload.insert(
                values_payload.end(), waves.begin(), waves.end());
            values_payload.insert(
                values_payload.end(), positions.begin(), positions.end());
            append_be64(values_payload, positions.size());
            values_payload.insert(
                values_payload.end(), times.begin(), times.end());
            append_be64(values_payload, times.size());
            append_be64(values_payload, times.size());
            append_be64(values_payload, timestamps.size());
            append_block(result, fst_block_value_changes, values_payload);
        } else {
            append_u8(result, fst_block_skip);
            result.resize(result.size() + 35U, 0);
        }
        append_block(result, fst_block_geometry, geometry_payload);
        append_block(result, fst_block_hierarchy, hierarchy_payload);
        if (result.size() > limits.maximum_container_bytes) {
            throw std::length_error("FST container exceeds its limit");
        }
        return result;
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
        impl_->buffered_bytes += bytes;
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
        const auto bytes = buffered_value_bytes(value, sizeof(event));
        impl_->require_buffer_capacity(
            bytes, "FST change buffer exceeds its limit");
        impl_->changes->append(event, value);
        impl_->buffered_bytes += bytes;
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
        const auto ordered_changes = std::move(*impl_->changes).freeze();
        impl_->changes.reset();
        const auto bytes = impl_->container(final_time, ordered_changes);
        if (bytes.size()
            > static_cast<std::size_t>(
                std::numeric_limits<std::streamsize>::max())) {
            throw std::length_error(
                "FST container exceeds the output stream size limit");
        }
        impl_->output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!impl_->output) {
            throw std::runtime_error("failed to publish FST output");
        }
        impl_->output.flush();
        if (!impl_->output) {
            throw std::runtime_error("failed to flush FST output");
        }
        impl_->published_bytes = bytes.size();
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
