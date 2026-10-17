// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaboration_targets.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/support/index_distance.hpp"
#include "fsim/support/vhdl_identifier.hpp"
#include "fsim/support/vhdl_time.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration::elaboration_detail {

using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;

// Dynamic event-array selection emits one branch per static element. Keep
// both elaboration materialization and executable dispatch bounded.
inline constexpr std::uint64_t maximum_compiled_event_array_elements
    = 65'536U;

using support::vhdl_time_units;

// ASCII case folding of identifiers (VHDL basic identifiers are
// case-insensitive, IEEE 1076-2008 15.4.2).
inline void lowercase_ascii(std::string& text) noexcept
{
    for (auto& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
}

[[nodiscard]] inline std::string lowercase_ascii_copy(
    const std::string_view text)
{
    std::string result { text };
    lowercase_ascii(result);
    return result;
}

// The lower-case last '.'-separated part of a VHDL selected name.
[[nodiscard]] inline std::string lowercase_vhdl_simple_name(
    const std::string_view name)
{
    const auto separator = name.find_last_of('.');
    return lowercase_ascii_copy(name.substr(
        separator == std::string_view::npos ? 0U : separator + 1U));
}

[[nodiscard]] inline bool equal_ignoring_ascii_case(
    const std::string_view left, const std::string_view right) noexcept
{
    return support::equal_ignoring_ascii_case(left, right);
}

// Identifier equality: exact for SystemVerilog, VHDL rules otherwise
// (basic identifiers ignore case, extended ones compare exactly).
[[nodiscard]] inline bool same_hir_identifier(const std::string_view left,
    const std::string_view right, const bool vhdl)
{
    return vhdl ? support::same_vhdl_identifier(left, right) : left == right;
}

// The runtime value domain of a VHDL value domain.
[[nodiscard]] constexpr frontend::ValueDomain vhdl_value_domain(
    const semantic::vhdl::ValueDomain domain) noexcept
{
    switch (domain) {
    case semantic::vhdl::ValueDomain::bit2:
        return frontend::ValueDomain::Bit2;
    case semantic::vhdl::ValueDomain::logic4:
        return frontend::ValueDomain::Logic4;
    case semantic::vhdl::ValueDomain::logic9:
        return frontend::ValueDomain::Logic9;
    case semantic::vhdl::ValueDomain::boolean:
        return frontend::ValueDomain::Boolean;
    case semantic::vhdl::ValueDomain::integer:
        return frontend::ValueDomain::Integer;
    case semantic::vhdl::ValueDomain::string:
        return frontend::ValueDomain::String;
    case semantic::vhdl::ValueDomain::unknown:
        break;
    }
    return frontend::ValueDomain::Unknown;
}

// The executable width of a VHDL subtype: its packed width, or the
// INTEGER storage width of an integer subtype.
[[nodiscard]] inline std::optional<std::size_t> vhdl_subtype_width(
    const semantic::vhdl::SubtypeIndication& subtype)
{
    if (subtype.executable_width && *subtype.executable_width != 0U
        && *subtype.executable_width
            <= std::numeric_limits<std::size_t>::max()) {
        return static_cast<std::size_t>(*subtype.executable_width);
    }
    if (subtype.domain == semantic::vhdl::ValueDomain::integer
        && subtype.integer_storage_width != 0U) {
        return subtype.integer_storage_width;
    }
    return std::nullopt;
}

// The last segment of an expanded or scoped name: `lib.pkg.item` and
// `pkg::item` give `item`.
[[nodiscard]] constexpr std::string_view simple_name_of(
    const std::string_view spelling) noexcept
{
    const auto separator = spelling.find_last_of(".:");
    return separator == std::string_view::npos
        ? spelling
        : spelling.substr(separator + 1U);
}

// The frontend direction of a compiled port or interface object.
[[nodiscard]] constexpr frontend::PortDirection compiled_port_direction(
    const semantic::sv::Direction direction) noexcept
{
    switch (direction) {
    case semantic::sv::Direction::input:
        return frontend::PortDirection::Input;
    case semantic::sv::Direction::output:
        return frontend::PortDirection::Output;
    case semantic::sv::Direction::inout:
        return frontend::PortDirection::Inout;
    case semantic::sv::Direction::ref:
        return frontend::PortDirection::Ref;
    case semantic::sv::Direction::unknown:
        break;
    }
    return frontend::PortDirection::Unknown;
}

[[nodiscard]] constexpr frontend::PortDirection compiled_port_direction(
    const semantic::vhdl::Direction direction) noexcept
{
    switch (direction) {
    case semantic::vhdl::Direction::input:
        return frontend::PortDirection::Input;
    case semantic::vhdl::Direction::output:
        return frontend::PortDirection::Output;
    case semantic::vhdl::Direction::inout:
        return frontend::PortDirection::Inout;
    case semantic::vhdl::Direction::buffer:
        return frontend::PortDirection::Buffer;
    case semantic::vhdl::Direction::unknown:
        break;
    }
    return frontend::PortDirection::Unknown;
}

// A SystemVerilog port declared without a direction is inout when it is an
// interface port.
[[nodiscard]] inline frontend::PortDirection compiled_port_direction(
    const semantic::sv::Declaration& declaration) noexcept
{
    const auto direction = compiled_port_direction(declaration.direction);
    if (direction != frontend::PortDirection::Unknown
        || declaration.form != semantic::sv::DeclarationForm::port) {
        return direction;
    }
    const auto interface_port = !declaration.interface_type.empty()
        || (declaration.type
            && declaration.type->target.spelling == "interface");
    return interface_port
        ? frontend::PortDirection::Inout
        : frontend::PortDirection::Unknown;
}

[[nodiscard]] inline std::string_view compiled_systemverilog_library(
    const semantic::sv::Unit& unit) noexcept
{
    return unit.library.empty() ? std::string_view { "work" }
                                : std::string_view { unit.library };
}

// The bounds of STD.STANDARD.INTEGER when it is `width` bits: 64 under
// VHDL-2019 (IEEE 1076-2019 5.2.3.1), otherwise 32.
[[nodiscard]] constexpr std::int64_t vhdl_integer_minimum(
    const std::size_t width) noexcept
{
    return width == 64U ? std::numeric_limits<std::int64_t>::min()
                        : std::numeric_limits<std::int32_t>::min();
}
[[nodiscard]] constexpr std::int64_t vhdl_integer_maximum(
    const std::size_t width) noexcept
{
    return width == 64U ? std::numeric_limits<std::int64_t>::max()
                        : std::numeric_limits<std::int32_t>::max();
}

[[nodiscard]] bool is_two_state_domain(
    frontend::ValueDomain domain) noexcept;
[[nodiscard]] ValueKind value_kind(
    frontend::ValueDomain domain) noexcept;

[[nodiscard]] std::string simple_top_name(std::string_view top);
[[nodiscard]] std::optional<std::uint64_t> unsigned_decimal(
    std::string_view text);
using support::index_distance;
[[nodiscard]] PackedLogic4 unsigned_value(
    std::uint64_t value,
    std::size_t width);
[[nodiscard]] PackedLogic4 integer_value(
    std::int64_t value,
    std::size_t width = 32U);

// The frontend span of a compiled source span, with its physical file and
// macro expansion stack.
[[nodiscard]] frontend::SourceSpan compiled_source_span(
    const semantic::CompiledDesign& compiled,
    semantic::SourceSpanId source);

[[nodiscard]] bool checked_add(
    std::int64_t left,
    std::int64_t right,
    std::int64_t& result);
[[nodiscard]] bool checked_subtract(
    std::int64_t left,
    std::int64_t right,
    std::int64_t& result);
[[nodiscard]] bool checked_multiply(
    std::int64_t left,
    std::int64_t right,
    std::int64_t& result);

} // namespace fsim::elaboration::elaboration_detail
