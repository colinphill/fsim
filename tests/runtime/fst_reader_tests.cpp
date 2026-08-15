// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_reader.hpp"
#include "fsim/runtime/fst_compression.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct TestBlock {
    std::size_t offset { };
    std::size_t payload_offset { };
    std::size_t payload_size { };
    std::size_t end { };
};

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

[[nodiscard]] fsim::runtime::FstReaderTrace read_trace_or_abort(
    const std::string_view bytes)
{
    auto result = fsim::runtime::read_fst(bytes);
    if (!result.ok()) {
        for (const auto& diagnostic : result.diagnostics) {
            std::cerr << diagnostic.code << " at " << diagnostic.offset
                      << ": " << diagnostic.message << '\n';
        }
        std::abort();
    }
    return std::move(*result.trace);
}

[[nodiscard]] const fsim::runtime::FstReaderDeclaration& find_declaration(
    const fsim::runtime::FstReaderTrace& trace,
    const std::string_view path)
{
    const auto found = std::ranges::find(trace.declarations, path,
        &fsim::runtime::FstReaderDeclaration::path);
    assert(found != trace.declarations.end());
    return *found;
}

[[nodiscard]] const fsim::runtime::FstReaderValue& find_value(
    const fsim::runtime::FstReaderTrace& trace,
    const fsim::runtime::TraceSignalId signal,
    const std::size_t sequence)
{
    const auto found = std::ranges::find_if(trace.values,
        [&](const auto& value) {
            return value.signal == signal
                && value.handle_sequence == sequence;
        });
    assert(found != trace.values.end());
    return *found;
}

[[nodiscard]] std::uint64_t read_varint(
    const std::string& bytes,
    std::size_t& offset)
{
    std::uint64_t result = 0;
    unsigned shift = 0;
    while (true) {
        assert(offset < bytes.size() && shift < 64U);
        const auto byte = static_cast<unsigned char>(bytes[offset++]);
        result |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0U) {
            return result;
        }
        shift += 7U;
    }
}

[[nodiscard]] std::uint64_t read_le64(
    const std::string& bytes,
    const std::size_t offset)
{
    assert(offset + 8U <= bytes.size());
    std::uint64_t result = 0;
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        result |= static_cast<std::uint64_t>(
                      static_cast<unsigned char>(bytes[offset + shift / 8U]))
            << shift;
    }
    return result;
}

[[nodiscard]] std::uint64_t read_be64(
    const std::string& bytes,
    const std::size_t offset)
{
    assert(offset + 8U <= bytes.size());
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8U; ++index) {
        result = (result << 8U)
            | static_cast<unsigned char>(bytes[offset + index]);
    }
    return result;
}

void write_be64(
    std::string& bytes, const std::size_t offset, const std::uint64_t value)
{
    assert(offset + 8U <= bytes.size());
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes[offset + index] = static_cast<char>(
            value >> ((7U - index) * 8U));
    }
}

void append_be64(std::string& bytes, const std::uint64_t value)
{
    const auto offset = bytes.size();
    bytes.resize(offset + 8U);
    write_be64(bytes, offset, value);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::string& bytes, const std::size_t offset)
{
    assert(offset + 2U <= bytes.size());
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(bytes[offset])
        | static_cast<std::uint16_t>(
            static_cast<unsigned char>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] TestBlock find_block(
    const std::string& bytes, const std::uint8_t type)
{
    std::size_t offset = 330U;
    while (offset < bytes.size()) {
        const auto found_type = static_cast<std::uint8_t>(bytes[offset]);
        assert(found_type != 0xffU);
        const auto length = static_cast<std::size_t>(
            read_be64(bytes, offset + 1U));
        assert(length >= 8U && length <= bytes.size() - offset - 1U);
        if (found_type == type) {
            return { offset, offset + 9U, length - 8U,
                offset + 1U + length };
        }
        offset += 1U + length;
    }
    assert(false);
    return { };
}

[[nodiscard]] std::string inflate_test_hierarchy(
    const std::string& bytes, const TestBlock& block)
{
    const auto expected = static_cast<std::size_t>(
        read_be64(bytes, block.payload_offset));
    std::size_t offset = block.payload_offset + 8U;
    assert(static_cast<unsigned char>(bytes[offset]) == 0x1fU);
    offset += 10U;
    std::string result;
    result.reserve(expected);
    bool final = false;
    while (!final) {
        const auto control = static_cast<unsigned char>(bytes.at(offset++));
        final = (control & 1U) != 0U;
        assert((control & 0xfeU) == 0U);
        const auto count = read_le16(bytes, offset);
        const auto complement = read_le16(bytes, offset + 2U);
        assert(static_cast<std::uint16_t>(~count) == complement);
        offset += 4U;
        result.append(bytes, offset, count);
        offset += count;
    }
    assert(result.size() == expected);
    return result;
}

[[nodiscard]] std::string replace_hierarchy(
    const std::string& bytes, const std::string_view hierarchy)
{
    using namespace fsim::runtime;
    const auto block = find_block(bytes, 4U);
    assert(block.end == bytes.size());
    const auto input = std::span {
        reinterpret_cast<const std::uint8_t*>(hierarchy.data()),
        hierarchy.size()
    };
    const auto compressed = compress_fst_block(
        input, FstCompressionKind::HierarchyGzipStoreV1);
    std::string result = bytes.substr(0U, block.offset);
    result.push_back(static_cast<char>(4U));
    append_be64(result, 16U + compressed.bytes.size());
    append_be64(result, hierarchy.size());
    result.append(reinterpret_cast<const char*>(compressed.bytes.data()),
        compressed.bytes.size());
    return result;
}

void replace_once(std::string& bytes, const std::string_view before,
    const std::string_view after)
{
    assert(before.size() == after.size());
    const auto offset = bytes.find(before);
    assert(offset != std::string::npos);
    assert(bytes.find(before, offset + 1U) == std::string::npos);
    bytes.replace(offset, before.size(), after);
}

void expect_failure(const std::string& bytes, const std::string_view code)
{
    const auto original = bytes;
    const auto first = fsim::runtime::read_fst(bytes);
    const auto second = fsim::runtime::read_fst(bytes);
    assert(bytes == original);
    assert(!first.trace && !second.trace);
    assert(first.diagnostics.size() == 1U);
    assert(second.diagnostics.size() == 1U);
    assert(first.diagnostics.front().code == code);
    assert(first.diagnostics.front().code == second.diagnostics.front().code);
    assert(first.diagnostics.front().message
        == second.diagnostics.front().message);
    assert(first.diagnostics.front().offset
        == second.diagnostics.front().offset);
}

void expect_result_failure(
    const fsim::runtime::FstReaderResult& result,
    const std::string_view code)
{
    assert(!result.trace);
    assert(result.diagnostics.size() == 1U);
    assert(result.diagnostics.front().code == code);
}

void test_extended_initial_value_equivalence()
{
    using namespace fsim::runtime;
    FstExtendedTypeMetadata enum_metadata;
    enum_metadata.kind = FstExtendedTypeKind::Enumeration;
    enum_metadata.language = FstTypeLanguage::SystemVerilog;
    enum_metadata.nominal_name = "state_t";
    enum_metadata.width = 3U;
    enum_metadata.four_state = true;
    enum_metadata.enumeration_literals = { "IDLE", "BUSY", "ERROR" };
    FstExtendedTypeMetadata physical_metadata;
    physical_metadata.kind = FstExtendedTypeKind::VhdlPhysical;
    physical_metadata.language = FstTypeLanguage::Vhdl;
    physical_metadata.nominal_name = "duration";
    physical_metadata.width = 32U;
    physical_metadata.physical_units = { { "fs", 1 }, { "ps", 1'000 } };
    FstExtendedTypeMetadata time_metadata;
    time_metadata.kind = FstExtendedTypeKind::VhdlTime;
    time_metadata.language = FstTypeLanguage::Vhdl;
    time_metadata.nominal_name = "@builtin:time";
    time_metadata.width = 64U;

    TraceDeclarationBuilder builder;
    const auto real = builder.add_typed_variable(
        "top.real_value", TraceTypeKind::SystemVerilogScalar, 64U,
        SystemVerilogScalarKind::Real,
        canonical_fst_systemverilog_real_type(SystemVerilogScalarKind::Real));
    const auto shortreal = builder.add_typed_variable(
        "top.short_value", TraceTypeKind::SystemVerilogScalar, 32U,
        SystemVerilogScalarKind::ShortReal,
        canonical_fst_systemverilog_real_type(
            SystemVerilogScalarKind::ShortReal));
    const auto string = builder.add_typed_variable(
        "top.string_value", TraceTypeKind::SystemVerilogString, 0U,
        SystemVerilogScalarKind::None, canonical_fst_string_type());
    const auto enumeration = builder.add_typed_variable(
        "top.state_value", TraceTypeKind::Enumeration, 3U,
        SystemVerilogScalarKind::None,
        canonical_fst_type_metadata(enum_metadata));
    const auto physical = builder.add_typed_variable(
        "top.physical_value", TraceTypeKind::VhdlPhysical, 32U,
        SystemVerilogScalarKind::None,
        canonical_fst_type_metadata(physical_metadata));
    const auto time = builder.add_typed_variable(
        "top.time_value", TraceTypeKind::VhdlTime, 64U,
        SystemVerilogScalarKind::None,
        canonical_fst_type_metadata(time_metadata));
    const auto model = std::move(builder).freeze();

    std::ostringstream output;
    FstWriter writer { output, -9 };
    writer.declare(model);
    writer.begin();
    writer.set_initial_value(real, encode_fst_systemverilog_real({ SystemVerilogScalarKind::Real, UINT64_C(0x8000000000000000) }));
    writer.set_initial_value(shortreal, encode_fst_systemverilog_real({ SystemVerilogScalarKind::ShortReal, UINT32_C(0x3fc00000) }));
    writer.set_initial_value(
        string, encode_fst_string(std::string_view { "A\0B\xc3\xa9", 5U }));
    writer.set_initial_value(enumeration, encode_fst_extended_value(PackedLogic4::from_msb_string("1X0"), FstValueProfile::Enumeration, enum_metadata));
    writer.set_initial_value(physical, encode_fst_extended_value(PackedLogic4::from_aval_bval(32U, 123U, 0U), FstValueProfile::VhdlPhysical, physical_metadata));
    writer.set_initial_value(time, encode_fst_extended_value(PackedLogic4::from_aval_bval(64U, 25U, 0U), FstValueProfile::VhdlTime, time_metadata));
    writer.close(1U);

    const auto bytes = output.str();
    const auto trace = read_trace_or_abort(bytes);
    assert(trace.initial_time == 0U);
    assert(trace.final_time == 1U);
    assert(trace.timescale_exponent == -9);
    assert(trace.declarations.size() == 6U);
    assert(trace.timestamps == std::vector<SimulationTick> { 0U });
    assert(trace.values.size() == 6U);
    assert(!trace.semantic_digest.empty());
    assert(trace.profile == kFstDeterministicContainerProfile);
    assert(trace.compression == FstReaderCompression::Deterministic);
    const auto& real_declaration
        = find_declaration(trace, "top.real_value");
    const auto& short_declaration
        = find_declaration(trace, "top.short_value");
    const auto& string_declaration
        = find_declaration(trace, "top.string_value");
    const auto& enum_declaration
        = find_declaration(trace, "top.state_value");
    const auto& physical_declaration
        = find_declaration(trace, "top.physical_value");
    const auto& time_declaration
        = find_declaration(trace, "top.time_value");
    assert(real_declaration.type_kind == TraceTypeKind::SystemVerilogScalar);
    assert(real_declaration.scalar_kind == SystemVerilogScalarKind::Real);
    assert(real_declaration.canonical_metadata
        == canonical_fst_systemverilog_real_type(
            SystemVerilogScalarKind::Real));
    assert(short_declaration.scalar_kind
        == SystemVerilogScalarKind::ShortReal);
    assert(string_declaration.type_kind == TraceTypeKind::SystemVerilogString);
    assert(enum_declaration.canonical_metadata
        == canonical_fst_type_metadata(enum_metadata));
    assert(physical_declaration.canonical_metadata
        == canonical_fst_type_metadata(physical_metadata));
    assert(time_declaration.canonical_metadata
        == canonical_fst_type_metadata(time_metadata));
    assert(find_value(trace, real, 0U).real_bits
        == UINT64_C(0x8000000000000000));
    assert(find_value(trace, shortreal, 0U).real_bits
        == UINT64_C(0x3ff8000000000000));
    assert(find_value(trace, string, 0U).payload
        == std::string("A\0B\xc3\xa9", 5U));
    assert(find_value(trace, enumeration, 0U).payload == "1x0");
    assert(find_value(trace, physical, 0U).payload
        == std::string(25U, '0') + "1111011");
    assert(find_value(trace, time, 0U).payload
        == std::string(59U, '0') + "11001");
    std::size_t offset = 363U;
    const auto initial_size = read_varint(bytes, offset);
    assert(initial_size == 115U);
    assert(read_varint(bytes, offset) == initial_size);
    assert(read_varint(bytes, offset) == 6U);
    offset += static_cast<std::size_t>(initial_size);
    assert(read_varint(bytes, offset) == 6U);
    assert(bytes.at(offset++) == '4');

    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 1U);
    assert(read_le64(bytes, offset) == UINT64_C(0x8000000000000000));
    offset += 8U;
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 1U);
    assert(read_le64(bytes, offset) == UINT64_C(0x3ff8000000000000));
    offset += 8U;
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 5U);
    assert(bytes.substr(offset, 5U) == std::string("A\0B\xc3\xa9", 5U));
    offset += 5U;
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 1U);
    assert(bytes.substr(offset, 3U) == "1x0");
    offset += 3U;
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 0U);
    assert(bytes.substr(offset, 4U) == std::string("\0\0\0\x7b", 4U));
    offset += 4U;
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 0U);
    assert(bytes.substr(offset, 8U)
        == std::string("\0\0\0\0\0\0\0\x19", 8U));
}

void test_typed_leaf_initial_value_equivalence()
{
    using namespace fsim::runtime;
    FstLeafTypeMetadata class_leaf;
    class_leaf.kind = FstLeafKind::DynamicClass;
    class_leaf.owner_identity = "object-7@generation-2";
    class_leaf.leaf_path = "payload.status";
    class_leaf.width = 5U;
    class_leaf.four_state = true;
    FstLeafTypeMetadata strength_leaf;
    strength_leaf.kind = FstLeafKind::ResolvedStrengthZero;
    strength_leaf.owner_identity = "top.resolved";
    strength_leaf.leaf_path = "value.strength0";
    strength_leaf.width = 3U;
    FstExtendedTypeMetadata logic9_metadata;
    logic9_metadata.kind = FstExtendedTypeKind::VhdlLogic9;
    logic9_metadata.language = FstTypeLanguage::Vhdl;
    logic9_metadata.nominal_name = "std_logic_vector";
    logic9_metadata.width = 9U;
    logic9_metadata.enumeration_literals = {
        "'U'", "'X'", "'0'", "'1'", "'Z'", "'W'", "'L'", "'H'", "'-'"
    };

    TraceDeclarationBuilder builder;
    const auto status = builder.add_typed_variable(
        "dynamic.object_7.status", TraceTypeKind::TypedLeaf, 5U,
        SystemVerilogScalarKind::None,
        canonical_fst_leaf_type_metadata(class_leaf));
    const auto strength = builder.add_typed_variable(
        "top.resolved_strength0", TraceTypeKind::TypedLeaf, 3U,
        SystemVerilogScalarKind::None,
        canonical_fst_leaf_type_metadata(strength_leaf));
    const auto logic9 = builder.add_typed_variable(
        "top.logic9", TraceTypeKind::VhdlLogic9, 9U,
        SystemVerilogScalarKind::None,
        canonical_fst_type_metadata(logic9_metadata));
    static_cast<void>(builder.add_alias("dynamic.latest_status", status));
    const auto model = std::move(builder).freeze();
    assert(model.variable(status).type != model.variable(strength).type);
    assert(model.alias({ 4U }).target == status);

    std::ostringstream output;
    FstWriter writer { output, -9 };
    writer.declare(model);
    writer.begin();
    writer.set_initial_value(status,
        encode_fst_leaf_value(
            PackedLogic4::from_msb_string("10XZ1"), class_leaf));
    writer.set_initial_value(strength,
        encode_fst_leaf_value(
            PackedLogic4::from_msb_string("101"), strength_leaf));
    writer.set_initial_value(logic9,
        encode_fst_extended_value(
            PackedLogic4::from_logic9_msb_string("UX01ZWLH-"),
            FstValueProfile::VhdlLogic9, logic9_metadata));
    writer.close(1U);

    const auto bytes = output.str();
    const auto trace = read_trace_or_abort(bytes);
    assert(trace.declarations.size() == 4U);
    assert(trace.timestamps == std::vector<SimulationTick> { 0U });
    assert(trace.values.size() == 3U);
    const auto& status_declaration
        = find_declaration(trace, "dynamic.object_7.status");
    const auto& alias_declaration
        = find_declaration(trace, "dynamic.latest_status");
    assert(status_declaration.canonical_metadata
        == canonical_fst_leaf_type_metadata(class_leaf));
    assert(alias_declaration.kind == TraceDeclarationKind::Alias);
    assert(alias_declaration.target == status);
    assert(alias_declaration.handle == status_declaration.handle);
    assert(find_value(trace, status, 0U).payload == "10xz1");
    assert(find_value(trace, strength, 0U).payload == "101");
    assert(find_value(trace, logic9, 0U).payload == "ux01zwlh-");
    std::size_t offset = 363U;
    const auto initial_size = read_varint(bytes, offset);
    assert(initial_size == 44U);
    assert(read_varint(bytes, offset) == initial_size);
    assert(read_varint(bytes, offset) == 3U);
    offset += static_cast<std::size_t>(initial_size);
    assert(read_varint(bytes, offset) == 3U);
    assert(bytes.at(offset++) == '4');
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 1U);
    assert(bytes.substr(offset, 5U) == "10xz1");
    offset += 5U;
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 0U);
    assert(static_cast<unsigned char>(bytes.at(offset)) == 0xa0U);
    ++offset;
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 0U);
    const std::string logic9_bytes { "\x01\x23\x45\x67\x80", 5U };
    assert(bytes.substr(offset, 5U) == logic9_bytes);
}

void test_ordered_change_equivalence()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto first = builder.add_variable("top.first", 1U);
    static_cast<void>(builder.add_alias("mirror.first", first));
    const auto second = builder.add_variable("top.second", 2U);
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output, -9 };
    writer.declare(model);
    writer.begin(5U);
    writer.set_initial_value(first,
        encode_fst_logic_value(PackedLogic4::from_msb_string("x")));
    writer.change({ first, 7U, 0U, TraceRegion::Active, 4U },
        encode_fst_logic_value(PackedLogic4::from_msb_string("1")));
    writer.change({ second, 6U, 0U, TraceRegion::Observed, 3U },
        encode_fst_logic_value(PackedLogic4::from_msb_string("10")));
    writer.change({ first, 5U, 0U, TraceRegion::Active, 2U },
        encode_fst_logic_value(PackedLogic4::from_msb_string("z")));
    writer.change({ first, 5U, 0U, TraceRegion::Active, 1U },
        encode_fst_logic_value(PackedLogic4::from_msb_string("0")));
    writer.close(7U);

    const auto bytes = output.str();
    const auto trace = read_trace_or_abort(bytes);
    assert(trace.initial_time == 5U);
    assert(trace.final_time == 7U);
    assert(trace.declarations.size() == 3U);
    assert(trace.timestamps == std::vector<SimulationTick>({ 5U, 6U, 7U }));
    assert(trace.values.size() == 5U);
    const auto& alias = find_declaration(trace, "mirror.first");
    assert(alias.kind == TraceDeclarationKind::Alias);
    assert(alias.target == first);
    assert(find_value(trace, first, 0U).payload == "x");
    assert(find_value(trace, first, 1U).payload == "0");
    assert(find_value(trace, first, 2U).payload == "z");
    assert(find_value(trace, first, 3U).payload == "1");
    assert(find_value(trace, second, 0U).payload == "10");
    assert(find_value(trace, second, 0U).time == 6U);
    assert(read_be64(bytes, 57U) == 2U);
    std::size_t offset = 363U;
    assert(read_varint(bytes, offset) == 3U);
    assert(read_varint(bytes, offset) == 3U);
    assert(read_varint(bytes, offset) == 2U);
    assert(bytes.substr(offset, 3U) == "xxx");
    offset += 3U;
    assert(read_varint(bytes, offset) == 2U);
    assert(bytes.at(offset++) == '4');
    const std::string wave_bytes {
        "\x00\x01\x00\x03\x0a\x00\x02\x80", 8U
    };
    assert(bytes.substr(offset, wave_bytes.size()) == wave_bytes);
    offset += wave_bytes.size();
    assert(read_varint(bytes, offset) == 3U);
    assert(read_varint(bytes, offset) == 11U);
    assert(read_be64(bytes, offset) == 2U);
    offset += 8U;
    assert(read_varint(bytes, offset) == 5U);
    assert(read_varint(bytes, offset) == 1U);
    assert(read_varint(bytes, offset) == 1U);
    assert(read_be64(bytes, offset) == 3U);
    offset += 8U;
    assert(read_be64(bytes, offset) == 3U);
    offset += 8U;
    assert(read_be64(bytes, offset) == 3U);
}

[[nodiscard]] std::string write_compression_fixture(
    const fsim::runtime::TraceDeclarationModel& model,
    const fsim::runtime::TraceSignalId signal,
    const fsim::runtime::FstWriterCompression compression)
{
    using namespace fsim::runtime;
    std::ostringstream output;
    FstWriter writer { output, -12, { }, compression };
    writer.declare(model);
    writer.begin(4U);
    writer.set_initial_value(signal, encode_fst_logic_value(
        PackedLogic4::from_msb_string(std::string(256U, '0'))));
    writer.close(9U);
    return output.str();
}

[[nodiscard]] std::string write_negative_fixture()
{
    using namespace fsim::runtime;
    FstExtendedTypeMetadata logic9_metadata;
    logic9_metadata.kind = FstExtendedTypeKind::VhdlLogic9;
    logic9_metadata.language = FstTypeLanguage::Vhdl;
    logic9_metadata.nominal_name = "std_logic_vector";
    logic9_metadata.width = 9U;
    logic9_metadata.enumeration_literals = {
        "'U'", "'X'", "'0'", "'1'", "'Z'", "'W'", "'L'", "'H'", "'-'"
    };

    TraceDeclarationBuilder builder;
    const auto data = builder.add_variable("top.data", 8U);
    const auto text = builder.add_typed_variable("top.text",
        TraceTypeKind::SystemVerilogString, 0U,
        SystemVerilogScalarKind::None, canonical_fst_string_type());
    const auto realtime = builder.add_typed_variable("top.realtime",
        TraceTypeKind::SystemVerilogScalar, 64U,
        SystemVerilogScalarKind::Realtime,
        canonical_fst_systemverilog_real_type(
            SystemVerilogScalarKind::Realtime));
    const auto logic9 = builder.add_typed_variable("top.logic9",
        TraceTypeKind::VhdlLogic9, 9U, SystemVerilogScalarKind::None,
        canonical_fst_type_metadata(logic9_metadata));
    static_cast<void>(builder.add_alias("mirror.data", data));
    const auto model = std::move(builder).freeze();

    std::ostringstream output;
    FstWriter writer {
        output, -9, { }, FstWriterCompression::None
    };
    writer.declare(model);
    writer.begin(5U);
    writer.set_initial_value(data,
        encode_fst_logic_value(PackedLogic4::from_msb_string("10XZ0101")));
    writer.set_initial_value(
        text, encode_fst_string(std::string_view { "A\0B", 3U }));
    writer.set_initial_value(realtime, encode_fst_systemverilog_real(
        { SystemVerilogScalarKind::Realtime,
            UINT64_C(0x4000000000000000) }));
    writer.set_initial_value(logic9, encode_fst_extended_value(
        PackedLogic4::from_logic9_msb_string("UX01ZWLH-"),
        FstValueProfile::VhdlLogic9, logic9_metadata));
    writer.change({ data, 6U, 0U, TraceRegion::Active, 1U },
        encode_fst_logic_value(PackedLogic4::from_msb_string("00000001")));
    writer.change({ data, 7U, 0U, TraceRegion::Observed, 2U },
        encode_fst_logic_value(PackedLogic4::from_msb_string("00000010")));
    writer.close(7U);
    return output.str();
}

void test_compression_semantic_equivalence()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto signal = builder.add_variable("top.payload", 256U);
    const auto model = std::move(builder).freeze();
    const auto deterministic_bytes = write_compression_fixture(
        model, signal, FstWriterCompression::Deterministic);
    const auto stored_bytes = write_compression_fixture(
        model, signal, FstWriterCompression::None);
    assert(deterministic_bytes != stored_bytes);
    const auto deterministic = read_trace_or_abort(deterministic_bytes);
    const auto stored = read_trace_or_abort(stored_bytes);
    assert(deterministic.compression == FstReaderCompression::Deterministic);
    assert(stored.compression == FstReaderCompression::None);
    assert(deterministic.profile == kFstDeterministicContainerProfile);
    assert(stored.profile == kFstStoredContainerProfile);
    assert(deterministic.semantic_digest == stored.semantic_digest);
    assert(deterministic.values.size() == 1U);
    assert(deterministic.values.front().payload == std::string(256U, '0'));
}

void test_container_header_and_truncation_rejections()
{
    using namespace fsim::runtime;
    const auto bytes = write_negative_fixture();
    for (std::size_t size = 0U; size < bytes.size(); ++size) {
        const auto result = read_fst(std::string_view { bytes }.substr(0U, size));
        assert(!result.trace);
        assert(result.diagnostics.size() == 1U);
        const auto& code = result.diagnostics.front().code;
        assert(code == "FSIM-FST-READ-001"
            || code == "FSIM-FST-READ-002"
            || code == "FSIM-FST-READ-003"
            || code == "FSIM-FST-READ-004");
    }

    auto unknown_block = bytes;
    unknown_block[330U] = static_cast<char>(0x7eU);
    expect_failure(unknown_block, "FSIM-FST-READ-001");

    auto trailing = bytes;
    trailing.push_back(static_cast<char>(0x5aU));
    expect_failure(trailing, "FSIM-FST-READ-001");

    const auto value_block = find_block(bytes, 8U);
    auto short_block = bytes;
    write_be64(short_block, value_block.offset + 1U, 7U);
    expect_failure(short_block, "FSIM-FST-READ-001");

    auto oversized_block = bytes;
    write_be64(oversized_block, value_block.offset + 1U, UINT64_MAX);
    expect_failure(oversized_block, "FSIM-FST-READ-003");

    auto reversed_time = bytes;
    write_be64(reversed_time, 17U, 4U);
    expect_failure(reversed_time, "FSIM-FST-READ-002");

    auto bad_endian = bytes;
    bad_endian[25U] = static_cast<char>(bad_endian[25U] ^ 1);
    expect_failure(bad_endian, "FSIM-FST-READ-001");

    auto bad_reserved = bytes;
    bad_reserved[202U] = static_cast<char>(1U);
    expect_failure(bad_reserved, "FSIM-FST-READ-001");

    auto bad_timescale = bytes;
    bad_timescale[73U] = static_cast<char>(1U);
    expect_failure(bad_timescale, "FSIM-FST-READ-002");

    auto stale_profile = bytes;
    stale_profile[74U] = static_cast<char>(stale_profile[74U] ^ 1);
    expect_failure(stale_profile, "FSIM-FST-READ-001");

    auto duplicate_block_identity = bytes;
    write_be64(duplicate_block_identity, 65U, 2U);
    expect_failure(duplicate_block_identity, "FSIM-FST-READ-002");
}

void test_hierarchy_identity_and_compression_rejections()
{
    const auto bytes = write_negative_fixture();
    const auto hierarchy_block = find_block(bytes, 4U);

    auto bad_gzip_header = bytes;
    bad_gzip_header[hierarchy_block.payload_offset + 8U]
        = static_cast<char>(0U);
    expect_failure(bad_gzip_header, "FSIM-FST-READ-004");

    auto bad_gzip_checksum = bytes;
    bad_gzip_checksum[hierarchy_block.end - 8U]
        = static_cast<char>(bad_gzip_checksum[hierarchy_block.end - 8U] ^ 1);
    expect_failure(bad_gzip_checksum, "FSIM-FST-READ-004");

    auto oversized_hierarchy = bytes;
    write_be64(oversized_hierarchy, hierarchy_block.payload_offset,
        UINT64_MAX);
    expect_failure(oversized_hierarchy, "FSIM-FST-READ-003");

    const auto hierarchy = inflate_test_hierarchy(bytes, hierarchy_block);
    auto unknown_metadata = hierarchy;
    const auto metadata = unknown_metadata.find(
        "fsim-trace-declaration-v1|");
    assert(metadata != std::string::npos);
    unknown_metadata[metadata] = 'x';
    expect_failure(replace_hierarchy(bytes, unknown_metadata),
        "FSIM-FST-READ-001");

    auto duplicate_metadata = hierarchy;
    const auto declaration = duplicate_metadata.find(
        "fsim-trace-declaration-v1|");
    assert(declaration >= 3U && declaration != std::string::npos);
    const auto terminator = duplicate_metadata.find('\0', declaration);
    assert(terminator != std::string::npos);
    const auto attribute = declaration - 3U;
    const auto record = duplicate_metadata.substr(
        attribute, terminator + 2U - attribute);
    duplicate_metadata.insert(attribute, record);
    expect_failure(replace_hierarchy(bytes, duplicate_metadata),
        "FSIM-FST-READ-002");

    auto duplicate_signal = hierarchy;
    replace_once(duplicate_signal, "|v|2|0|", "|v|1|0|");
    expect_failure(replace_hierarchy(bytes, duplicate_signal),
        "FSIM-FST-READ-002");

    auto stale_alias = hierarchy;
    replace_once(stale_alias, "|a|5|1|", "|a|5|2|");
    expect_failure(replace_hierarchy(bytes, stale_alias),
        "FSIM-FST-READ-002");

    auto wrong_width = hierarchy;
    replace_once(wrong_width, "|v|1|0|0|0|8|", "|v|1|0|0|0|9|");
    expect_failure(replace_hierarchy(bytes, wrong_width),
        "FSIM-FST-READ-002");

    auto hierarchy_trailing = hierarchy;
    hierarchy_trailing.push_back(static_cast<char>(0xffU));
    expect_failure(replace_hierarchy(bytes, hierarchy_trailing),
        "FSIM-FST-READ-002");
}

void test_value_time_and_compression_rejections()
{
    using namespace fsim::runtime;
    const auto bytes = write_negative_fixture();
    const auto value_block = find_block(bytes, 8U);
    const auto time_size = static_cast<std::size_t>(
        read_be64(bytes, value_block.end - 24U));
    const auto time_begin = value_block.end - 24U - time_size;
    assert(time_size >= 3U);
    auto duplicate_time = bytes;
    duplicate_time[time_begin + 1U] = static_cast<char>(0U);
    expect_failure(duplicate_time, "FSIM-FST-READ-002");

    auto bad_logic = bytes;
    const auto logic = bad_logic.find("10xz0101");
    assert(logic != std::string::npos);
    bad_logic[logic + 2U] = 'q';
    expect_failure(bad_logic, "FSIM-FST-READ-002");

    auto bad_string = bytes;
    const std::string string_payload { "A\0B", 3U };
    const auto string = bad_string.find(string_payload);
    assert(string >= 2U && string != std::string::npos);
    bad_string[string - 2U] = static_cast<char>(1U);
    expect_failure(bad_string, "FSIM-FST-READ-002");

    auto bad_logic9 = bytes;
    const std::string logic9_payload { "\x01\x23\x45\x67\x80", 5U };
    const auto logic9 = bad_logic9.find(logic9_payload);
    assert(logic9 != std::string::npos);
    bad_logic9[logic9] = static_cast<char>(0x91U);
    expect_failure(bad_logic9, "FSIM-FST-READ-002");

    auto bad_real = bytes;
    const std::string real_payload { "\0\0\0\0\0\0\0\x40", 8U };
    const auto real = bad_real.find(real_payload);
    assert(real >= 1U && real != std::string::npos);
    bad_real[real - 1U] = static_cast<char>(0U);
    expect_failure(bad_real, "FSIM-FST-READ-002");

    TraceDeclarationBuilder builder;
    const auto signal = builder.add_variable("top.payload", 256U);
    const auto model = std::move(builder).freeze();
    const auto compressed = write_compression_fixture(
        model, signal, FstWriterCompression::Deterministic);
    const auto compressed_block = find_block(compressed, 8U);
    std::size_t offset = compressed_block.payload_offset + 24U;
    const auto initial_size = read_varint(compressed, offset);
    const auto stored_size = read_varint(compressed, offset);
    assert(stored_size < initial_size);
    assert(read_varint(compressed, offset) == 1U);
    auto bad_zlib_header = compressed;
    bad_zlib_header[offset] = static_cast<char>(0U);
    expect_failure(bad_zlib_header, "FSIM-FST-READ-004");

    auto bad_zlib_checksum = compressed;
    bad_zlib_checksum[offset + static_cast<std::size_t>(stored_size) - 1U]
        = static_cast<char>(bad_zlib_checksum[
            offset + static_cast<std::size_t>(stored_size) - 1U] ^ 1);
    expect_failure(bad_zlib_checksum, "FSIM-FST-READ-004");
}

void test_resource_limits_and_file_input()
{
    using namespace fsim::runtime;
    const auto bytes = write_negative_fixture();

    FstReaderLimits limits;
    limits.maximum_container_bytes = bytes.size() - 1U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");
    limits = { };
    limits.maximum_block_bytes = 1U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");
    limits = { };
    limits.maximum_hierarchy_bytes = 1U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");
    limits = { };
    limits.maximum_scopes = 1U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");
    limits = { };
    limits.maximum_declarations = 1U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");
    limits = { };
    limits.maximum_timestamps = 1U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");
    limits = { };
    limits.maximum_values = 1U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");
    limits = { };
    limits.maximum_decoded_value_bytes = 1U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");
    limits = { };
    limits.maximum_text_bytes = 2U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");
    limits = { };
    limits.maximum_metadata_bytes = 2U;
    expect_result_failure(read_fst(bytes, limits), "FSIM-FST-READ-003");

    constexpr std::array limit_members {
        &FstReaderLimits::maximum_container_bytes,
        &FstReaderLimits::maximum_block_bytes,
        &FstReaderLimits::maximum_hierarchy_bytes,
        &FstReaderLimits::maximum_scopes,
        &FstReaderLimits::maximum_declarations,
        &FstReaderLimits::maximum_timestamps,
        &FstReaderLimits::maximum_values,
        &FstReaderLimits::maximum_decoded_value_bytes,
        &FstReaderLimits::maximum_text_bytes,
        &FstReaderLimits::maximum_metadata_bytes,
    };
    for (const auto member : limit_members) {
        limits = { };
        limits.*member = 0U;
        expect_result_failure(read_fst(bytes, limits),
            "FSIM-FST-READ-003");
    }

    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    TemporaryDirectory temporary {
        std::filesystem::temp_directory_path()
        / ("fsim-fst-reader-" + std::to_string(unique))
    };
    assert(std::filesystem::create_directory(temporary.path));
    const auto valid_path = temporary.path / "valid.fst";
    {
        std::ofstream output { valid_path, std::ios::binary };
        assert(output);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        assert(output);
    }
    const auto memory = read_fst(bytes);
    const auto file = read_fst_file(valid_path);
    assert(memory.ok() && file.ok());
    assert(memory.trace->semantic_digest == file.trace->semantic_digest);
    expect_result_failure(read_fst_file(temporary.path / "missing.fst"),
        "FSIM-FST-READ-005");
    limits = { };
    limits.maximum_container_bytes = bytes.size() - 1U;
    expect_result_failure(read_fst_file(valid_path, limits),
        "FSIM-FST-READ-003");
}

void test_empty_trace()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output };
    writer.declare(model);
    writer.begin(12U);
    writer.flush();
    writer.close(12U);
    const auto trace = read_trace_or_abort(output.str());
    assert(trace.initial_time == 12U && trace.final_time == 12U);
    assert(trace.scopes.empty());
    assert(trace.declarations.empty());
    assert(trace.timestamps.empty());
    assert(trace.values.empty());
}

void test_remaining_value_profiles()
{
    using namespace fsim::runtime;
    const auto bits = PackedBit2::from_msb_string("10100101");
    const auto logic = PackedLogic4::from_msb_string("10XZ0101");
    const auto bit_vector
        = encode_fst_bit_value(bits, FstValueProfile::BitVector);
    const auto sv_bit
        = encode_fst_bit_value(bits, FstValueProfile::SystemVerilogBit);
    const auto logic_vector
        = encode_fst_logic_value(logic, FstValueProfile::LogicVector);
    const auto sv_logic
        = encode_fst_logic_value(logic, FstValueProfile::SystemVerilogLogic);
    const auto sv_reg
        = encode_fst_logic_value(logic, FstValueProfile::SystemVerilogReg);
    const auto realtime = encode_fst_systemverilog_real(
        { SystemVerilogScalarKind::Realtime,
            UINT64_C(0x4000000000000000) });
    const auto time_value = encode_fst_systemverilog_scalar(
        PackedLogic4::from_aval_bval(64U, 17U, 0U),
        SystemVerilogScalarKind::Time);
    const auto chandle = encode_fst_systemverilog_scalar(
        PackedLogic4::from_aval_bval(64U, 0x1234U, 0U),
        SystemVerilogScalarKind::Chandle);

    TraceDeclarationBuilder builder;
    const auto add_packed = [&](const std::string_view name,
                                const FstEncodedValue& value) {
        return builder.add_typed_variable(name, TraceTypeKind::Packed,
            value.width(), SystemVerilogScalarKind::None,
            std::string { value.canonical_type() });
    };
    const auto bit_vector_signal = add_packed("top.bit_vector", bit_vector);
    const auto sv_bit_signal = add_packed("top.sv_bit", sv_bit);
    const auto logic_vector_signal
        = add_packed("top.logic_vector", logic_vector);
    const auto sv_logic_signal = add_packed("top.sv_logic", sv_logic);
    const auto sv_reg_signal = add_packed("top.sv_reg", sv_reg);
    const auto realtime_signal = builder.add_typed_variable("top.realtime",
        TraceTypeKind::SystemVerilogScalar, 64U,
        SystemVerilogScalarKind::Realtime,
        std::string { realtime.canonical_type() });
    const auto time_signal = builder.add_typed_variable("top.time",
        TraceTypeKind::SystemVerilogScalar, 64U,
        SystemVerilogScalarKind::Time,
        std::string { time_value.canonical_type() });
    const auto chandle_signal = builder.add_typed_variable("top.chandle",
        TraceTypeKind::SystemVerilogScalar, 64U,
        SystemVerilogScalarKind::Chandle,
        std::string { chandle.canonical_type() });
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output };
    writer.declare(model);
    writer.begin();
    writer.set_initial_value(bit_vector_signal, bit_vector);
    writer.set_initial_value(sv_bit_signal, sv_bit);
    writer.set_initial_value(logic_vector_signal, logic_vector);
    writer.set_initial_value(sv_logic_signal, sv_logic);
    writer.set_initial_value(sv_reg_signal, sv_reg);
    writer.set_initial_value(realtime_signal, realtime);
    writer.set_initial_value(time_signal, time_value);
    writer.set_initial_value(chandle_signal, chandle);
    writer.close(1U);

    const auto trace = read_trace_or_abort(output.str());
    assert(trace.values.size() == 8U);
    assert(find_value(trace, bit_vector_signal, 0U).payload == "10100101");
    assert(find_value(trace, sv_bit_signal, 0U).payload == "10100101");
    assert(find_value(trace, logic_vector_signal, 0U).payload == "10xz0101");
    assert(find_value(trace, sv_logic_signal, 0U).payload == "10xz0101");
    assert(find_value(trace, sv_reg_signal, 0U).payload == "10xz0101");
    assert(find_value(trace, realtime_signal, 0U).real_bits
        == UINT64_C(0x4000000000000000));
    assert(find_value(trace, time_signal, 0U).payload.ends_with("10001"));
    assert(find_value(trace, chandle_signal, 0U).payload.ends_with("1001000110100"));
    assert(find_declaration(trace, "top.realtime").canonical_metadata
        == realtime.canonical_type());
}

void test_all_leaf_kinds()
{
    using namespace fsim::runtime;
    constexpr std::array kinds {
        FstLeafKind::ResolvedState,
        FstLeafKind::ResolvedStrengthZero,
        FstLeafKind::ResolvedStrengthOne,
        FstLeafKind::PackedAggregate,
        FstLeafKind::UnpackedAggregate,
        FstLeafKind::DynamicClass,
        FstLeafKind::Container,
        FstLeafKind::Coverage,
        FstLeafKind::Assertion,
    };
    TraceDeclarationBuilder builder;
    std::vector<TraceSignalId> signals;
    std::vector<FstLeafTypeMetadata> metadata;
    signals.reserve(kinds.size());
    metadata.reserve(kinds.size());
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        FstLeafTypeMetadata item;
        item.kind = kinds[index];
        item.owner_identity = "owner-" + std::to_string(index);
        item.leaf_path = "leaf-" + std::to_string(index);
        if (item.kind == FstLeafKind::ResolvedState) {
            item.width = 1U;
            item.four_state = true;
        } else if (item.kind == FstLeafKind::ResolvedStrengthZero
            || item.kind == FstLeafKind::ResolvedStrengthOne) {
            item.width = 3U;
        } else {
            item.width = 8U;
            item.four_state = true;
        }
        if (item.kind == FstLeafKind::PackedAggregate) {
            item.dimensions = { { 7, 0, true } };
        } else if (item.kind == FstLeafKind::UnpackedAggregate
            || item.kind == FstLeafKind::Container) {
            item.dimensions = { { 0, 7, false } };
        }
        const auto name = "top.leaf_" + std::to_string(index);
        signals.push_back(builder.add_typed_variable(name,
            TraceTypeKind::TypedLeaf, item.width,
            SystemVerilogScalarKind::None,
            canonical_fst_leaf_type_metadata(item)));
        metadata.push_back(std::move(item));
    }
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output };
    writer.declare(model);
    writer.begin();
    for (std::size_t index = 0U; index < signals.size(); ++index) {
        const auto symbols = metadata[index].kind == FstLeafKind::ResolvedState
            ? "z"
            : metadata[index].kind == FstLeafKind::ResolvedStrengthZero
                    || metadata[index].kind
                        == FstLeafKind::ResolvedStrengthOne
            ? "101"
            : "10XZ0101";
        writer.set_initial_value(signals[index], encode_fst_leaf_value(
            PackedLogic4::from_msb_string(symbols), metadata[index]));
    }
    writer.close(1U);
    const auto trace = read_trace_or_abort(output.str());
    assert(trace.declarations.size() == kinds.size());
    assert(trace.values.size() == kinds.size());
    for (std::size_t index = 0U; index < signals.size(); ++index) {
        const auto path = "top.leaf_" + std::to_string(index);
        assert(find_declaration(trace, path).canonical_metadata
            == canonical_fst_leaf_type_metadata(metadata[index]));
        const auto expected
            = metadata[index].kind == FstLeafKind::ResolvedState
            ? "z"
            : metadata[index].kind == FstLeafKind::ResolvedStrengthZero
                    || metadata[index].kind
                        == FstLeafKind::ResolvedStrengthOne
            ? "101"
            : "10xz0101";
        assert(find_value(trace, signals[index], 0U).payload == expected);
    }
}

} // namespace

int main()
{
    test_extended_initial_value_equivalence();
    test_typed_leaf_initial_value_equivalence();
    test_ordered_change_equivalence();
    test_compression_semantic_equivalence();
    test_container_header_and_truncation_rejections();
    test_hierarchy_identity_and_compression_rejections();
    test_value_time_and_compression_rejections();
    test_resource_limits_and_file_input();
    test_empty_trace();
    test_remaining_value_profiles();
    test_all_leaf_kinds();
}
