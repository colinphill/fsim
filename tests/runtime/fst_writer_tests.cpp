// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_writer.hpp"

#include "fsim/runtime/fst_compression.hpp"
#include "fsim/runtime/fst_reader.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

class ControlledStreamBuffer final : public std::streambuf {
public:
    bool fail_write { };
    bool fail_sync { };
    std::size_t write_prefix { };
    std::string bytes;

protected:
    std::streamsize xsputn(
        const char* data,
        const std::streamsize count) override
    {
        if (fail_write) {
            const auto accepted = std::min(
                static_cast<std::size_t>(count), write_prefix);
            bytes.append(data, accepted);
            return static_cast<std::streamsize>(accepted);
        }
        bytes.append(data, static_cast<std::size_t>(count));
        return count;
    }

    int_type overflow(const int_type value) override
    {
        if (traits_type::eq_int_type(value, traits_type::eof())) {
            return traits_type::not_eof(value);
        }
        if (fail_write) {
            return traits_type::eof();
        }
        bytes.push_back(traits_type::to_char_type(value));
        return value;
    }

    int sync() override { return fail_sync ? -1 : 0; }
};

[[nodiscard]] std::uint64_t read_be64(
    const std::string& bytes,
    const std::size_t offset)
{
    assert(offset + 8U <= bytes.size());
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        result = (result << 8U)
            | static_cast<unsigned char>(bytes[offset + index]);
    }
    return result;
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
        if ((byte & 0x80U) == 0) {
            return result;
        }
        shift += 7U;
    }
}

[[nodiscard]] Bytes inflate_stored_gzip(
    const std::string& bytes,
    std::size_t offset)
{
    assert(offset + 18U <= bytes.size());
    assert(static_cast<unsigned char>(bytes[offset]) == 0x1f);
    assert(static_cast<unsigned char>(bytes[offset + 1U]) == 0x8b);
    assert(static_cast<unsigned char>(bytes[offset + 2U]) == 0x08);
    offset += 10U;
    Bytes result;
    bool final = false;
    while (!final) {
        const auto header = static_cast<unsigned char>(bytes.at(offset++));
        final = (header & 1U) != 0;
        assert((header & 6U) == 0);
        const auto length = static_cast<std::uint16_t>(
            static_cast<unsigned char>(bytes.at(offset))
            | (static_cast<unsigned char>(bytes.at(offset + 1U)) << 8U));
        const auto complement = static_cast<std::uint16_t>(
            static_cast<unsigned char>(bytes.at(offset + 2U))
            | (static_cast<unsigned char>(bytes.at(offset + 3U)) << 8U));
        assert(static_cast<std::uint16_t>(~length) == complement);
        offset += 4U;
        assert(offset + length <= bytes.size());
        result.insert(
            result.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    }
    return result;
}

[[nodiscard]] fsim::runtime::TraceDeclarationModel make_model()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto clock = builder.add_variable("top.clock", 1);
    static_cast<void>(builder.add_alias("mirror.clock", clock));
    static_cast<void>(builder.add_variable("top.core.bus", 17));
    return std::move(builder).freeze();
}

[[nodiscard]] std::string write_container(
    const fsim::runtime::TraceDeclarationModel& model)
{
    std::ostringstream output;
    fsim::runtime::FstWriter writer { output, -9 };
    writer.declare(model);
    assert(output.str().empty());
    assert(writer.status().state == fsim::runtime::FstWriterState::configuring);
    assert(writer.status().buffered_bytes != 0U);
    writer.begin(5);
    assert(writer.begun() && !writer.closed());
    writer.flush();
    assert(output.str().empty());
    writer.close(10);
    assert(writer.closed());
    assert(writer.bytes_written() == output.str().size());
    assert(writer.status().state == fsim::runtime::FstWriterState::complete);
    assert(writer.status().buffered_bytes == 0U);
    return output.str();
}

[[nodiscard]] std::string write_value_container()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto value = builder.add_variable("top.value", 4U);
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output, -9 };
    writer.declare(model);
    writer.begin();
    writer.set_initial_value(value, encode_fst_logic_value(PackedLogic4::from_msb_string("01XZ")));
    writer.close(1U);
    return output.str();
}

[[nodiscard]] std::string write_wide_unknown_container()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    static_cast<void>(builder.add_variable("top.wide", 257U));
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output, -9 };
    writer.declare(model);
    writer.begin();
    writer.close(0U);
    return output.str();
}

[[nodiscard]] std::string write_change_container()
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
    return output.str();
}

[[nodiscard]] fsim::runtime::TraceDeclarationModel make_provenance_model()
{
    using namespace fsim::runtime;
    const auto source = [](const TraceLanguage language,
                            std::string root,
                            std::string library,
                            std::string owner) {
        TraceSourceMetadata result;
        result.kind = language == TraceLanguage::SystemC
            ? TraceSourceKind::SystemC
            : TraceSourceKind::Hdl;
        result.language = language;
        result.root_identity = std::move(root);
        result.library = std::move(library);
        result.owner_identity = std::move(owner);
        return result;
    };
    TraceDeclarationBuilder builder;
    const auto verilog = builder.add_typed_variable(
        "verilog.value", TraceTypeKind::Packed, 1U,
        SystemVerilogScalarKind::None, { },
        source(TraceLanguage::Verilog, "verilog", "vlib",
            "vlib:producer@verilog"));
    static_cast<void>(builder.add_typed_variable(
        "systemverilog.value", TraceTypeKind::Packed, 1U,
        SystemVerilogScalarKind::None, { },
        source(TraceLanguage::SystemVerilog, "systemverilog", "svlib",
            "svlib:monitor@systemverilog")));
    static_cast<void>(builder.add_typed_variable(
        "vhdl.value", TraceTypeKind::Packed, 1U,
        SystemVerilogScalarKind::None, { },
        source(TraceLanguage::Vhdl, "vhdl", "work",
            "work:consumer(rtl)@vhdl")));
    static_cast<void>(builder.add_typed_variable(
        "systemc.value", TraceTypeKind::Packed, 1U,
        SystemVerilogScalarKind::None, { },
        source(TraceLanguage::SystemC, "systemc", "native",
            "native:bridge@systemc")));
    static_cast<void>(builder.add_alias(
        "vhdl.verilog_value", verilog,
        source(TraceLanguage::Vhdl, "vhdl", "work",
            "work:consumer(rtl)@vhdl")));
    return std::move(builder).freeze();
}

[[nodiscard]] Bytes hierarchy_bytes(const std::string& bytes)
{
    std::size_t offset = 330U;
    while (offset < bytes.size()) {
        const auto type = static_cast<unsigned char>(bytes.at(offset));
        const auto length = read_be64(bytes, offset + 1U);
        if (type == 4U) {
            return inflate_stored_gzip(bytes, offset + 17U);
        }
        offset += 1U + static_cast<std::size_t>(length);
    }
    assert(false);
    return { };
}

void test_mixed_root_provenance_records()
{
    using namespace fsim::runtime;
    const auto model = make_provenance_model();
    const auto bytes = write_container(model);
    const auto hierarchy = hierarchy_bytes(bytes);
    const std::string text(hierarchy.begin(), hierarchy.end());
    const std::array expected {
        std::string { "fsim-trace-provenance-v1|v|1|0|0|1|7:verilog4:vlib"
                      "21:vlib:producer@verilog13:verilog.value" },
        std::string { "fsim-trace-provenance-v1|v|2|0|0|2|13:systemverilog"
                      "5:svlib27:svlib:monitor@systemverilog19:systemverilog.value" },
        std::string { "fsim-trace-provenance-v1|v|3|0|0|3|4:vhdl4:work"
                      "23:work:consumer(rtl)@vhdl10:vhdl.value" },
        std::string { "fsim-trace-provenance-v1|a|5|1|0|3|4:vhdl4:work"
                      "23:work:consumer(rtl)@vhdl18:vhdl.verilog_value" },
        std::string { "fsim-trace-provenance-v1|v|4|0|1|4|7:systemc6:native"
                      "21:native:bridge@systemc13:systemc.value" },
    };
    for (const auto& record : expected) {
        const auto found = text.find(record);
        assert(found != std::string::npos && found >= 3U);
        assert(static_cast<unsigned char>(text[found - 3U]) == 0xfcU);
        assert(static_cast<unsigned char>(text[found - 2U]) == 0U);
        assert(static_cast<unsigned char>(text[found - 1U]) == 0U);
        assert(static_cast<unsigned char>(text[found + record.size()]) == 0U);
    }
    const std::string vhdl_scope {
        "vhdl\0work:consumer(rtl)@vhdl\0", 29U
    };
    const auto vhdl_scope_offset = text.find(vhdl_scope);
    assert(vhdl_scope_offset != std::string::npos && vhdl_scope_offset >= 2U);
    assert(static_cast<unsigned char>(text[vhdl_scope_offset - 2U]) == 0xfeU);
    assert(static_cast<unsigned char>(text[vhdl_scope_offset - 1U]) == 12U);
    const std::string systemc_scope {
        "systemc\0native:bridge@systemc\0", 30U
    };
    const auto systemc_scope_offset = text.find(systemc_scope);
    assert(systemc_scope_offset != std::string::npos
        && systemc_scope_offset >= 2U);
    assert(static_cast<unsigned char>(text[systemc_scope_offset - 2U]) == 0xfeU);
    assert(static_cast<unsigned char>(text[systemc_scope_offset - 1U]) == 0U);
    assert(write_container(model) == bytes);

    std::ostringstream limited_output;
    FstWriterLimits limits;
    limits.maximum_name_bytes = 64U;
    FstWriter limited { limited_output, -9, limits };
    bool rejected = false;
    try {
        limited.declare(model);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected && limited_output.str().empty());
}

[[nodiscard]] std::string write_scalar_container(const bool with_change)
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto signal = builder.add_variable("top.value", 1U);
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output, -9 };
    writer.declare(model);
    writer.begin(5U);
    writer.set_initial_value(signal,
        encode_fst_logic_value(PackedLogic4::from_msb_string("x")));
    if (with_change) {
        writer.change({ signal, 7U, 0U, TraceRegion::Active, 0U },
            encode_fst_logic_value(PackedLogic4::from_msb_string("1")));
    }
    writer.close(with_change ? 7U : 5U);
    return output.str();
}

struct ExtendedFixture {
    fsim::runtime::TraceDeclarationModel model;
    std::array<fsim::runtime::TraceSignalId, 7> signals;
    fsim::runtime::FstExtendedTypeMetadata enum_metadata;
    fsim::runtime::FstExtendedTypeMetadata physical_metadata;
    fsim::runtime::FstExtendedTypeMetadata time_metadata;
};

[[nodiscard]] ExtendedFixture make_extended_fixture()
{
    using namespace fsim::runtime;
    FstExtendedTypeMetadata enum_metadata;
    enum_metadata.kind = FstExtendedTypeKind::Enumeration;
    enum_metadata.language = FstTypeLanguage::SystemVerilog;
    enum_metadata.nominal_name = "top.state_t";
    enum_metadata.width = 3U;
    enum_metadata.four_state = true;
    enum_metadata.enumeration_literals = { "IDLE", "BUSY", "ERROR" };

    FstExtendedTypeMetadata physical_metadata;
    physical_metadata.kind = FstExtendedTypeKind::VhdlPhysical;
    physical_metadata.language = FstTypeLanguage::Vhdl;
    physical_metadata.nominal_name = "work.distance";
    physical_metadata.width = 32U;
    physical_metadata.physical_units = { { "mm", 1 }, { "m", 1'000 } };

    FstExtendedTypeMetadata time_metadata;
    time_metadata.kind = FstExtendedTypeKind::VhdlTime;
    time_metadata.language = FstTypeLanguage::Vhdl;
    time_metadata.nominal_name = "@builtin:time";
    time_metadata.width = 64U;

    TraceDeclarationBuilder builder;
    std::array<TraceSignalId, 7> signals;
    signals[0] = builder.add_typed_variable(
        "top.shortreal_value", TraceTypeKind::SystemVerilogScalar, 32U,
        SystemVerilogScalarKind::ShortReal,
        canonical_fst_systemverilog_real_type(
            SystemVerilogScalarKind::ShortReal));
    signals[1] = builder.add_typed_variable(
        "top.real_value", TraceTypeKind::SystemVerilogScalar, 64U,
        SystemVerilogScalarKind::Real,
        canonical_fst_systemverilog_real_type(
            SystemVerilogScalarKind::Real));
    signals[2] = builder.add_typed_variable(
        "top.realtime_value", TraceTypeKind::SystemVerilogScalar, 64U,
        SystemVerilogScalarKind::Realtime,
        canonical_fst_systemverilog_real_type(
            SystemVerilogScalarKind::Realtime));
    signals[3] = builder.add_typed_variable(
        "top.string_value", TraceTypeKind::SystemVerilogString, 0U,
        SystemVerilogScalarKind::None, canonical_fst_string_type());
    signals[4] = builder.add_typed_variable(
        "top.enum_value", TraceTypeKind::Enumeration, 3U,
        SystemVerilogScalarKind::None,
        canonical_fst_type_metadata(enum_metadata));
    signals[5] = builder.add_typed_variable(
        "top.physical_value", TraceTypeKind::VhdlPhysical, 32U,
        SystemVerilogScalarKind::None,
        canonical_fst_type_metadata(physical_metadata));
    signals[6] = builder.add_typed_variable(
        "top.vhdl_time_value", TraceTypeKind::VhdlTime, 64U,
        SystemVerilogScalarKind::None,
        canonical_fst_type_metadata(time_metadata));
    return {
        std::move(builder).freeze(),
        signals,
        std::move(enum_metadata),
        std::move(physical_metadata),
        std::move(time_metadata)
    };
}

[[nodiscard]] std::string write_extended_container()
{
    using namespace fsim::runtime;
    const auto fixture = make_extended_fixture();
    std::ostringstream output;
    FstWriter writer { output, -9 };
    writer.declare(fixture.model);
    writer.begin();
    writer.set_initial_value(fixture.signals[0],
        encode_fst_systemverilog_real(SystemVerilogScalarValue {
            SystemVerilogScalarKind::ShortReal, UINT32_C(0x7fc12345) }));
    writer.set_initial_value(fixture.signals[1],
        encode_fst_systemverilog_real(SystemVerilogScalarValue {
            SystemVerilogScalarKind::Real,
            UINT64_C(0x8000000000000000) }));
    writer.set_initial_value(fixture.signals[2],
        encode_fst_systemverilog_real(SystemVerilogScalarValue {
            SystemVerilogScalarKind::Realtime,
            UINT64_C(0x7ff0000000000000) }));
    writer.set_initial_value(fixture.signals[3],
        encode_fst_string(std::string_view { "A\0B\xc3\xa9", 5U }));
    writer.set_initial_value(fixture.signals[4],
        encode_fst_extended_value(
            PackedLogic4::from_msb_string("1X0"),
            FstValueProfile::Enumeration,
            fixture.enum_metadata));
    writer.set_initial_value(fixture.signals[5],
        encode_fst_extended_value(
            PackedLogic4::from_aval_bval(
                32U, UINT32_C(0xffffff85), 0),
            FstValueProfile::VhdlPhysical,
            fixture.physical_metadata));
    writer.set_initial_value(fixture.signals[6],
        encode_fst_extended_value(
            PackedLogic4::from_aval_bval(64U, 25U, 0),
            FstValueProfile::VhdlTime,
            fixture.time_metadata));
    writer.close(1U);
    return output.str();
}

[[nodiscard]] std::string write_real_container()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto signal = builder.add_typed_variable(
        "top.value", TraceTypeKind::SystemVerilogScalar, 64U,
        SystemVerilogScalarKind::Real,
        canonical_fst_systemverilog_real_type(SystemVerilogScalarKind::Real));
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output, -9 };
    writer.declare(model);
    writer.begin();
    writer.set_initial_value(signal,
        encode_fst_systemverilog_real(SystemVerilogScalarValue {
            SystemVerilogScalarKind::Real,
            UINT64_C(0x8000000000000000) }));
    writer.close(0U);
    return output.str();
}

void test_container_and_hierarchy()
{
    const auto model = make_model();
    const auto bytes = write_container(model);
    assert(bytes.size() > 330U);
    assert(static_cast<unsigned char>(bytes[0]) == 0);
    assert(read_be64(bytes, 1) == 329);
    assert(read_be64(bytes, 9) == 5);
    assert(read_be64(bytes, 17) == 10);
    assert(read_be64(bytes, 41) == 3);
    assert(read_be64(bytes, 49) == 3);
    assert(read_be64(bytes, 57) == 2);
    assert(read_be64(bytes, 65) == 1);
    assert(static_cast<unsigned char>(bytes[73]) == 0xf7);
    assert(bytes.substr(
               74, fsim::runtime::kFstDeterministicContainerProfile.size())
        == fsim::runtime::kFstDeterministicContainerProfile);

    std::size_t offset = 330;
    assert(static_cast<unsigned char>(bytes.at(offset)) == 8);
    const auto values_length = read_be64(bytes, offset + 1U);
    assert(read_be64(bytes, offset + 9U) == 5);
    assert(read_be64(bytes, offset + 17U) == 10);
    assert(read_be64(bytes, offset + 25U) == 0);
    assert(values_length == 88);
    offset += 1U + static_cast<std::size_t>(values_length);
    assert(static_cast<unsigned char>(bytes.at(offset)) == 3);
    const auto geometry_length = read_be64(bytes, offset + 1U);
    assert(geometry_length == 26);
    assert(read_be64(bytes, offset + 9U) == 2);
    assert(read_be64(bytes, offset + 17U) == 2);
    assert(static_cast<unsigned char>(bytes[offset + 25U]) == 1);
    assert(static_cast<unsigned char>(bytes[offset + 26U]) == 17);
    offset += 1U + static_cast<std::size_t>(geometry_length);

    assert(static_cast<unsigned char>(bytes.at(offset)) == 4);
    const auto hierarchy_length = read_be64(bytes, offset + 1U);
    const auto hierarchy_size = read_be64(bytes, offset + 9U);
    const auto hierarchy = inflate_stored_gzip(bytes, offset + 17U);
    assert(hierarchy.size() == hierarchy_size);
    assert(offset + 1U + hierarchy_length == bytes.size());
    const auto parsed = fsim::runtime::read_fst(bytes);
    assert(parsed.ok());
    assert(parsed.trace->scopes.size() == 3U);
    assert(parsed.trace->declarations.size() == 3U);
    assert(std::ranges::any_of(parsed.trace->declarations,
        [](const auto& declaration) {
            return declaration.path == "top.clock";
        }));
    assert(std::ranges::any_of(parsed.trace->declarations,
        [](const auto& declaration) {
            return declaration.path == "top.core.bus";
        }));
    assert(std::ranges::any_of(parsed.trace->declarations,
        [](const auto& declaration) {
            return declaration.path == "mirror.clock";
        }));
    assert(write_container(model) == bytes);
}

void test_transactional_failures()
{
    using namespace fsim::runtime;
    const auto model = make_model();
    {
        std::ostringstream output;
        FstWriter writer { output };
        bool rejected = false;
        try {
            writer.begin();
        } catch (const std::logic_error&) {
            rejected = true;
        }
        assert(rejected && output.str().empty());
        const auto status = writer.status();
        assert(status.state == FstWriterState::failed);
        assert(status.failure == "FST declarations must precede begin");
    }
    {
        std::ostringstream output;
        FstWriter writer { output };
        writer.declare(model);
        writer.begin(10);
        bool rejected = false;
        try {
            writer.close(9);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected && output.str().empty() && !writer.closed());
        const auto first_status = writer.status();
        assert(first_status.state == FstWriterState::failed);
        assert(first_status.failure == "FST final time precedes initial time");
        bool retry_rejected = false;
        try {
            writer.close(10);
        } catch (const std::logic_error&) {
            retry_rejected = true;
        }
        assert(retry_rejected);
        assert(writer.status().failure == first_status.failure);
    }
    {
        std::ostringstream output;
        FstWriterLimits limits;
        limits.maximum_name_bytes = 3;
        FstWriter writer { output, -9, limits };
        bool rejected = false;
        try {
            writer.declare(model);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected && output.str().empty() && !writer.begun());
    }
    {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_variable(
            "top.real", 64, SystemVerilogScalarKind::Real));
        const auto typed = std::move(builder).freeze();
        std::ostringstream output;
        FstWriter writer { output };
        bool rejected = false;
        try {
            writer.declare(typed);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected && output.str().empty() && !writer.begun());
    }
}

void test_bounded_terminal_lifecycle()
{
    using namespace fsim::runtime;
    const auto model = make_model();
    std::ostringstream probe_output;
    FstWriter probe { probe_output };
    probe.declare(model);
    const auto declaration_bytes = probe.status().buffered_bytes;
    assert(declaration_bytes != 0U);

    {
        FstWriterLimits limits;
        limits.maximum_buffer_bytes = declaration_bytes - 1U;
        std::ostringstream output;
        FstWriter writer { output, -9, limits };
        bool full_rejected = false;
        try {
            writer.declare(model);
        } catch (const std::length_error&) {
            full_rejected = true;
        }
        assert(full_rejected && output.str().empty());
        assert(writer.status().state == FstWriterState::failed);
        assert(writer.status().failure
            == "FST declaration buffer exceeds its limit");
    }
    {
        FstWriterLimits limits;
        limits.maximum_buffer_bytes
            = declaration_bytes + sizeof(std::uint64_t);
        std::ostringstream output;
        FstWriter writer { output, -9, limits };
        writer.declare(model);
        writer.begin();
        bool full_rejected = false;
        try {
            writer.set_initial_value({ 1U },
                encode_fst_logic_value(PackedLogic4::from_msb_string("1")));
        } catch (const std::length_error&) {
            full_rejected = true;
        }
        assert(full_rejected && output.str().empty());
        const auto status = writer.status();
        assert(status.state == FstWriterState::failed);
        assert(status.buffered_bytes == 0U);
        assert(status.bytes_written == 0U);
        assert(status.failure
            == "FST initial-value buffer exceeds its limit");
        bool close_rejected = false;
        try {
            writer.close(0U);
        } catch (const std::logic_error&) {
            close_rejected = true;
        }
        assert(close_rejected);
        assert(writer.status().failure == status.failure);
    }
    {
        FstWriterLimits limits;
        limits.maximum_buffer_bytes
            = declaration_bytes + sizeof(TraceEvent);
        std::ostringstream output;
        FstWriter writer { output, -9, limits };
        writer.declare(model);
        writer.begin();
        bool full_rejected = false;
        try {
            writer.change(
                { { 1U }, 0U, 0U, TraceRegion::Active, 0U },
                encode_fst_logic_value(PackedLogic4::from_msb_string("1")));
        } catch (const std::length_error&) {
            full_rejected = true;
        }
        assert(full_rejected && output.str().empty());
        assert(writer.status().state == FstWriterState::failed);
        assert(writer.status().failure == "FST change buffer exceeds its limit");
    }
}

void test_io_and_destructor_containment()
{
    using namespace fsim::runtime;
    const auto model = make_model();
    {
        ControlledStreamBuffer buffer;
        buffer.fail_sync = true;
        std::ostream output { &buffer };
        FstWriter writer { output };
        writer.declare(model);
        writer.begin();
        bool rejected = false;
        try {
            writer.flush();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        assert(rejected && buffer.bytes.empty());
        assert(writer.status().state == FstWriterState::failed);
        assert(writer.status().failure == "failed to flush FST output");
    }
    {
        ControlledStreamBuffer buffer;
        buffer.fail_write = true;
        buffer.write_prefix = 23U;
        std::ostream output { &buffer };
        FstWriter writer { output };
        writer.declare(model);
        writer.begin();
        bool rejected = false;
        try {
            writer.close(0U);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        assert(rejected && buffer.bytes.size() == buffer.write_prefix);
        const auto status = writer.status();
        assert(status.state == FstWriterState::failed);
        assert(status.bytes_written == 0U);
        assert(status.failure == "failed to publish FST output");
        assert(!writer.closed());
    }
    {
        std::ostringstream output;
        {
            FstWriter writer { output };
            writer.declare(model);
            writer.begin();
        }
        assert(output.str().empty());
    }
    {
        std::ostringstream output;
        FstWriter writer { output };
        writer.declare(model);
        writer.begin();
        writer.close(0U);
        const auto complete_bytes = output.str();
        bool rejected = false;
        try {
            writer.close(0U);
        } catch (const std::logic_error&) {
            rejected = true;
        }
        assert(rejected && output.str() == complete_bytes);
        assert(writer.status().state == FstWriterState::failed);
        assert(writer.status().failure == "FST writer is already closed");
        assert(writer.bytes_written() == complete_bytes.size());
    }
}

void test_exact_initial_values()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto bit = builder.add_variable("top.bit_value", 1U);
    const auto logic = builder.add_variable("top.logic_value", 17U);
    const auto time = builder.add_variable(
        "top.time_value", 64U, SystemVerilogScalarKind::Time);
    const auto chandle = builder.add_variable(
        "top.chandle_value", 64U, SystemVerilogScalarKind::Chandle);
    static_cast<void>(builder.add_alias("mirror.logic_value", logic));
    const auto model = std::move(builder).freeze();

    const auto logic_symbols = std::string { "01xz1010zx0101xz1" };
    std::string time_symbols(64U, '0');
    time_symbols.front() = '1';
    time_symbols.back() = 'z';
    std::string chandle_symbols(64U, '0');
    chandle_symbols[1U] = '1';
    chandle_symbols[chandle_symbols.size() - 2U] = 'x';

    std::ostringstream output;
    FstWriter writer { output };
    writer.declare(model);
    writer.begin(5U);
    writer.set_initial_value(bit, encode_fst_logic_value(PackedLogic4::from_msb_string("1")));
    writer.set_initial_value(logic, encode_fst_logic_value(PackedLogic4::from_msb_string(logic_symbols)));
    writer.set_initial_value(time, encode_fst_systemverilog_scalar(PackedLogic4::from_msb_string(time_symbols), SystemVerilogScalarKind::Time));
    writer.set_initial_value(chandle, encode_fst_systemverilog_scalar(PackedLogic4::from_msb_string(chandle_symbols), SystemVerilogScalarKind::Chandle));
    writer.close(10U);

    const auto bytes = output.str();
    assert(static_cast<unsigned char>(bytes.at(330U)) == 8U);
    assert(read_be64(bytes, 355U) >= 146U);
    std::size_t offset = 363U;
    assert(read_varint(bytes, offset) == 146U);
    const auto stored_size = read_varint(bytes, offset);
    assert(stored_size < 146U);
    assert(read_varint(bytes, offset) == 4U);
    assert(static_cast<unsigned char>(bytes.at(offset)) == 0x78U);
    assert(static_cast<unsigned char>(bytes.at(offset + 1U)) == 0x01U);
    offset += stored_size;
    assert(read_varint(bytes, offset) == 4U);
    assert(bytes.at(offset++) == '4');
    const auto wave_end = bytes.size();
    assert(bytes.find(logic_symbols, offset) < wave_end);
    assert(bytes.find(time_symbols, offset) < wave_end);
    assert(bytes.find(chandle_symbols, offset) < wave_end);

    bool duplicate_rejected = false;
    try {
        writer.set_initial_value(bit, encode_fst_logic_value(PackedLogic4::from_msb_string("0")));
    } catch (const std::logic_error&) {
        duplicate_rejected = true;
    }
    assert(duplicate_rejected);
}

void test_wide_unknown_frame()
{
    const auto bytes = write_wide_unknown_container();
    std::size_t offset = 363U;
    assert(read_varint(bytes, offset) == 257U);
    const auto stored_size = read_varint(bytes, offset);
    assert(stored_size < 257U);
    assert(read_varint(bytes, offset) == 1U);
    assert(static_cast<unsigned char>(bytes.at(offset)) == 0x78U);
    assert(static_cast<unsigned char>(bytes.at(offset + 1U)) == 0x01U);
    assert(write_wide_unknown_container() == bytes);
}

void test_root_declarations_are_scoped()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    static_cast<void>(builder.add_variable("value", 1U));
    const auto model = std::move(builder).freeze();
    const auto bytes = write_container(model);
    assert(read_be64(bytes, 41U) == 1U);

    std::size_t block = 330U;
    block += 1U + static_cast<std::size_t>(read_be64(bytes, block + 1U));
    block += 1U + static_cast<std::size_t>(read_be64(bytes, block + 1U));
    assert(static_cast<unsigned char>(bytes.at(block)) == 4U);
    const auto hierarchy = inflate_stored_gzip(bytes, block + 17U);
    const std::string root_scope { "__fsim_root" };
    assert(std::search(
               hierarchy.begin(), hierarchy.end(),
               root_scope.begin(), root_scope.end())
        != hierarchy.end());
}

void test_initial_value_validation()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto logic = builder.add_variable("top.logic", 2U);
    const auto time = builder.add_variable(
        "top.time", 64U, SystemVerilogScalarKind::Time);
    const auto alias = builder.add_alias("mirror.logic", logic);
    const auto model = std::move(builder).freeze();
    {
        std::ostringstream invalid_output;
        FstWriter invalid_writer { invalid_output };
        invalid_writer.declare(model);
        bool lifecycle_rejected = false;
        try {
            invalid_writer.set_initial_value(logic, encode_fst_logic_value(PackedLogic4::from_msb_string("01")));
        } catch (const std::logic_error&) {
            lifecycle_rejected = true;
        }
        assert(lifecycle_rejected);
        assert(invalid_writer.status().state == FstWriterState::failed);
        assert(invalid_output.str().empty());
    }

    std::ostringstream output;
    FstWriter writer { output };
    writer.declare(model);
    writer.begin();

    bool alias_rejected = false;
    try {
        writer.set_initial_value(alias, encode_fst_logic_value(PackedLogic4::from_msb_string("01")));
    } catch (const std::out_of_range&) {
        alias_rejected = true;
    }
    assert(alias_rejected);
    bool width_rejected = false;
    try {
        writer.set_initial_value(logic, encode_fst_logic_value(PackedLogic4::from_msb_string("1")));
    } catch (const std::invalid_argument&) {
        width_rejected = true;
    }
    assert(width_rejected);
    bool profile_rejected = false;
    try {
        writer.set_initial_value(time, make_fst_unknown_value(FstValueProfile::LogicVector, 64U));
    } catch (const std::invalid_argument&) {
        profile_rejected = true;
    }
    assert(profile_rejected);
    assert(output.str().empty());
}

void test_extended_typed_values()
{
    const auto bytes = write_extended_container();
    assert(static_cast<unsigned char>(bytes.at(330U)) == 8U);
    std::size_t offset = 363U;
    assert(read_varint(bytes, offset) == 123U);
    assert(read_varint(bytes, offset) == 123U);
    assert(read_varint(bytes, offset) == 7U);
    assert(offset + 123U <= bytes.size());
    const std::string quiet_nan_le {
        "\0\0\0\0\0\0\xf8\x7f", 8U
    };
    assert(bytes.substr(offset, 8U) == quiet_nan_le);
    assert(bytes.substr(offset + 8U, 8U) == quiet_nan_le);
    assert(bytes.substr(offset + 16U, 8U) == quiet_nan_le);
    offset += 123U;
    assert(read_varint(bytes, offset) == 7U);
    assert(bytes.at(offset++) == '4');
    const std::string negative_zero_le {
        "\0\0\0\0\0\0\0\x80", 8U
    };
    const std::string infinity_le {
        "\0\0\0\0\0\0\xf0\x7f", 8U
    };
    assert(bytes.find(negative_zero_le, offset) != std::string::npos);
    assert(bytes.find(infinity_le, offset) != std::string::npos);
    assert(bytes.find(std::string { "A\0B\xc3\xa9", 5U }, offset)
        != std::string::npos);

    std::size_t block = 330U;
    block += 1U + static_cast<std::size_t>(read_be64(bytes, block + 1U));
    assert(static_cast<unsigned char>(bytes.at(block)) == 3U);
    assert(read_be64(bytes, block + 9U) == 7U);
    assert(read_be64(bytes, block + 17U) == 7U);
    const std::array<unsigned char, 7> geometry {
        8U, 8U, 8U, 0U, 3U, 32U, 64U
    };
    for (std::size_t index = 0; index < geometry.size(); ++index) {
        assert(static_cast<unsigned char>(bytes.at(block + 25U + index))
            == geometry[index]);
    }
    block += 1U + static_cast<std::size_t>(read_be64(bytes, block + 1U));
    assert(static_cast<unsigned char>(bytes.at(block)) == 4U);
    const auto hierarchy = inflate_stored_gzip(bytes, block + 17U);
    for (const auto variable_type : {
             29U, 3U, 20U, 21U, 28U, 1U, 8U }) {
        assert(std::find(
                   hierarchy.begin(), hierarchy.end(),
                   static_cast<std::uint8_t>(variable_type))
            != hierarchy.end());
    }
    assert(write_extended_container() == bytes);
}

void test_extended_value_validation()
{
    using namespace fsim::runtime;
    const auto fixture = make_extended_fixture();
    std::ostringstream output;
    FstWriter writer { output };
    writer.declare(fixture.model);
    writer.begin();

    bool metadata_rejected = false;
    try {
        writer.set_initial_value(fixture.signals[1],
            encode_fst_systemverilog_real(SystemVerilogScalarValue {
                SystemVerilogScalarKind::Realtime, 0 }));
    } catch (const std::invalid_argument&) {
        metadata_rejected = true;
    }
    assert(metadata_rejected);

    bool payload_rejected = false;
    try {
        writer.set_initial_value(fixture.signals[3],
            encode_fst_extended_value(
                PackedLogic4::from_msb_string("000"),
                FstValueProfile::Enumeration,
                fixture.enum_metadata));
    } catch (const std::invalid_argument&) {
        payload_rejected = true;
    }
    assert(payload_rejected);
    assert(output.str().empty());

    {
        FstWriterLimits limits;
        limits.maximum_type_metadata_bytes = 4U;
        std::ostringstream bounded_output;
        FstWriter bounded_writer { bounded_output, -9, limits };
        bool metadata_limit_rejected = false;
        try {
            bounded_writer.declare(fixture.model);
        } catch (const std::length_error&) {
            metadata_limit_rejected = true;
        }
        assert(metadata_limit_rejected && bounded_output.str().empty());
    }
    {
        FstWriterLimits limits;
        limits.maximum_value_bytes = 4U;
        std::ostringstream bounded_output;
        FstWriter bounded_writer { bounded_output, -9, limits };
        bounded_writer.declare(fixture.model);
        bounded_writer.begin();
        bool value_limit_rejected = false;
        try {
            bounded_writer.set_initial_value(
                fixture.signals[3], encode_fst_string("12345"));
        } catch (const std::length_error&) {
            value_limit_rejected = true;
        }
        assert(value_limit_rejected && bounded_output.str().empty());
    }
}

void test_typed_leaf_profiles()
{
    using namespace fsim::runtime;
    FstLeafTypeMetadata first_metadata;
    first_metadata.kind = FstLeafKind::DynamicClass;
    first_metadata.owner_identity = "object-41@generation-3";
    first_metadata.leaf_path = "payload.word";
    first_metadata.width = 17U;
    first_metadata.four_state = true;
    auto second_metadata = first_metadata;
    second_metadata.owner_identity = "object-42@generation-3";

    const auto first_type = canonical_fst_leaf_type_metadata(first_metadata);
    const auto second_type = canonical_fst_leaf_type_metadata(second_metadata);
    TraceDeclarationBuilder builder;
    const auto first = builder.add_typed_variable(
        "dynamic.object_41.payload", TraceTypeKind::TypedLeaf, 17U,
        SystemVerilogScalarKind::None, first_type);
    const auto second = builder.add_typed_variable(
        "dynamic.object_42.payload", TraceTypeKind::TypedLeaf, 17U,
        SystemVerilogScalarKind::None, second_type);
    static_cast<void>(builder.add_alias("dynamic.latest", second));
    const auto model = std::move(builder).freeze();

    const auto write = [&] {
        std::ostringstream output;
        FstWriter writer { output };
        writer.declare(model);
        writer.begin();
        bool owner_mismatch_rejected = false;
        try {
            writer.set_initial_value(first,
                encode_fst_leaf_value(
                    PackedLogic4::from_msb_string("10XZ0011010101010"),
                    second_metadata));
        } catch (const std::invalid_argument&) {
            owner_mismatch_rejected = true;
        }
        assert(owner_mismatch_rejected);
        writer.set_initial_value(first,
            encode_fst_leaf_value(
                PackedLogic4::from_msb_string("10XZ0011010101010"),
                first_metadata));
        writer.set_initial_value(second,
            encode_fst_leaf_value(
                PackedLogic4::from_msb_string("01ZX1100101010101"),
                second_metadata));
        writer.close(0);
        return output.str();
    };

    const auto bytes = write();
    assert(bytes.find("10xz0011010101010") != std::string::npos);
    assert(bytes.find("01zx1100101010101") != std::string::npos);
    assert(write() == bytes);
}

void test_logic9_storage_profile()
{
    using namespace fsim::runtime;
    FstExtendedTypeMetadata metadata;
    metadata.kind = FstExtendedTypeKind::VhdlLogic9;
    metadata.language = FstTypeLanguage::Vhdl;
    metadata.nominal_name = "std_logic_vector";
    metadata.width = 9U;
    metadata.enumeration_literals = {
        "'U'", "'X'", "'0'", "'1'", "'Z'", "'W'", "'L'", "'H'", "'-'"
    };
    TraceDeclarationBuilder builder;
    const auto signal = builder.add_typed_variable(
        "top.logic9", TraceTypeKind::VhdlLogic9, 9U,
        SystemVerilogScalarKind::None,
        canonical_fst_type_metadata(metadata));
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output };
    writer.declare(model);
    writer.begin();
    writer.set_initial_value(signal, encode_fst_extended_value(PackedLogic4::from_logic9_msb_string("UX01ZWLH-"), FstValueProfile::VhdlLogic9, metadata));
    writer.close(0U);

    const auto bytes = output.str();
    std::size_t offset = 363U;
    assert(read_varint(bytes, offset) == 36U);
    assert(read_varint(bytes, offset) == 36U);
    assert(read_varint(bytes, offset) == 1U);
    offset += 36U;
    assert(read_varint(bytes, offset) == 1U);
    assert(bytes.at(offset++) == '4');
    assert(read_varint(bytes, offset) == 0U);
    assert(read_varint(bytes, offset) == 0U);
    const std::string packed { "\x01\x23\x45\x67\x80", 5U };
    assert(bytes.substr(offset, packed.size()) == packed);
}

void test_ordered_changes_and_validation()
{
    using namespace fsim::runtime;
    const auto bytes = write_change_container();
    assert(!bytes.empty());
    assert(write_change_container() == bytes);

    TraceDeclarationBuilder builder;
    const auto signal = builder.add_variable("top.value", 1U);
    const auto alias = builder.add_alias("mirror.value", signal);
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    FstWriter writer { output, -9,
        { .maximum_events = 1U, .maximum_timestamps = 1U } };
    writer.declare(model);
    writer.begin(5U);
    bool alias_rejected = false;
    try {
        writer.change({ alias, 5U, 0U, TraceRegion::Active, 0U },
            encode_fst_logic_value(PackedLogic4::from_msb_string("0")));
    } catch (const std::out_of_range&) {
        alias_rejected = true;
    }
    assert(alias_rejected);
    bool early_rejected = false;
    try {
        writer.change({ signal, 4U, 0U, TraceRegion::Active, 0U },
            encode_fst_logic_value(PackedLogic4::from_msb_string("0")));
    } catch (const std::invalid_argument&) {
        early_rejected = true;
    }
    assert(early_rejected);
    writer.change({ signal, 5U, 0U, TraceRegion::Active, 0U },
        encode_fst_logic_value(PackedLogic4::from_msb_string("0")));
    bool count_rejected = false;
    try {
        writer.change({ signal, 6U, 0U, TraceRegion::Active, 1U },
            encode_fst_logic_value(PackedLogic4::from_msb_string("1")));
    } catch (const std::length_error&) {
        count_rejected = true;
    }
    assert(count_rejected);
    assert(writer.status().state == FstWriterState::failed);
    assert(writer.status().failure == "FST change count exceeds its limit");
    bool close_rejected = false;
    try {
        writer.close(5U);
    } catch (const std::logic_error&) {
        close_rejected = true;
    }
    assert(close_rejected && output.str().empty());
}

void test_compression_profiles()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto signal = builder.add_variable("top.payload", 256U);
    const auto model = std::move(builder).freeze();
    const auto write = [&](const FstWriterCompression compression) {
        std::ostringstream output;
        FstWriter writer { output, -9, { }, compression };
        writer.declare(model);
        writer.begin();
        writer.set_initial_value(signal,
            encode_fst_logic_value(PackedLogic4::from_msb_string(
                std::string(256U, '0'))));
        writer.close(0U);
        return output.str();
    };

    const auto deterministic = write(FstWriterCompression::Deterministic);
    const auto stored = write(FstWriterCompression::None);
    constexpr auto deterministic_profile
        = fsim::runtime::kFstDeterministicContainerProfile;
    constexpr auto stored_profile = fsim::runtime::kFstStoredContainerProfile;
    assert(deterministic.substr(74U, deterministic_profile.size())
        == deterministic_profile);
    assert(stored.substr(74U, stored_profile.size()) == stored_profile);
    assert(deterministic != stored);

    std::size_t deterministic_offset = 363U;
    const auto deterministic_bits
        = read_varint(deterministic, deterministic_offset);
    const auto deterministic_stored
        = read_varint(deterministic, deterministic_offset);
    std::size_t stored_offset = 363U;
    const auto stored_bits = read_varint(stored, stored_offset);
    const auto stored_bytes = read_varint(stored, stored_offset);
    assert(deterministic_bits == stored_bits && stored_bytes == stored_bits);
    assert(deterministic_stored < deterministic_bits);

    bool rejected = false;
    try {
        std::ostringstream output;
        FstWriter writer { output, -9, { },
            static_cast<FstWriterCompression>(255U) };
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

} // namespace

int main(const int argc, const char* const argv[])
{
    test_container_and_hierarchy();
    test_mixed_root_provenance_records();
    test_transactional_failures();
    test_bounded_terminal_lifecycle();
    test_io_and_destructor_containment();
    test_exact_initial_values();
    test_wide_unknown_frame();
    test_root_declarations_are_scoped();
    test_initial_value_validation();
    test_extended_typed_values();
    test_extended_value_validation();
    test_typed_leaf_profiles();
    test_logic9_storage_profile();
    test_ordered_changes_and_validation();
    test_compression_profiles();
    if (argc == 3 && std::string_view { argv[1] } == "--emit") {
        const auto bytes = write_container(make_model());
        std::ofstream output { argv[2], std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output ? 0 : 1;
    }
    if (argc == 3 && std::string_view { argv[1] } == "--emit-provenance") {
        const auto bytes = write_container(make_provenance_model());
        std::ofstream output { argv[2], std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output ? 0 : 1;
    }
    if (argc == 3 && std::string_view { argv[1] } == "--emit-values") {
        const auto bytes = write_value_container();
        std::ofstream output { argv[2], std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output ? 0 : 1;
    }
    if (argc == 3 && std::string_view { argv[1] } == "--emit-wide") {
        const auto bytes = write_wide_unknown_container();
        std::ofstream output { argv[2], std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output ? 0 : 1;
    }
    if (argc == 3 && std::string_view { argv[1] } == "--emit-extended") {
        const auto bytes = write_extended_container();
        std::ofstream output { argv[2], std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output ? 0 : 1;
    }
    if (argc == 3 && std::string_view { argv[1] } == "--emit-real") {
        const auto bytes = write_real_container();
        std::ofstream output { argv[2], std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output ? 0 : 1;
    }
    if (argc == 3 && std::string_view { argv[1] } == "--emit-changes") {
        const auto bytes = write_change_container();
        std::ofstream output { argv[2], std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output ? 0 : 1;
    }
    if (argc == 3 && std::string_view { argv[1] } == "--emit-scalar") {
        const auto bytes = write_scalar_container(false);
        std::ofstream output { argv[2], std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output ? 0 : 1;
    }
    if (argc == 3 && std::string_view { argv[1] } == "--emit-scalar-change") {
        const auto bytes = write_scalar_container(true);
        std::ofstream output { argv[2], std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output ? 0 : 1;
    }
    return argc == 1 ? 0 : 2;
}
