// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/vpi_value.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

template <typename Function>
void expect_invalid(Function&& function)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

template <typename Function>
void expect_too_large(Function&& function)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::length_error&) {
        rejected = true;
    }
    assert(rejected);
}

[[nodiscard]] std::string bit_pattern(const std::size_t width)
{
    std::string result(width, '0');
    for (std::size_t index = 0; index < width; ++index) {
        result[index] = index % 5U == 1U || index % 7U == 3U ? '1' : '0';
    }
    return result;
}

[[nodiscard]] std::string logic_pattern(const std::size_t width)
{
    constexpr char symbols[] = { '0', '1', 'X', 'Z' };
    std::string result(width, '0');
    for (std::size_t index = 0; index < width; ++index) {
        result[index] = symbols[(index * 3U + 1U) % 4U];
    }
    return result;
}

[[nodiscard]] std::string canonical(std::string value)
{
    for (auto& symbol : value) {
        if (symbol == 'X') {
            symbol = 'x';
        } else if (symbol == 'Z') {
            symbol = 'z';
        }
    }
    return value;
}

void test_arbitrary_width_bit_profiles()
{
    using namespace fsim::runtime;
    const auto expected = bit_pattern(4'097U);
    const auto value = PackedBit2::from_msb_string(expected);
    for (const auto profile : {
             FstValueProfile::BitVector,
             FstValueProfile::SystemVerilogBit }) {
        const auto encoded = encode_fst_bit_value(value, profile);
        assert(encoded.profile() == profile);
        assert(encoded.width() == expected.size());
        assert(encoded.symbols() == expected);
        assert(encoded.symbols().front() == expected.front());
        assert(encoded.symbols().back() == expected.back());
    }
}

void test_arbitrary_width_four_state_profiles()
{
    using namespace fsim::runtime;
    const auto source = logic_pattern(4'103U);
    const auto expected = canonical(source);
    const auto value = PackedLogic4::from_msb_string(source);
    for (const auto profile : {
             FstValueProfile::LogicVector,
             FstValueProfile::SystemVerilogLogic,
             FstValueProfile::SystemVerilogReg }) {
        const auto encoded = encode_fst_logic_value(value, profile);
        assert(encoded.profile() == profile);
        assert(encoded.width() == expected.size());
        assert(encoded.symbols() == expected);
        assert(encoded.symbols().front() == expected.front());
        assert(encoded.symbols().back() == expected.back());
    }
}

void test_time_chandle_and_unknown_intervals()
{
    using namespace fsim::runtime;
    auto time_source = logic_pattern(64U);
    time_source.front() = '0';
    time_source.back() = 'Z';
    const auto time_value = PackedLogic4::from_msb_string(time_source);
    const auto time = encode_fst_systemverilog_scalar(
        time_value, SystemVerilogScalarKind::Time);
    assert(time.profile() == FstValueProfile::SystemVerilogTime);
    assert(time.symbols() == canonical(time_source));

    auto chandle_source = bit_pattern(64U);
    chandle_source.front() = '1';
    chandle_source.back() = '0';
    const auto chandle = encode_fst_systemverilog_scalar(
        PackedLogic4::from_msb_string(chandle_source),
        SystemVerilogScalarKind::Chandle);
    assert(chandle.profile() == FstValueProfile::SystemVerilogOpaqueHandle);
    assert(chandle.symbols() == chandle_source);

    const auto unknown = make_fst_unknown_value(
        FstValueProfile::SystemVerilogLogic, 257U);
    assert(unknown.width() == 257U);
    assert(unknown.symbols() == std::string(257U, 'x'));
}

void test_exact_real_family_payloads()
{
    using namespace fsim::runtime;
    const auto negative_zero = encode_fst_systemverilog_real(
        SystemVerilogScalarValue {
            SystemVerilogScalarKind::Real,
            UINT64_C(0x8000000000000000) });
    assert(negative_zero.profile() == FstValueProfile::SystemVerilogReal);
    assert(negative_zero.payload_kind() == FstPayloadKind::Real);
    assert(negative_zero.width() == 64U);
    assert(negative_zero.real_bits() == UINT64_C(0x8000000000000000));
    assert(negative_zero.canonical_type()
        == "systemverilog.real:binary64:v1");

    for (const auto bits : {
             UINT64_C(0x7ff0000000000000),
             UINT64_C(0xfff0000000000000),
             UINT64_C(0x7ff8123456789abc),
             UINT64_C(0xfff0123456789abc) }) {
        const auto encoded = encode_fst_systemverilog_real(
            SystemVerilogScalarValue {
                SystemVerilogScalarKind::Realtime, bits });
        assert(encoded.profile()
            == FstValueProfile::SystemVerilogRealtime);
        assert(encoded.real_bits() == bits);
    }

    const auto shortreal = encode_fst_systemverilog_real(
        SystemVerilogScalarValue {
            SystemVerilogScalarKind::ShortReal,
            UINT32_C(0x3fc00000) });
    assert(shortreal.profile()
        == FstValueProfile::SystemVerilogShortReal);
    assert(shortreal.width() == 32U);
    assert(shortreal.real_bits() == UINT64_C(0x3ff8000000000000));
    assert(shortreal.canonical_type()
        == "systemverilog.shortreal:binary32-binary64-exact:v1");

    for (const auto bits : {
             UINT32_C(0x80000000),
             UINT32_C(0x7f800000),
             UINT32_C(0xff800000),
             UINT32_C(0x7fc12345),
             UINT32_C(0xff812345) }) {
        const auto encoded = encode_fst_systemverilog_real(
            SystemVerilogScalarValue {
                SystemVerilogScalarKind::ShortReal, bits });
        const auto exponent = (bits >> 23U) & UINT32_C(0xff);
        const auto fraction = bits & UINT32_C(0x7fffff);
        const auto expected = exponent == UINT32_C(0xff) && fraction != 0
            ? (static_cast<std::uint64_t>(bits >> 31U) << 63U)
                | UINT64_C(0x7ff0000000000000)
                | (static_cast<std::uint64_t>(fraction) << 29U)
            : std::bit_cast<std::uint64_t>(static_cast<double>(
                  std::bit_cast<float>(bits)));
        assert(encoded.real_bits() == expected);
    }
}

void test_string_and_extended_metadata()
{
    using namespace fsim::runtime;
    const std::string bytes { "A\0B\xc3\xa9", 5U };
    const auto string_value = encode_fst_string(bytes);
    assert(string_value.profile() == FstValueProfile::SystemVerilogString);
    assert(string_value.payload_kind() == FstPayloadKind::String);
    assert(string_value.width() == 0U);
    assert(string_value.string_bytes() == bytes);
    assert(string_value.canonical_type() == canonical_fst_string_type());

    FstExtendedTypeMetadata systemverilog_enum;
    systemverilog_enum.kind = FstExtendedTypeKind::Enumeration;
    systemverilog_enum.language = FstTypeLanguage::SystemVerilog;
    systemverilog_enum.nominal_name = "pkg.state_t";
    systemverilog_enum.width = 7U;
    systemverilog_enum.four_state = true;
    systemverilog_enum.enumeration_literals = { "IDLE", "'x'" };
    const auto enum_metadata
        = canonical_fst_type_metadata(systemverilog_enum);
    assert(enum_metadata
        == "fsim.fst.type.v1;k=0;l=0;w=7;s=1;n=11:pkg.state_t;"
           "e=2;v=4:IDLE;v=3:'x';u=0;");
    const auto enum_value = encode_fst_extended_value(
        PackedLogic4::from_msb_string("10Xz001"),
        FstValueProfile::Enumeration,
        systemverilog_enum);
    assert(enum_value.payload_kind() == FstPayloadKind::Symbols);
    assert(enum_value.symbols() == "10xz001");
    assert(enum_value.canonical_type() == enum_metadata);

    FstExtendedTypeMetadata physical;
    physical.kind = FstExtendedTypeKind::VhdlPhysical;
    physical.language = FstTypeLanguage::Vhdl;
    physical.nominal_name = "work.distance";
    physical.width = 32U;
    physical.physical_units = { { "mm", 1 }, { "m", 1'000 } };
    const auto physical_metadata = canonical_fst_type_metadata(physical);
    assert(physical_metadata
        == "fsim.fst.type.v1;k=1;l=1;w=32;s=0;n=13:work.distance;"
           "e=0;u=2;v=2:mm;t=1;v=1:m;t=1000;");
    const auto physical_value = encode_fst_extended_value(
        PackedLogic4::from_aval_bval(32U, UINT32_C(0xffffff85), 0),
        FstValueProfile::VhdlPhysical,
        physical);
    assert(physical_value.width() == 32U);
    assert(physical_value.canonical_type() == physical_metadata);

    FstExtendedTypeMetadata time;
    time.kind = FstExtendedTypeKind::VhdlTime;
    time.language = FstTypeLanguage::Vhdl;
    time.nominal_name = "@builtin:time";
    time.width = 64U;
    const auto time_value = encode_fst_extended_value(
        PackedLogic4::from_aval_bval(64U, 25U, 0),
        FstValueProfile::VhdlTime,
        time);
    assert(time_value.symbols().ends_with("11001"));
    assert(time_value.canonical_type()
        == "fsim.fst.type.v1;k=2;l=1;w=64;s=0;n=13:@builtin:time;"
           "e=0;u=0;");
}

void test_exact_logic9_payload()
{
    using namespace fsim::runtime;
    FstExtendedTypeMetadata metadata;
    metadata.kind = FstExtendedTypeKind::VhdlLogic9;
    metadata.language = FstTypeLanguage::Vhdl;
    metadata.nominal_name = "ieee.std_logic_1164.std_logic_vector";
    metadata.width = 9U;
    metadata.enumeration_literals = {
        "'U'", "'X'", "'0'", "'1'", "'Z'", "'W'", "'L'", "'H'", "'-'"
    };
    const auto canonical_type = canonical_fst_type_metadata(metadata);
    assert(canonical_type.find("k=3;l=1;w=9;s=0;") != std::string::npos);
    const auto encoded = encode_fst_extended_value(
        PackedLogic4::from_logic9_msb_string("UX01ZWLH-"),
        FstValueProfile::VhdlLogic9,
        metadata);
    assert(encoded.width() == 9U);
    assert(encoded.symbols() == "ux01zwlh-");
    assert(encoded.canonical_type() == canonical_type);
    expect_invalid([&] {
        static_cast<void>(encode_fst_extended_value(
            PackedLogic4::from_msb_string("XX01ZZ01X"),
            FstValueProfile::VhdlLogic9,
            metadata));
    });
}

void test_stable_typed_leaf_payloads()
{
    using namespace fsim::runtime;
    FstLeafTypeMetadata packed;
    packed.kind = FstLeafKind::PackedAggregate;
    packed.owner_identity = "top.packet@declaration-17";
    packed.leaf_path = "header.tag";
    packed.width = 4'109U;
    packed.four_state = true;
    packed.dimensions = { { 7, 0, true }, { 3, 0, true } };
    const auto source = logic_pattern(packed.width);
    const auto encoded = encode_fst_leaf_value(
        PackedLogic4::from_msb_string(source), packed);
    assert(encoded.profile() == FstValueProfile::TypedLeaf);
    assert(encoded.width() == 4'109U);
    assert(encoded.symbols() == canonical(source));
    assert(encoded.canonical_type().starts_with(
        "fsim.fst.leaf.v1;k=3;l=0;w=4109;s=1;"));
    assert(encoded.canonical_type().find(
               "o=25:top.packet@declaration-17;")
        != std::string::npos);
    assert(encoded.canonical_type().find(
               "p=10:header.tag;d=2;a=7;b=0;p=1;a=3;b=0;p=1;")
        != std::string::npos);

    auto distinct_owner = packed;
    distinct_owner.owner_identity = "top.packet@declaration-18";
    assert(canonical_fst_leaf_type_metadata(distinct_owner)
        != encoded.canonical_type());

    for (const auto kind : {
             FstLeafKind::UnpackedAggregate,
             FstLeafKind::DynamicClass,
             FstLeafKind::Container,
             FstLeafKind::Coverage,
             FstLeafKind::Assertion }) {
        FstLeafTypeMetadata metadata;
        metadata.kind = kind;
        metadata.owner_identity = "owner-9";
        metadata.leaf_path = "leaf.value";
        metadata.width = 17U;
        metadata.four_state = true;
        if (kind == FstLeafKind::UnpackedAggregate
            || kind == FstLeafKind::Container) {
            metadata.dimensions = { { -2, 5, false } };
        }
        const auto leaf = encode_fst_leaf_value(
            PackedLogic4::from_msb_string("10XZ0011010101010"),
            metadata);
        assert(leaf.symbols() == "10xz0011010101010");
        assert(leaf.canonical_type().find("o=7:owner-9;")
            != std::string::npos);
        assert(leaf.canonical_type().find("p=10:leaf.value;")
            != std::string::npos);
    }

    FstLeafTypeMetadata state;
    state.kind = FstLeafKind::ResolvedState;
    state.owner_identity = "top.resolved";
    state.leaf_path = "value.state";
    state.width = 1U;
    state.four_state = true;
    assert(encode_fst_leaf_value(
               PackedLogic4::from_msb_string("Z"), state)
               .symbols()
        == "z");

    FstLeafTypeMetadata strength_zero;
    strength_zero.kind = FstLeafKind::ResolvedStrengthZero;
    strength_zero.owner_identity = "top.resolved";
    strength_zero.leaf_path = "value.strength0";
    strength_zero.width = 3U;
    assert(encode_fst_leaf_value(
               PackedLogic4::from_msb_string("101"), strength_zero)
               .symbols()
        == "101");
    auto strength_one = strength_zero;
    strength_one.kind = FstLeafKind::ResolvedStrengthOne;
    strength_one.leaf_path = "value.strength1";
    assert(encode_fst_leaf_value(
               PackedLogic4::from_msb_string("111"), strength_one)
               .symbols()
        == "111");

    SystemVerilogVpiStrengthValue resolved;
    resolved.state = Logic4::z;
    resolved.drive.zero = SystemVerilogVpiStrengthRank::Weak;
    resolved.drive.one = SystemVerilogVpiStrengthRank::Supply;
    const auto resolved_encoding = encode_fst_resolved_strength(
        resolved, "top.resolved@net-4", "drivers.effective");
    assert(resolved_encoding.state.symbols() == "z");
    assert(resolved_encoding.zero_strength.symbols() == "011");
    assert(resolved_encoding.one_strength.symbols() == "111");
    assert(resolved_encoding.state.canonical_type().find(
               "p=23:drivers.effective.state;")
        != std::string::npos);
    assert(resolved_encoding.zero_strength.canonical_type().find(
               "p=27:drivers.effective.strength0;")
        != std::string::npos);
    assert(resolved_encoding.one_strength.canonical_type().find(
               "p=27:drivers.effective.strength1;")
        != std::string::npos);

    expect_invalid([&] {
        auto invalid = packed;
        invalid.dimensions.clear();
        static_cast<void>(canonical_fst_leaf_type_metadata(invalid));
    });
    expect_invalid([&] {
        auto invalid = state;
        invalid.width = 2U;
        static_cast<void>(canonical_fst_leaf_type_metadata(invalid));
    });
    expect_invalid([&] {
        auto invalid = strength_zero;
        invalid.four_state = true;
        static_cast<void>(canonical_fst_leaf_type_metadata(invalid));
    });
    expect_invalid([&] {
        auto invalid = strength_zero;
        invalid.language = FstTypeLanguage::Vhdl;
        static_cast<void>(canonical_fst_leaf_type_metadata(invalid));
    });
    expect_invalid([&] {
        static_cast<void>(encode_fst_leaf_value(
            PackedLogic4::from_msb_string("1X0"), strength_zero));
    });
    expect_invalid([&] {
        static_cast<void>(encode_fst_resolved_strength(
            resolved, "top.resolved@net-4", { }));
    });
    expect_too_large([&] {
        auto invalid = packed;
        invalid.dimensions.assign(
            fst_maximum_shape_dimensions + 1U, { 0, 0, false });
        static_cast<void>(canonical_fst_leaf_type_metadata(invalid));
    });
}

void test_invalid_profiles_and_values()
{
    using namespace fsim::runtime;
    expect_invalid([] {
        static_cast<void>(encode_fst_bit_value(PackedBit2 { }));
    });
    expect_invalid([] {
        static_cast<void>(encode_fst_logic_value(PackedLogic4 { }));
    });
    expect_invalid([] {
        static_cast<void>(encode_fst_bit_value(
            PackedBit2 { 1U }, FstValueProfile::LogicVector));
    });
    expect_invalid([] {
        static_cast<void>(encode_fst_logic_value(
            PackedLogic4 { 1U }, FstValueProfile::SystemVerilogBit));
    });
    expect_invalid([] {
        static_cast<void>(encode_fst_logic_value(
            PackedLogic4::from_logic9_msb_string("H")));
    });
    expect_invalid([] {
        static_cast<void>(encode_fst_systemverilog_scalar(
            PackedLogic4 { 63U }, SystemVerilogScalarKind::Time));
    });
    expect_invalid([] {
        static_cast<void>(encode_fst_systemverilog_scalar(
            PackedLogic4 { 64U }, SystemVerilogScalarKind::Real));
    });
    expect_invalid([] {
        static_cast<void>(make_fst_unknown_value(
            FstValueProfile::LogicVector, 0U));
    });
    expect_invalid([] {
        static_cast<void>(encode_fst_systemverilog_real(
            SystemVerilogScalarValue::time(1U)));
    });
    expect_invalid([] {
        FstExtendedTypeMetadata metadata;
        metadata.kind = FstExtendedTypeKind::Enumeration;
        metadata.nominal_name = "state_t";
        metadata.width = 2U;
        metadata.enumeration_literals = { "idle", "idle" };
        static_cast<void>(canonical_fst_type_metadata(metadata));
    });
    expect_invalid([] {
        FstExtendedTypeMetadata metadata;
        metadata.kind = FstExtendedTypeKind::VhdlPhysical;
        metadata.language = FstTypeLanguage::Vhdl;
        metadata.nominal_name = "distance";
        metadata.width = 32U;
        metadata.physical_units = { { "mm", 2 } };
        static_cast<void>(canonical_fst_type_metadata(metadata));
    });
    expect_invalid([] {
        FstExtendedTypeMetadata metadata;
        metadata.kind = FstExtendedTypeKind::VhdlPhysical;
        metadata.language = FstTypeLanguage::Vhdl;
        metadata.nominal_name = "distance";
        metadata.width = 32U;
        metadata.physical_units = { { "mm", 1 } };
        static_cast<void>(encode_fst_extended_value(
            PackedLogic4::from_msb_string(
                "0000000000000000000000000000000X"),
            FstValueProfile::VhdlPhysical,
            metadata));
    });
    expect_invalid([] {
        FstExtendedTypeMetadata metadata;
        metadata.kind = FstExtendedTypeKind::Enumeration;
        metadata.nominal_name = "state_t";
        metadata.width = 2U;
        metadata.enumeration_literals = { "idle", "busy" };
        static_cast<void>(encode_fst_extended_value(
            PackedLogic4::from_msb_string("01"),
            FstValueProfile::VhdlTime,
            metadata));
    });
    expect_invalid([] {
        static_cast<void>(make_fst_unknown_value(
            FstValueProfile::SystemVerilogReal, 64U));
    });
    expect_too_large([] {
        static_cast<void>(encode_fst_string(
            std::string(fst_maximum_string_bytes + 1U, 's')));
    });
    expect_too_large([] {
        FstExtendedTypeMetadata metadata;
        metadata.kind = FstExtendedTypeKind::Enumeration;
        metadata.nominal_name
            = std::string(fst_maximum_type_metadata_bytes + 1U, 'n');
        metadata.width = 1U;
        metadata.enumeration_literals = { "only" };
        static_cast<void>(canonical_fst_type_metadata(metadata));
    });
}

} // namespace

int main()
{
    test_arbitrary_width_bit_profiles();
    test_arbitrary_width_four_state_profiles();
    test_time_chandle_and_unknown_intervals();
    test_exact_real_family_payloads();
    test_string_and_extended_metadata();
    test_exact_logic9_payload();
    test_stable_typed_leaf_payloads();
    test_invalid_profiles_and_values();
}
