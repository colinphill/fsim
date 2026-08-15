// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_value_encoder.hpp"

#include "fsim/runtime/vpi_value.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {
namespace {

    [[nodiscard]] bool bit_profile(const FstValueProfile profile) noexcept
    {
        return profile == FstValueProfile::BitVector
            || profile == FstValueProfile::SystemVerilogBit;
    }

    [[nodiscard]] bool logic_profile(const FstValueProfile profile) noexcept
    {
        return profile == FstValueProfile::LogicVector
            || profile == FstValueProfile::SystemVerilogLogic
            || profile == FstValueProfile::SystemVerilogReg;
    }

    [[nodiscard]] std::string canonical_symbols(const PackedLogic4& value)
    {
        if (value.empty()) {
            throw std::invalid_argument("FST values must have a nonzero width");
        }
        if (value.is_logic9()) {
            throw std::invalid_argument(
                "FST Logic9 values require the extended value profile");
        }
        auto result = value.to_msb_string();
        for (auto& symbol : result) {
            if (symbol == 'X') {
                symbol = 'x';
            } else if (symbol == 'Z') {
                symbol = 'z';
            }
        }
        return result;
    }

    [[nodiscard]] std::string canonical_logic9_symbols(
        const PackedLogic4& value)
    {
        if (value.empty() || !value.is_logic9()) {
            throw std::invalid_argument(
                "FST Logic9 values require an exact nine-state payload");
        }
        auto result = value.to_msb_string();
        for (auto& symbol : result) {
            if (symbol >= 'A' && symbol <= 'Z') {
                symbol = static_cast<char>(symbol - 'A' + 'a');
            }
        }
        return result;
    }

    void append_unsigned(std::string& output, const std::uint64_t value)
    {
        std::array<char, 32> buffer { };
        const auto result = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), value);
        if (result.ec != std::errc { }) {
            throw std::length_error("FST metadata integer cannot be serialized");
        }
        output.append(buffer.data(), result.ptr);
    }

    void append_signed(std::string& output, const std::int64_t value)
    {
        std::array<char, 32> buffer { };
        const auto result = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), value);
        if (result.ec != std::errc { }) {
            throw std::length_error("FST metadata integer cannot be serialized");
        }
        output.append(buffer.data(), result.ptr);
    }

    void append_text_field(
        std::string& output,
        const char tag,
        const std::string_view value)
    {
        constexpr std::size_t field_overhead = 32U;
        if (value.size() > fst_maximum_type_metadata_bytes
            || output.size() > fst_maximum_type_metadata_bytes - value.size()
            || fst_maximum_type_metadata_bytes - output.size() - value.size()
                < field_overhead) {
            throw std::length_error("FST type metadata exceeds its byte limit");
        }
        output.push_back(tag);
        output.push_back('=');
        append_unsigned(output, value.size());
        output.push_back(':');
        output.append(value);
        output.push_back(';');
    }

    void require_metadata_text(
        const std::string_view value,
        const char* description)
    {
        if (value.empty() || value.find('\0') != std::string_view::npos) {
            throw std::invalid_argument(
                std::string { "FST " } + description
                + " is empty or contains a NUL byte");
        }
        if (value.size() > fst_maximum_type_metadata_bytes) {
            throw std::length_error("FST type metadata exceeds its byte limit");
        }
    }

    void validate_metadata(const FstExtendedTypeMetadata& metadata)
    {
        require_metadata_text(metadata.nominal_name, "nominal type name");
        if (metadata.enumeration_literals.size() > fst_maximum_type_members
            || metadata.physical_units.size() > fst_maximum_type_members) {
            throw std::length_error("FST type metadata has too many members");
        }
        if (metadata.width == 0) {
            throw std::invalid_argument("FST extended type width is zero");
        }
        if (metadata.kind == FstExtendedTypeKind::Enumeration) {
            if (metadata.enumeration_literals.empty()
                || !metadata.physical_units.empty()) {
                throw std::invalid_argument(
                    "FST enumeration metadata lacks literals or has units");
            }
            if (metadata.language == FstTypeLanguage::Vhdl
                && metadata.four_state) {
                throw std::invalid_argument(
                    "FST VHDL enumeration metadata cannot be four-state");
            }
            std::unordered_set<std::string_view> literals;
            for (const auto& literal : metadata.enumeration_literals) {
                require_metadata_text(literal, "enumeration literal");
                if (!literals.insert(literal).second) {
                    throw std::invalid_argument(
                        "FST enumeration metadata repeats a literal");
                }
            }
            return;
        }
        if (metadata.kind == FstExtendedTypeKind::VhdlLogic9) {
            if (metadata.language != FstTypeLanguage::Vhdl
                || metadata.four_state || !metadata.physical_units.empty()
                || metadata.enumeration_literals.size() != 9U) {
                throw std::invalid_argument(
                    "FST Logic9 metadata is not an exact VHDL nine-state type");
            }
            std::unordered_set<std::string_view> literals;
            for (const auto& literal : metadata.enumeration_literals) {
                require_metadata_text(literal, "Logic9 literal");
                if (!literals.insert(literal).second) {
                    throw std::invalid_argument(
                        "FST Logic9 metadata repeats a literal");
                }
            }
            return;
        }
        if (metadata.language != FstTypeLanguage::Vhdl
            || metadata.four_state
            || !metadata.enumeration_literals.empty()) {
            throw std::invalid_argument("FST physical metadata is not VHDL");
        }
        if (metadata.kind == FstExtendedTypeKind::VhdlPhysical
            && metadata.physical_units.empty()) {
            throw std::invalid_argument("FST physical metadata lacks units");
        }
        std::unordered_set<std::string_view> units;
        for (std::size_t index = 0;
            index < metadata.physical_units.size(); ++index) {
            const auto& unit = metadata.physical_units[index];
            require_metadata_text(unit.name, "physical unit name");
            if (unit.primary_ticks <= 0
                || (index == 0 && unit.primary_ticks != 1)
                || !units.insert(unit.name).second) {
                throw std::invalid_argument(
                    "FST physical metadata has an invalid unit scale");
            }
        }
    }

    void validate_leaf_metadata(const FstLeafTypeMetadata& metadata)
    {
        require_metadata_text(metadata.owner_identity, "leaf owner identity");
        require_metadata_text(metadata.leaf_path, "leaf path");
        if (metadata.width == 0) {
            throw std::invalid_argument("FST leaf type width is zero");
        }
        if (metadata.dimensions.size() > fst_maximum_shape_dimensions) {
            throw std::length_error("FST leaf type has too many dimensions");
        }
        const auto systemverilog_only
            = metadata.kind == FstLeafKind::ResolvedState
            || metadata.kind == FstLeafKind::ResolvedStrengthZero
            || metadata.kind == FstLeafKind::ResolvedStrengthOne
            || metadata.kind == FstLeafKind::DynamicClass
            || metadata.kind == FstLeafKind::Container
            || metadata.kind == FstLeafKind::Coverage
            || metadata.kind == FstLeafKind::Assertion;
        if (systemverilog_only
            && metadata.language != FstTypeLanguage::SystemVerilog) {
            throw std::invalid_argument(
                "FST leaf type has an incompatible source language");
        }
        if (metadata.kind == FstLeafKind::ResolvedState
            && (metadata.width != 1U || !metadata.four_state)) {
            throw std::invalid_argument(
                "FST resolved-state leaf is not exact four-state scalar data");
        }
        if ((metadata.kind == FstLeafKind::ResolvedStrengthZero
                || metadata.kind == FstLeafKind::ResolvedStrengthOne)
            && (metadata.width != 3U || metadata.four_state)) {
            throw std::invalid_argument(
                "FST resolved-strength leaf is not an exact three-bit rank");
        }
        if ((metadata.kind == FstLeafKind::PackedAggregate
                || metadata.kind == FstLeafKind::UnpackedAggregate
                || metadata.kind == FstLeafKind::Container)
            && metadata.dimensions.empty()) {
            throw std::invalid_argument(
                "FST aggregate leaf lacks explicit shape metadata");
        }
    }

    [[nodiscard]] bool canonical_type_is_four_state(
        const std::string_view canonical_type) noexcept
    {
        return canonical_type.find(";s=1;") != std::string_view::npos;
    }

    [[nodiscard]] bool extended_profile(
        const FstValueProfile profile) noexcept
    {
        return profile == FstValueProfile::Enumeration
            || profile == FstValueProfile::VhdlPhysical
            || profile == FstValueProfile::VhdlTime
            || profile == FstValueProfile::VhdlLogic9;
    }

    [[nodiscard]] std::uint64_t exact_shortreal_binary64(
        const std::uint32_t source) noexcept
    {
        const auto exponent = (source >> 23U) & UINT32_C(0xff);
        const auto fraction = source & UINT32_C(0x7fffff);
        if (exponent == UINT32_C(0xff) && fraction != 0) {
            return (static_cast<std::uint64_t>(source >> 31U) << 63U)
                | UINT64_C(0x7ff0000000000000)
                | (static_cast<std::uint64_t>(fraction) << 29U);
        }
        return std::bit_cast<std::uint64_t>(
            static_cast<double>(std::bit_cast<float>(source)));
    }

} // namespace

FstEncodedValue::FstEncodedValue(
    const FstValueProfile profile,
    const FstPayloadKind payload_kind,
    const std::size_t width,
    std::string payload,
    const std::uint64_t real_bits,
    std::string canonical_type)
    : profile_(profile)
    , payload_kind_(payload_kind)
    , width_(width)
    , payload_(std::move(payload))
    , real_bits_(real_bits)
    , canonical_type_(std::move(canonical_type))
{
    if (payload_kind_ == FstPayloadKind::Symbols
        && (width_ == 0 || payload_.size() != width_)) {
        throw std::invalid_argument("FST values must have a nonzero width");
    }
    if (payload_kind_ == FstPayloadKind::Real
        && (width_ != 32U && width_ != 64U)) {
        throw std::invalid_argument("FST real value has an invalid source width");
    }
    if (payload_kind_ == FstPayloadKind::String && width_ != 0) {
        throw std::invalid_argument("FST string value has a nonzero bit width");
    }
}

FstEncodedValue encode_fst_bit_value(
    const PackedBit2& value,
    const FstValueProfile profile)
{
    if (!bit_profile(profile)) {
        throw std::invalid_argument("FST bit value has an incompatible profile");
    }
    if (value.empty()) {
        throw std::invalid_argument("FST values must have a nonzero width");
    }
    return FstEncodedValue {
        profile, FstPayloadKind::Symbols, value.width(),
        value.to_msb_string(), 0, { }
    };
}

FstEncodedValue encode_fst_logic_value(
    const PackedLogic4& value,
    const FstValueProfile profile)
{
    if (!logic_profile(profile)) {
        throw std::invalid_argument("FST logic value has an incompatible profile");
    }
    return FstEncodedValue {
        profile, FstPayloadKind::Symbols, value.width(),
        canonical_symbols(value), 0, { }
    };
}

FstEncodedValue encode_fst_systemverilog_scalar(
    const PackedLogic4& value,
    const SystemVerilogScalarKind kind)
{
    const auto profile = kind == SystemVerilogScalarKind::Time
        ? FstValueProfile::SystemVerilogTime
        : kind == SystemVerilogScalarKind::Chandle
        ? FstValueProfile::SystemVerilogOpaqueHandle
        : throw std::invalid_argument(
              "FST scalar profile is not a time or chandle");
    if (value.width() != 64U) {
        throw std::invalid_argument(
            "FST time and chandle values must have exactly 64 bits");
    }
    return FstEncodedValue {
        profile, FstPayloadKind::Symbols, value.width(),
        canonical_symbols(value), 0, { }
    };
}

FstEncodedValue encode_fst_systemverilog_real(
    const SystemVerilogScalarValue& value)
{
    const auto profile = value.kind == SystemVerilogScalarKind::ShortReal
        ? FstValueProfile::SystemVerilogShortReal
        : value.kind == SystemVerilogScalarKind::Real
        ? FstValueProfile::SystemVerilogReal
        : value.kind == SystemVerilogScalarKind::Realtime
        ? FstValueProfile::SystemVerilogRealtime
        : throw std::invalid_argument(
              "FST real value has a non-real SystemVerilog kind");
    const auto width = value.kind == SystemVerilogScalarKind::ShortReal
        ? 32U
        : 64U;
    const auto bits = value.kind == SystemVerilogScalarKind::ShortReal
        ? exact_shortreal_binary64(static_cast<std::uint32_t>(value.bits))
        : value.bits;
    return FstEncodedValue {
        profile, FstPayloadKind::Real, width, { }, bits,
        std::string { canonical_fst_systemverilog_real_type(value.kind) }
    };
}

std::string_view canonical_fst_systemverilog_real_type(
    const SystemVerilogScalarKind kind)
{
    switch (kind) {
    case SystemVerilogScalarKind::ShortReal:
        return "systemverilog.shortreal:binary32-binary64-exact:v1";
    case SystemVerilogScalarKind::Real:
        return "systemverilog.real:binary64:v1";
    case SystemVerilogScalarKind::Realtime:
        return "systemverilog.realtime:binary64:v1";
    case SystemVerilogScalarKind::None:
    case SystemVerilogScalarKind::Time:
    case SystemVerilogScalarKind::Chandle:
        break;
    }
    throw std::invalid_argument("FST real type has a non-real scalar kind");
}

FstEncodedValue encode_fst_string(const std::string_view value)
{
    if (value.size() > fst_maximum_string_bytes) {
        throw std::length_error("FST string exceeds its byte limit");
    }
    return FstEncodedValue {
        FstValueProfile::SystemVerilogString,
        FstPayloadKind::String,
        0,
        std::string { value },
        0,
        std::string { canonical_fst_string_type() }
    };
}

std::string_view canonical_fst_string_type() noexcept
{
    return "systemverilog.string:bytes:v1";
}

std::string canonical_fst_type_metadata(
    const FstExtendedTypeMetadata& metadata)
{
    validate_metadata(metadata);
    std::string result { "fsim.fst.type.v1;" };
    result += "k=";
    append_unsigned(result, static_cast<std::uint8_t>(metadata.kind));
    result += ";l=";
    append_unsigned(result, static_cast<std::uint8_t>(metadata.language));
    result += ";w=";
    append_unsigned(result, metadata.width);
    result += metadata.four_state ? ";s=1;" : ";s=0;";
    append_text_field(result, 'n', metadata.nominal_name);
    result += "e=";
    append_unsigned(result, metadata.enumeration_literals.size());
    result.push_back(';');
    for (const auto& literal : metadata.enumeration_literals) {
        append_text_field(result, 'v', literal);
    }
    result += "u=";
    append_unsigned(result, metadata.physical_units.size());
    result.push_back(';');
    for (const auto& unit : metadata.physical_units) {
        append_text_field(result, 'v', unit.name);
        result += "t=";
        append_unsigned(
            result, static_cast<std::uint64_t>(unit.primary_ticks));
        result.push_back(';');
    }
    if (result.size() > fst_maximum_type_metadata_bytes) {
        throw std::length_error("FST type metadata exceeds its byte limit");
    }
    return result;
}

std::string canonical_fst_leaf_type_metadata(
    const FstLeafTypeMetadata& metadata)
{
    validate_leaf_metadata(metadata);
    std::string result { "fsim.fst.leaf.v1;" };
    result += "k=";
    append_unsigned(result, static_cast<std::uint8_t>(metadata.kind));
    result += ";l=";
    append_unsigned(result, static_cast<std::uint8_t>(metadata.language));
    result += ";w=";
    append_unsigned(result, metadata.width);
    result += metadata.four_state ? ";s=1;" : ";s=0;";
    append_text_field(result, 'o', metadata.owner_identity);
    append_text_field(result, 'p', metadata.leaf_path);
    result += "d=";
    append_unsigned(result, metadata.dimensions.size());
    result.push_back(';');
    for (const auto& dimension : metadata.dimensions) {
        result += "a=";
        append_signed(result, dimension.left);
        result += ";b=";
        append_signed(result, dimension.right);
        result += dimension.packed ? ";p=1;" : ";p=0;";
    }
    if (result.size() > fst_maximum_type_metadata_bytes) {
        throw std::length_error("FST leaf type metadata exceeds its byte limit");
    }
    return result;
}

FstEncodedValue encode_fst_leaf_value(
    const PackedLogic4& value,
    const FstLeafTypeMetadata& metadata)
{
    if (value.width() != metadata.width) {
        throw std::invalid_argument(
            "FST leaf value does not match its type metadata");
    }
    return encode_fst_leaf_value(
        value, canonical_fst_leaf_type_metadata(metadata));
}

FstEncodedValue encode_fst_leaf_value(
    const PackedLogic4& value,
    const std::string_view canonical_type)
{
    if (!canonical_type.starts_with("fsim.fst.leaf.v1;")
        || canonical_type.size() > fst_maximum_type_metadata_bytes) {
        throw std::invalid_argument("FST leaf value metadata is invalid");
    }
    const auto symbols = canonical_symbols(value);
    if (!canonical_type_is_four_state(canonical_type)
        && (symbols.find('x') != std::string::npos
            || symbols.find('z') != std::string::npos)) {
        throw std::invalid_argument(
            "FST exact leaf value contains an unknown state");
    }
    return FstEncodedValue {
        FstValueProfile::TypedLeaf,
        FstPayloadKind::Symbols,
        value.width(),
        symbols,
        0,
        std::string { canonical_type }
    };
}

FstResolvedStrengthEncoding encode_fst_resolved_strength(
    const SystemVerilogVpiStrengthValue& value,
    const std::string_view owner_identity,
    const std::string_view leaf_path)
{
    require_metadata_text(owner_identity, "resolved-strength owner identity");
    require_metadata_text(leaf_path, "resolved-strength leaf path");
    const auto state_symbol = value.state == Logic4::zero ? "0"
        : value.state == Logic4::one                      ? "1"
        : value.state == Logic4::x                        ? "x"
                                                          : "z";
    FstLeafTypeMetadata state_metadata;
    state_metadata.kind = FstLeafKind::ResolvedState;
    state_metadata.owner_identity = owner_identity;
    state_metadata.leaf_path = std::string { leaf_path } + ".state";
    state_metadata.width = 1U;
    state_metadata.four_state = true;
    FstLeafTypeMetadata zero_metadata;
    zero_metadata.kind = FstLeafKind::ResolvedStrengthZero;
    zero_metadata.owner_identity = owner_identity;
    zero_metadata.leaf_path = std::string { leaf_path } + ".strength0";
    zero_metadata.width = 3U;
    auto one_metadata = zero_metadata;
    one_metadata.kind = FstLeafKind::ResolvedStrengthOne;
    one_metadata.leaf_path = std::string { leaf_path } + ".strength1";
    return {
        encode_fst_leaf_value(
            PackedLogic4::from_msb_string(state_symbol), state_metadata),
        encode_fst_leaf_value(
            PackedLogic4::from_aval_bval(3U,
                static_cast<std::uint8_t>(value.drive.zero), 0U),
            zero_metadata),
        encode_fst_leaf_value(
            PackedLogic4::from_aval_bval(3U,
                static_cast<std::uint8_t>(value.drive.one), 0U),
            one_metadata)
    };
}

FstEncodedValue encode_fst_extended_value(
    const PackedLogic4& value,
    const FstValueProfile profile,
    const FstExtendedTypeMetadata& metadata)
{
    const auto expected = metadata.kind == FstExtendedTypeKind::Enumeration
        ? FstValueProfile::Enumeration
        : metadata.kind == FstExtendedTypeKind::VhdlPhysical
        ? FstValueProfile::VhdlPhysical
        : metadata.kind == FstExtendedTypeKind::VhdlTime
        ? FstValueProfile::VhdlTime
        : FstValueProfile::VhdlLogic9;
    if (profile != expected || value.width() != metadata.width) {
        throw std::invalid_argument(
            "FST extended value does not match its type metadata");
    }
    return encode_fst_extended_value(
        value, profile, canonical_fst_type_metadata(metadata));
}

FstEncodedValue encode_fst_extended_value(
    const PackedLogic4& value,
    const FstValueProfile profile,
    const std::string_view canonical_type)
{
    if (!extended_profile(profile)
        || !canonical_type.starts_with("fsim.fst.type.v1;")
        || canonical_type.size() > fst_maximum_type_metadata_bytes
        || (profile != FstValueProfile::Enumeration
            && canonical_type_is_four_state(canonical_type))) {
        throw std::invalid_argument("FST extended value metadata is invalid");
    }
    const auto symbols = profile == FstValueProfile::VhdlLogic9
        ? canonical_logic9_symbols(value)
        : canonical_symbols(value);
    if (profile != FstValueProfile::VhdlLogic9
        && !canonical_type_is_four_state(canonical_type)
        && (symbols.find('x') != std::string::npos
            || symbols.find('z') != std::string::npos)) {
        throw std::invalid_argument(
            "FST exact typed value contains an unknown state");
    }
    return FstEncodedValue {
        profile, FstPayloadKind::Symbols, value.width(),
        symbols, 0, std::string { canonical_type }
    };
}

FstEncodedValue make_fst_unknown_value(
    const FstValueProfile profile,
    const std::size_t width)
{
    if (width == 0) {
        throw std::invalid_argument("FST values must have a nonzero width");
    }
    if (!fst_profile_is_packed(profile)) {
        throw std::invalid_argument(
            "FST unknown intervals require a packed profile");
    }
    return FstEncodedValue {
        profile, FstPayloadKind::Symbols, width,
        std::string(width, 'x'), 0, { }
    };
}

bool fst_profile_is_packed(const FstValueProfile profile) noexcept
{
    return bit_profile(profile) || logic_profile(profile);
}

} // namespace fsim::runtime
