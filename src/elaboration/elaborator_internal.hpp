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
#include <cstdlib>
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

// Whether FSIM_HIR_LOWER_TRACE_FAILURES asks for rejected-lowering traces.
// Read once: rejection paths are taken routinely during elaboration.
[[nodiscard]] inline bool trace_lowering_failures() noexcept
{
    static const bool enabled
        = std::getenv("FSIM_HIR_LOWER_TRACE_FAILURES") != nullptr;
    return enabled;
}

// A resolved subelement of a composite VHDL signal whose own subtype is
// unresolved: `width` bits at `offset` from the least significant bit,
// resolved by `function` (IEEE 1076-2008 4.6, 14.7.3.2).
struct VhdlResolverSegment {
    std::size_t offset { };
    std::size_t width { };
    semantic::DeclarationId function;
};
// The bits (offset, width) one driver of a composite signal drives; empty
// when it drives the whole signal.
using VhdlDriverRegions = std::vector<std::pair<std::size_t, std::size_t>>;

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

// The position of a named CHARACTER literal such as NUL, DEL or C128
// (IEEE 1076-2008 16.3).
[[nodiscard]] inline std::optional<std::uint64_t>
vhdl_character_control_literal_code(const std::string_view name)
{
    static constexpr std::array<std::string_view, 32U> low {
        "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel",
        "bs", "ht", "lf", "vt", "ff", "cr", "so", "si",
        "dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb",
        "can", "em", "sub", "esc", "fsp", "gsp", "rsp", "usp",
    };
    const auto lower = lowercase_ascii_copy(name);
    if (const auto found = std::ranges::find(low, lower); found != low.end()) {
        return static_cast<std::uint64_t>(found - low.begin());
    }
    if (lower == "del") {
        return 127U;
    }
    if (lower.size() == 4U && lower.front() == 'c'
        && std::ranges::all_of(lower.substr(1U), [](const char digit) {
               return digit >= '0' && digit <= '9';
           })) {
        const auto code = std::stoul(lower.substr(1U));
        if (code >= 128U && code <= 159U) {
            return code;
        }
    }
    return std::nullopt;
}

// The lower-case last '.'-separated part of a VHDL selected name.
[[nodiscard]] inline std::string lowercase_vhdl_simple_name(
    const std::string_view name)
{
    const auto separator = name.find_last_of('.');
    return lowercase_ascii_copy(name.substr(
        separator == std::string_view::npos ? 0U : separator + 1U));
}

// The binding key of an object of a package instance declared in a design
// unit or block: the instance has its own objects (IEEE 1076-2008 4.9), so
// they are bound by instance declaration and lower-case simple name.
[[nodiscard]] inline std::string vhdl_package_object_key(
    const semantic::DeclarationId instance, const std::string_view name)
{
    return "@pkgobj:" + std::to_string(instance.value()) + "."
        + lowercase_ascii_copy(name);
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

// The storage width of a VHDL subtype as a record or array element: its
// executable width, or zero for a null array that occupies no bits (IEEE
// 1076-2008 5.3.2.2).
[[nodiscard]] inline std::optional<std::size_t> vhdl_subtype_storage_width(
    const semantic::vhdl::SubtypeIndication& subtype)
{
    if (const auto width = vhdl_subtype_width(subtype)) {
        return width;
    }
    for (const auto& range : subtype.constraints) {
        if (range.kind == semantic::vhdl::RangeKind::array_index
            && (range.null
                || (!range.direction_from_bounds && range.left
                    && range.right
                    && (range.descending ? *range.left < *range.right
                                         : *range.left > *range.right)))) {
            return 0U;
        }
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
