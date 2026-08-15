// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_value_codec.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fsim::systemc;

bool has_code(const fsim::diagnostic::Engine& diagnostics,
    const std::string& code)
{
    return std::ranges::any_of(diagnostics.diagnostics(),
        [&](const auto& item) { return item.code == code; });
}

std::size_t words_for(const std::uint32_t width)
{
    return (static_cast<std::size_t>(width) + 63U) / 64U;
}

std::vector<std::uint64_t> patterned_plane(const std::uint32_t width,
    const std::uint64_t seed)
{
    std::vector<std::uint64_t> plane(words_for(width));
    for (std::size_t index = 0U; index < plane.size(); ++index) {
        plane[index] = seed
            ^ (std::uint64_t { 0x9e3779b97f4a7c15ULL }
                * static_cast<std::uint64_t>(index + 1U));
    }
    const auto remainder = width % 64U;
    if (remainder != 0U) {
        plane.back() &= (std::uint64_t { 1U } << remainder) - 1U;
    }
    return plane;
}

void set_logic9_code(
    SystemCKernelValue& value, const std::uint32_t bit, const unsigned code)
{
    const auto word = static_cast<std::size_t>(bit / 64U);
    const auto mask = std::uint64_t { 1U } << (bit % 64U);
    for (unsigned plane = 0U; plane < 4U; ++plane) {
        if ((code & (1U << plane)) != 0U) {
            value.planes[plane][word] |= mask;
        }
    }
}

std::vector<std::byte> round_trip(const SystemCKernelValue& value,
    const SystemCKernelValueLimits& limits = { })
{
    fsim::diagnostic::Engine encode_diagnostics;
    const auto encoded = serialize_systemc_kernel_value(
        value, limits, encode_diagnostics);
    assert(encoded);
    assert(encode_diagnostics.diagnostics().empty());

    fsim::diagnostic::Engine decode_diagnostics;
    const auto decoded = deserialize_systemc_kernel_value(
        *encoded, limits, decode_diagnostics);
    assert(decoded);
    assert(*decoded == value);
    assert(decode_diagnostics.diagnostics().empty());

    fsim::diagnostic::Engine reencode_diagnostics;
    const auto reencoded = serialize_systemc_kernel_value(
        *decoded, limits, reencode_diagnostics);
    assert(reencoded == encoded);
    assert(reencode_diagnostics.diagnostics().empty());
    return *encoded;
}

void expect_invalid_value(
    const SystemCKernelValue& value, const std::string& code,
    const SystemCKernelValueLimits& limits = { })
{
    fsim::diagnostic::Engine diagnostics;
    assert(!validate_systemc_kernel_value(value, limits, diagnostics));
    assert(has_code(diagnostics, code));
}

void expect_invalid_payload(const std::vector<std::byte>& bytes,
    const std::string& code, const SystemCKernelValueLimits& limits = { })
{
    fsim::diagnostic::Engine diagnostics;
    assert(!deserialize_systemc_kernel_value(bytes, limits, diagnostics));
    assert(has_code(diagnostics, code));
}

SystemCKernelValue make_bit2()
{
    SystemCKernelValue value;
    value.kind = SystemCKernelValueKind::bit2;
    value.width = 129U;
    value.is_signed = true;
    value.range = { 64, -64, SystemCKernelRangeDirection::descending };
    value.type_name = "signed_bus_t";
    value.planes = { patterned_plane(value.width, 0x0123456789abcdefULL) };
    return value;
}

SystemCKernelValue make_logic4()
{
    SystemCKernelValue value;
    value.kind = SystemCKernelValueKind::logic4;
    value.width = 257U;
    value.range = { -128, 128, SystemCKernelRangeDirection::ascending };
    value.type_name = "four_state_word_t";
    value.planes = {
        patterned_plane(value.width, 0x5555555555555555ULL),
        patterned_plane(value.width, 0x3333333333333333ULL),
    };
    return value;
}

SystemCKernelValue make_logic9()
{
    SystemCKernelValue value;
    value.kind = SystemCKernelValueKind::logic9;
    value.width = 257U;
    value.range = { 512, 256, SystemCKernelRangeDirection::descending };
    value.type_name = "std_logic_vector_t";
    value.planes.assign(4U, std::vector<std::uint64_t>(words_for(value.width)));
    for (std::uint32_t bit = 0U; bit < value.width; ++bit) {
        set_logic9_code(value, bit, static_cast<unsigned>(bit % 9U));
    }
    return value;
}

SystemCKernelValue make_enumeration()
{
    SystemCKernelValue value;
    value.kind = SystemCKernelValueKind::enumeration;
    value.width = 2U;
    value.range = { 1, 0, SystemCKernelRangeDirection::descending };
    value.type_name = "traffic_light_t";
    value.enum_literals = { "red", "amber", "green" };
    value.planes = { { 2U } };
    return value;
}

SystemCKernelValue make_time()
{
    SystemCKernelValue value;
    value.kind = SystemCKernelValueKind::time;
    value.width = 130U;
    value.range = { 129, 0, SystemCKernelRangeDirection::descending };
    value.type_name = "simulation_time_t";
    value.time_unit_fs = 1000U;
    value.planes = { patterned_plane(value.width, 0xa5a5a5a5a5a5a5a5ULL) };
    return value;
}

void audit_exact_round_trips()
{
    const auto bit2_bytes = round_trip(make_bit2());
    const auto logic4_bytes = round_trip(make_logic4());
    const auto logic9_bytes = round_trip(make_logic9());
    const auto enum_bytes = round_trip(make_enumeration());
    const auto time_bytes = round_trip(make_time());
    assert(bit2_bytes != logic4_bytes);
    assert(logic4_bytes != logic9_bytes);
    assert(enum_bytes != time_bytes);
}

void audit_scalar_projection()
{
    const SystemCKernelScalarProjection scalar {
        0x123456789ULL, 37U, true
    };
    fsim::diagnostic::Engine make_diagnostics;
    const auto value = make_systemc_kernel_scalar_value(
        scalar, { }, make_diagnostics);
    assert(value);
    assert(make_diagnostics.diagnostics().empty());

    fsim::diagnostic::Engine project_diagnostics;
    const auto projected = project_systemc_kernel_scalar_value(
        *value, { }, project_diagnostics);
    assert(projected == scalar);
    assert(project_diagnostics.diagnostics().empty());

    auto lossy = *value;
    lossy.range = { 0, 36, SystemCKernelRangeDirection::ascending };
    fsim::diagnostic::Engine lossy_diagnostics;
    assert(!project_systemc_kernel_scalar_value(
        lossy, { }, lossy_diagnostics));
    assert(has_code(lossy_diagnostics, "FSIM-SC-V004"));

    fsim::diagnostic::Engine excessive_diagnostics;
    assert(!make_systemc_kernel_scalar_value(
        { 0U, 65U, false }, { }, excessive_diagnostics));
    assert(has_code(excessive_diagnostics, "FSIM-SC-V004"));
}

void audit_metadata_rejections()
{
    auto value = make_bit2();
    value.kind = static_cast<SystemCKernelValueKind>(255U);
    expect_invalid_value(value, "FSIM-SC-V001");

    value = make_bit2();
    value.planes.back().back() |= 2U;
    expect_invalid_value(value, "FSIM-SC-V001");

    value = make_logic9();
    value.planes[3][0] |= 1U;
    value.planes[0][0] |= 1U;
    expect_invalid_value(value, "FSIM-SC-V001");

    value = make_enumeration();
    value.enum_literals.back() = value.enum_literals.front();
    expect_invalid_value(value, "FSIM-SC-V001");

    value = make_enumeration();
    value.is_signed = true;
    expect_invalid_value(value, "FSIM-SC-V001");

    value = make_time();
    value.time_unit_fs = 0U;
    expect_invalid_value(value, "FSIM-SC-V001");

    value = make_logic4();
    value.enum_literals = { "not_allowed" };
    expect_invalid_value(value, "FSIM-SC-V001");

    value = make_enumeration();
    SystemCKernelValueLimits text_limits;
    text_limits.max_enum_literal_bytes = 16U;
    text_limits.max_enum_text_bytes = 2U;
    expect_invalid_value(value, "FSIM-SC-V003", text_limits);

    value = make_bit2();
    SystemCKernelValueLimits width_limits;
    width_limits.max_width_bits = 128U;
    expect_invalid_value(value, "FSIM-SC-V003", width_limits);
}

void audit_payload_rejections()
{
    const auto canonical = round_trip(make_logic4());

    auto bytes = canonical;
    bytes[0] = std::byte { 0U };
    expect_invalid_payload(bytes, "FSIM-SC-V002");

    bytes = canonical;
    bytes[4] = std::byte { 2U };
    expect_invalid_payload(bytes, "FSIM-SC-V002");

    bytes = canonical;
    bytes[8] = std::byte { 0xffU };
    expect_invalid_payload(bytes, "FSIM-SC-V002");

    bytes = canonical;
    bytes[11] = std::byte { 1U };
    expect_invalid_payload(bytes, "FSIM-SC-V002");

    bytes = canonical;
    bytes[52] = std::byte { 1U };
    expect_invalid_payload(bytes, "FSIM-SC-V002");

    bytes = canonical;
    bytes.pop_back();
    expect_invalid_payload(bytes, "FSIM-SC-V002");

    bytes = canonical;
    bytes.push_back(std::byte { 0U });
    expect_invalid_payload(bytes, "FSIM-SC-V002");

    bytes.assign(8U, std::byte { 0U });
    expect_invalid_payload(bytes, "FSIM-SC-V003");

    SystemCKernelValueLimits encoded_limits;
    encoded_limits.max_encoded_bytes = canonical.size() - 1U;
    expect_invalid_payload(canonical, "FSIM-SC-V003", encoded_limits);
}

void audit_diagnostic_contract()
{
    assert(std::string { systemc_kernel_value_diagnostic_code(
                             SystemCKernelValueCode::none) }
            .empty());
    assert(std::string { systemc_kernel_value_diagnostic_code(
               SystemCKernelValueCode::metadata) }
        == "FSIM-SC-V001");
    assert(std::string { systemc_kernel_value_diagnostic_code(
               SystemCKernelValueCode::payload) }
        == "FSIM-SC-V002");
    assert(std::string { systemc_kernel_value_diagnostic_code(
               SystemCKernelValueCode::resource) }
        == "FSIM-SC-V003");
    assert(std::string { systemc_kernel_value_diagnostic_code(
               SystemCKernelValueCode::lossy) }
        == "FSIM-SC-V004");
}

} // namespace

int main()
{
    audit_exact_round_trips();
    audit_scalar_projection();
    audit_metadata_rejections();
    audit_payload_rejections();
    audit_diagnostic_contract();
    return 0;
}
